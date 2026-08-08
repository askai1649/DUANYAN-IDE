import os, mmap, struct

f = os.open("/dev/gpiomem", os.O_RDWR | os.O_SYNC)
m = mmap.mmap(f, 4096)
gpfsel1 = struct.unpack("<I", m[0x04:0x08])[0]
names = {0: "INPUT", 1: "OUTPUT", 4: "ALT0(UART)", 5: "ALT1", 6: "ALT2", 7: "ALT3", 2: "ALT5", 3: "ALT4"}
for gpio in (14, 15):
    fn = (gpfsel1 >> (3 * (gpio % 10))) & 7
    print("GPIO%d func = %s (%d)" % (gpio, names.get(fn, "?"), fn))
# GPLEV0 看引脚电平
gplev0 = struct.unpack("<I", m[0x34:0x38])[0]
print("GPIO14 level =", (gplev0 >> 14) & 1)
print("GPIO15 level =", (gplev0 >> 15) & 1)
m.close(); os.close(f)
