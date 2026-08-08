#!/bin/bash
# 全排针 High 扫描: ESP 正在强驱 GPIO4 High, 找哪只脚读到 High
PINS="1 2 3 4 7 8 9 10 11 12 13 15 16 17 18 19 20 21 22 23 24 25 26 27 28"
H=""
for p in $PINS; do
  v=$(gpioget -c gpiochip0 "$p" 2>/dev/null)
  case "$v" in
    *inactive) :;;
    *) H="$H $p";;
  esac
done
echo "HIGH_PINS:$H"
pinctrl 14 a0 >/dev/null 2>&1
pinctrl 15 a0 >/dev/null 2>&1
echo SCAN_HIGH_DONE
