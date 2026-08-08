#!/bin/bash
# Pi 侧: 等 ESP 进 phase B 后 115200 长抓 40s (救援流 [RESCUE] round N)
pinctrl 15 a0 >/dev/null 2>&1
stty -F /dev/ttyAMA0 115200 cs8 raw -echo
sleep 10
timeout 40 cat /dev/ttyAMA0 > /tmp/rescue2.bin
echo "RESCUE_BYTES=$(wc -c < /tmp/rescue2.bin)"
od -c /tmp/rescue2.bin | head -12
echo RESCUE2_DONE
