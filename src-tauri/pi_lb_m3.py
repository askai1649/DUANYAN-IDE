#!/usr/bin/env python3
# pi_lb_m3.py - M3 灯效指令演示
# 用法: python3 pi_lb_m3.py
# 流程: 红 -> 绿 -> 蓝 (静态各3帧幂等重发) -> 彩虹20步 -> 熄灭
import os, time, termios

DEV = '/dev/ttyAMA0'
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

def send_round(payload):
    """U突发 + CMD=0x10 灯效帧, 收应答并核对 ACK 回显"""
    os.write(fd, b'UUUUUUUU')
    time.sleep(0.2)
    body = bytes([len(payload), 0x10]) + payload
    os.write(fd, b'\xAA\x55' + body + bytes([crc8(body)]))
    time.sleep(0.3)
    rx = os.read(fd, 256)
    exp_body = bytes([1 + len(payload), 0x06, 0x10]) + payload
    exp = b'\xAA\x55' + exp_body + bytes([crc8(exp_body)])
    return rx == exp, rx

t0 = time.time()
while time.time() - t0 < 1.0:
    os.read(fd, 4096)

ok_all = True
# 红 绿 蓝 静态 (hue, val=80, mode=0)
# 色相按 ESP hsv_grb 轮实测标定: 0=红, 170=绿, 85=蓝
for name, hue in (('RED', 0), ('GREEN', 170), ('BLUE', 85)):
    m, rx = send_round(bytes([hue, 80, 0]))
    ok_all &= m
    print(f"M3_{name}_ACK={'YES' if m else 'NO'} rx={rx.hex()}")
    time.sleep(2)

# 彩虹 20 步 (mode=1, hue=0xFF 表示继续当前相位, ESP 自推 hue+2/轮)
rb_ok = 0
for step in range(20):
    m, rx = send_round(bytes([0xFF, 120, 1]))
    if m: rb_ok += 1
    time.sleep(0.3)
print(f"M3_RAINBOW_ACK={rb_ok}/20")
ok_all &= (rb_ok == 20)

# 熄灭 (val=0)
m, rx = send_round(bytes([0, 0, 0]))
ok_all &= m
print(f"M3_OFF_ACK={'YES' if m else 'NO'} rx={rx.hex()}")
os.close(fd)
print("M3_PASS=YES" if ok_all else "M3_PASS=NO")
