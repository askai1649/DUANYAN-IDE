#!/bin/bash
# Pi 侧复测: 9600 发 XAGAIN (X为校准牺牲字节), 收 ESP 回显
stty -F /dev/ttyAMA0 9600 cs8 -cstopb -parenb raw
sleep 1
printf 'XAGAIN' > /dev/ttyAMA0
timeout 4 cat /dev/ttyAMA0 > /tmp/esp_again.bin 2>/dev/null
echo "AGAIN_ECHO=$(wc -c < /tmp/esp_again.bin)"
od -c /tmp/esp_again.bin | head -2
echo AGAIN_TEST_DONE
