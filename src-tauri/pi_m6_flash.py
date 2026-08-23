#!/usr/bin/env python3
# pi_m6_flash.py - M6 端侧自烧录判决: Pi write-bin 写镜像 -> 硬复位 -> 读回校验
import subprocess, time

EF = '/usr/local/bin/espflash'
IMG = '/home/duanyan/uart_linkm5.bin'
t0 = time.time()
r = subprocess.run([EF, '-S', 'write-bin', '0x0', IMG,
                    '-p', '/dev/ttyACM0', '--after', 'hard-reset'],
                   capture_output=True, text=True, timeout=180)
dt = time.time() - t0
print(f"FLASH_RC={r.returncode} elapsed={dt:.1f}s")
print("STDOUT:", r.stdout[-1200:])
print("STDERR:", r.stderr[-1200:])
if r.returncode != 0:
    raise SystemExit(1)

# 复位后等 USB 重枚举, 再读回 0..8192 与新镜像比对 (前 6240 字节)
time.sleep(6)
for retry in range(5):
    r2 = subprocess.run([EF, '-S', 'read-flash', '0', '8192',
                         '/home/duanyan/fw_after.bin',
                         '-p', '/dev/ttyACM0'],
                        capture_output=True, text=True, timeout=120)
    if r2.returncode == 0:
        break
    print(f"readback retry {retry}: {r2.stderr[-200:]}")
    time.sleep(3)
print(f"READBACK_RC={r2.returncode}")
if r2.returncode == 0:
    a = open(IMG, 'rb').read()
    b = open('/home/duanyan/fw_after.bin', 'rb').read()
    ok = a == b[:len(a)]
    print(f"VERIFY_MATCH={'YES' if ok else 'NO'} (img={len(a)}B)")
print("M6_FLASH_DONE")
