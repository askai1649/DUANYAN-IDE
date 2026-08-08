#!/bin/bash
# Pi 侧 9600 双向: 发 HELLO-PI, 保持 9600 收 ESP 回传
stty -F /dev/ttyAMA0 9600 cs8 -cstopb -parenb raw
sleep 1
printf 'HELLO-PI' > /dev/ttyAMA0
timeout 4 cat /dev/ttyAMA0 > /tmp/esp_echo.bin 2>/dev/null
echo "ECHO_BYTES=$(wc -c < /tmp/esp_echo.bin)"
od -c /tmp/esp_echo.bin | head -4
sleep 2
printf 'AGAIN' > /dev/ttyAMA0
timeout 3 cat /dev/ttyAMA0 > /tmp/esp_echo2.bin 2>/dev/null
echo "ECHO2_BYTES=$(wc -c < /tmp/esp_echo2.bin)"
od -c /tmp/esp_echo2.bin | head -2
echo SEND9600C_DONE
