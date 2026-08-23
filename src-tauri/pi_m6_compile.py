#!/usr/bin/env python3
# pi_m6_compile.py - M6 编译段判决: Pi 自编译 → (可选参考比对) → 自烧 → 读回 → 启动回归
# 验收对象默认: uart_linkl1.c (L1 舵机固件, 同时为 L1 备产烧录镜像)
#
# 前置 (开发机):
#   sudo systemctl stop duanyan-edge   # 释放 /dev/ttyAMA0
#   scp uart_linkl1.c duanyan@pi:/home/duanyan/
#   (可选, 镜像一致性加测) 开发机 --build-only 产参考镜像后:
#   scp flash_out/uart_linkl1.bin duanyan@pi:/home/duanyan/uart_linkl1_ref.bin
#
# 判决链:
#   1) TOOLCHAIN_OK   ~/xtensa-esp32s3-elf 工具链在场
#   2) BUILD_OK       pi_build_esp.py 编译产镜像 (格式与开发机产线逐字节对齐)
#   3) IMAGE_MATCH    与开发机参考镜像逐字节一致 (无参考则跳过;
#                     工具链版本不同可能 NO —— 此时以启动回归为权威判决)
#   4) FLASH_RC=0     espflash write-bin 写自产镜像 → 硬复位
#   5) VERIFY_MATCH   读回 flash 与自产镜像逐字节一致 (读写通路自证)
#   6) LINK_ALIVE     复位后心跳 CMD 0x01 回 ACK → Pi 自编译固件启动, 链路活
#   M6_COMPILE_PASS=YES 需 2+4+5+6 全过 (3 为加测项, 不影响主判决)
#
# 用法: python3 pi_m6_compile.py [c源文件] [参考镜像]
import os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = sys.argv[1] if len(sys.argv) > 1 else '/home/duanyan/uart_linkl1.c'
REF = sys.argv[2] if len(sys.argv) > 2 else '/home/duanyan/uart_linkl1_ref.bin'
OUT = '/home/duanyan/pi_build_out'
EF = '/usr/local/bin/espflash'
PORT = '/dev/ttyACM0'
TOOLCHAIN = os.path.expanduser('~/xtensa-esp32s3-elf/bin/xtensa-esp32s3-elf-gcc')

build_ok = flash_ok = verify_ok = link_ok = False
image_match = None

# --- 1) 工具链在场 ---
tool_ok = os.path.exists(TOOLCHAIN)
print(f"TOOLCHAIN_OK={'YES' if tool_ok else 'NO'} ({TOOLCHAIN})")
if not tool_ok:
    print("M6_COMPILE_PASS=NO (工具链缺失, 编译段无法执行)")
    raise SystemExit(1)

# --- 2) Pi 自编译 ---
stem = os.path.splitext(os.path.basename(SRC))[0]
IMG = f"{OUT}/{stem}.bin"
r = subprocess.run([sys.executable, os.path.join(HERE, 'pi_build_esp.py'), SRC, OUT],
                   capture_output=True, text=True, timeout=300)
print("BUILD:", r.stdout.strip()[-400:])
if r.returncode != 0:
    print("STDERR:", r.stderr[-800:])
build_ok = r.returncode == 0 and os.path.exists(IMG)
print(f"BUILD_OK={'YES' if build_ok else 'NO'}")
if not build_ok:
    print("M6_COMPILE_PASS=NO (编译失败)")
    raise SystemExit(1)
img = open(IMG, 'rb').read()

# --- 3) 参考镜像比对 (加测项, 无参考跳过) ---
if os.path.exists(REF):
    ref = open(REF, 'rb').read()
    image_match = img == ref
    print(f"IMAGE_MATCH={'YES' if image_match else 'NO'} "
          f"(pi={len(img)}B ref={len(ref)}B)")
else:
    print("IMAGE_MATCH=SKIP (无参考镜像, 仅做启动回归判决)")

# --- 4) 自产镜像自烧 ---
t0 = time.time()
r = subprocess.run([EF, '-S', 'write-bin', '0x0', IMG,
                    '-p', PORT, '--after', 'hard-reset'],
                   capture_output=True, text=True, timeout=180)
dt = time.time() - t0
flash_ok = r.returncode == 0
print(f"FLASH_RC={r.returncode} elapsed={dt:.1f}s "
      f"FLASH_OK={'YES' if flash_ok else 'NO'}")
if not flash_ok:
    print("STDERR:", r.stderr[-600:])
    print("M6_COMPILE_PASS=NO (烧录失败)")
    raise SystemExit(1)

# --- 5) 读回校验 ---
time.sleep(6)   # 等 USB 重枚举
BACK = '/home/duanyan/fw_compile_back.bin'
verify_ok = False
for retry in range(5):
    r2 = subprocess.run([EF, '-S', 'read-flash', '0', str(len(img)), BACK,
                         '-p', PORT],
                        capture_output=True, text=True, timeout=120)
    if r2.returncode == 0:
        b = open(BACK, 'rb').read()
        verify_ok = b[:len(img)] == img
        break
    print(f"readback retry {retry}: {r2.stderr[-200:]}")
    time.sleep(3)
print(f"VERIFY_MATCH={'YES' if verify_ok else 'NO'} ({len(img)}B)")

# --- 6) 启动回归: 心跳 CMD 0x01 → ACK 回显 ---
def crc8(bs):
    c = 0
    for b in bs:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c

def heartbeat_once():
    """U突发 + 心跳帧, 核对 ACK 回显: AA 55 01 06 01 CRC"""
    import termios
    fd = os.open('/dev/ttyAMA0', os.O_RDWR | os.O_NOCTTY)
    a = termios.tcgetattr(fd)
    a[0] = 0; a[1] = 0; a[3] = 0
    a[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
    a[4] = termios.B9600; a[5] = termios.B9600
    a[6][termios.VMIN] = 0; a[6][termios.VTIME] = 1
    termios.tcsetattr(fd, termios.TCSANOW, a)
    t0 = time.time()
    while time.time() - t0 < 1.0:
        os.read(fd, 4096)   # 清缓冲
    body = bytes([0, 0x01])   # LEN=0 CMD=0x01
    os.write(fd, b'UUUUUUUU')
    time.sleep(0.2)
    os.write(fd, b'\xAA\x55' + body + bytes([crc8(body)]))
    time.sleep(0.5)
    rx = os.read(fd, 256)
    os.close(fd)
    exp = b'\xAA\x55\x01\x06\x01' + bytes([crc8(b'\x01\x06\x01')])
    return exp in rx, rx

if verify_ok:
    print("wait firmware boot (worst case: servo calib ~10s)...")
    time.sleep(12)
    for i in range(3):
        ok, rx = heartbeat_once()
        print(f"HEARTBEAT[{i}]={'OK' if ok else 'RETRY'} rx={rx.hex()}")
        if ok:
            link_ok = True
            break
        time.sleep(2)

print(f"LINK_ALIVE={'YES' if link_ok else 'NO'}")
print("M6_COMPILE_PASS=YES" if build_ok and flash_ok and verify_ok and link_ok
      else "M6_COMPILE_PASS=NO")
if image_match is False:
    print("注: IMAGE_MATCH=NO 但主判决通过 → 工具链版本差异, 功能等价以启动回归为准,")
    print("    建议登记两边 gcc --version 于模式B文档备查")
print("M6_COMPILE_DONE")
