#!/bin/bash

# 检查输入参数的数量是否正确
if [ "$#" -ne 2 ]; then
    echo "用法: $0 <TRACE_ID> <MERGE_LOG_PATH>"
    exit 1
fi

TRACE_ID=$1
MERGE_LOG_PATH=$2
LOG_PATH=/home/gushengjie/OceanBaseFinal/log

# 检查是否存在包含指定 TRACE_ID 的日志
if ! grep -q "$TRACE_ID" ${LOG_PATH}/observer* ${LOG_PATH}/election* ${LOG_PATH}/rootservice*; then
    echo "错误: 在日志文件中未找到包含 TRACE_ID 为 $TRACE_ID 的日志"
    exit 1
fi

# 执行查找、处理和排序，并保存到合并日志文件中
grep ${TRACE_ID} ${LOG_PATH}/observer* ${LOG_PATH}/election* ${LOG_PATH}/rootservice* \
  | sed 's/:/ /' | awk '{tmp=$1;$1=$2;$2=$3;$3=$4;$4=tmp;print $0}' \
  | sort > /home/gushengjie/OceanBaseFinal/log/${MERGE_LOG_PATH}

echo "合并日志已保存到: /home/gushengjie/OceanBaseFinal/log/${MERGE_LOG_PATH}"
