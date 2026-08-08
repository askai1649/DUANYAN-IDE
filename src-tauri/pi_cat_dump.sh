#!/bin/bash
# Pi 侧: 两阶段发包 + 停止 cat + 展示全程捕获
printf 'XXXXXXXX' > /dev/ttyAMA0
sleep 2
printf 'HELLO-PI' > /dev/ttyAMA0
sleep 6
pkill -f "cat /dev/ttyAMA0" 2>/dev/null
sleep 1
echo "=== FULL LOG ($(wc -c < /tmp/full.log) bytes) ==="
od -c /tmp/full.log | head -30
echo CAT_DUMP_DONE
