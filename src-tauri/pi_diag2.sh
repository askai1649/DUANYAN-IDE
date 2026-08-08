#!/bin/bash
# Pi 侧诊断: ttyAMA0 状态 + 占用检查
echo "--- stty ---"
stty -F /dev/ttyAMA0
echo "--- ps ttyAMA0 users ---"
ps aux | grep -i "ttyAMA0\|serial" | grep -v grep
echo "--- serial device ---"
ls -l /dev/ttyAMA0 /dev/serial0 2>/dev/null
echo "--- dmesg uart tail ---"
dmesg 2>/dev/null | grep -i "uart\|ttyAMA" | tail -5
echo "--- quick 2s capture ---"
timeout 2 cat /dev/ttyAMA0 | wc -c
echo DIAG_DONE
