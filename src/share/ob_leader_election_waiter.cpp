/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX SHARE
#include "share/ob_leader_election_waiter.h"

#include "share/schema/ob_table_schema.h"
#include "share/schema/ob_part_mgr_util.h"
#include "share/ls/ob_ls_table_operator.h"
#include "lib/mysqlclient/ob_mysql_result.h"
#include "lib/mysqlclient/ob_mysql_proxy.h"
#include "lib/string/ob_sql_string.h"
namespace oceanbase
{
using namespace common;
using namespace share::schema;
using namespace share;
namespace share
{

#define LOG_WAIT_RESULT(start_time, ...) \
    LOG_INFO("wait leader elect finish", K(ret), \
        "wait_time", ObTimeUtility::current_time() - start_time, ##__VA_ARGS__)


///////ExpectedLeader
int ObLSLeaderElectionWaiter::ExpectedLeader::init(const uint64_t tenant_id,
        const share::ObLSID &ls_id,
        const common::ObAddr &expect_leader)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_INVALID_TENANT_ID == tenant_id
                  || !ls_id.is_valid() || !expect_leader.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(ls_id), K(expect_leader));
  } else {
    tenant_id_ = tenant_id;
    ls_id_ = ls_id;
    exp_leader_ = expect_leader;
  }
  return ret;
}

int ObLSLeaderElectionWaiter::ExpectedLeader::assgin(const ObLSLeaderElectionWaiter::ExpectedLeader &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    tenant_id_ = other.tenant_id_;
    ls_id_ = other.ls_id_;
    exp_leader_ = other.exp_leader_;
    new_leader_ = other.new_leader_;
  }
  return ret;
}

ObLSLeaderElectionWaiter::ObLSLeaderElectionWaiter(
    ObLSTableOperator &lst_operator,
    volatile bool &stop)
  : stop_(stop),
    allocator_(ObModIds::OB_RS_PARTITION_TABLE_TEMP),
    lst_operator_(lst_operator)
{
}

ObLSLeaderElectionWaiter::~ObLSLeaderElectionWaiter()
{
}

int ObLSLeaderElectionWaiter::wait(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    const int64_t timeout)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_INVALID_ID == tenant_id
                  || !ls_id.is_valid()
                  || timeout < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(ls_id), K(timeout));
  } else {
    common::ObAddr dummy_leader;
    if (OB_FAIL(wait(tenant_id, ls_id, timeout, dummy_leader))) {
      LOG_WARN("fail to wait leader", KR(ret), K(tenant_id), K(ls_id), K(timeout));
    }
  }
  return ret;
}

int ObLSLeaderElectionWaiter::wait(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    const int64_t timeout,
    common::ObAddr &leader)
{
  //最后一个就是前面传入的master_rs
  int ret = OB_SUCCESS;
  const int64_t start_time = ObTimeUtility::current_time();
  //这个时间应该是最终判断是否超时的时间
  const int64_t abs_timeout = start_time + timeout;
  if (OB_UNLIKELY(OB_INVALID_ID == tenant_id
                  || !ls_id.is_valid()
                  || timeout < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(ls_id), K(timeout));
  } else {
    //前面不用管，它判断参数而已。重点应该分析这个方法
    //第三个参数是检查leader选举的间隔，500ms
    if (OB_FAIL(wait_elect_leader(
            tenant_id, ls_id, CHECK_LEADER_ELECT_INTERVAL_US, abs_timeout, leader))) {
              //没有打印的
      LOG_WARN("fail to wait elect leader",
               KR(ret), K(tenant_id), K(ls_id), K(timeout), K(abs_timeout));
    }
  }
  LOG_WAIT_RESULT(start_time, K(timeout), K(tenant_id), K(ls_id));
  return ret;
}

int ObLSLeaderElectionWaiter::wait(
    common::ObIArray<ExpectedLeader> &expected_leaders,
    const int64_t timeout)
{
  int ret = OB_SUCCESS;
  const int64_t start_time = ObTimeUtility::current_time();
  const int64_t abs_timeout = start_time + timeout;
  if (OB_UNLIKELY(expected_leaders.count() <= 0
                  || timeout <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(timeout),
             "expected_leader_array_cnt", expected_leaders.count());
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < expected_leaders.count(); ++i) {
      ExpectedLeader &expected_leader = expected_leaders.at(i);
      if (OB_UNLIKELY(!expected_leader.is_valid())) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid argument", KR(ret));
      } else if (OB_FAIL(wait_elect_leader(
              expected_leader.tenant_id_, expected_leader.ls_id_,
              CHECK_LEADER_CHANGE_INTERVAL_US, abs_timeout, expected_leader.exp_leader_))) {
        LOG_WARN("fail to wait elect leader", KR(ret), K(expected_leader));
      } else {
        expected_leader.new_leader_ = expected_leader.exp_leader_;
      }
    }
  }
  LOG_WAIT_RESULT(start_time, K(timeout), K(expected_leaders));
  return ret;
}

int ObLSLeaderElectionWaiter::wait_elect_leader(
    const uint64_t tenant_id,
    const share::ObLSID &ls_id,
    const int64_t check_interval,
    const int64_t abs_timeout,
    common::ObAddr &leader)
{
  int ret = OB_SUCCESS;
  //日志信息
  ObLSInfo ls_info;
  //leader就是前面传入的master_rs，是引用，说明要修改这个值
  //leader的副本
  const ObLSReplica *leader_replica = NULL;
  if (OB_UNLIKELY(OB_INVALID_ID == tenant_id
                  || !ls_id.is_valid())
                  || check_interval <= 0
                  || abs_timeout <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(ls_id),
             K(check_interval), K(abs_timeout));
  } else {
    //前面判断的参数不用管
    //睡眠的间隔时间，500ms/100那就是5ms吧。
    int64_t sleep_interval = std::max(1l, check_interval / 100);
    //开始循环
    while (!stop_) {
      const int64_t cluster_id = GCONF.cluster_id;
      //这个就是获取日志流和日志信息吧
      if (OB_FAIL(lst_operator_.get(cluster_id, tenant_id,
          ls_id, share::ObLSTable::DEFAULT_MODE,ls_info))) {
            //没打印过
        LOG_WARN("get partition info failed", K(tenant_id), K(ls_id), KR(ret));
      } 
      //能否做一个判断，发现server_list只有一个的话，直接赋值，感觉不行，还是需要在选举那里指定。不然很容易出问题，因为其它模块也需要leader吧
      else if (OB_FAIL(ls_info.find_leader(leader_replica))) {
        //这就是一直调的那个方法呀，不对，文件名都不同。对的，因为是调用了ls_info的
        LOG_WARN("find_ledaer失败了",K(ret));
        // failure is normal, since leader may have not taked over
        //失败是正常的，因为leader可能还没有接管？
      } else if (NULL == leader_replica) {
        ret = OB_ERR_UNEXPECTED;
        //没打印过
        LOG_WARN("NULL leader", KR(ret));
      } 
      //终结应该是这两个
      else if (!leader.is_valid()) {
        leader = leader_replica->get_server();
        break;
      } else if (leader == leader_replica->get_server()) {
        break;
      }
      if (OB_SUCCESS != ret || leader != leader_replica->get_server()) {
        const int64_t now = ObTimeUtility::current_time();
        if (now < abs_timeout) {
          //这是去睡一会吧，这里的谁只是说等一会再检查，但是核心目的是解决早点选出来呀
          if (OB_FAIL(check_sleep(std::min(sleep_interval, abs_timeout - now)))) {
            //没打印过
            LOG_WARN("check sleep failed", KR(ret));
            break;
          }
        } else {
          ret = OB_WAIT_ELEC_LEADER_TIMEOUT;
          //没打印过
          LOG_WARN("wait elect sys leader timeout", KR(ret),
                   K(abs_timeout), K(tenant_id), K(ls_id));
          break;
        }
      }
      sleep_interval = std::min(sleep_interval * 2, check_interval);
    }
    //这里已经出了循环了
    if (stop_ && OB_SUCC(ret)) {
      ret = OB_CANCELED;
      //没打印过
      LOG_WARN("stop flag set, cancel task", KR(ret));
    }
  }
  return ret;
}

int ObLSLeaderElectionWaiter::check_sleep(
    const int64_t interval_us)
{
  int ret = OB_SUCCESS;
  int64_t escaped = 0;
  const static int64_t max_step = 10 * 1000; // 10ms;
  while (!stop_ && escaped < interval_us) {
    const int32_t step = static_cast<int32_t>(std::min(max_step, interval_us - escaped));
    ob_usleep(step);
    escaped += step;
  }
  if (stop_) {
    ret = OB_CANCELED;
    LOG_WARN("stop flag set, cancel task", K(ret));
  }
  return ret;
}

}//end namespace share
}//end namespace oceanbase
