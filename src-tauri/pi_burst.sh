#!/bin/bash
# Pi 侧串口突发: /dev/ttyAMA0 (GPIO14=TXD) 发 0x00 与 U 交替长串
stty -F /dev/ttyAMA0 115200 cs8 -cstopb -parenb raw
for i in 1 2 3 4 5; do
    head -c 3000 /dev/zero > /dev/ttyAMA0
    sleep 2
    python3 -c "open('/dev/ttyAMA0','wb').write(b'U'*3000)"
    sleep 2
    echo "BURST_$i SENT"
done
echo ALL_DONE
