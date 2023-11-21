#!/bin/bash

# 获取输入参数
build_type=$1
rm -rf /data/obcluster/bin/observer
# 判断输入参数，根据不同的参数执行相应的命令
if [ "$build_type" == "release" ]; then
    # 如果输入参数是 "release"，执行 release 版本的命令
    cp /home/gushengjie/OceanBaseFinal/build_release/src/observer/observer /data/obcluster/bin/
elif [ "$build_type" == "debug" ]; then
    # 如果输入参数是 "debug"，执行 debug 版本的命令
    cp /home/gushengjie/OceanBaseFinal/build_debug/src/observer/observer /data/obcluster/bin/
else
    # 如果输入参数不是 "release" 或 "debug"，输出错误信息
    echo "Invalid build type. Please specify 'release' or 'debug'."
fi
