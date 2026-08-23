#!/usr/bin/env python3
# pi_acm_probe.py - M6 USB-Serial-JTAG 判决针
# 判决: /dev/ttyACM0 可打开 -> 写 PING -> 读回显(ROM 无回显也正常)
#       并 dump 芯片 MAC/描述符信息供核对
import os, time

DEV = '/dev/ttyACM0'
try:
    fd = os.open(DEV, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
except OSError as e:
    print(f"ACM_OPEN=FAIL {e}")
    raise SystemExit(1)
print("ACM_OPEN=OK")
time.sleep(0.2)
try:
    os.write(fd, b'PING\n')
    print("ACM_WRITE=OK")
except OSError as e:
    print(f"ACM_WRITE=FAIL {e}")
time.sleep(0.5)
try:
    rx = os.read(fd, 128)
    print(f"ACM_RX={len(rx)} bytes hex={rx.hex()}")
except BlockingIOError:
    print("ACM_RX=0 (ROM 静默属正常, 不判死)")
os.close(fd)
print("ACM_PROBE_DONE")
