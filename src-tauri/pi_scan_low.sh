#!/bin/bash
# Pi 侧全排针扫描: 找出当前处于 Low 的脚 (ESP 正在强驱某脚 Low)
PINS="4 17 18 27 22 23 24 25 5 6 12 13 16 20 21 14 15"
LO=""
for p in $PINS; do
  v=$(gpioget -c gpiochip0 "$p" 2>/dev/null)
  case "$v" in *inactive) LO="$LO $p";; esac
done
echo "LOW_PINS:$LO"
# 恢复 UART 复用
pinctrl 14 a0 >/dev/null 2>&1
pinctrl 15 a0 >/dev/null 2>&1
echo SCAN_LOW_DONE
