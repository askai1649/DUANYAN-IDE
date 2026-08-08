import os, mmap, struct

def rd(base, off):
    f = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
    m = mmap.mmap(f, 4096, offset=base)
    v = struct.unpack("<I", m[off:off+4])[0]
    m.close(); os.close(f)
    return v

# GPIO base on BCM2710A1 (Pi Zero 2W): 0x3F200000
gpfsel1 = rd(0x3F200000, 0x04)
f14 = (gpfsel1 >> 12) & 7
f15 = (gpfsel1 >> 15) & 7
names = {0:"INPUT",1:"OUTPUT",4:"ALT0",5:"ALT1",6:"ALT2",7:"ALT3"}
print("GPIO14 func:", names.get(f14, f14))
print("GPIO15 func:", names.get(f15, f15))
# GPPUD clock/status skipped

# PL011 @ 0x3F201000
for k, o in {"IBRD":0x24,"FBRD":0x28,"LCRH":0x2C,"CR":0x30,"FR":0x18}.items():
    print(k, hex(rd(0x3F201000, o)))

ib = rd(0x3F201000, 0x24) & 0xFFFF
fb = rd(0x3F201000, 0x28) & 0x3F
div = ib + fb/64.0
for clk in (250e6, 400e6):
    print("if uart_clk=%dMHz -> baud=%.0f" % (clk/1e6, clk/(16*div)))
