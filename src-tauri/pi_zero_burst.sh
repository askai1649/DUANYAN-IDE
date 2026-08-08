#!/bin/bash
# Pi 侧: 向 ttyAMA0 发 3KB 0x00 突发 (~3s @9600), 供 ESP g5mon 判决 Pi TX 线
stty -F /dev/ttyAMA0 9600 cs8 raw -echo
head -c 3000 /dev/zero > /dev/ttyAMA0
echo PI_ZERO_BURST_SENT
