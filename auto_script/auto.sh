grep "add table schema succeed" /home/gushengjie/OceanBaseFinal/log/observer.log | awk -F'i=' '{print $2}' | awk -F',' '{print $1, $0}' | sort -n -k1 | cut -d' ' -f2- > /home/gushengjie/OceanBaseFinal/log/schema.log

awk -F, '{print $1}' /home/gushengjie/OceanBaseFinal/log/schema.log > /home/gushengjie/OceanBaseFinal/log/data.log

comm -3 <(seq 20 1131) <(awk '{print $1}' /home/gushengjie/OceanBaseFinal/log/data.log | sort -n)