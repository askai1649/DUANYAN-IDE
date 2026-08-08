#!/bin/bash
# 校准 v2: gpioget 轮询 BCM15 (paddrv 方波, 名义 1s 相位), 时间戳算周期
T=$(
prev=""
n=0
while [ $n -lt 900 ]; do
  v=$(gpioget -c gpiochip0 15 2>/dev/null)
  case "$v" in *active) c=1;; *) c=0;; esac
  if [ -n "$prev" ] && [ "$c" != "$prev" ]; then
    date +%s.%N
  fi
  prev=$c
  n=$((n+1))
done
)
echo "$T"
echo "$T" | awk 'NR==1{f=$1} {l=$1; n=NR} END{ if(n>=2){span=l-f; half=span/(n-1); per=2*half; printf "EDGES=%d HALF_MS=%.1f PERIOD_MS=%.1f STRETCH=%.4f BIT_ITERS=%.0f\n", n, half*1000, per*1000, per, 1666.7/per} else print "NO_EDGES"}'
pinctrl 15 a0 >/dev/null 2>&1
echo CAL_DONE
