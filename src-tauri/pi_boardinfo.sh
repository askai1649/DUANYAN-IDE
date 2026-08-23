#!/bin/sh
# pi_boardinfo.sh - M6 espflash 握手判决 (只读, 不动 flash)
timeout 45 /usr/local/bin/espflash -S board-info -p /dev/ttyACM0 2>&1 | head -25
echo "RC=$?"
