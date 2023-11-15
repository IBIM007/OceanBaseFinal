import pymysql as mysql
import argparse
import time
import datetime
import subprocess
import os
import sys
import shutil
import logging
import traceback

_logger = logging.getLogger('DeployDemo')

def param_check(args):
    # TODO
    return True

def __data_path(cluster_home_path:str) -> str:
    return os.path.join(cluster_home_path, 'store')

def __observer_bin_path(cluster_home_path:str) -> str:
    return os.path.join(cluster_home_path, 'bin', 'observer')

def __build_env(cluster_home_path:str) -> None:
    paths_to_create = ['clog', 'sstable', 'slog']
    data_path = __data_path(cluster_home_path)
    for path in paths_to_create:
        path_to_create = os.path.join(data_path, path)
        os.makedirs(path_to_create, exist_ok=True)

def __clear_env(cluster_home_path:str) -> None:
    observer_bin_path = __observer_bin_path(cluster_home_path)
    pid = subprocess.getoutput(f'pidof {observer_bin_path}')
    if pid:
        subprocess.run(f'kill -9 {pid}', shell=True)

    paths_to_clear = ['audit', 'etc', 'etc2', 'etc3', 'log', 'run', __data_path(cluster_home_path)]
    for path in paths_to_clear:
        path_to_clear = os.path.join(cluster_home_path, path)
        shutil.rmtree(path_to_clear, ignore_errors=True)

def __try_to_connect(host, mysql_port:int, *, timeout_seconds=60):
    error_return = None
    for _ in range(0, timeout_seconds):
        try:
            return mysql.connect(host=host, user="root", port=mysql_port, passwd="")
        except mysql.err.Error as error:
            error_return = error
            time.sleep(1)

    _logger.info('failed to connect to observer fater %f seconds', timeout_seconds)
    raise error_return

def __create_tenant(cursor, *,
                    cpu:int,
                    memory_size:int,
                    unit_name:str='test_unit',
                    resource_pool_name:str='test_pool',
                    zone_name:str='zone1',
                    tenant_name:str='test') -> None:
    create_unit_sql = f'CREATE RESOURCE UNIT {unit_name} max_cpu={cpu},min_cpu={cpu}, memory_size={memory_size};'
    create_resource_pool_sql = f"CREATE RESOURCE POOL {resource_pool_name} unit='{unit_name}', unit_num=1,ZONE_LIST=('{zone_name}');"
    create_tenant_sql = f"CREATE TENANT IF NOT EXISTS {tenant_name} resource_pool_list = ('{resource_pool_name}') set ob_tcp_invited_nodes = '%';"

    cursor.execute(create_unit_sql)
    _logger.info(f'unit create done: {create_unit_sql}')

    cursor.execute(create_resource_pool_sql)
    _logger.info(f'resource pool create done: {create_unit_sql}')

    cursor.execute(create_tenant_sql)
    _logger.info(f'tenant create done: {create_unit_sql}')


if __name__ == "__main__":
    #main一开始进来是这个
    #日志级别
    log_level = logging.INFO
    #日志格式
    log_format = "%(asctime)s.%(msecs)03d [%(levelname)-5s] - %(message)s " \
                "(%(name)s [%(funcName)s@%(filename)s:%(lineno)s] [%(threadName)s] P:%(process)d T:%(thread)d)"
    #日志日期格式
    log_date_format = "%Y-%m-%d %H:%M:%S"
    #日志设置
    logging.basicConfig(format=log_format, level=log_level, datefmt=log_date_format, stream=sys.stdout)


    parser = argparse.ArgumentParser()
    #添加各种参数
    #部署的路径
    parser.add_argument("--cluster-home-path", dest='cluster_home_path', type=str, help="the path of sys log / config file / sql.sock / audit info")
    #这两个参数是指如果有这个参数那么跳到对应方法吧
    parser.add_argument("--only_build_env", action='store_true', help="build env & start observer without bootstrap and basic check")
    parser.add_argument('--clean', action='store_true', help='clean deploy directory and exit')
    #端口默认2881
    parser.add_argument("-p", dest="mysql_port", type=str, default="2881")
    #rpc端口默认2882
    parser.add_argument("-P", dest="rpc_port", type=str, default="2882")
    #zone默认就一个
    parser.add_argument("-z", dest="zone", type=str, default="zone1")
    #集群id
    parser.add_argument("-c", dest="cluster_id", type=str, default="1")
    #设备名字
    parser.add_argument("-i", dest="devname", type=str, default="lo")
    #ip
    parser.add_argument("-I", dest="ip", type=str, default="127.0.0.1")
    #住户的资源设置吧
    parser.add_argument("-o", dest="opt_str", type=str, default="__min_full_resource_pool_memory=1073741824,datafile_size=60G,datafile_next=20G,datafile_maxsize=100G,log_disk_size=40G,memory_limit=10G,system_memory=1G,cpu_count=24,cache_wash_threshold=1G,workers_per_cpu_quota=10,schema_history_expire_time=1d,net_thread_count=4,syslog_io_bandwidth_limit=10G")
    #租户相关
    tenant_group = parser.add_argument_group('tenant', 'tenant options')
    #租户名字
    tenant_group.add_argument('--tenant-name', dest='tenant_name', type=str, default='test')
    #租户资源池
    tenant_group.add_argument('--tenant-resource-pool-name', dest='tenant_resource_pool_name', type=str, default='test_pool')
    #租户资源单元
    tenant_group.add_argument('--tenant-unit-name', dest='tenant_unit_name', type=str, default='test_unit')
    #租户的CPU
    tenant_group.add_argument('--tenant-cpu', dest='tenant_cpu', type=str, default='18')
    #租户的内存
    tenant_group.add_argument('--tenant-memory', dest='tenant_memory', type=str, default='8589934592')

    args = parser.parse_args()

    if not param_check(args):
        _logger.error("param check failed")
        exit(1)
    #指定路径
    home_abs_path = os.path.abspath(args.cluster_home_path)
    bin_abs_path = __observer_bin_path(home_abs_path)
    data_abs_path = os.path.abspath(__data_path(args.cluster_home_path))

    #清除原来路径下的东西
    if args.clean:
        __clear_env(home_abs_path)
        exit(0)

    #构建环境，应该就是创建目录吧
    __build_env(home_abs_path)

    #root的配置
    rootservice = f'{args.ip}:{args.rpc_port}'
    #observer的相关配置
    observer_args = f"-p {args.mysql_port} -P {args.rpc_port} -z {args.zone} -c {args.cluster_id} -d {data_abs_path} -i {args.devname} -r {rootservice} -I {args.ip} -o {args.opt_str}"

    #就是我们指定的目录
    os.chdir(args.cluster_home_path)
    #命令
    observer_cmd = f"{bin_abs_path} {observer_args}"
    _logger.info(observer_cmd)
    #官方说这个
    #启动Observe
    #不要run。
    #启动子进程。
    shell_result = subprocess.Popen(observer_cmd, shell=True)
    _logger.info('deploy done. returncode=%d', shell_result.returncode)

    #这个应该是等子进程彻底跑起来
    #TODO，群里有人说等的久一点，bootstrap时间反而减少了？
    # 试了一下2.5秒和3秒等待，没发现时间更短。
    #感觉可能的原因是如果过早连接，子进程有些东西没准备好，就会造成这边alter system bootstrap阻塞？
    #TODO探活方式
    time.sleep(2)
    try:
        #尝试连接
        db = __try_to_connect(args.ip, int(args.mysql_port))
        #类似于拿到数据库的对象吧
        cursor = db.cursor(cursor=mysql.cursors.DictCursor)
        _logger.info(f'connect to server success! host={args.ip}, port={args.mysql_port}')

        #计时开始时间
        #执行之前需要input卡一下，然后先attach上进程号，哪个程序，再继续跑,断点打到哪里？
        #前面其实把进程启动了
        #a=input()
        #打印一下
        #print(args.zone)
        #print(rootservice)
        bootstrap_begin = datetime.datetime.now()
        #关键的执行语句,真正初始化
        #这里的zone只有一个，那就是不用选举，但是还应该涉及到分区吧，多个分区仍然需要选举？
        #一个zone，一个observer，分区数应该也一定是1.因为有其他分区的概念一定是其它observer的分区副本。
        cursor.execute(f"ALTER SYSTEM BOOTSTRAP ZONE '{args.zone}' SERVER '{rootservice}'")
        #计时结束时间
        bootstrap_end = datetime.datetime.now()
            #60S
        _logger.info('bootstrap success: %s ms' % ((bootstrap_end - bootstrap_begin).total_seconds() * 1000))
        # checkout server status
        #TODO检查服务器状态，这里其实也可以注销掉，减少时间
        cursor.execute("select * from oceanbase.__all_server")
        server_status = cursor.fetchall()
        if len(server_status) != 1 or server_status[0]['status'] != 'ACTIVE':
            _logger.info("get server status failed")
            exit(1)
        _logger.info('checkout server status ok')
        # ObRootService::check_config_result

        #创建租户。这后面也要算时间.25S
        __create_tenant(cursor,
                        cpu=args.tenant_cpu,
                        memory_size=args.tenant_memory,
                        unit_name=args.tenant_unit_name,
                        resource_pool_name=args.tenant_resource_pool_name,
                        zone_name=args.zone,
                        tenant_name=args.tenant_name)
        _logger.info('create tenant done')

    #异常不用管
    except mysql.err.Error as e:
        _logger.info("deploy observer failed. ex=%s", str(e))
        _logger.info(traceback.format_exc())
        exit(1)
    except Exception as ex:
        _logger.info('exception: %s', str(ex))
        _logger.info(traceback.format_exc())
        exit(1)
