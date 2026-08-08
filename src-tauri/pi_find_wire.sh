#!/bin/bash
# ESP U1 正在 GPIO4 狂吐 'U'@9600; 扫 Pi 各 GPIO, 哪个脚出现 0/1 混合就是 TX 线落点
for pin in 15 14 4 17 18 27 22 23 24 25 5 6 12 13 16 20 21; do
  H=0; L=0
  for i in $(seq 1 60); do
    v=$(gpioget -c gpiochip0 "$pin" 2>/dev/null)
    case "$v" in *active) H=$((H+1));; *) L=$((L+1));; esac
  done
  echo "pin=$pin active=$H inactive=$L"
done
echo SCAN_DONE
