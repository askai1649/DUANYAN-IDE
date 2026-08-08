#!/usr/bin/env python3
# pi_lb_stress.py - linkb M2 长时压测
# 用法: python3 pi_lb_stress.py [轮数N]
# 每轮: U*8 校准突发 + 心跳帧(CMD=0x01, payload=seq 2字节)
# 期望应答: AA 55 03 06 01 seqH seqL CRC (8字节, 回显核对)
import os, sys, time, termios

N = int(sys.argv[1]) if len(sys.argv) > 1 else 20
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

def expect(seq):
    body = bytes([0x03, 0x06, 0x01, seq >> 8, seq & 0xFF])
    return b'\xAA\x55' + body + bytes([crc8(body)])

# 排空残余
t0 = time.time()
while time.time() - t0 < 1.0:
    os.read(fd, 4096)

buf = b''
sent = 0
t_start = time.time()
for seq in range(N):
    os.write(fd, b'UUUUUUUU')
    time.sleep(0.2)
    body = bytes([0x02, 0x01, seq >> 8, seq & 0xFF])
    os.write(fd, b'\xAA\x55' + body + bytes([crc8(body)]))
    sent += 1
    time.sleep(0.25)
    buf += os.read(fd, 4096)
# 收尾读取
t_end = time.time() + 3
while time.time() < t_end:
    d = os.read(fd, 4096)
    if d:
        buf += d
        t_end = time.time() + 1
    else:
        time.sleep(0.1)
os.close(fd)
elapsed = time.time() - t_start

# 逐帧核对 (应答帧恒 8 字节, AA 55 同步)
ok = 0; bad = 0; lost = 0
i = 0; nxt = 0
while nxt < N:
    e = expect(nxt)
    j = buf.find(e, i)
    if j < 0:
        lost += 1; nxt += 1; continue
    # j 之前若有杂字节 → 计一次帧间噪声
    if j > i:
        bad += 1
    ok += 1; nxt += 1; i = j + 8
extra = len(buf) - i

print(f"STRESS_N={N} SENT={sent}")
print(f"STRESS_OK={ok} LOST={lost} NOISE={bad}")
print(f"STRESS_RXBYTES={len(buf)} EXTRA_TAIL={extra}")
print(f"STRESS_SECONDS={elapsed:.1f} RATE={N / elapsed:.2f} round/s")
print("STRESS_MATCH=YES" if ok == N and lost == 0 else "STRESS_MATCH=NO")
