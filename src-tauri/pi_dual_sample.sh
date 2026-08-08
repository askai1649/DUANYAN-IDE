#!/bin/bash
# 快速交替采样 GPIO14/GPIO15, 找哪个脚在跟随 ESP U1 的 9600 数据流
echo "--- GPIO14 x300 fast ---"
H14=0; L14=0
for i in $(seq 1 300); do
  v=$(gpioget -c gpiochip0 14)
  case "$v" in *active) H14=$((H14+1));; *) L14=$((L14+1));; esac
done
echo "GPIO14 active=$H14 inactive=$L14"
echo "--- GPIO15 x300 fast ---"
H15=0; L15=0
for i in $(seq 1 300); do
  v=$(gpioget -c gpiochip0 15)
  case "$v" in *active) H15=$((H15+1));; *) L15=$((L15+1));; esac
done
echo "GPIO15 active=$H15 inactive=$L15"
echo DUAL_SAMPLE_DONE
