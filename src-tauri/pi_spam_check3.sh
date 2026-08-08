#!/bin/bash
# 换线后组合判决: 恢复 a0 复用, 等 u1spam 起来, 抓 8 秒
pinctrl 14 a0 >/dev/null 2>&1
pinctrl 15 a0 >/dev/null 2>&1
stty -F /dev/ttyAMA0 9600 cs8 raw -echo
sleep 8
timeout 8 cat /dev/ttyAMA0 > /tmp/spam3.bin
echo "SPAM_BYTES=$(wc -c < /tmp/spam3.bin)"
od -An -tx1 /tmp/spam3.bin | head -3
echo SPAM3_DONE
