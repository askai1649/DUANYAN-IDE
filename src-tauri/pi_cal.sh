#!/bin/bash
# 精确测量 paddrv 方波实际周期 (名义 1000ms 相位), 算拉伸系数
# paddrv 仍挂在板上跑 (GPIO4 -> BCM15)
pinctrl 15 ip >/dev/null 2>&1
echo 15 > /sys/class/gpio/export 2>/dev/null
echo in > /sys/class/gpio/gpio15/direction
python3 - <<'EOF'
import time
edges = []
prev = None
t_end = time.monotonic() + 22.0
f = open('/sys/class/gpio/gpio15/value')
while time.monotonic() < t_end:
    f.seek(0)
    v = f.read(1)
    if prev is not None and v != prev:
        edges.append(time.monotonic())
    prev = v
f.close()
if len(edges) >= 2:
    span = edges[-1] - edges[0]
    half = span / (len(edges) - 1)
    period = half * 2
    print("EDGES=%d" % len(edges))
    print("HALF_MS=%.1f" % (half * 1000))
    print("PERIOD_MS=%.1f" % (period * 1000))
    print("STRETCH=%.3f" % period)
    print("BIT_ITERS=%.0f" % (1666.7 / period))
else:
    print("NO_EDGES (paddrv 没在跑或线断)")
EOF
echo 15 > /sys/class/gpio/unexport 2>/dev/null
pinctrl 15 a0 >/dev/null 2>&1
echo CAL_DONE
