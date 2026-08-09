#!/usr/bin/env python3
# pi_lb_m5.py - M5 回链验收 (Zero 2W 侧)
# 用法: python3 pi_lb_m5.py [轮数,默认8]
# 流程: S3 应处于自治模式; Pi 每秒发 U突发+心跳帧(CMD 0x01)
#       S3 固件探测到 falling edge → "pi back -> linked" → ACK 回显
# 判定: 输出含 ACK 帧 (AA 55 .. 06 01) 即为回链成功
import os, time, sys, termios

DEV = '/dev/ttyAMA0'
N = int(sys.argv[1]) if len(sys.argv) > 1 else 8
os.system('pinctrl 14 a0 >/dev/null 2>&1')
os.system('pinctrl 15 a0 >/dev/null 2>&1')

fd = os.open(DEV, os.O_RDWR | os.O_NOCTTY)
a = termios.tcgetattr(fd)
a[0] = 0; a[1] = 0; a[3] = 0
a[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
a[4] = termios.B9600; a[5] = termios.B9600
a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 1
termios.tcsetattr(fd, termios.TCSANOW, a)

def crc8(bs):
    c = 0
    for b in bs:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c

# 清缓冲
t0 = time.time()
while time.time() - t0 < 0.5:
    os.read(fd, 4096)

for i in range(N):
    os.write(fd, b'UUUUUUUU')
    time.sleep(0.2)
    body = bytes([0, 0x01])          # LEN=0 (只计载荷), CMD 0x01 心跳
    os.write(fd, b'\xAA\x55' + body + bytes([crc8(body)]))
    time.sleep(0.6)
    rx = os.read(fd, 256)
    print(f"M5_BEAT[{i}] rx={rx.hex()}")
    if len(rx) >= 5 and rx[0] == 0xAA and rx[1] == 0x55 and rx[3] == 0x06:
        # ACK 已回 → 固件已回链, 再确认一拍后收工
        continue

os.close(fd)
print("M5_RELINK=DONE")
