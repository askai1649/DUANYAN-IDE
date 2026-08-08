#!/bin/bash
# Pi 侧: 恢复 a0 复用 + 115200 抓 8 秒 (BITMEAS phase B 的 U0TXD 救援流)
pinctrl 15 a0 >/dev/null 2>&1
pinctrl get 15
stty -F /dev/ttyAMA0 115200 cs8 raw -echo
sleep 1
timeout 8 cat /dev/ttyAMA0 > /tmp/rescue.bin
echo "RESCUE_BYTES=$(wc -c < /tmp/rescue.bin)"
od -c /tmp/rescue.bin | head -10
echo RESCUE_CAPTURE_DONE
