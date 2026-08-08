#!/bin/bash
# Pi 侧: 启动全程 cat 捕获 ttyAMA0 (含 ESP 启动日志)
stty -F /dev/ttyAMA0 9600 cs8 -cstopb -parenb raw
nohup cat /dev/ttyAMA0 > /tmp/full.log 2>/dev/null &
echo CAT_PID=$!
echo CAT_STARTED
