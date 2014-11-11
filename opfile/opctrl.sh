
#!/bin/bash
#opcontrol --separate=thread --no-vmlinux
opcontrol --reset
opcontrol --separate=none --no-vmlinux
opcontrol --event=DATA_CACHE_MISSES:500:0x01 --event=L2_CACHE_MISS:500:0x17 --event=IBS_OP_BANK_CONF_LOAD:50000:0x01
opcontrol --start
./detect-mc-mapping.sh
opcontrol --dump
opcontrol --stop
opcontrol --shutdown
