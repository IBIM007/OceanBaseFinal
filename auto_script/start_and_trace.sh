#!/bin/bash

sudo perf record -F 200 -g python3 ../deploy.py --cluster-home-path /data/obcluster -o "__min_full_resource_pool_memory=1073741824,datafile_size=2G,datafile_next=2G,datafile_maxsize=8G,log_disk_size=40G,memory_limit=10G,system_memory=1G,cpu_count=24,cache_wash_threshold=1G,workers_per_cpu_quota=10,schema_history_expire_time=1d,net_thread_count=4,syslog_io_bandwidth_limit=10G"

sudo perf script > out.perf

 ~/FlameGraph/stackcollapse-perf.pl out.perf > out.folded

cp /data/obcluster/log/* /home/gushengjie/OceanBaseFinal/log
