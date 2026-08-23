#!/usr/bin/env python3
# pi_chip_probe.py - M6 espflash 握手判决 (只读, 不动 flash)
import subprocess
r = subprocess.run(['/usr/local/bin/espflash', '-S', 'board-info',
                    '-p', '/dev/ttyACM0'],
                   capture_output=True, text=True, timeout=60)
print("STDOUT:")
print(r.stdout[:2000])
print("STDERR:")
print(r.stderr[:2000])
print("RC=", r.returncode)
