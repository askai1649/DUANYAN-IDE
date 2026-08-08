#!/usr/bin/env python3
# Pi 侧交叉驱动: GPIO14 设为输出, 每 4s 交替 Low/High, 共 15 轮
# ESP32 端 uart_xwatch.c 读 GPIO4 电平, 应跟随变化
import time, mmap

with open('/dev/gpiomem', 'r+b', buffering=0) as f:
    m = mmap.mmap(f.fileno(), 0x1000)
    # GPFSEL1 (word 1): GPIO14 在 bits[14:12], 设 001=输出
    fsel1 = int.from_bytes(m[4:8], 'little')
    fsel1 = (fsel1 & ~(7 << 12)) | (1 << 12)
    m[4:8] = fsel1.to_bytes(4, 'little')
    for i in range(15):
        lvl = i % 2  # 0,1,0,1...
        if lvl == 0:
            m[40:44] = (1 << 14).to_bytes(4, 'little')   # GPCLR0 word 10 = 0x28
        else:
            m[28:32] = (1 << 14).to_bytes(4, 'little')   # GPSET0 word 7 = 0x1C
        print(f"t={i*4}s DRIVE GPIO14={lvl}", flush=True)
        time.sleep(4)
