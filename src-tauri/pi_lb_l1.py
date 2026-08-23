#!/usr/bin/env python3
# pi_lb_l1.py - 演进路线 L1 单关节闭环验收 (Zero 2W 侧)
# 用法: python3 pi_lb_l1.py
# 固件: uart_linkl1.c (M5 基线 + LEDC 舵机驱动, CMD 0x20/0x21)
# 流程:
#   1) CMD 0x20 静态定位 30°/150°, 逐字节核对 ACK [0x20][ang_hi][ang_lo][status]
#   2) CMD 0x21 查询: 角值回读 == 最后一次设置, mode=0(静态)
#   3) CMD 0x20 开启扫描 30°..150° (固件周期任务样例), 2.5s 后再查询:
#      mode=1(扫描中) 且角值已离开起点 —— 周期任务活体证明
#   4) CMD 0x20 占位脚保护: pin=48 → ACK status=1 (拒绝接管)
#   5) CMD 0x20 收束: 回中位 90° + 停扫描
# 判定: L1_PASS=YES 需 1~4 全部通过 (舵机实体角误差留人工目视/量角器记录)
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

def send_round(cmd, payload):
    """U突发 + 命令帧, 收应答原始字节"""
    os.write(fd, b'UUUUUUUU')
    time.sleep(0.2)
    body = bytes([len(payload), cmd]) + payload
    os.write(fd, b'\xAA\x55' + body + bytes([crc8(body)]))
    time.sleep(0.4)
    return os.read(fd, 256)

def find_ack(rx):
    """在应答流中定位 AA 55 帧, 校验 CRC, 返回 (ack_cmd, payload) 或 None"""
    i = 0
    while i + 4 <= len(rx):
        if rx[i] == 0xAA and rx[i + 1] == 0x55:
            ln = rx[i + 2]
            if i + 4 + ln + 1 <= len(rx) and ln <= 64:
                body = rx[i + 2:i + 3 + ln + 1]
                if crc8(body) == rx[i + 4 + ln]:
                    return rx[i + 3], rx[i + 4:i + 4 + ln]
        i += 1
    return None

def servo_set(pin, ang10, lo, hi):
    """ang10/lo/hi 单位 0.1°"""
    pl = bytes([pin, ang10 >> 8, ang10 & 0xFF,
                lo >> 8, lo & 0xFF, hi >> 8, hi & 0xFF])
    rx = send_round(0x20, pl)
    r = find_ack(rx)
    if r is None or r[0] != 0x06 or len(r[1]) < 4 or r[1][0] != 0x20:
        return None
    return (r[1][1] << 8) | r[1][2], r[1][3]   # (实际角值, status)

def servo_query():
    rx = send_round(0x21, b'')
    r = find_ack(rx)
    if r is None or r[0] != 0x06 or len(r[1]) < 9 or r[1][0] != 0x21:
        return None
    p = r[1]
    return {'pin': p[1], 'mode': p[2],
            'ang': (p[3] << 8) | p[4],
            'lo': (p[5] << 8) | p[6],
            'hi': (p[7] << 8) | p[8]}

# 清缓冲
t0 = time.time()
while time.time() - t0 < 1.0:
    os.read(fd, 4096)

ok_all = True

# --- 1) 静态定位 30° / 150°, ACK 角值逐字节核对 ---
for target in (300, 1500):
    r = servo_set(0, target, 0, 0)
    ok = r is not None and r[0] == target and r[1] == 0
    ok_all &= ok
    print(f"L1_SET[{target / 10:.0f}deg]={'OK' if ok else 'FAIL'} resp={r}")
    time.sleep(0.6)

# --- 2) 查询回读: 角值 == 150° 且静态 ---
q = servo_query()
ok = q is not None and q['ang'] == 1500 and q['mode'] == 0
ok_all &= ok
print(f"L1_QUERY_STATIC={'OK' if ok else 'FAIL'} resp={q}")

# --- 3) 周期扫描任务活体: 30°..150°, 2.5s 后角值应已离开起点 ---
r = servo_set(0, 300, 300, 1500)
ok = r is not None and r[1] == 0
ok_all &= ok
print(f"L1_SWEEP_START={'OK' if ok else 'FAIL'} resp={r}")
time.sleep(2.5)
q = servo_query()
ok = q is not None and q['mode'] == 1 and q['ang'] != 300 \
     and q['lo'] == 300 and q['hi'] == 1500
ok_all &= ok
print(f"L1_SWEEP_ALIVE={'OK' if ok else 'FAIL'} resp={q}")

# --- 4) 占位脚保护: pin=48 (WS2812 数据脚) 必须拒绝, status=1 ---
r = servo_set(48, 900, 0, 0)
ok = r is not None and r[1] == 1
ok_all &= ok
print(f"L1_PIN_GUARD={'OK' if ok else 'FAIL'} resp={r}")

# --- 5) 收束: 回中位 90°, 停扫描 ---
r = servo_set(0, 900, 0, 0)
print(f"L1_HOME resp={r}")

os.close(fd)
print("L1_PASS=YES" if ok_all else "L1_PASS=NO")
print("注: 实体角误差 (验收标准 ≤2°) 需量角器目视记录, 脚本只核对协议与任务活性")
