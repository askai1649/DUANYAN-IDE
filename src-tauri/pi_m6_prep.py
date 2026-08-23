#!/usr/bin/env python3
# pi_m6_prep.py - M6 烧录前置: write-bin 语法确认 + 现有镜像读回备份
import subprocess

r = subprocess.run(['/usr/local/bin/espflash', '-S', 'write-bin', '--help'],
                   capture_output=True, text=True, timeout=30)
print("=== WRITE-BIN HELP ===")
print(r.stdout[:1500])
print(r.stderr[:500])

r2 = subprocess.run(['/usr/local/bin/espflash', '-S', 'read-flash',
                     '0', '8192', '/home/duanyan/fw_backup_8k.bin',
                     '-p', '/dev/ttyACM0'],
                    capture_output=True, text=True, timeout=120)
print("=== READ-BACKUP RC=", r2.returncode, "===")
print(r2.stdout[-800:])
print(r2.stderr[-800:])
