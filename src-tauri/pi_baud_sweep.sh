#!/bin/bash
# 多波特率试探: ESP u1spam 在吐 'U'(0x55), 找到实际波特率
for B in 9600 19200 38400 57600 115200 230400; do
  stty -F /dev/ttyAMA0 "$B" cs8 raw -echo
  sleep 0.2
  N=$(timeout 2 cat /dev/ttyAMA0 | wc -c)
  echo "BAUD=$B bytes=$N"
done
stty -F /dev/ttyAMA0 9600 cs8 raw -echo
echo BAUD_SWEEP_DONE
