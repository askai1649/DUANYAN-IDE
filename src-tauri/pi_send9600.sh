#!/bin/bash
# Pi 侧 9600 慢速发送: HELLO-PI
stty -F /dev/ttyAMA0 9600 cs8 -cstopb -parenb raw
sleep 1
printf 'HELLO-PI' > /dev/ttyAMA0
sleep 1
echo SEND9600_DONE
