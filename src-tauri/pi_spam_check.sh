#!/bin/bash
# Pi 侧: 抓 6 秒 ttyAMA0, 判决 ESP U1 TX 通路是否恢复 (期望大量 'U' = 0x55)
stty -F /dev/ttyAMA0 9600 cs8 raw -echo
sleep 1
N=$(timeout 6 cat /dev/ttyAMA0 | tee /tmp/spam.bin | wc -c)
echo "SPAM_BYTES=$N"
od -An -tx1 /tmp/spam.bin | head -4
echo SPAM_CHECK_DONE
