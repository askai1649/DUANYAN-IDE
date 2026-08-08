#!/bin/bash
# 判决: 3s 边沿计数(pad 数据流真实性) -> 恢复 a0 复用 -> 立即重抓 5s
E=$(timeout 3 gpiomon -c gpiochip0 15 2>/dev/null | wc -l)
echo "EDGES_3S=$E"
pinctrl 15 a0 >/dev/null 2>&1
pinctrl get 15
stty -F /dev/ttyAMA0 9600 cs8 raw -echo
N=$(timeout 5 cat /dev/ttyAMA0 | tee /tmp/spam2.bin | wc -c)
echo "SPAM_BYTES=$N"
od -An -tx1 /tmp/spam2.bin | head -3
echo EDGE_THEN_CAT_DONE
