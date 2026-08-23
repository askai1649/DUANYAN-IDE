//! DUANYAN Flash 测试工具
//! 用法:
//!   cargo run --bin flash_test -- COM4                          # 检测芯片
//!   cargo run --bin flash_test -- COM4 test_blink.bin 0x10000  # 烧录 raw bin
//!   cargo run --bin flash_test -- COM4 --build test_blink.c    # C→ESP镜像→烧录
//!   cargo run --bin flash_test -- COM4 --build-only foo.c      # 仅生成镜像不烧录 (M6)
//!   cargo run --bin flash_test -- COM4 --monitor 8             # 监控串口 8 秒

use duanyan_ide_lib::native_flasher;

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let port = args.get(1).map(|s| s.as_str()).unwrap_or("COM4");

    println!("=== DUANYAN Flash Test ===");
    println!("Port: {}", port);
    println!();

    // 模式: --erase (擦除整片 Flash)
    if args.get(2).map(|s| s.as_str()) == Some("--erase") {
        println!("[ERASE] 擦除整片 Flash ({})...", port);
        match native_flasher::erase_flash(port) {
            Ok(msg) => println!("  {}", msg),
            Err(e) => {
                eprintln!("  ERROR: {}", e);
                std::process::exit(1);
            }
        }
        return;
    }

    // 模式: --read (读回 Flash 诊断)
    if args.get(2).map(|s| s.as_str()) == Some("--read") {
        let addr: u32 = args.get(3).and_then(|s| s.parse().ok()).unwrap_or(0x10000);
        println!("[READ FLASH] 0x{:X}, 64 bytes", addr);
        match native_flasher::read_flash_region(port, addr, 64) {
            Ok(data) => {
                for (i, chunk) in data.chunks(16).enumerate() {
                    print!("  {:08X}: ", addr + (i * 16) as u32);
                    for b in chunk { print!("{:02X} ", b); }
                    print!(" |");
                    for &b in chunk {
                        print!("{}", if (0x20..0x7F).contains(&b) { b as char } else { '.' });
                    }
                    println!("|");
                }
            }
            Err(e) => eprintln!("  ERROR: {}", e),
        }
        // 对比本地镜像
        if let Ok(local) = std::fs::read("flash_out/test_hello.bin") {
            println!("\n本地镜像 flash_out/test_hello.bin ({} bytes) 前 32 字节:", local.len());
            print!("  ");
            for b in local.iter().take(32) { print!("{:02X} ", b); }
            println!();
        }
        return;
    }

    // 模式: --monitor (串口监控, 验证闭环)
    if args.get(2).map(|s| s.as_str()) == Some("--monitor") {
        let secs: u32 = args.get(3).and_then(|s| s.parse().ok()).unwrap_or(8);
        println!("[MONITOR] {} @ 115200, {} 秒...", port, secs);
        match native_flasher::monitor_port(port, 115200, secs) {
            Ok(output) => {
                println!("--- 串口输出 ---");
                print!("{}", output);
                println!("--- 结束 ---");
                if output.contains("DUANYAN-OK") {
                    println!("\n✅ 验证通过: 程序真实运行!");
                } else if output.is_empty() {
                    println!("\n⚠ 无输出");
                } else {
                    println!("\n⚠ 有输出但未匹配 DUANYAN-OK");
                }
            }
            Err(e) => {
                eprintln!("  ERROR: {}", e);
                std::process::exit(1);
            }
        }
        return;
    }

    // 模式: --build-only (仅编译包装镜像, 不烧录; M6 端侧自烧录的产线入口)
    if args.get(2).map(|s| s.as_str()) == Some("--build-only") {
        let c_file = args.get(3).map(|s| s.as_str()).unwrap_or("test_blink.c");
        println!("[BUILD ONLY] C → ESP32-S3 Image (不烧录)");
        match native_flasher::build_esp32s3_bin(c_file, "flash_out") {
            Ok(build) => {
                println!("  镜像: {}", build.bin_path);
                println!("  大小: {} bytes", build.bin_size);
            }
            Err(e) => {
                eprintln!("  ERROR: {}", e);
                std::process::exit(1);
            }
        }
        return;
    }

    // 模式: --build (C → ESP镜像 → 烧录)
    if args.get(2).map(|s| s.as_str()) == Some("--build") {
        let c_file = args.get(3).map(|s| s.as_str()).unwrap_or("test_blink.c");
        println!("[FULL PIPELINE] C → ESP32-S3 Image → Flash");
        println!("Source: {}", c_file);
        println!();
        match native_flasher::build_flash_esp32s3(c_file, port, "flash_out", 460_800) {
            Ok(result) => {
                println!("  {}", result.message);
                println!("  Bytes: {}", result.bytes_written);
                println!("  Time: {}ms", result.duration_ms);
                // 烧录成功后自动监控 8 秒验证运行
                println!("\n[VERIFY] 芯片已复位, 监控串口 8 秒...");
                std::thread::sleep(std::time::Duration::from_millis(500));
                match native_flasher::monitor_port(port, 115200, 8) {
                    Ok(output) => {
                        println!("--- 串口输出 ---");
                        print!("{}", output);
                        println!("--- 结束 ---");
                        if output.contains("DUANYAN-OK") {
                            println!("\n✅ 闭环验证通过: 程序真实运行!");
                        } else if output.is_empty() {
                            println!("\n⚠ 无输出");
                        } else {
                            println!("\n⚠ 有输出但未匹配 DUANYAN-OK");
                        }
                    }
                    Err(e) => eprintln!("  监控失败: {}", e),
                }
            }
            Err(e) => {
                eprintln!("  ERROR: {}", e);
                std::process::exit(1);
            }
        }
        println!("\n=== Done ===");
        return;
    }

    // 模式: 检测 + 可选烧录
    let bin_file = args.get(2).map(|s| s.as_str());
    let address: u32 = args
        .get(3)
        .map(|s| {
            if let Some(hex) = s.strip_prefix("0x").or_else(|| s.strip_prefix("0X")) {
                u32::from_str_radix(hex, 16).unwrap_or(0x10000)
            } else {
                s.parse().unwrap_or(0x10000)
            }
        })
        .unwrap_or(0x10000);

    // Step 1: 检测串口
    println!("[1/4] 扫描串口...");
    let ports = native_flasher::detect_ports();
    if ports.is_empty() {
        eprintln!("  ERROR: 未检测到任何串口!");
        std::process::exit(1);
    }
    for p in &ports {
        let esp_mark = if p.is_esp_device { " [ESP]" } else { "" };
        println!("  {} - {}{}", p.port_name, p.description, esp_mark);
    }
    println!();

    // Step 2: 读取芯片信息
    println!("[2/4] 读取芯片信息 ({})...", port);
    match native_flasher::read_chip_info(port) {
        Ok(info) => {
            println!("  Chip: {}", info.chip);
            println!("  MAC:  {}", info.mac_address);
            println!("  Flash: {}", info.flash_size);
            println!("  Crystal: {}", info.crystal_frequency);
            println!();
        }
        Err(e) => {
            eprintln!("  ERROR: 无法读取芯片信息: {}", e);
            eprintln!("  请确认 ESP32 已进入下载模式 (按住 BOOT + 按一下 RESET)");
            std::process::exit(1);
        }
    }

    // Step 3: 烧录
    if let Some(bin_path) = bin_file {
        println!("[3/4] 烧录 {} @ 0x{:X}...", bin_path, address);
        match native_flasher::flash_file(port, bin_path, address, 460_800) {
            Ok(result) => {
                println!("  Result: {}", if result.success { "SUCCESS" } else { "FAILED" });
                println!("  Bytes: {}", result.bytes_written);
                println!("  Time: {}ms", result.duration_ms);
                println!("  {}", result.message);
            }
            Err(e) => {
                eprintln!("  ERROR: {}", e);
                std::process::exit(1);
            }
        }
    } else {
        println!("[3/4] 未指定 bin 文件, 跳过烧录");
    }

    // Step 4: 检测工具链
    println!("[4/4] 检测工具链...");
    let tc = native_flasher::detect_toolchain();
    println!("  Toolchain: {}", if tc.found { "FOUND" } else { "NOT FOUND" });
    if tc.found {
        println!("  GCC: {}", tc.gcc_path);
    }
    println!();
    println!("=== Done ===");
}
