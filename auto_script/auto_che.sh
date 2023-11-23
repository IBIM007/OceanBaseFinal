#!/bin/bash

log_file="../log/schema.log"

# 提取日志中的序号
existing_numbers=$(grep "add table schema succeed" $log_file | awk -F', ' '{print $1}' | cut -d' ' -f1)

# 生成完整的序号范围
complete_numbers=$(seq 0 1131)

# 查找缺失的序号
missing_numbers=$(comm -23 <(echo "$complete_numbers") <(echo "$existing_numbers"))

# 输出缺失的序号
echo "缺失的序号：$missing_numbers"