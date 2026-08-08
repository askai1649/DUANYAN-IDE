//! DUANYAN Native Flasher - ESP32 系列芯片原生烧录器
//!
//! 基于 espflash 库实现，零外部依赖（不需要 Python / esptool / ESP-IDF）。
//! 支持: ESP32 / ESP32-S2 / ESP32-S3 / ESP32-C3 / ESP32-C6 / ESP32-H2
//!
//! 功能:
//! - 自动检测串口设备
//! - 读取芯片信息（型号、MAC、Flash 大小）
//! - 烧录二进制固件到指定地址
//! - 擦除 Flash

use espflash::connection::{Connection, ResetAfterOperation, ResetBeforeOperation};
use espflash::flasher::Flasher;
use espflash::target::{DefaultProgressCallback, ProgressCallbacks};
use serde::Serialize;
use std::time::Duration;

/// 串口设备信息（返回给前端）
#[derive(Debug, Clone, Serialize)]
pub struct PortInfo {
    pub port_name: String,
    pub description: String,
    pub vid: Option<u16>,
    pub pid: Option<u16>,
    pub is_esp_device: bool,
}

/// 芯片信息（返回给前端）
#[derive(Debug, Clone, Serialize)]
pub struct ChipInfoResult {
    pub chip: String,
    pub mac_address: String,
    pub flash_size: String,
    pub features: Vec<String>,
    pub crystal_frequency: String,
}

/// 烧录结果
#[derive(Debug, Clone, Serialize)]
pub struct FlashResult {
    pub success: bool,
    pub message: String,
    pub bytes_written: usize,
    pub duration_ms: u64,
}

// ========== 核心功能 ==========

/// 已知 ESP 设备 USB VID
const ESP_VIDS: &[u16] = &[
    0x10C4, // Silicon Labs (CP2102/CP2104)
    0x1A86, // QinHeng (CH340/CH341) - 你的板子用这个
    0x303A, // Espressif native USB (ESP32-S2/S3/C3)
    0x0403, // FTDI
];

/// 扫描所有串口，标记可能的 ESP 设备
pub fn detect_ports() -> Vec<PortInfo> {
    let mut ports = Vec::new();

    if let Ok(available) = serialport::available_ports() {
        for port in available {
            let (vid, pid, is_esp) = match &port.port_type {
                serialport::SerialPortType::UsbPort(info) => {
                    let esp = ESP_VIDS.contains(&info.vid);
                    (Some(info.vid), Some(info.pid), esp)
                }
                _ => (None, None, false),
            };

            let description = match &port.port_type {
                serialport::SerialPortType::UsbPort(info) => {
                    info.product.clone().unwrap_or_else(|| "USB Serial".to_string())
                }
                _ => "Serial Port".to_string(),
            };

            ports.push(PortInfo {
                port_name: port.port_name,
                description,
                vid,
                pid,
                is_esp_device: is_esp,
            });
        }
    }

    // ESP 设备排前面
    ports.sort_by(|a, b| b.is_esp_device.cmp(&a.is_esp_device));
    ports
}

/// 打开串口连接并创建 Flasher
fn open_flasher(port_name: &str, baud: u32) -> Result<Flasher, String> {
    // Windows: open_native() 返回 COMPort (espflash 需要的具体类型)
    let serial = serialport::new(port_name, 115_200)
        .timeout(Duration::from_secs(3))
        .open_native()
        .map_err(|e| format!("无法打开串口 {}: {}", port_name, e))?;

    // 获取 USB 信息
    let port_info = serialport::available_ports()
        .unwrap_or_default()
        .into_iter()
        .find(|p| p.port_name == port_name)
        .and_then(|p| match p.port_type {
            serialport::SerialPortType::UsbPort(info) => Some(info),
            _ => None,
        })
        .unwrap_or_else(|| serialport::UsbPortInfo {
            vid: 0,
            pid: 0,
            serial_number: None,
            manufacturer: None,
            product: None,
        });

    // 创建 espflash 连接
    let connection = Connection::new(
        serial,
        port_info,
        ResetAfterOperation::HardReset,
        ResetBeforeOperation::DefaultReset,
        115_200,
    );

    // 连接到芯片（自动检测型号）
    let flasher = Flasher::connect(
        connection,
        true,        // use_stub: 使用 stub loader 加速
        true,        // verify: 写入后校验
        false,       // skip: 不跳过
        None,        // chip: 自动检测
        Some(baud),  // baud: 连接后切换到的波特率
    )
    .map_err(|e| {
        format!(
            "连接芯片失败: {}\n提示: 请按住 BOOT 键后按 RESET 键进入下载模式",
            e
        )
    })?;

    Ok(flasher)
}

/// 读取芯片信息
pub fn read_chip_info(port_name: &str) -> Result<ChipInfoResult, String> {
    let mut flasher = open_flasher(port_name, 460_800)?;

    let chip = flasher.chip();
    let info = flasher
        .device_info()
        .map_err(|e| format!("读取设备信息失败: {}", e))?;

    // MAC 地址 (已经是 Option<String>)
    let mac_str = info.mac_address.unwrap_or_else(|| "Unknown".to_string());

    // Flash 大小
    let flash_size_str = format!("{:?}", info.flash_size);

    // 晶振频率
    let xtal_str = format!("{:?}", info.crystal_frequency);

    Ok(ChipInfoResult {
        chip: format!("{:?}", chip),
        mac_address: mac_str,
        flash_size: flash_size_str,
        features: info.features,
        crystal_frequency: xtal_str,
    })
}

/// 烧录二进制数据到 Flash 指定地址
pub fn flash_binary(
    port_name: &str,
    data: &[u8],
    address: u32,
    baud: u32,
    progress: &mut dyn ProgressCallbacks,
) -> Result<FlashResult, String> {
    let start = std::time::Instant::now();

    let mut flasher = open_flasher(port_name, baud)?;

    // 禁用看门狗（防止烧录过程中复位）
    let _ = flasher.disable_watchdog();

    // 写入 Flash
    flasher
        .write_bin_to_flash(address, data, progress)
        .map_err(|e| format!("烧录失败: {}", e))?;

    // 复位芯片，启动新固件
    flasher
        .connection()
        .reset()
        .map_err(|e| format!("复位芯片失败（固件可能已写入）: {}", e))?;

    let duration = start.elapsed();

    Ok(FlashResult {
        success: true,
        message: format!(
            "烧录成功! {} bytes → 0x{:08X}, 耗时 {:.1}s",
            data.len(),
            address,
            duration.as_secs_f64()
        ),
        bytes_written: data.len(),
        duration_ms: duration.as_millis() as u64,
    })
}

/// 擦除整个 Flash
pub fn erase_flash(port_name: &str) -> Result<String, String> {
    let mut flasher = open_flasher(port_name, 460_800)?;
    flasher
        .erase_flash()
        .map_err(|e| format!("擦除失败: {}", e))?;
    Ok("Flash 擦除完成".to_string())
}

/// 读回 Flash 内容 (诊断用)
pub fn read_flash_region(port_name: &str, address: u32, size: u32) -> Result<Vec<u8>, String> {
    let mut flasher = open_flasher(port_name, 460_800)?;
    let _ = flasher.disable_watchdog();
    let tmp = std::env::temp_dir().join("duanyan_flash_read.bin");
    flasher
        .read_flash_rom(address, size, 0x1000, 64, tmp.clone())
        .map_err(|e| format!("读取 Flash 失败: {}", e))?;
    std::fs::read(&tmp).map_err(|e| format!("读取临时文件失败: {}", e))
}

/// 从文件路径烧录（便捷接口）
pub fn flash_file(
    port_name: &str,
    bin_path: &str,
    address: u32,
    baud: u32,
) -> Result<FlashResult, String> {
    let data =
        std::fs::read(bin_path).map_err(|e| format!("读取固件文件失败 {}: {}", bin_path, e))?;

    if data.is_empty() {
        return Err("固件文件为空".to_string());
    }

    let mut progress = DefaultProgressCallback::default();
    flash_binary(port_name, &data, address, baud, &mut progress)
}

// ========== 工具链检测与编译 ==========

/// Xtensa GCC 工具链信息
#[derive(Debug, Clone, Serialize)]
pub struct ToolchainInfo {
    pub found: bool,
    pub gcc_path: String,
    pub version: String,
}

/// 编译结果
#[derive(Debug, Clone, Serialize)]
pub struct BuildResult {
    pub success: bool,
    pub bin_path: String,
    pub bin_size: usize,
    pub message: String,
}

/// 已知的 xtensa-gcc 工具链路径
const XTENSA_GCC_PATHS: &[&str] = &[
    r"C:\Users\askai\.platformio\packages\toolchain-xtensa-esp32s3\bin\xtensa-esp32s3-elf-gcc.exe",
    r"C:\Espressif\tools\xtensa-esp32s3-elf\bin\xtensa-esp32s3-elf-gcc.exe",
    r"C:\msys64\mingw64\bin\xtensa-esp32s3-elf-gcc.exe",
];

/// 自动检测 xtensa-gcc 工具链
pub fn detect_toolchain() -> ToolchainInfo {
    // 先检查已知路径
    for path in XTENSA_GCC_PATHS {
        let p = std::path::Path::new(path);
        if p.exists() {
            let version = get_gcc_version(path);
            return ToolchainInfo {
                found: true,
                gcc_path: path.to_string(),
                version,
            };
        }
    }

    // 尝试 PATH 中查找
    if let Ok(output) = std::process::Command::new("where")
        .arg("xtensa-esp32s3-elf-gcc")
        .output()
    {
        if output.status.success() {
            let path = String::from_utf8_lossy(&output.stdout)
                .lines()
                .next()
                .unwrap_or("")
                .trim()
                .to_string();
            if !path.is_empty() {
                let version = get_gcc_version(&path);
                return ToolchainInfo {
                    found: true,
                    gcc_path: path,
                    version,
                };
            }
        }
    }

    ToolchainInfo {
        found: false,
        gcc_path: String::new(),
        version: String::new(),
    }
}

fn get_gcc_version(gcc_path: &str) -> String {
    std::process::Command::new(gcc_path)
        .arg("--version")
        .output()
        .map(|o| {
            String::from_utf8_lossy(&o.stdout)
                .lines()
                .next()
                .unwrap_or("unknown")
                .to_string()
        })
        .unwrap_or_else(|_| "unknown".to_string())
}

/// 编译裸机 C 文件并生成可烧录的 bin
///
/// 流程: C → ELF (xtensa-gcc) → BIN (esptool elf2image 或 objcopy)
pub fn build_bare_metal(
    c_source_path: &str,
    output_dir: &str,
    linker_script: Option<&str>,
) -> Result<BuildResult, String> {
    let toolchain = detect_toolchain();
    if !toolchain.found {
        return Err(
            "未找到 xtensa-esp32s3-elf-gcc 工具链\n\
             请安装: PlatformIO 或 Espressif 工具链"
                .to_string(),
        );
    }

    let gcc = &toolchain.gcc_path;
    let stem = std::path::Path::new(c_source_path)
        .file_stem()
        .map(|s| s.to_string_lossy().to_string())
        .unwrap_or_else(|| "firmware".to_string());

    let out_dir = std::path::Path::new(output_dir);
    std::fs::create_dir_all(out_dir)
        .map_err(|e| format!("创建输出目录失败: {}", e))?;

    let elf_path = out_dir.join(format!("{}.elf", stem));
    let bin_path = out_dir.join(format!("{}.bin", stem));

    // 构建 GCC 命令
    let mut cmd = std::process::Command::new(gcc);
    cmd.arg("-nostdlib")
        .arg("-ffreestanding")
        .arg("-O2")
        .arg("-mlongcalls")
        .arg("-Wl,--gc-sections");

    // 链接脚本
    if let Some(ld) = linker_script {
        cmd.arg("-T").arg(ld);
    }

    cmd.arg("-o").arg(&elf_path).arg(c_source_path);

    // 执行编译
    let output = cmd
        .output()
        .map_err(|e| format!("启动编译器失败: {}", e))?;

    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        return Err(format!("编译失败:\n{}", stderr));
    }

    // ELF → BIN: 使用 objcopy
    let objcopy_path = gcc.replace("gcc", "objcopy");
    let objcopy_output = std::process::Command::new(&objcopy_path)
        .arg("-O")
        .arg("binary")
        .arg(&elf_path)
        .arg(&bin_path)
        .output()
        .map_err(|e| format!("启动 objcopy 失败: {}", e))?;

    if !objcopy_output.status.success() {
        let stderr = String::from_utf8_lossy(&objcopy_output.stderr);
        return Err(format!("objcopy 失败:\n{}", stderr));
    }

    let bin_size = std::fs::metadata(&bin_path)
        .map(|m| m.len() as usize)
        .unwrap_or(0);

    Ok(BuildResult {
        success: true,
        bin_path: bin_path.to_string_lossy().to_string(),
        bin_size,
        message: format!(
            "编译成功: {} ({} bytes)",
            bin_path.display(),
            bin_size
        ),
    })
}

/// 一键编译+烧录
pub fn build_and_flash_native(
    c_source_path: &str,
    port_name: &str,
    output_dir: &str,
    address: u32,
    baud: u32,
    linker_script: Option<&str>,
) -> Result<FlashResult, String> {
    // Step 1: 编译
    let build = build_bare_metal(c_source_path, output_dir, linker_script)?;

    // Step 2: 烧录
    flash_file(port_name, &build.bin_path, address, baud)
}

// =============================================================================
// ESP32-S3 Application Image Format
// =============================================================================

/// ESP32-S3 镜像头 (固定 24 bytes, esp_image_header_t + extended)
const ESP_IMAGE_MAGIC: u8 = 0xE9;
const ESP32S3_CHIP_ID: u16 = 0x0009;

/// SHA-256 纯 Rust 实现 (无外部依赖, 用于 ESP32-S3 镜像验证哈希)
fn sha256(data: &[u8]) -> [u8; 32] {
    const K: [u32; 64] = [
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    ];
    let mut h: [u32; 8] = [
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    ];
    let bit_len = (data.len() as u64).wrapping_mul(8);
    let mut msg = data.to_vec();
    msg.push(0x80);
    while msg.len() % 64 != 56 {
        msg.push(0);
    }
    msg.extend_from_slice(&bit_len.to_be_bytes());

    let mut w = [0u32; 64];
    for chunk in msg.chunks(64) {
        for i in 0..16 {
            w[i] = u32::from_be_bytes([chunk[i * 4], chunk[i * 4 + 1], chunk[i * 4 + 2], chunk[i * 4 + 3]]);
        }
        for i in 16..64 {
            let s0 = w[i - 15].rotate_right(7) ^ w[i - 15].rotate_right(18) ^ (w[i - 15] >> 3);
            let s1 = w[i - 2].rotate_right(17) ^ w[i - 2].rotate_right(19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16]
                .wrapping_add(s0)
                .wrapping_add(w[i - 7])
                .wrapping_add(s1);
        }
        let (mut a, mut b, mut c, mut d, mut e, mut f, mut g, mut hh) =
            (h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7]);
        for i in 0..64 {
            let s1 = e.rotate_right(6) ^ e.rotate_right(11) ^ e.rotate_right(25);
            let ch = (e & f) ^ ((!e) & g);
            let t1 = hh
                .wrapping_add(s1)
                .wrapping_add(ch)
                .wrapping_add(K[i])
                .wrapping_add(w[i]);
            let s0 = a.rotate_right(2) ^ a.rotate_right(13) ^ a.rotate_right(22);
            let maj = (a & b) ^ (a & c) ^ (b & c);
            let t2 = s0.wrapping_add(maj);
            hh = g;
            g = f;
            f = e;
            e = d.wrapping_add(t1);
            d = c;
            c = b;
            b = a;
            a = t1.wrapping_add(t2);
        }
        h[0] = h[0].wrapping_add(a);
        h[1] = h[1].wrapping_add(b);
        h[2] = h[2].wrapping_add(c);
        h[3] = h[3].wrapping_add(d);
        h[4] = h[4].wrapping_add(e);
        h[5] = h[5].wrapping_add(f);
        h[6] = h[6].wrapping_add(g);
        h[7] = h[7].wrapping_add(hh);
    }

    let mut out = [0u8; 32];
    for (i, v) in h.iter().enumerate() {
        out[i * 4..i * 4 + 4].copy_from_slice(&v.to_be_bytes());
    }
    out
}

/// 创建 ESP32-S3 可启动应用镜像 (支持多段: 代码段 IRAM + 数据段 DRAM)
///
/// 格式 (与 esptool elf2image 输出逐字节对齐):
/// - Image Header (24 bytes, 含 extended header, hash_appended=1)
/// - 每段: Segment Header (8 bytes) + Data (4字节对齐)
/// - 16 字节对齐填充 + Checksum (1 byte)
/// - SHA-256 验证哈希 (32 bytes, ROM 强制校验)
pub fn create_esp32s3_image(segments: &[(u32, &[u8])], entry_addr: u32) -> Vec<u8> {
    let total: usize = segments.iter().map(|(_, d)| d.len() + 8).sum::<usize>() + 96;
    let mut image = Vec::with_capacity(total);

    // === Image Header (恰好 24 bytes) ===
    image.push(ESP_IMAGE_MAGIC);         // 0: magic
    image.push(segments.len() as u8);    // 1: segment_count
    image.push(0x02);                    // 2: spi_mode = DIO
    image.push(0x4F);                    // 3: spi_speed=80MHz(4), spi_size=16MB(F)
    image.extend_from_slice(&entry_addr.to_le_bytes()); // 4-7: entry point
    // --- Extended header ---
    image.push(0xEE);                    // 8: wp_pin = disabled
    image.extend_from_slice(&[0u8; 3]);  // 9-11: clk/q/d drv
    image.extend_from_slice(&ESP32S3_CHIP_ID.to_le_bytes()); // 12-13: chip_id
    image.push(0);                       // 14: min_chip_rev
    image.extend_from_slice(&[0u8; 3]);  // 15-17: reserved
    image.extend_from_slice(&0xFFFFu16.to_le_bytes()); // 18-19: max_rev_full
    image.extend_from_slice(&[0u8; 3]);  // 20-22: reserved
    image.push(1);                       // 23: hash_appended = true
    debug_assert_eq!(image.len(), 24);

    // === 各段: Segment Header (8 bytes) + Data ===
    let mut checksum: u8 = 0xEF;
    for (load_addr, data) in segments {
        let seg_len = (data.len() + 3) / 4 * 4; // 4 字节对齐后的段长
        image.extend_from_slice(&load_addr.to_le_bytes());
        image.extend_from_slice(&(seg_len as u32).to_le_bytes());
        let mut padded = data.to_vec();
        while padded.len() % 4 != 0 {
            padded.push(0);
        }
        image.extend_from_slice(&padded);
        // checksum 只算段数据 (不含 segment header)
        for &b in &padded {
            checksum ^= b;
        }
    }

    // === 填充至 (N*16 - 1), 使 checksum 落在 16 字节边界 ===
    let pad = (15 - image.len() % 16) % 16;
    image.extend(std::iter::repeat(0u8).take(pad));
    image.push(checksum);

    // === SHA-256 验证哈希 (覆盖到 checksum 为止的全部内容) ===
    let hash = sha256(&image);
    image.extend_from_slice(&hash);

    image
}

/// 编译 + 包装 ESP32-S3 镜像 (不烧录), 直出可烧录 .bin
pub fn build_esp32s3_bin(
    c_source_path: &str,
    output_dir: &str,
) -> Result<BuildResult, String> {
    // Step 1: 编译 C → ELF
    let toolchain = detect_toolchain();
    if !toolchain.found {
        return Err("未找到 xtensa-esp32s3-elf-gcc".into());
    }

    let out_dir = std::path::Path::new(output_dir);
    std::fs::create_dir_all(out_dir).map_err(|e| format!("创建目录失败: {}", e))?;

    let stem = std::path::Path::new(c_source_path)
        .file_stem()
        .map(|s| s.to_string_lossy().to_string())
        .unwrap_or("app".into());

    let elf_path = out_dir.join(format!("{}.elf", stem));
    let code_bin_path = out_dir.join(format!("{}_code.bin", stem));
    let data_bin_path = out_dir.join(format!("{}_data.bin", stem));
    let esp_img_path = out_dir.join(format!("{}.bin", stem));
    let ld_path = out_dir.join(format!("{}.ld", stem));

    // 链接脚本: 代码 → IRAM (0x403B2000), 数据 → DRAM (0x3FC88000)
    // ESP32-S3 内部 SRAM 起始 0x3FC88000; 只有代码能放在 0x403xxxxx,
    // 数据访问 IRAM 地址会 LoadStoreError!
    // C 代码模板: void _start(void) __attribute__((section(".entry"), used));
    // (ENTRY(_start) 使 e_entry = 函数真实地址, 跳过前面的 literal pool)
    let ld_script = r#"
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
"#;
    std::fs::write(&ld_path, ld_script)
        .map_err(|e| format!("写入链接脚本失败: {}", e))?;

    // GCC 编译 (裸机: 无标准库, 自定义链接脚本定位段, 不加 --gc-sections 避免删除入口)
    let status = std::process::Command::new(&toolchain.gcc_path)
        .args(&["-nostdlib", "-ffreestanding", "-O2", "-mlongcalls",
                "-Wl,--build-id=none"])
        .arg("-T").arg(&ld_path)
        .arg("-o").arg(&elf_path)
        .arg(c_source_path)
        .status()
        .map_err(|e| format!("GCC 执行失败: {}", e))?;
    if !status.success() {
        return Err("编译失败".into());
    }

    // objcopy 分段导出: 代码段 (IRAM) 与数据段 (DRAM) 各自连续
    let objcopy = toolchain.gcc_path.replace("gcc", "objcopy");
    std::process::Command::new(&objcopy)
        .args(&["-O", "binary",
                "-j", ".entry.literal", "-j", ".entry", "-j", ".text"])
        .arg(&elf_path)
        .arg(&code_bin_path)
        .status()
        .map_err(|e| format!("objcopy(代码) 失败: {}", e))?;
    std::process::Command::new(&objcopy)
        .args(&["-O", "binary", "-j", ".rodata", "-j", ".data"])
        .arg(&elf_path)
        .arg(&data_bin_path)
        .status()
        .map_err(|e| format!("objcopy(数据) 失败: {}", e))?;

    let raw_code = std::fs::read(&code_bin_path)
        .map_err(|e| format!("读取代码 bin 失败: {}", e))?;
    if raw_code.is_empty() {
        return Err("代码段为空: 请确保入口函数标记了 section(\".entry\")".into());
    }
    let raw_data = std::fs::read(&data_bin_path).unwrap_or_default();

    // 从 ELF 头解析真实入口点 (e_entry @ offset 0x18, ELF32-LE)
    let elf_bytes = std::fs::read(&elf_path)
        .map_err(|e| format!("读取 ELF 失败: {}", e))?;
    let entry_addr: u32 = if elf_bytes.len() > 0x1C && &elf_bytes[0..4] == b"\x7FELF" {
        u32::from_le_bytes([elf_bytes[0x18], elf_bytes[0x19], elf_bytes[0x1A], elf_bytes[0x1B]])
    } else {
        return Err("无效 ELF 文件".into());
    };
    if entry_addr == 0 {
        return Err("ELF 入口点为 0: 请确保入口函数标记了 section(\".entry\")".into());
    }

    // Step 2: 包装 ESP 镜像 (代码段 → IRAM, 数据段 → DRAM)
    const IRAM_BASE: u32 = 0x403B_2000;
    const DRAM_BASE: u32 = 0x3FC8_8000;
    let mut segments: Vec<(u32, &[u8])> = vec![(IRAM_BASE, &raw_code)];
    if !raw_data.is_empty() {
        segments.push((DRAM_BASE, &raw_data));
    }
    let esp_image = create_esp32s3_image(&segments, entry_addr);

    std::fs::write(&esp_img_path, &esp_image)
        .map_err(|e| format!("写入镜像失败: {}", e))?;

    Ok(BuildResult {
        success: true,
        bin_path: esp_img_path.to_string_lossy().to_string(),
        bin_size: esp_image.len(),
        message: format!(
            "镜像生成成功: {} ({} bytes, entry=0x{:08X})",
            esp_img_path.display(),
            esp_image.len(),
            entry_addr
        ),
    })
}

/// 编译 + 包装 ESP 镜像 + 烧录 一键完成
pub fn build_flash_esp32s3(
    c_source_path: &str,
    port_name: &str,
    output_dir: &str,
    baud: u32,
) -> Result<FlashResult, String> {
    let build = build_esp32s3_bin(c_source_path, output_dir)?;

    // Step 3: 烧录到 0x0
    // ESP32-S3 ROM bootloader 从 flash 偏移 0x0 读取镜像头
    // (0x10000 是二级 bootloader 加载 app 的地址, 无 bootloader 时直接用 0x0)
    let esp_image = std::fs::read(&build.bin_path)
        .map_err(|e| format!("读取镜像失败: {}", e))?;
    let mut progress = DefaultProgressCallback::default();
    flash_binary(port_name, &esp_image, 0x0, baud, &mut progress)
}

// =============================================================================
// 串口监控 (验证闭环)
// =============================================================================

/// 监控串口输出，用于验证烧录后程序真实运行
#[cfg(feature = "bridge_serial")]
pub fn monitor_port(port_name: &str, baud: u32, duration_secs: u32) -> Result<String, String> {
    use std::io::Read;
    let mut port = serialport::new(port_name, baud)
        .timeout(Duration::from_millis(100))
        .open()
        .map_err(|e| format!("打开串口失败: {}", e))?;

    // 烧录后芯片自动复位，等待启动日志
    let start = std::time::Instant::now();
    let mut buf = [0u8; 512];
    let mut out = Vec::new();
    while start.elapsed().as_secs() < duration_secs as u64 {
        match port.read(&mut buf) {
            Ok(n) if n > 0 => out.extend_from_slice(&buf[..n]),
            Ok(_) => {}
            Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => continue,
            Err(e) => return Err(format!("读取失败: {}", e)),
        }
    }
    Ok(String::from_utf8_lossy(&out).into_owned())
}
