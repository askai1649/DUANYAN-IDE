#!/usr/bin/env python3
# RX 线判决: 阶段1 直驱 GPIO15 交替 0/1 (各 5s x6 轮);
#           阶段2 ttyAMA0 @115200 连发 "UDRIVE\r\n" x20
import time, mmap, os

with open('/dev/gpiomem', 'r+b', buffering=0) as f:
    m = mmap.mmap(f.fileno(), 0x1000)
    fsel1 = int.from_bytes(m[4:8], 'little')
    fsel1 = (fsel1 & ~(7 << 15)) | (1 << 15)   # GPIO15 = output
    m[4:8] = fsel1.to_bytes(4, 'little')
    for i in range(12):
        lvl = i % 2
        if lvl == 0:
            m[40:44] = (1 << 15).to_bytes(4, 'little')   # GPCLR0
        else:
            m[28:32] = (1 << 15).to_bytes(4, 'little')   # GPSET0
        print(f"[PHASE1] t={i*5}s GPIO15={lvl}", flush=True)
        time.sleep(5)
    # 还原 GPIO15 为 UART RXD (ALT0)
    fsel1 = int.from_bytes(m[4:8], 'little')
    fsel1 = (fsel1 & ~(7 << 15)) | (4 << 15)
    m[4:8] = fsel1.to_bytes(4, 'little')

print("[PHASE2] ttyAMA0 burst...", flush=True)
os.system("stty -F /dev/ttyAMA0 115200 raw -echo")
fd = os.open('/dev/ttyAMA0', os.O_WRONLY)
for i in range(20):
    os.write(fd, b"UDRIVE %d\r\n" % i)
    print(f"[PHASE2] burst {i}", flush=True)
    time.sleep(2)
os.close(fd)
print("[DONE]", flush=True)
