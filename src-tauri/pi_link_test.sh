#!/bin/bash
# Pi 侧双向测试: 后台收 25s, 前台每 1.5s 发一个 PING, 共 10 个
stty -F /dev/ttyAMA0 115200 raw -echo min 0 time 5
timeout 25 cat /dev/ttyAMA0 > /tmp/pi_rx.bin &
RXPID=$!
sleep 5
for i in 1 2 3 4 5 6 7 8 9 10; do
  printf "PING%d-FROM-PI\n" $i > /dev/ttyAMA0
  sleep 1.5
done
wait $RXPID
echo "PI-RX-BYTES: $(wc -c < /tmp/pi_rx.bin)"
od -c /tmp/pi_rx.bin | head -10
