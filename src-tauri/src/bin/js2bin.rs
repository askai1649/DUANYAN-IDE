//! js2bin — JS 示例 → ESP32-S3 可烧录 .bin 的命令行工具
//! 管线与 IDE 的"编译成 .bin"按钮完全一致:
//!   JS → (HardyScript Esp32S3Bare 后端) → C → xtensa-gcc → ESP 镜像
//!
//! 用法: js2bin <file.js> [输出目录]

use duanyan_ide_lib::native_flasher;

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 {
        eprintln!("用法: js2bin <file.js> [输出目录]");
        std::process::exit(1);
    }
    let js_path = &args[1];
    let out_dir = args.get(2).cloned().unwrap_or_else(|| "js2bin_out".to_string());

    let js = std::fs::read_to_string(js_path).expect("读取 JS 失败");
    let tokens = hardyscript::lexer::tokenize(&js).expect("词法分析失败");
    let ast = hardyscript::parser::parse(&tokens).expect("语法解析失败");
    let c_result = hardyscript::c_codegen::generate_c_source(
        &ast,
        &hardyscript::c_codegen::CTarget::Esp32S3Bare,
    )
    .expect("C 代码生成失败");

    std::fs::create_dir_all(&out_dir).expect("创建输出目录失败");
    let c_path = format!("{}/firmware.c", out_dir);
    std::fs::write(&c_path, &c_result.source).expect("写入 C 文件失败");
    println!("[1/2] JS → C 完成: {} ({} chars)", c_path, c_result.source.len());

    match native_flasher::build_esp32s3_bin(&c_path, &out_dir) {
        Ok(r) => println!("[2/2] {}", r.message),
        Err(e) => {
            eprintln!("[FAIL] {}", e);
            std::process::exit(1);
        }
    }
}
