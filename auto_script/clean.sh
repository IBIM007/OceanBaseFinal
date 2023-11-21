#!/bin/bash

# 检查输入参数的数量是否正确
if [ "$#" -ne 1 ]; then
    echo "用法: $0 <clean-option>"
    echo "  <clean-option> 可选值: ob, perf, log"
    exit 1
fi

CLEAN_OPTION=$1

case "$CLEAN_OPTION" in
    "ob")
        # 清理 OceanBase
        echo "清理 OceanBase"
        python3 ../deploy.py --clean --cluster-home-path /data/obcluster
        ;;
    "perf")
        # 清理 perf 文件
        echo "清理 perf 文件"
        rm -rf out.perf out.folded perf.data*
        ;;
    "log")
        # 清理日志文件
        echo "清理日志文件"
        rm -rf /home/gushengjie/OceanBaseFinal/log/*
        ;;
    *)
        echo "错误: 不支持的清理选项: $CLEAN_OPTION"
        exit 1
        ;;
esac

echo "清理完成"
