#!/bin/bash
# Pi 侧两阶段: 先发校准突发 XXXXXXXX, 睡 2s, 再发负载 HELLO-PI, 收回显
stty -F /dev/ttyAMA0 9600 cs8 -cstopb -parenb raw
sleep 1
printf 'XXXXXXXX' > /dev/ttyAMA0
sleep 2
printf 'HELLO-PI' > /dev/ttyAMA0
timeout 5 cat /dev/ttyAMA0 > /tmp/esp_payload.bin 2>/dev/null
echo "PAYLOAD_ECHO=$(wc -c < /tmp/esp_payload.bin)"
od -c /tmp/esp_payload.bin | head -2
echo TWO_PHASE_DONE
