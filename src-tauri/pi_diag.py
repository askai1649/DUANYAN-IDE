import os, subprocess

# 1. firmware 报告的 uart 时钟
try:
    print("uart clock:", subprocess.check_output(["vcgencmd", "measure_clock", "uart"]).decode().strip())
except Exception as e:
    print("vcgencmd uart err:", e)

# 2. pinctrl debug
try:
    p = "/sys/kernel/debug/pinctrl/soc/pinmux-pins"
    if os.path.exists(p):
        for line in open(p, errors="ignore"):
            if "pin 14" in line or "pin 15" in line:
                print("PINMUX:", line.strip())
except Exception as e:
    print("pinmux err:", e)

# 3. pyserial 环回测试
try:
    import serial
    s = serial.Serial("/dev/ttyAMA0", 115200, timeout=2)
    s.write(b"SERIAL-TEST-12345\n")
    data = s.read(64)
    print("pyserial rx:", len(data), repr(data[:40]))
    s.close()
except ImportError:
    print("pyserial NOT installed")
except Exception as e:
    print("pyserial err:", e)
