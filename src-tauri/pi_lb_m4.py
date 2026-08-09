#!/usr/bin/env python3
# pi_lb_m4.py - M4 传感回传链路验收 (Zero 2W 侧)
# 用法: python3 pi_lb_m4.py
# 流程: 1) CMD 0x02 传感查询 ×3, 逐字节核对 ACK 载荷 [0x02][sid][hi][lo][status]
#       2) 取均值 → Pi 侧湿度决策 (干→红 湿→蓝 中→绿, 阈值同 edge_daemon)
#       3) 下发决策灯效 CMD 0x10 并核对 ACK
# 判定: M4_PASS=YES 需 3/3 传感帧结构合法 + status=0 + 决策灯效 ACK 通过
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
    """U突发 + 命令帧, 收应答。返回 (rx_bytes)"""
    os.write(fd, b'UUUUUUUU')
    time.sleep(0.2)
    body = bytes([len(payload), cmd]) + payload
    os.write(fd, b'\xAA\x55' + body + bytes([crc8(body)]))
    time.sleep(0.3)
    return os.read(fd, 256)

def parse_ack_sensor(rx):
    """核对 ACK 传感帧: AA 55 05 06 02 01 hi lo st CRC → raw
    线协议: LEN 只计载荷(含回声CMD), CRC8 覆盖 LEN..载荷尾, 位于 rx[4+ln]"""
    if len(rx) < 10 or rx[0] != 0xAA or rx[1] != 0x55:
        return None
    ln, ack_cmd, echo = rx[2], rx[3], rx[4]
    if ln != 5 or ack_cmd != 0x06 or echo != 0x02:
        return None
    body = rx[2:4 + ln]
    if crc8(body) != rx[4 + ln]:
        return None
    # payload: [0x02回声=rx4][sid][hi][lo][status]
    sid, hi, lo, st = rx[5], rx[6], rx[7], rx[8]
    if sid != 1 or st != 0:
        return None
    return (hi << 8) | lo

# 清缓冲
t0 = time.time()
while time.time() - t0 < 1.0:
    os.read(fd, 4096)

# --- 1) 传感查询 ×3 ---
raws = []
for i in range(3):
    rx = send_round(0x02, bytes([0x01]))
    raw = parse_ack_sensor(rx)
    ok = raw is not None
    print(f"M4_SENSOR[{i}]={'OK' if ok else 'FAIL'} raw={raw} rx={rx.hex()}")
    if ok:
        raws.append(raw)
    time.sleep(0.5)

if len(raws) != 3:
    os.close(fd)
    print("M4_PASS=NO (sensor frames < 3)")
    raise SystemExit

avg = sum(raws) // len(raws)

# --- 2) Pi 侧决策 (阈值与 edge_daemon soil_decide 一致) ---
DRY_TH, WET_TH = 3000, 1500
if avg >= DRY_TH:
    hue, why = 0, 'DRY'      # 红: 浇水告警
elif avg <= WET_TH:
    hue, why = 85, 'WET'     # 蓝
else:
    hue, why = 170, 'MID'    # 绿
print(f"M4_AVG={avg} DECISION={why} hue={hue}")

# --- 3) 决策灯效下发 + ACK 核对 ---
rx = send_round(0x10, bytes([hue, 80, 0]))
body = bytes([4, 0x06, 0x10, hue, 80, 0])
exp = b'\xAA\x55' + body + bytes([crc8(body)])
led_ok = rx == exp
print(f"M4_LED_ACK={'YES' if led_ok else 'NO'} rx={rx.hex()}")

os.close(fd)
print("M4_PASS=YES" if led_ok else "M4_PASS=NO")
