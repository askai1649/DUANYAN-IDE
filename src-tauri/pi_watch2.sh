#!/bin/bash
# 60秒连续采样脚10/脚8, 收集出现过的状态 (判跟随)
S15=""
S14=""
for i in $(seq 1 60); do
  v15=$(gpioget -c gpiochip0 15 2>/dev/null)
  v14=$(gpioget -c gpiochip0 14 2>/dev/null)
  case "$S15" in *"$v15"*) :;; *) S15="$S15 $v15";; esac
  case "$S14" in *"$v14"*) :;; *) S14="$S14 $v14";; esac
  sleep 1
done
echo "PIN10_STATES:$S15"
echo "PIN8_STATES:$S14"
pinctrl 14 a0 >/dev/null 2>&1
pinctrl 15 a0 >/dev/null 2>&1
echo WATCH2_DONE
