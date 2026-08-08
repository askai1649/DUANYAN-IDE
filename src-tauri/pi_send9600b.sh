#!/bin/bash
# Pi 侧 9600 发送 + 115200 收 ESP 回传
stty -F /dev/ttyAMA0 9600 cs8 -cstopb -parenb raw
sleep 1
printf 'HELLO-PI' > /dev/ttyAMA0
sleep 2
stty -F /dev/ttyAMA0 115200 cs8 raw
timeout 3 cat /dev/ttyAMA0 > /tmp/esp_echo.bin 2>/dev/null
echo "ECHO_BYTES=$(wc -c < /tmp/esp_echo.bin)"
xxd /tmp/esp_echo.bin | head -4
echo SEND9600B_DONE
