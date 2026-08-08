#!/bin/bash
# seldual 判决: BCM14=脚8(ESP GPIO5, MCU_SEL=1), BCM15=脚10(ESP GPIO4, MCU_SEL=2)
echo "== seldual sample start"
for i in $(seq 1 40); do
  v=$(gpioget -c gpiochip0 14 15 2>/dev/null)
  echo "$i $v"
  sleep 1
done
pinctrl 14 a0 >/dev/null 2>&1
pinctrl 15 a0 >/dev/null 2>&1
echo "== restored a0"
