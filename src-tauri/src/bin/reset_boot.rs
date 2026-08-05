//! 把卡在下载模式的 ESP32-S3 复位回正常运行模式
//! 用法: cargo run --bin reset_boot -- COM6
use std::time::Duration;

fn main() {
    let port_name = std::env::args().nth(1).unwrap_or_else(|| "COM6".to_string());
    let mut port = serialport::new(&port_name, 115200)
        .timeout(Duration::from_millis(200))
        .open()
        .expect("打开串口失败");

    // 经典复位序列: RTS 拉低 EN(复位), DTR 保持 GPIO0 高(正常运行)
    let _ = port.write_data_terminal_ready(false);
    let _ = port.write_request_to_send(true); // EN 低 = 保持复位
    std::thread::sleep(Duration::from_millis(100));
    let _ = port.write_request_to_send(false); // 释放 EN, 采样 GPIO0=高 → 正常运行
    std::thread::sleep(Duration::from_millis(100));
    let _ = port.write_data_terminal_ready(false);

    // 监视 6 秒看启动日志
    println!("=== 复位已触发, 监视 6 秒 ===");
    let start = std::time::Instant::now();
    let mut buf = [0u8; 512];
    use std::io::Read;
    let mut total = Vec::new();
    while start.elapsed() < Duration::from_secs(6) {
        match port.read(&mut buf) {
            Ok(n) if n > 0 => {
                total.extend_from_slice(&buf[..n]);
                let s = String::from_utf8_lossy(&buf[..n]);
                print!("{}", s);
            }
            _ => {}
        }
    }
    println!();
    if total.windows(10).any(|w| String::from_utf8_lossy(w).contains("DUANYAN")) {
        println!("✅ 固件正常运行");
    } else {
        println!("❌ 未看到固件输出 ({} bytes)", total.len());
    }
}
