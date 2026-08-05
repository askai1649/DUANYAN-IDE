//! 端到端验证: DUANYAN-IDE/examples 下的 JS 示例 → ESP32-S3 可烧录 .bin
//! 管线与 IDE 的"编译成 .bin"按钮完全一致:
//!   JS → (hardyscript lexer/parser/c_codegen Esp32S3Bare) → C → xtensa-gcc → ESP 镜像

use duanyan_ide_lib::native_flasher;

fn js_to_bin(js_path: &str, out_dir: &str) -> native_flasher::BuildResult {
    let js = std::fs::read_to_string(js_path).expect("读取示例 JS 失败");
    let tokens = hardyscript::lexer::tokenize(&js).expect("词法分析失败");
    let ast = hardyscript::parser::parse(&tokens).expect("语法解析失败");
    let c_result = hardyscript::c_codegen::generate_c_source(
        &ast,
        &hardyscript::c_codegen::CTarget::Esp32S3Bare,
    )
    .expect("C 代码生成失败");

    std::fs::create_dir_all(out_dir).unwrap();
    let c_path = format!("{}/firmware.c", out_dir);
    std::fs::write(&c_path, &c_result.source).unwrap();

    native_flasher::build_esp32s3_bin(&c_path, out_dir).expect("gcc 编译 + 镜像打包失败")
}

#[test]
fn test_rgb_on_to_bin() {
    let r = js_to_bin("../examples/rgb_on.js", "target/test_bin_rgb_on");
    assert!(r.bin_size > 100, ".bin 过小: {}", r.bin_size);
    let img = std::fs::read(&r.bin_path).unwrap();
    assert_eq!(img[0], 0xE9, "ESP 镜像 magic 字节错误");
    println!("[rgb_on] {}", r.message);
}

#[test]
fn test_rgb_rainbow_to_bin() {
    let r = js_to_bin("../examples/rgb_rainbow.js", "target/test_bin_rgb_rainbow");
    assert!(r.bin_size > 100, ".bin 过小: {}", r.bin_size);
    let img = std::fs::read(&r.bin_path).unwrap();
    assert_eq!(img[0], 0xE9, "ESP 镜像 magic 字节错误");
    println!("[rgb_rainbow] {}", r.message);
}
