#!/usr/bin/env python3
# Pi 侧物理层探针: 读 GPIO14 (接 ESP32 TX) 电平 20 次, 间隔 0.5s
# 空闲应为 High(1); 若恒 Low → 线被拉低/ESP TX 无驱动; 若跳变 → TX 活着但驱动层没收到
import time, mmap, os

GPIOMEM = '/dev/gpiomem'
GPLEV0 = 13  # 偏移 (0x34/4)

with open(GPIOMEM, 'r+b', buffering=0) as f:
    m = mmap.mmap(f.fileno(), 0x1000)
    for i in range(20):
        lv = (int.from_bytes(m[GPLEV0*4:GPLEV0*4+4], 'little') >> 14) & 1
        print(f"t={i*0.5:.1f}s GPIO14={lv}", flush=True)
        time.sleep(0.5)
