#!/usr/bin/env python3
# 配合 uart_rxscan.c: ttyAMA0 @115200 每秒发一包, 持续 150 秒
import time, os
os.system("stty -F /dev/ttyAMA0 115200 raw -echo")
fd = os.open('/dev/ttyAMA0', os.O_WRONLY)
for i in range(150):
    os.write(fd, b"SCANSND %03d\r\n" % i)
    if i % 25 == 0:
        print(f"[SENDER] {i}", flush=True)
    time.sleep(1)
os.close(fd)
print("[SENDER] done", flush=True)
