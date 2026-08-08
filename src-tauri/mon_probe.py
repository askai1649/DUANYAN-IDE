import serial, time, sys

port = sys.argv[1] if len(sys.argv) > 1 else 'COM4'
baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
secs = int(sys.argv[3]) if len(sys.argv) > 3 else 20

s = serial.Serial(port, baud, timeout=0.2)
# 触发硬复位 (DTR/RTS 翻转, 与烧录器一致)
s.dtr = False
s.rts = True
time.sleep(0.1)
s.rts = False
time.sleep(0.1)

t0 = time.time()
buf = b''
while time.time() - t0 < secs:
    d = s.read(4096)
    if d:
        buf += d
        stamp = time.time() - t0
        try:
            txt = d.decode('utf-8', 'replace')
        except Exception:
            txt = repr(d)
        print(f"[{stamp:5.1f}s] {txt!r}", flush=True)
s.close()
print(f"== total {len(buf)} bytes @ {baud} ==")
