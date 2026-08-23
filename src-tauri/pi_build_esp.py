#!/usr/bin/env python3
# pi_build_esp.py - M6 端侧编译产线: C → xtensa ELF → ESP32-S3 可启动镜像
# 镜像格式与 DUANYAN-IDE native_flasher::create_esp32s3_image 逐字节对齐
import hashlib, os, subprocess, sys

TOOLCHAIN = os.path.expanduser(
    "~/xtensa-esp32s3-elf/bin/xtensa-esp32s3-elf-gcc")

LD_SCRIPT = """
ENTRY(_start)
SECTIONS {
    . = 0x403B2000;
    .entry.literal : { *(.entry.literal) }
    .entry : { KEEP(*(.entry)) }
    .text : { *(.literal .text .text.*) }
    . = 0x3FC88000;
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { __bss_start = .; *(.bss .bss.* COMMON) __bss_end = .; }
    /DISCARD/ : { *(.comment) *(.xt.lit) *(.xt.prop) *(.eh_frame) }
}
"""

ESP_IMAGE_MAGIC = 0xE9
ESP32S3_CHIP_ID = 0x0009
IRAM_BASE = 0x403B2000
DRAM_BASE = 0x3FC88000


def create_esp32s3_image(segments, entry_addr):
    """与 Rust 版 create_esp32s3_image 逐字节一致"""
    img = bytearray()
    img.append(ESP_IMAGE_MAGIC)
    img.append(len(segments))
    img.append(0x02)  # spi_mode = DIO
    img.append(0x4F)  # spi_speed=80MHz(4), spi_size=16MB(F)
    img += entry_addr.to_bytes(4, "little")
    img.append(0xEE)  # wp_pin disabled
    img += b"\x00" * 3
    img += ESP32S3_CHIP_ID.to_bytes(2, "little")
    img.append(0)  # min_chip_rev
    img += b"\x00" * 3
    img += (0xFFFF).to_bytes(2, "little")  # max_rev_full
    img += b"\x00" * 3
    img.append(1)  # hash_appended
    assert len(img) == 24

    checksum = 0xEF
    for load_addr, data in segments:
        padded = data + b"\x00" * ((-len(data)) % 4)
        img += load_addr.to_bytes(4, "little")
        img += len(padded).to_bytes(4, "little")
        img += padded
        for b in padded:
            checksum ^= b

    img += b"\x00" * ((15 - len(img) % 16) % 16)
    img.append(checksum)
    img += hashlib.sha256(img).digest()
    return bytes(img)


def build(c_path, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    stem = os.path.splitext(os.path.basename(c_path))[0]
    elf = f"{out_dir}/{stem}.elf"
    code_bin = f"{out_dir}/{stem}_code.bin"
    data_bin = f"{out_dir}/{stem}_data.bin"
    esp_img = f"{out_dir}/{stem}.bin"
    ld = f"{out_dir}/{stem}.ld"
    open(ld, "w").write(LD_SCRIPT)

    objcopy = TOOLCHAIN.replace("gcc", "objcopy")

    r = subprocess.run(
        [TOOLCHAIN, "-nostdlib", "-ffreestanding", "-O2", "-mlongcalls",
         "-Wl,--build-id=none", "-T", ld, "-o", elf, c_path],
        capture_output=True, text=True)
    if r.returncode != 0:
        print("COMPILE_FAIL:", r.stderr[-1500:])
        sys.exit(1)

    for args, out in ((["-j", ".entry.literal", "-j", ".entry", "-j", ".text"], code_bin),
                      (["-j", ".rodata", "-j", ".data"], data_bin)):
        r = subprocess.run([objcopy, "-O", "binary"] + args + [elf, out],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print("OBJCOPY_FAIL:", r.stderr[-800:])
            sys.exit(1)

    raw_code = open(code_bin, "rb").read()
    if not raw_code:
        print("COMPILE_FAIL: 代码段为空 (缺 .entry 段)")
        sys.exit(1)
    raw_data = open(data_bin, "rb").read() if os.path.exists(data_bin) else b""

    elf_bytes = open(elf, "rb").read()
    assert elf_bytes[:4] == b"\x7FELF", "无效 ELF"
    entry = int.from_bytes(elf_bytes[0x18:0x1C], "little")
    assert entry != 0, "ELF 入口点为 0"

    segments = [(IRAM_BASE, raw_code)]
    if raw_data:
        segments.append((DRAM_BASE, raw_data))
    image = create_esp32s3_image(segments, entry)
    open(esp_img, "wb").write(image)
    print(f"BUILD_OK {esp_img} {len(image)} entry=0x{entry:08X}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("usage: pi_build_esp.py <c_file> [out_dir]")
        sys.exit(2)
    build(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "pi_build_out")
