//! DUANYAN IDE - Rust Backend
//!
//! Integrates HardyScript compiler, execution engine, debugger, and duanyan AI

use std::fs;
use std::path::Path;
use std::sync::Mutex;

mod bridge_protocol;
mod bridge_serial;
mod cluster_protocol;
mod cluster_serial;
mod board_detect;
pub mod native_flasher;

use bridge_protocol::BridgeLog;
use bridge_serial::BridgeSerial;
use cluster_serial::ClusterSerial;

/// Persistent duanyan model state
struct DuanyanState {
    model: duanyan::DuanyanModel,
}

impl DuanyanState {
    fn new() -> Self {
        Self {
            model: duanyan::DuanyanModel::new(),
        }
    }
}

/// OpenSNAR Bridge state: one active serial connection + ring log buffer.
struct BridgeState {
    serial: Mutex<BridgeSerial>,
    logs: Mutex<Vec<BridgeLog>>,
}

impl BridgeState {
    fn new() -> Self {
        Self {
            serial: Mutex::new(BridgeSerial::new_closed()),
            logs: Mutex::new(Vec::with_capacity(4096)),
        }
    }
}

/// Dual-FPGA cluster state.
struct ClusterState {
    serial: Mutex<ClusterSerial>,
}

impl ClusterState {
    fn new() -> Self {
        Self {
            serial: Mutex::new(ClusterSerial::new()),
        }
    }
}

/// Raw serial monitor state (independent from Bridge protocol)
struct SerialMonitorState {
    port: Mutex<Option<RawSerialPort>>,
}

struct RawSerialPort {
    name: String,
    baud: u32,
    #[cfg(feature = "bridge_serial")]
    inner: Box<dyn serialport::SerialPort>,
}

impl SerialMonitorState {
    fn new() -> Self {
        Self {
            port: Mutex::new(None),
        }
    }
}

/// Save file to disk
#[tauri::command]
fn save_file(path: String, content: String) -> Result<String, String> {
    let p = Path::new(&path);
    if let Some(parent) = p.parent() {
        fs::create_dir_all(parent).map_err(|e| format!("Cannot create dir: {}", e))?;
    }
    fs::write(p, &content).map_err(|e| format!("Write failed: {}", e))?;
    Ok(format!("Saved: {}", path))
}

/// Create a new file with optional initial content
#[tauri::command]
fn create_file(path: String, content: String) -> Result<String, String> {
    let p = Path::new(&path);
    if p.exists() {
        return Err(format!("File already exists: {}", path));
    }
    if let Some(parent) = p.parent() {
        fs::create_dir_all(parent).map_err(|e| format!("Cannot create dir: {}", e))?;
    }
    fs::write(p, &content).map_err(|e| format!("Create failed: {}", e))?;
    Ok(format!("Created: {}", path))
}

/// Create a new directory
#[tauri::command]
fn create_dir(path: String) -> Result<String, String> {
    let p = Path::new(&path);
    if p.exists() {
        return Err(format!("Directory already exists: {}", path));
    }
    fs::create_dir_all(p).map_err(|e| format!("Create dir failed: {}", e))?;
    Ok(format!("Created directory: {}", path))
}

/// Delete a file or directory
#[tauri::command]
fn delete_path(path: String) -> Result<String, String> {
    let p = Path::new(&path);
    if !p.exists() {
        return Err(format!("Path not found: {}", path));
    }
    if p.is_dir() {
        fs::remove_dir_all(p).map_err(|e| format!("Delete dir failed: {}", e))?;
    } else {
        fs::remove_file(p).map_err(|e| format!("Delete file failed: {}", e))?;
    }
    Ok(format!("Deleted: {}", path))
}

/// Rename/move a file or directory
#[tauri::command]
fn rename_path(from: String, to: String) -> Result<String, String> {
    let src = Path::new(&from);
    let dst = Path::new(&to);
    if !src.exists() {
        return Err(format!("Source not found: {}", from));
    }
    if let Some(parent) = dst.parent() {
        fs::create_dir_all(parent).map_err(|e| format!("Cannot create dir: {}", e))?;
    }
    fs::rename(src, dst).map_err(|e| format!("Rename failed: {}", e))?;
    Ok(format!("Renamed: {} -> {}", from, to))
}

/// Compile JavaScript source to COUNPRE64 assembly
#[tauri::command]
fn compile_source(source: &str) -> Result<String, String> {
    let tokens = hardyscript::lexer::tokenize(source)
        .map_err(|e| format!("Lexer error: {}", e))?;
    let ast = hardyscript::parser::parse(&tokens)
        .map_err(|e| format!("Parser error: {}", e))?;
    let optimized = hardyscript::optimizer::optimize(&ast);
    let instructions = hardyscript::compiler::compile(&optimized)
        .map_err(|e| format!("Compile error: {}", e))?;

    let mut asm_lines = Vec::new();
    for (i, instr) in instructions.iter().enumerate() {
        let encoded = instr.encode();
        asm_lines.push(format!(
            "{:04x}  {:08x}  {}",
            i * 4,
            encoded,
            instr.to_asm()
        ));
    }
    asm_lines.push(format!("\nTotal: {} instructions", instructions.len()));
    Ok(asm_lines.join("\n"))
}

/// Compile and execute JavaScript source
#[tauri::command]
fn compile_and_run(source: &str) -> Result<String, String> {
    let tokens = hardyscript::lexer::tokenize(source)
        .map_err(|e| format!("Lexer error: {}", e))?;
    let ast = hardyscript::parser::parse(&tokens)
        .map_err(|e| format!("Parser error: {}", e))?;
    let optimized = hardyscript::optimizer::optimize(&ast);
    let instructions = hardyscript::compiler::compile(&optimized)
        .map_err(|e| format!("Compile error: {}", e))?;
    let machine_code: Vec<u32> = instructions.iter().map(|i| i.encode()).collect();

    let mut engine = hardyscript::engine::Engine::new(machine_code);
    match engine.run() {
        Ok((output, cycles)) => Ok(format!(
            "=== Compiled: {} instructions ===\n{}\n=== Done ({} cycles) ===",
            engine.instruction_count(),
            if output.is_empty() { "(no output)" } else { &output },
            cycles
        )),
        Err(e) => Ok(format!(
            "=== Compiled: {} instructions ===\nRuntime error: {}",
            engine.instruction_count(),
            e
        )),
    }
}

/// Get HardyScript version info
#[tauri::command]
fn get_version() -> String {
    format!("HardyScript v{} | COUNPRE64 ISA", env!("CARGO_PKG_VERSION"))
}

// ========== OpenSNAR Bridge Commands ==========

#[tauri::command]
fn bridge_list_ports() -> Result<Vec<String>, String> {
    Ok(BridgeSerial::list_ports())
}

#[tauri::command]
fn bridge_connect(
    state: tauri::State<'_, BridgeState>,
    port: String,
    baud: u32,
) -> Result<String, String> {
    let mut serial = state.serial.lock().map_err(|e| e.to_string())?;
    *serial = BridgeSerial::open(&port, baud)?;
    Ok(format!("Connected to {} @ {}", port, baud))
}

#[tauri::command]
fn bridge_disconnect(state: tauri::State<'_, BridgeState>) -> Result<String, String> {
    let mut serial = state.serial.lock().map_err(|e| e.to_string())?;
    serial.close();
    Ok("Disconnected".to_string())
}

#[tauri::command]
fn bridge_read_logs(
    state: tauri::State<'_, BridgeState>,
    limit: usize,
) -> Result<Vec<BridgeLog>, String> {
    let mut serial = state.serial.lock().map_err(|e| e.to_string())?;
    let logs = serial.read_frames(limit)?;
    if !logs.is_empty() {
        let mut all_logs = state.logs.lock().map_err(|e| e.to_string())?;
        all_logs.extend(logs.clone());
        if all_logs.len() > 4096 {
            let drop = all_logs.len() - 4096;
            all_logs.drain(..drop);
        }
    }
    Ok(logs)
}

#[tauri::command]
fn bridge_send_command(
    state: tauri::State<'_, BridgeState>,
    cmd: String,
) -> Result<String, String> {
    let mut serial = state.serial.lock().map_err(|e| e.to_string())?;
    serial.send_command(&cmd)?;
    Ok(format!("Sent: {}", cmd))
}

// ========== Daisy-Chain FPGA Cluster Commands ==========

#[tauri::command]
fn cluster_connect_captain(
    state: tauri::State<'_, ClusterState>,
    port: String,
    baud: u32,
) -> Result<String, String> {
    let mut serial = state.serial.lock().map_err(|e| e.to_string())?;
    serial.connect_captain(&port, baud)?;
    Ok(format!("Captain connected on {} @ {}", port, baud))
}

#[tauri::command]
fn cluster_disconnect(state: tauri::State<'_, ClusterState>) -> Result<String, String> {
    let mut serial = state.serial.lock().map_err(|e| e.to_string())?;
    serial.disconnect();
    Ok("Cluster disconnected".to_string())
}

#[tauri::command]
fn cluster_status(state: tauri::State<'_, ClusterState>) -> Result<(bool, usize), String> {
    let serial = state.serial.lock().map_err(|e| e.to_string())?;
    Ok((serial.connected(), serial.member_count()))
}

#[tauri::command]
fn cluster_discover(
    state: tauri::State<'_, ClusterState>,
    max_shards: u8,
) -> Result<usize, String> {
    let mut serial = state.serial.lock().map_err(|e| e.to_string())?;
    serial.discover_chain(max_shards)
}

#[tauri::command]
fn cluster_start_inference(
    state: tauri::State<'_, ClusterState>,
    input: Vec<f32>,
) -> Result<String, String> {
    let mut serial = state.serial.lock().map_err(|e| e.to_string())?;
    serial.start_inference(input)?;
    Ok("Inference request sent to captain".to_string())
}

/// Split a model description into two FPGA shards.
#[tauri::command]
fn cluster_split_model(layers_json: String) -> Result<serde_json::Value, String> {
    let layers: Vec<serde_json::Value> = serde_json::from_str(&layers_json)
        .map_err(|e| format!("Invalid layers JSON: {}", e))?;
    let total = layers.len();
    if total == 0 {
        return Err("No layers to split".to_string());
    }
    let mid = (total + 1) / 2;
    let master_layers = layers[..mid].to_vec();
    let slave_layers = layers[mid..].to_vec();
    Ok(serde_json::json!({
        "master": { "layers": master_layers, "outputs_to_slave": true },
        "slave": { "layers": slave_layers, "receives_from_master": true },
    }))
}

/// Create a new HardyScript project directory with hardy.config.json and main.js.
#[tauri::command]
fn create_snar_project(project_dir: String, name: String) -> Result<String, String> {
    let base = std::path::Path::new(&project_dir);
    std::fs::create_dir_all(base).map_err(|e| format!("Create project dir failed: {}", e))?;
    let mut cfg = hardyscript::config::ProjectConfig::default();
    cfg.name = name;
    let config_path = base.join("hardy.config.json");
    std::fs::write(&config_path, cfg.to_json())
        .map_err(|e| format!("Write hardy.config.json failed: {}", e))?;
    let main_path = base.join("main.js");
    if !main_path.exists() {
        std::fs::write(&main_path, "// New HardyScript project\nlet x = 42;\nconsole.log(x);\n")
            .map_err(|e| format!("Write main.js failed: {}", e))?;
    }
    Ok(format!("Project created at {}", base.display()))
}

/// Compile current source and flash to hardware via HardyScript.
#[tauri::command]
fn build_and_flash(
    source: String,
    project_dir: String,
    stem: String,
    target: String,
) -> Result<String, String> {
    let base = std::path::Path::new(&project_dir);
    std::fs::create_dir_all(base).map_err(|e| format!("Create project dir failed: {}", e))?;
    let cfg = hardyscript::config::ProjectConfig::load(base)?;
    let effective_target = if target.is_empty() { cfg.target.platform.clone() } else { target };
    hardyscript::flash_source(&source, base, &stem, &effective_target)
}

// ========== DUANYAN Native Flasher Commands ==========

/// 扫描可用串口，标记 ESP 设备
#[tauri::command]
fn flasher_detect_ports() -> Result<Vec<native_flasher::PortInfo>, String> {
    Ok(native_flasher::detect_ports())
}

/// 读取 ESP 芯片信息（型号、MAC、Flash 大小）
#[tauri::command]
fn flasher_chip_info(port: String) -> Result<native_flasher::ChipInfoResult, String> {
    native_flasher::read_chip_info(&port)
}

/// 烧录二进制文件到 ESP 设备
#[tauri::command]
fn flasher_flash_bin(
    port: String,
    bin_path: String,
    address: u32,
    baud: Option<u32>,
) -> Result<native_flasher::FlashResult, String> {
    let baud = baud.unwrap_or(460_800);
    native_flasher::flash_file(&port, &bin_path, address, baud)
}

/// 烧录原始字节数据到 ESP 设备（用于内存中编译结果直接烧录）
#[tauri::command]
fn flasher_flash_data(
    port: String,
    data: Vec<u8>,
    address: u32,
    baud: Option<u32>,
) -> Result<native_flasher::FlashResult, String> {
    let baud = baud.unwrap_or(460_800);
    let mut progress = espflash::target::DefaultProgressCallback::default();
    native_flasher::flash_binary(&port, &data, address, baud, &mut progress)
}

/// 擦除整个 Flash
#[tauri::command]
fn flasher_erase(port: String) -> Result<String, String> {
    native_flasher::erase_flash(&port)
}

/// 检测 xtensa-gcc 工具链
#[tauri::command]
fn flasher_detect_toolchain() -> Result<native_flasher::ToolchainInfo, String> {
    Ok(native_flasher::detect_toolchain())
}

/// 编译裸机 C 文件为 bin
#[tauri::command]
fn flasher_build(
    c_source: String,
    output_dir: String,
    linker_script: Option<String>,
) -> Result<native_flasher::BuildResult, String> {
    native_flasher::build_bare_metal(&c_source, &output_dir, linker_script.as_deref())
}

/// 一键编译+烧录
#[tauri::command]
fn flasher_build_and_flash(
    c_source: String,
    port: String,
    output_dir: String,
    address: u32,
    baud: Option<u32>,
    linker_script: Option<String>,
) -> Result<native_flasher::FlashResult, String> {
    let baud = baud.unwrap_or(460_800);
    native_flasher::build_and_flash_native(
        &c_source, &port, &output_dir, address, baud, linker_script.as_deref(),
    )
}

/// JS → C 代码生成（HardyScript 编译器）
#[tauri::command]
fn flasher_compile_js(js_source: String, target: Option<String>) -> Result<serde_json::Value, String> {
    let target_str = target.unwrap_or_else(|| "esp32".to_string());
    let c_target = hardyscript::c_codegen::CTarget::from_str(&target_str)
        .ok_or_else(|| format!("不支持的目标平台: {}", target_str))?;

    let tokens = hardyscript::lexer::tokenize(&js_source)?;
    let ast = hardyscript::parser::parse(&tokens)?;
    let result = hardyscript::c_codegen::generate_c_source(&ast, &c_target)?;

    Ok(serde_json::json!({
        "c_source": result.source,
        "target": format!("{:?}", result.target),
        "hw_imports": result.hw_imports.iter().collect::<Vec<_>>(),
        "uses_wifi": result.uses_wifi,
        "uses_ai": result.uses_ai,
    }))
}

/// JS → C → gcc → bin → flash 全自动管线
#[tauri::command]
fn flasher_js_to_flash(
    js_source: String,
    port: String,
    output_dir: String,
    address: u32,
    baud: Option<u32>,
    linker_script: Option<String>,
) -> Result<native_flasher::FlashResult, String> {
    let baud = baud.unwrap_or(460_800);

    // Step 1: JS → C (HardyScript 编译器)
    let tokens = hardyscript::lexer::tokenize(&js_source)?;
    let ast = hardyscript::parser::parse(&tokens)?;
    let c_result = hardyscript::c_codegen::generate_c_source(
        &ast,
        &hardyscript::c_codegen::CTarget::Esp32,
    )?;

    // Step 2: 写入临时 C 文件
    let out_dir = std::path::Path::new(&output_dir);
    std::fs::create_dir_all(out_dir)
        .map_err(|e| format!("创建输出目录失败: {}", e))?;
    let c_path = out_dir.join("firmware.c");
    std::fs::write(&c_path, &c_result.source)
        .map_err(|e| format!("写入 C 文件失败: {}", e))?;

    // Step 3: C → ELF → BIN (xtensa-gcc)
    let build = native_flasher::build_bare_metal(
        c_path.to_str().unwrap_or("firmware.c"),
        &output_dir,
        linker_script.as_deref(),
    )?;

    // Step 4: BIN → Flash (espflash 原生烧录)
    native_flasher::flash_file(&port, &build.bin_path, address, baud)
}

// ========== duanyan AI Commands (persistent model via State) ==========

/// 全流程: JS → C → ESP32-S3 镜像 → Flash (一键烧录)
#[tauri::command]
fn flasher_full_pipeline(
    js_source: String,
    port: String,
    output_dir: String,
) -> Result<native_flasher::FlashResult, String> {
    // Step 1: JS → C (HardyScript 编译器)
    let tokens = hardyscript::lexer::tokenize(&js_source)?;
    let ast = hardyscript::parser::parse(&tokens)?;
    let c_result = hardyscript::c_codegen::generate_c_source(
        &ast,
        &hardyscript::c_codegen::CTarget::Esp32,
    )?;

    // Step 2: 写入临时 C 文件
    let out_dir = std::path::Path::new(&output_dir);
    std::fs::create_dir_all(out_dir)
        .map_err(|e| format!("创建输出目录失败: {}", e))?;
    let c_path = out_dir.join("firmware.c");
    std::fs::write(&c_path, &c_result.source)
        .map_err(|e| format!("写入 C 文件失败: {}", e))?;

    // Step 3: C → ESP镜像 → Flash (build_flash_esp32s3 包含编译+包装+烧录)
    native_flasher::build_flash_esp32s3(
        c_path.to_str().unwrap_or("firmware.c"),
        &port,
        &output_dir,
        460_800,
    )
}

/// JS → C → gcc → .bin (仅编译打包, 不烧录)
/// target 缺省 "esp32s3-bare" (裸机寄存器级后端, 直出可烧录镜像)
#[tauri::command]
fn flasher_compile_to_bin(
    js_source: String,
    output_dir: String,
    target: Option<String>,
) -> Result<native_flasher::BuildResult, String> {
    let target_str = target.unwrap_or_else(|| "esp32s3-bare".to_string());
    let c_target = hardyscript::c_codegen::CTarget::from_str(&target_str)
        .ok_or_else(|| format!("不支持的目标平台: {}", target_str))?;

    // Step 1: JS → C
    let tokens = hardyscript::lexer::tokenize(&js_source)?;
    let ast = hardyscript::parser::parse(&tokens)?;
    let c_result = hardyscript::c_codegen::generate_c_source(&ast, &c_target)?;

    // Step 2: 写入 C 文件
    let out_dir = std::path::Path::new(&output_dir);
    std::fs::create_dir_all(out_dir)
        .map_err(|e| format!("创建输出目录失败: {}", e))?;
    let c_path = out_dir.join("firmware.c");
    std::fs::write(&c_path, &c_result.source)
        .map_err(|e| format!("写入 C 文件失败: {}", e))?;

    // Step 3: C → gcc → ESP32-S3 镜像 .bin
    native_flasher::build_esp32s3_bin(
        c_path.to_str().unwrap_or("firmware.c"),
        &output_dir,
    )
}


// 466 个 duanyan_* 研究演示命令已拆分至 src/duanyan_cmds.rs (2026-08 工程治理)
mod duanyan_cmds;
#[allow(unused_imports)]
use duanyan_cmds::*;


// ========== Phase G: Board Management Commands ==========

/// Scan USB devices and detect connected boards
#[tauri::command]
fn board_scan() -> Vec<board_detect::DetectedBoard> {
    board_detect::scan_boards()
}

/// Get all known board definitions
#[tauri::command]
fn board_get_all() -> Vec<board_detect::BoardInfoDto> {
    board_detect::get_all_boards()
}

/// Get pin definitions for a specific board
#[tauri::command]
fn board_get_pins(board: String) -> Vec<board_detect::PinInfo> {
    board_detect::get_board_pins(&board)
}

// ========== Phase G: Serial Monitor Commands ==========

/// List available serial ports with descriptions
#[tauri::command]
fn serial_list_ports() -> Vec<serde_json::Value> {
    #[cfg(feature = "bridge_serial")]
    {
        match serialport::available_ports() {
            Ok(ports) => ports.iter().map(|p| {
                let description = match &p.port_type {
                    serialport::SerialPortType::UsbPort(info) => info.product.clone()
                        .or_else(|| info.manufacturer.clone())
                        .unwrap_or_else(|| "USB Serial Device".to_string()),
                    serialport::SerialPortType::PciPort => "PCI Serial Port".to_string(),
                    serialport::SerialPortType::BluetoothPort => "Bluetooth Serial Port".to_string(),
                    serialport::SerialPortType::Unknown => "Serial Port".to_string(),
                };
                serde_json::json!({
                    "name": p.port_name,
                    "description": description,
                })
            }).collect(),
            Err(_) => Vec::new(),
        }
    }
    #[cfg(not(feature = "bridge_serial"))]
    {
        let ports = hardyscript::board::scan_serial_ports();
        ports.iter().map(|p| serde_json::json!({
            "name": p.name,
            "description": p.description,
        })).collect()
    }
}

/// Open a raw serial connection for monitoring
#[tauri::command]
fn serial_open(
    state: tauri::State<'_, SerialMonitorState>,
    port: String,
    baud: u32,
) -> Result<String, String> {
    let mut guard = state.port.lock().map_err(|e| e.to_string())?;
    if guard.is_some() {
        return Err("Serial port already open. Close it first.".into());
    }
    #[cfg(feature = "bridge_serial")]
    {
        let p = serialport::new(&port, baud)
            .timeout(std::time::Duration::from_millis(100))
            .open()
            .map_err(|e| format!("Open failed: {}", e))?;
        *guard = Some(RawSerialPort { name: port.clone(), baud, inner: p });
    }
    #[cfg(not(feature = "bridge_serial"))]
    {
        *guard = Some(RawSerialPort { name: port.clone(), baud });
    }
    Ok(format!("Opened {} @ {} baud", port, baud))
}

/// Close the raw serial connection
#[tauri::command]
fn serial_close(state: tauri::State<'_, SerialMonitorState>) -> Result<String, String> {
    let mut guard = state.port.lock().map_err(|e| e.to_string())?;
    *guard = None;
    Ok("Serial port closed".into())
}

/// Read raw text from serial
#[tauri::command]
fn serial_read(state: tauri::State<'_, SerialMonitorState>) -> Result<String, String> {
    let mut guard = state.port.lock().map_err(|e| e.to_string())?;
    let Some(port) = guard.as_mut() else {
        return Ok(String::new());
    };
    #[cfg(feature = "bridge_serial")]
    {
        let mut buf = [0u8; 512];
        match port.inner.read(&mut buf) {
            Ok(n) if n > 0 => Ok(String::from_utf8_lossy(&buf[..n]).to_string()),
            Ok(_) => Ok(String::new()),
            Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => Ok(String::new()),
            Err(e) => Err(format!("Read error: {}", e)),
        }
    }
    #[cfg(not(feature = "bridge_serial"))]
    {
        let _ = port;
        Ok(String::new())
    }
}

/// Write raw text to serial
#[tauri::command]
fn serial_write(
    state: tauri::State<'_, SerialMonitorState>,
    data: String,
) -> Result<String, String> {
    let mut guard = state.port.lock().map_err(|e| e.to_string())?;
    let Some(port) = guard.as_mut() else {
        return Err("Serial port not open".into());
    };
    #[cfg(feature = "bridge_serial")]
    {
        use std::io::Write;
        port.inner.write_all(data.as_bytes())
            .and_then(|_| port.inner.flush())
            .map_err(|e| format!("Write error: {}", e))?;
    }
    #[cfg(not(feature = "bridge_serial"))]
    {
        let _ = port;
        return Err("Serial support disabled in this build".into());
    }
    Ok(format!("Sent: {}", data))
}

// ========== Phase G: Project Templates ==========

use serde::Serialize;

#[derive(Serialize)]
struct TemplateInfo {
    id: String,
    name: String,
    description: String,
    icon: String,
    category: String,
}

/// List available project templates
#[tauri::command]
fn template_list() -> Vec<TemplateInfo> {
    vec![
        TemplateInfo { id: "blink".into(), name: "LED闪烁".into(), description: "基础GPIO控制，LED闪烁示例".into(), icon: "💡".into(), category: "基础".into() },
        TemplateInfo { id: "uart".into(), name: "串口通信".into(), description: "UART串口收发数据".into(), icon: "📡".into(), category: "通信".into() },
        TemplateInfo { id: "motor".into(), name: "电机控制".into(), description: "PWM驱动直流电机".into(), icon: "⚙️".into(), category: "控制".into() },
        TemplateInfo { id: "ai_gesture".into(), name: "AI手势灯带".into(), description: "千瀌Mini + MPU6050 + WS2812".into(), icon: "🧠".into(), category: "AI".into() },
        TemplateInfo { id: "sensor".into(), name: "传感器读取".into(), description: "I2C读取温湿度传感器".into(), icon: "🌡️".into(), category: "传感器".into() },
        TemplateInfo { id: "servo".into(), name: "舵机控制".into(), description: "PWM驱动舵机转动".into(), icon: "🤖".into(), category: "控制".into() },
        TemplateInfo { id: "button".into(), name: "按键输入".into(), description: "GPIO读取按键状态".into(), icon: "🔘".into(), category: "基础".into() },
        TemplateInfo { id: "display".into(), name: "OLED显示".into(), description: "I2C OLED屏幕显示文字".into(), icon: "🖥️".into(), category: "显示".into() },
    ]
}

/// Create a project from a template
#[tauri::command]
fn template_create(template_id: String, project_dir: String) -> Result<String, String> {
    let base = Path::new(&project_dir);
    fs::create_dir_all(base).map_err(|e| format!("Create dir failed: {}", e))?;

    let content = match template_id.as_str() {
        "blink" => include_str!("../templates/blink.js").to_string(),
        "uart" => include_str!("../templates/uart.js").to_string(),
        "motor" => include_str!("../templates/motor.js").to_string(),
        "ai_gesture" => include_str!("../templates/ai_gesture.js").to_string(),
        "sensor" => include_str!("../templates/sensor.js").to_string(),
        "servo" => include_str!("../templates/servo.js").to_string(),
        "button" => include_str!("../templates/button.js").to_string(),
        "display" => include_str!("../templates/display.js").to_string(),
        _ => return Err(format!("Unknown template: {}", template_id)),
    };

    let main_path = base.join("main.js");
    fs::write(&main_path, &content).map_err(|e| format!("Write failed: {}", e))?;

    // Create hardy.config.json
    let mut cfg = hardyscript::config::ProjectConfig::default();
    cfg.name = template_id.clone();
    fs::write(base.join("hardy.config.json"), cfg.to_json())
        .map_err(|e| format!("Config write failed: {}", e))?;

    Ok(format!("Project created from '{}' at {}", template_id, base.display()))
}

// ========== Phase G: TF Card Manager ==========

#[derive(Serialize)]
struct ModelFileInfo {
    name: String,
    size_kb: f64,
    path: String,
}

/// List .duanyan model files in a directory
#[tauri::command]
fn tfcard_list(model_dir: String) -> Vec<ModelFileInfo> {
    let dir = Path::new(&model_dir);
    if !dir.exists() { return Vec::new(); }
    let mut files = Vec::new();
    if let Ok(entries) = fs::read_dir(dir) {
        for entry in entries.flatten() {
            let name = entry.file_name().to_string_lossy().to_string();
            if name.ends_with(".duanyan") {
                let size = entry.metadata().map(|m| m.len() as f64 / 1024.0).unwrap_or(0.0);
                files.push(ModelFileInfo {
                    name,
                    size_kb: size,
                    path: entry.path().to_string_lossy().to_string(),
                });
            }
        }
    }
    files.sort_by(|a, b| a.name.cmp(&b.name));
    files
}

/// Deploy (copy) a model file to TF card directory
#[tauri::command]
fn tfcard_deploy(source_path: String, dest_dir: String) -> Result<String, String> {
    let src = Path::new(&source_path);
    let dst_dir = Path::new(&dest_dir);
    if !src.exists() { return Err(format!("Source not found: {}", source_path)); }
    fs::create_dir_all(dst_dir).map_err(|e| format!("Create dest dir failed: {}", e))?;
    let filename = src.file_name().ok_or("Invalid source file")?;
    let dest = dst_dir.join(filename);
    fs::copy(src, &dest).map_err(|e| format!("Copy failed: {}", e))?;
    Ok(format!("Deployed {} -> {}", source_path, dest.display()))
}

/// Read log files from TF card directory
#[tauri::command]
fn tfcard_logs(log_dir: String) -> Vec<String> {
    let dir = Path::new(&log_dir);
    if !dir.exists() { return Vec::new(); }
    let mut logs = Vec::new();
    if let Ok(entries) = fs::read_dir(dir) {
        for entry in entries.flatten() {
            let name = entry.file_name().to_string_lossy().to_string();
            if name.ends_with(".log") {
                if let Ok(content) = fs::read_to_string(entry.path()) {
                    logs.push(format!("=== {} ===", name));
                    logs.extend(content.lines().take(100).map(String::from));
                }
            }
        }
    }
    logs
}

// ========== Phase G: Benchmark System ==========

#[derive(Serialize)]
struct BenchResult {
    flash_bytes: u32,
    ram_bytes: u32,
    instruction_count: u32,
    grade: String,
    details: String,
}

#[derive(Serialize)]
struct BenchComparison {
    hardyscript_flash: u32,
    c_flash: u32,
    hardyscript_ram: u32,
    c_ram: u32,
    flash_overhead_pct: f64,
    ram_overhead_pct: f64,
    grade: String,
}

/// Run benchmark on current source
#[tauri::command]
fn bench_run(source: String, target: String) -> Result<BenchResult, String> {
    let tokens = hardyscript::lexer::tokenize(&source)
        .map_err(|e| format!("Lexer error: {}", e))?;
    let ast = hardyscript::parser::parse(&tokens)
        .map_err(|e| format!("Parser error: {}", e))?;
    let optimized = hardyscript::optimizer::optimize(&ast);
    let instructions = hardyscript::compiler::compile(&optimized)
        .map_err(|e| format!("Compile error: {}", e))?;

    let instr_count = instructions.len() as u32;
    let flash = instr_count * 4; // Each instruction is 4 bytes (32-bit)
    // Rough RAM estimate: 4 bytes per instruction for runtime + 256 bytes base
    let ram = instr_count * 4 + 256;

    let grade = if flash < 4096 { "A" } else if flash < 8192 { "B" } else if flash < 16384 { "C" } else { "D" };

    let details = format!(
        "Target: {} | Instructions: {} | Flash: {:.1}KB | RAM: {:.1}KB",
        if target.is_empty() { "COUNPRE64" } else { &target },
        instr_count,
        flash as f64 / 1024.0,
        ram as f64 / 1024.0
    );

    Ok(BenchResult {
        flash_bytes: flash,
        ram_bytes: ram,
        instruction_count: instr_count,
        grade: grade.to_string(),
        details,
    })
}

/// Compare HardyScript output vs estimated equivalent C code
#[tauri::command]
fn bench_compare(source: String, target: String) -> Result<BenchComparison, String> {
    let tokens = hardyscript::lexer::tokenize(&source)
        .map_err(|e| format!("Lexer error: {}", e))?;
    let ast = hardyscript::parser::parse(&tokens)
        .map_err(|e| format!("Parser error: {}", e))?;
    let optimized = hardyscript::optimizer::optimize(&ast);
    let instructions = hardyscript::compiler::compile(&optimized)
        .map_err(|e| format!("Compile error: {}", e))?;

    let hs_flash = (instructions.len() as u32) * 4;
    let hs_ram = (instructions.len() as u32) * 4 + 256;

    // Estimate C equivalent: typically 60-80% of interpreter overhead
    let c_flash = (hs_flash as f64 * 0.7) as u32;
    let c_ram = (hs_ram as f64 * 0.6) as u32;

    let flash_overhead = if c_flash > 0 { ((hs_flash - c_flash) as f64 / c_flash as f64 * 100.0) } else { 0.0 };
    let ram_overhead = if c_ram > 0 { ((hs_ram - c_ram) as f64 / c_ram as f64 * 100.0) } else { 0.0 };

    let grade = if flash_overhead < 20.0 { "A" } else if flash_overhead < 40.0 { "B" } else if flash_overhead < 80.0 { "C" } else { "D" };

    Ok(BenchComparison {
        hardyscript_flash: hs_flash,
        c_flash,
        hardyscript_ram: hs_ram,
        c_ram,
        flash_overhead_pct: flash_overhead,
        ram_overhead_pct: ram_overhead,
        grade: grade.to_string(),
    })
}

// =============================================================================
// Phase H: Workspace Commands
// =============================================================================

#[tauri::command]
fn workspace_init(dir: String, name: String, monorepo: bool) -> Result<serde_json::Value, String> {
    let path = std::path::Path::new(&dir);
    let ws = hardyscript::workspace::workspace_init(path, &name, monorepo)?;
    Ok(serde_json::json!({
        "name": ws.name,
        "version": ws.version,
        "packages": ws.packages.iter().map(|p| serde_json::json!({
            "name": p.name, "path": p.path, "target": p.target, "dependencies": p.dependencies
        })).collect::<Vec<_>>(),
        "shared_libs": ws.shared_libs,
        "default_target": ws.default_target,
    }))
}

#[tauri::command]
fn workspace_load(dir: String) -> Result<serde_json::Value, String> {
    let path = std::path::Path::new(&dir);
    let ws = hardyscript::workspace::WorkspaceConfig::load(path)?;
    Ok(serde_json::json!({
        "name": ws.name,
        "version": ws.version,
        "packages": ws.packages.iter().map(|p| serde_json::json!({
            "name": p.name, "path": p.path, "target": p.target, "dependencies": p.dependencies
        })).collect::<Vec<_>>(),
        "shared_libs": ws.shared_libs,
        "default_target": ws.default_target,
        "build_order": ws.build_order,
    }))
}

#[tauri::command]
fn workspace_status(dir: String) -> Result<Vec<serde_json::Value>, String> {
    let path = std::path::Path::new(&dir);
    let ws = hardyscript::workspace::WorkspaceConfig::load(path)?;
    let statuses = ws.status(path);
    Ok(statuses.iter().map(|s| serde_json::json!({
        "name": s.name, "path": s.path, "target": s.target,
        "has_config": s.has_config, "has_entry": s.has_entry, "last_build": s.last_build,
    })).collect())
}

#[tauri::command]
fn workspace_build_all(dir: String) -> Result<String, String> {
    let path = std::path::Path::new(&dir);
    let mut ws = hardyscript::workspace::WorkspaceConfig::load(path)?;
    let order = ws.resolve_build_order()?;
    Ok(format!("Build order: {}", order.join(" -> ")))
}

#[tauri::command]
fn workspace_add_package(dir: String, name: String, path: String, target: Option<String>) -> Result<(), String> {
    let ws_dir = std::path::Path::new(&dir);
    let mut ws = hardyscript::workspace::WorkspaceConfig::load(ws_dir)?;
    ws.add_package(hardyscript::workspace::PackageRef {
        name, path, target, dependencies: Vec::new(),
    });
    ws.save(ws_dir)
}

// =============================================================================
// Phase H: Git Commands
// =============================================================================

#[tauri::command]
fn git_status(dir: String) -> Result<serde_json::Value, String> {
    let path = std::path::Path::new(&dir);
    let status = hardyscript::git_ops::git_status(path)?;
    Ok(serde_json::json!({
        "branch": status.branch,
        "staged": status.staged.iter().map(|f| serde_json::json!({
            "path": f.path, "status": f.status.label()
        })).collect::<Vec<_>>(),
        "unstaged": status.unstaged.iter().map(|f| serde_json::json!({
            "path": f.path, "status": f.status.label()
        })).collect::<Vec<_>>(),
        "untracked": status.untracked,
        "ahead": status.ahead,
        "behind": status.behind,
    }))
}

#[tauri::command]
fn git_log(dir: String, n: usize) -> Result<Vec<serde_json::Value>, String> {
    let path = std::path::Path::new(&dir);
    let entries = hardyscript::git_ops::git_log(path, n)?;
    Ok(entries.iter().map(|e| serde_json::json!({
        "hash": e.hash, "author": e.author, "date": e.date, "message": e.message
    })).collect())
}

#[tauri::command]
fn git_diff(dir: String, file: Option<String>) -> Result<Vec<serde_json::Value>, String> {
    let path = std::path::Path::new(&dir);
    let diffs = hardyscript::git_ops::git_diff(path, file.as_deref())?;
    Ok(diffs.iter().map(|d| serde_json::json!({
        "path": d.path,
        "hunks": d.hunks.iter().map(|h| serde_json::json!({
            "old_start": h.old_start, "old_count": h.old_count,
            "new_start": h.new_start, "new_count": h.new_count,
            "lines": h.lines.iter().map(|l| serde_json::json!({
                "kind": match l.kind {
                    hardyscript::git_ops::DiffLineKind::Added => "Added",
                    hardyscript::git_ops::DiffLineKind::Removed => "Removed",
                    hardyscript::git_ops::DiffLineKind::Context => "Context",
                    hardyscript::git_ops::DiffLineKind::Header => "Header",
                },
                "content": l.content, "old_lineno": l.old_lineno, "new_lineno": l.new_lineno
            })).collect::<Vec<_>>()
        })).collect::<Vec<_>>()
    })).collect())
}

#[tauri::command]
fn git_staged_diff(dir: String) -> Result<Vec<serde_json::Value>, String> {
    let path = std::path::Path::new(&dir);
    let diffs = hardyscript::git_ops::git_staged_diff(path)?;
    Ok(diffs.iter().map(|d| serde_json::json!({
        "path": d.path,
        "hunks": d.hunks.iter().map(|h| serde_json::json!({
            "old_start": h.old_start, "old_count": h.old_count,
            "new_start": h.new_start, "new_count": h.new_count,
            "lines": h.lines.iter().map(|l| serde_json::json!({
                "kind": match l.kind {
                    hardyscript::git_ops::DiffLineKind::Added => "Added",
                    hardyscript::git_ops::DiffLineKind::Removed => "Removed",
                    hardyscript::git_ops::DiffLineKind::Context => "Context",
                    hardyscript::git_ops::DiffLineKind::Header => "Header",
                },
                "content": l.content, "old_lineno": l.old_lineno, "new_lineno": l.new_lineno
            })).collect::<Vec<_>>()
        })).collect::<Vec<_>>()
    })).collect())
}

#[tauri::command]
fn git_add(dir: String, files: Vec<String>) -> Result<(), String> {
    hardyscript::git_ops::git_add(std::path::Path::new(&dir), &files)
}

#[tauri::command]
fn git_add_all(dir: String) -> Result<(), String> {
    hardyscript::git_ops::git_add_all(std::path::Path::new(&dir))
}

#[tauri::command]
fn git_unstage(dir: String, files: Vec<String>) -> Result<(), String> {
    hardyscript::git_ops::git_unstage(std::path::Path::new(&dir), &files)
}

#[tauri::command]
fn git_commit(dir: String, message: String) -> Result<String, String> {
    hardyscript::git_ops::git_commit(std::path::Path::new(&dir), &message)
}

#[tauri::command]
fn git_push(dir: String, remote: Option<String>, branch: Option<String>) -> Result<String, String> {
    hardyscript::git_ops::git_push(std::path::Path::new(&dir), remote.as_deref(), branch.as_deref())
}

#[tauri::command]
fn git_pull(dir: String) -> Result<String, String> {
    hardyscript::git_ops::git_pull(std::path::Path::new(&dir))
}

#[tauri::command]
fn git_branches(dir: String) -> Result<Vec<serde_json::Value>, String> {
    let branches = hardyscript::git_ops::git_branches(std::path::Path::new(&dir))?;
    Ok(branches.iter().map(|b| serde_json::json!({
        "name": b.name, "is_current": b.is_current, "is_remote": b.is_remote
    })).collect())
}

#[tauri::command]
fn git_checkout(dir: String, branch: String) -> Result<String, String> {
    hardyscript::git_ops::git_checkout(std::path::Path::new(&dir), &branch)
}

#[tauri::command]
fn git_create_branch(dir: String, name: String) -> Result<String, String> {
    hardyscript::git_ops::git_create_branch(std::path::Path::new(&dir), &name)
}

#[tauri::command]
fn git_init(dir: String) -> Result<String, String> {
    hardyscript::git_ops::git_init(std::path::Path::new(&dir))
}

#[tauri::command]
fn git_install_hooks(dir: String) -> Result<(), String> {
    hardyscript::git_ops::git_install_hooks(std::path::Path::new(&dir))
}

// =============================================================================
// Phase H: Lint Commands
// =============================================================================

#[tauri::command]
fn lint_project(dir: String) -> Result<serde_json::Value, String> {
    let path = std::path::Path::new(&dir);
    let config = hardyscript::lint::LintConfig::load(path);
    let result = hardyscript::lint::lint_project(path, &config);
    Ok(serde_json::json!({
        "diagnostics": result.diagnostics.iter().map(|d| serde_json::json!({
            "rule_id": d.rule_id, "severity": d.severity.label(),
            "message": d.message, "file": d.file,
            "line": d.line, "column": d.column,
            "suggestion": d.suggestion, "has_fix": d.fix.is_some()
        })).collect::<Vec<_>>(),
        "error_count": result.error_count,
        "warning_count": result.warning_count,
        "fixable_count": result.fixable_count,
    }))
}

#[tauri::command]
fn lint_file_content(source: String, file_path: String) -> Result<serde_json::Value, String> {
    let config = hardyscript::lint::LintConfig::default_config();
    let result = hardyscript::lint::lint_file(std::path::Path::new(&file_path), &source, &config);
    Ok(serde_json::json!({
        "diagnostics": result.diagnostics.iter().map(|d| serde_json::json!({
            "rule_id": d.rule_id, "severity": d.severity.label(),
            "message": d.message, "file": d.file,
            "line": d.line, "column": d.column,
            "suggestion": d.suggestion, "has_fix": d.fix.is_some()
        })).collect::<Vec<_>>(),
        "error_count": result.error_count,
        "warning_count": result.warning_count,
        "fixable_count": result.fixable_count,
    }))
}

#[tauri::command]
fn lint_fix(dir: String) -> Result<String, String> {
    let path = std::path::Path::new(&dir);
    let config = hardyscript::lint::LintConfig::load(path);
    let js_files = find_js_files_for_lint(path);
    let mut fixed_count = 0;
    for file in &js_files {
        if let Ok(source) = std::fs::read_to_string(file) {
            if let Ok(fixed) = hardyscript::lint::lint_fix(file, &source, &config) {
                if fixed != source {
                    std::fs::write(file, &fixed).ok();
                    fixed_count += 1;
                }
            }
        }
    }
    Ok(format!("Fixed {} files", fixed_count))
}

#[tauri::command]
fn lint_rules() -> Result<Vec<serde_json::Value>, String> {
    let rules = hardyscript::lint::builtin_rules();
    Ok(rules.iter().map(|r| serde_json::json!({
        "id": r.id, "description": r.description,
        "severity": r.severity.label(), "fixable": r.fixable
    })).collect())
}

fn find_js_files_for_lint(dir: &std::path::Path) -> Vec<std::path::PathBuf> {
    let mut files = Vec::new();
    if let Ok(entries) = std::fs::read_dir(dir) {
        for entry in entries.flatten() {
            let path = entry.path();
            if path.is_file() {
                if let Some(ext) = path.extension() {
                    if ext == "js" || ext == "ts" {
                        files.push(path);
                    }
                }
            } else if path.is_dir() {
                let name = path.file_name().unwrap_or_default().to_string_lossy();
                if !name.starts_with('.') && name != "node_modules" && name != "build" && name != "target" {
                    files.extend(find_js_files_for_lint(&path));
                }
            }
        }
    }
    files
}

// =============================================================================
// Phase H: CI/CD Commands
// =============================================================================

#[tauri::command]
fn ci_build(dir: String) -> Result<serde_json::Value, String> {
    let path = std::path::Path::new(&dir);
    let config = hardyscript::ci::CiConfig::default();
    let result = hardyscript::ci::ci_build_all(path, &config);
    Ok(serde_json::json!({
        "success": result.success,
        "targets_built": result.targets_built,
        "targets_failed": result.targets_failed,
        "lint_passed": result.lint_passed,
        "total_size_bytes": result.total_size_bytes,
        "duration_ms": result.duration_ms,
    }))
}

#[tauri::command]
fn ci_release(dir: String, version: String) -> Result<serde_json::Value, String> {
    let path = std::path::Path::new(&dir);
    let info = hardyscript::ci::ci_release(path, &version)?;
    Ok(serde_json::json!({
        "version": info.version,
        "artifacts": info.artifacts.iter().map(|a| serde_json::json!({
            "name": a.name, "path": a.path, "size_bytes": a.size_bytes,
            "target": a.target, "hash": a.hash
        })).collect::<Vec<_>>(),
        "resource_report": info.resource_report,
        "timestamp": info.timestamp,
    }))
}

#[tauri::command]
fn ci_generate_template(dir: String, github: bool, gitlab: bool) -> Result<String, String> {
    let path = std::path::Path::new(&dir);
    if github {
        hardyscript::ci::ci_generate_github_action(path)?;
        return Ok("GitHub Actions template generated".to_string());
    }
    if gitlab {
        hardyscript::ci::ci_generate_gitlab_ci(path)?;
        return Ok("GitLab CI template generated".to_string());
    }
    Err("Specify --github or --gitlab".to_string())
}

#[tauri::command]
fn ci_archive(dir: String, version: String) -> Result<String, String> {
    hardyscript::ci::ci_archive(std::path::Path::new(&dir), &version)
}

// ========== DUANYAN Language Intelligence ==========

/// 实时诊断: 返回 JSON 格式诊断列表
#[tauri::command]
fn hardy_diagnose(source: String, target: Option<String>) -> Result<String, String> {
    use hardyscript::language_intelligence::*;
    let mut diags = diagnose(&source);
    if let Some(t) = &target {
        diags.extend(check_industry_rules(&source, t));
    }
    let json: Vec<String> = diags.iter().map(|d| {
        format!(r#"{{"severity":"{:?}","line":{},"col":{},"message":"{}","code":"{}"}}"#,
            d.severity, d.line, d.col,
            d.message.replace('"', "\\\""),
            d.code)
    }).collect();
    Ok(format!("[{}]", json.join(",")))
}

/// 上下文补全: 返回 JSON 格式补全列表
#[tauri::command]
fn hardy_complete(source: String, line: usize, col: usize, target: Option<String>) -> Result<String, String> {
    use hardyscript::language_intelligence::*;
    let items = complete(&source, line, col, target.as_deref());
    let json: Vec<String> = items.iter().map(|i| {
        format!(r#"{{"label":"{}","kind":"{:?}","detail":"{}","insertText":"{}"}}"#,
            i.label.replace('"', "\\\""),
            i.kind,
            i.detail.replace('"', "\\\""),
            i.insert_text.replace('"', "\\\"").replace('\n', "\\n"))
    }).collect();
    Ok(format!("[{}]", json.join(",")))
}

/// 悬停信息: 返回 JSON 格式悬停内容
#[tauri::command]
fn hardy_hover(source: String, line: usize, col: usize) -> Result<String, String> {
    use hardyscript::language_intelligence::*;
    match hover(&source, line, col) {
        Some(info) => {
            let wcet = info.wcet_hint.unwrap_or_default();
            Ok(format!(r#"{{"title":"{}","signature":"{}","description":"{}","wcet":"{}"}}"#,
                info.title.replace('"', "\\\""),
                info.signature.replace('"', "\\\""),
                info.description.replace('"', "\\\""),
                wcet.replace('"', "\\\"")))
        }
        None => Ok("null".into()),
    }
}

// ========== Deterministic Industry Analysis ==========

#[tauri::command]
fn deterministic_analyze(source: String, target: String) -> Result<String, String> {
    let tokens = hardyscript::lexer::tokenize(&source)
        .map_err(|e| format!("Lexer error: {}", e))?;
    let ast = hardyscript::parser::parse(&tokens)
        .map_err(|e| format!("Parser error: {}", e))?;

    let mut report = String::new();
    let func_count = ast.functions.len();

    match target.as_str() {
        "ic" => {
            use hardyscript::industrial_control::*;
            report.push_str("=== 工业控制分析 (IEC 61131-3 / EtherCAT / SIL-3) ===\n\n");
            report.push_str(&format!("函数: {}\n", func_count));

            let slaves = vec![
                EcatSlave { name: "Servo_X".into(), station: 1001, vendor_id: 0, product_code: 0, input_bytes: 32, output_bytes: 32, dc_supported: true },
                EcatSlave { name: "Servo_Y".into(), station: 1002, vendor_id: 0, product_code: 0, input_bytes: 32, output_bytes: 32, dc_supported: true },
            ];
            let sched = plan_ecat_schedule(&slaves, 1000);
            report.push_str(&format!("\nEtherCAT: {} 从站, 周期 {}μs\n", sched.slaves.len(), sched.cycle_us));
            report.push_str(&format!("  总线时间: {:.1}μs\n", sched.bus_time_us));
            report.push_str(&format!("  可行: {}\n", sched.cycle_feasible));
            report.push_str(&format!("  抖动: {:.1}μs\n", sched.jitter_us));

            for sf in &ast.functions {
                let pou = PlcPou { name: sf.func.name.clone(), kind: PouKind::FunctionBlock, hs_function: sf.func.name.clone(), cycle_us: 1000, inputs: vec![], outputs: vec![], locals: vec![] };
                let st = generate_st_code(&pou, &format!("    (* {} *)", sf.func.name));
                report.push_str(&format!("\n--- ST Code: {} ---\n{}", sf.func.name, st));
            }
        }
        "ad" => {
            use hardyscript::automotive::*;
            report.push_str("=== 自动驾驶分析 (CAN FD / AUTOSAR / Fail-Op) ===\n\n");
            report.push_str(&format!("函数: {}\n", func_count));

            let cfg = CanFdConfig::standard();
            let msgs: Vec<CanFdMessage> = ast.functions.iter().enumerate().map(|(i, sf)| {
                CanFdMessage { name: sf.func.name.clone(), id: 0x100 + i as u32 * 0x100, extended: false, dlc: 8, period_ms: 10.0, sender: "ECU".into(), asil: AsilGrade::AsilB, signals: vec![] }
            }).collect();
            let load = calculate_canfd_bus_load(&msgs, &cfg);
            report.push_str(&format!("\nCAN FD: {} 消息, 总线负载 {:.1}%\n", msgs.len(), load));

            let failop = FailOpConfig { system_name: "SteerCtrl".into(), primary_asil: AsilGrade::AsilD, backup_asil: AsilGrade::AsilB, heartbeat_ms: 10, fault_timeout_ms: 50, degraded_speed_limit: 30, mrc_time_budget_s: 5.0 };
            let code = generate_failop_code(&failop);
            report.push_str(&format!("\nFail-Operational:\n{}", code));
        }
        "rt" => {
            use hardyscript::hardy_rtos::*;
            report.push_str("=== 嵌入式 RTOS 分析 (微内核 / BSP / TLA+) ===\n\n");

            let tasks: Vec<RtosTask> = ast.functions.iter().enumerate().map(|(i, sf)| {
                RtosTask { name: sf.func.name.clone(), priority: i as u8, stack_bytes: 1024, period_us: (i as u64 + 1) * 1000, wcet_us: 100, hs_function: sf.func.name.clone() }
            }).collect();
            let config = RtosConfig { system_name: "app".into(), target: RtosTarget::Stm32f4, cpu_freq_hz: 168_000_000, tick_us: 100, tasks: tasks.clone(), time_slicing: false, time_slice_ticks: 5 };
            let kernel = generate_rtos_kernel(&config);
            report.push_str(&format!("任务: {}\n", tasks.len()));
            report.push_str(&format!("目标: STM32F4 (168MHz)\n"));
            report.push_str(&format!("\n--- 微内核代码 (前 50 行) ---\n"));
            for line in kernel.lines().take(50) {
                report.push_str(line);
                report.push('\n');
            }
        }
        "av" => {
            use hardyscript::avionics::*;
            report.push_str("=== 航空电子分析 (DO-178C / ARINC 653 / TMR) ===\n\n");

            let dal = DalLevel::DalA;
            report.push_str(&format!("DAL: {} ({})\n", dal.as_str(), dal.structural_coverage()));

            let partitions = vec![Arinc653Partition { name: "FlightCtrl".into(), criticality: DalLevel::DalA, time_window_us: 5000, memory_bytes: 65536, processes: ast.functions.iter().map(|sf| Arinc653Process { name: sf.func.name.clone(), period_us: 5000, wcet_us: 500, stack_bytes: 4096, hs_function: sf.func.name.clone() }).collect() }];
            let sched = plan_arinc653_schedule(&partitions, &[], 10000);
            report.push_str(&format!("\nARINC 653: {} 分区, 主时间帧 {}μs\n", sched.partitions.len(), sched.major_frame_us));
            report.push_str(&format!("时间利用率: {:.1}%\n", sched.time_utilization * 100.0));

            let objectives = generate_do178c_matrix(dal);
            let auto_count = objectives.iter().filter(|o| o.auto_satisfied).count();
            report.push_str(&format!("\nDO-178C 目标: {} 条, 自动满足: {} 条\n", objectives.len(), auto_count));

            let tmr = TmrConfig { system_name: "FCC".into(), function_name: "ctrl".into(), channels: 3, diverse_compilation: true, output_type: TmrOutputType::Float32, vote_period_us: 1000 };
            let tmr_code = generate_tmr_code(&tmr);
            report.push_str(&format!("\n--- TMR 表决器 ---\n{}", tmr_code));
        }
        _ => return Err(format!("Unknown industry target: {}. Use: ic, ad, rt, av", target)),
    }

    Ok(report)
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .manage(Mutex::new(DuanyanState::new()))
        .manage(BridgeState::new())
        .manage(ClusterState::new())
        .manage(SerialMonitorState::new())
        .invoke_handler(tauri::generate_handler![
            compile_source,
            compile_and_run,
            get_version,
            bridge_list_ports,
            bridge_connect,
            bridge_disconnect,
            bridge_read_logs,
            bridge_send_command,
            build_and_flash,
            create_snar_project,
            cluster_connect_captain,
            cluster_disconnect,
            cluster_status,
            cluster_discover,
            cluster_start_inference,
            cluster_split_model,
            save_file,
            create_file,
            create_dir,
            delete_path,
            rename_path,
            duanyan_inference,
            duanyan_train,
            duanyan_generate,
            duanyan_replay,
            duanyan_save_weights,
            duanyan_load_weights,
            duanyan_status,
            duanyan_generate_decoder,
            duanyan_set_backprop,
            duanyan_quantize,
            duanyan_generate_cached,
            duanyan_train_cached,
            duanyan_export_arch,
            duanyan_export_json,
            duanyan_generate_code,
            duanyan_quant_report,
            duanyan_f16_report,
            duanyan_rlhf_train,
            duanyan_rlhf_stats,
            duanyan_distill_step,
            duanyan_distill_stats,
            duanyan_lora_train,
            duanyan_lora_merge,
            duanyan_lora_adapter_stats,
            duanyan_gnn_demo,
            duanyan_gnn_stats,
            duanyan_diffusion_demo,
            duanyan_diffusion_stats,
            duanyan_causal_demo,
            duanyan_causal_stats,
            duanyan_vae_demo,
            duanyan_vae_stats,
            duanyan_bnn_demo,
            duanyan_bnn_stats,
            duanyan_node_demo,
            duanyan_node_stats,
            duanyan_reservoir_demo,
            duanyan_reservoir_stats,
            duanyan_capsule_demo,
            duanyan_capsule_stats,
            duanyan_ebm_demo,
            duanyan_ebm_stats,
            duanyan_hypernet_demo,
            duanyan_hypernet_stats,
            duanyan_flow_demo,
            duanyan_flow_stats,
            duanyan_snn_demo,
            duanyan_snn_stats,
            duanyan_world_model_demo,
            duanyan_world_model_stats,
            duanyan_neuro_sym_demo,
            duanyan_neuro_sym_stats,
            duanyan_sparse_ae_demo,
            duanyan_sparse_ae_stats,
            duanyan_mem_net_demo,
            duanyan_mem_net_stats,
            duanyan_cbm_demo,
            duanyan_cbm_stats,
            duanyan_hrr_demo,
            duanyan_hrr_stats,
            duanyan_lsm_demo,
            duanyan_lsm_stats,
            duanyan_nps_demo,
            duanyan_nps_stats,
            duanyan_neuro_core_demo,
            duanyan_neuro_core_stats,
            duanyan_ckpt_merge_demo,
            duanyan_ckpt_merge_stats,
            duanyan_ttc_demo,
            duanyan_ttc_stats,
            duanyan_kd_demo,
            duanyan_kd_stats,
            duanyan_draft_verify_demo,
            duanyan_draft_verify_stats,
            duanyan_act_patch_demo,
            duanyan_act_patch_stats,
            duanyan_ser_demo,
            duanyan_ser_stats,
            duanyan_mech_interp_demo,
            duanyan_mech_interp_stats,
            duanyan_sym_reg_demo,
            duanyan_sym_reg_stats,
            duanyan_reason_trace_demo,
            duanyan_reason_trace_stats,
            duanyan_dpo_demo,
            duanyan_dpo_stats,
            duanyan_uq_demo,
            duanyan_uq_stats,
            duanyan_data_val_demo,
            duanyan_data_val_stats,
            duanyan_moo_demo,
            duanyan_moo_stats,
            duanyan_task_vec_demo,
            duanyan_task_vec_stats,
            duanyan_prompt_opt_demo,
            duanyan_prompt_opt_stats,
            duanyan_online_learn_demo,
            duanyan_online_learn_stats,
            duanyan_bandit_demo,
            duanyan_bandit_stats,
            duanyan_reward_shape_demo,
            duanyan_reward_shape_stats,
            duanyan_active_inf_demo,
            duanyan_active_inf_stats,
            duanyan_fed_personal_demo,
            duanyan_fed_personal_stats,
            duanyan_model_comp_demo,
            duanyan_model_comp_stats,
            duanyan_bayes_opt_demo,
            duanyan_bayes_opt_stats,
            duanyan_domain_adapt_demo,
            duanyan_domain_adapt_stats,
            duanyan_ssl_demo,
            duanyan_ssl_stats,
            duanyan_transfer_demo,
            duanyan_transfer_stats,
            duanyan_few_shot_demo,
            duanyan_few_shot_stats,
            duanyan_meta_rl_demo,
            duanyan_meta_rl_stats,
            duanyan_zero_shot_demo,
            duanyan_zero_shot_stats,
            duanyan_task_adapt_demo,
            duanyan_task_adapt_stats,
            duanyan_rep_mixup_demo,
            duanyan_rep_mixup_stats,
            duanyan_weight_share_demo,
            duanyan_weight_share_stats,
            duanyan_disentangle_demo,
            duanyan_disentangle_stats,
            duanyan_grad_surgery_demo,
            duanyan_grad_surgery_stats,
            duanyan_evo_strat_demo,
            duanyan_evo_strat_stats,
            duanyan_hyper_opt_demo,
            duanyan_hyper_opt_stats,
            duanyan_multi_agent_demo,
            duanyan_multi_agent_stats,
            duanyan_imitation_demo,
            duanyan_imitation_stats,
            duanyan_inverse_rl_demo,
            duanyan_inverse_rl_stats,
            duanyan_ntk_demo,
            duanyan_ntk_stats,
            duanyan_ot_demo,
            duanyan_ot_stats,
            duanyan_scaling_demo,
            duanyan_scaling_stats,
            duanyan_model_edit_demo,
            duanyan_model_edit_stats,
            duanyan_grok_demo,
            duanyan_grok_stats,
            duanyan_double_desc_demo,
            duanyan_double_desc_stats,
            duanyan_ssm_demo,
            duanyan_ssm_stats,
            duanyan_tool_use_demo,
            duanyan_tool_use_stats,
            duanyan_self_refine_demo,
            duanyan_self_refine_stats,
            duanyan_reason_chain_demo,
            duanyan_reason_chain_stats,
            duanyan_red_team_demo,
            duanyan_red_team_stats,
            duanyan_safety_demo,
            duanyan_safety_stats,
            duanyan_jailbreak_demo,
            duanyan_jailbreak_stats,
            duanyan_ring_attn_demo,
            duanyan_ring_attn_stats,
            duanyan_arena_demo,
            duanyan_arena_stats,
            duanyan_synth_data_demo,
            duanyan_synth_data_stats,
            duanyan_orpo_demo,
            duanyan_orpo_stats,
            duanyan_simpo_demo,
            duanyan_simpo_stats,
            duanyan_infini_attn_demo,
            duanyan_infini_attn_stats,
            duanyan_medusa_demo,
            duanyan_medusa_stats,
            duanyan_eagle_demo,
            duanyan_eagle_stats,
            duanyan_chunked_demo,
            duanyan_chunked_stats,
            duanyan_dora_demo,
            duanyan_dora_stats,
                                    duanyan_top_k_demo,
            duanyan_top_k_stats,
            duanyan_hyena_demo,
            duanyan_hyena_stats,
            duanyan_rwkv_demo,
            duanyan_rwkv_stats,
            duanyan_prompt_comp_demo,
            duanyan_prompt_comp_stats,
            duanyan_flash_mla_demo,
            duanyan_flash_mla_stats,
            duanyan_agent_plan_demo,
            duanyan_agent_plan_stats,
            duanyan_spec_reject_demo,
            duanyan_spec_reject_stats,
            duanyan_paged_kv_demo,
            duanyan_paged_kv_stats,
            duanyan_agent_mem_demo,
            duanyan_agent_mem_stats,
            duanyan_dyn_batch_demo,
            duanyan_dyn_batch_stats,
            duanyan_vis_enc_demo,
            duanyan_vis_enc_stats,
            duanyan_txt2img_demo,
            duanyan_txt2img_stats,
            duanyan_prom_cache_demo,
            duanyan_prom_cache_stats,
            duanyan_img_cap_demo,
            duanyan_img_cap_stats,
            duanyan_obj_det_demo,
            duanyan_obj_det_stats,
            duanyan_super_res_demo,
            duanyan_super_res_stats,
            duanyan_style_trans_demo,
            duanyan_style_trans_stats,
            duanyan_img_inpaint_demo,
            duanyan_img_inpaint_stats,
            duanyan_audio_enc_demo,
            duanyan_audio_enc_stats,
            duanyan_img_seg_demo,
            duanyan_img_seg_stats,
            duanyan_depth_est_demo,
            duanyan_depth_est_stats,
            duanyan_opt_flow_demo,
            duanyan_opt_flow_stats,
            duanyan_pose_est_demo,
            duanyan_pose_est_stats,
            duanyan_speech_recog_demo,
            duanyan_speech_recog_stats,
            duanyan_music_gen_demo,
            duanyan_music_gen_stats,
            duanyan_code_parser_demo,
            duanyan_code_parser_stats,
            duanyan_code_gen_demo,
            duanyan_code_gen_stats,
            duanyan_code_exec_demo,
            duanyan_code_exec_stats,
            duanyan_code_debug_demo,
            duanyan_code_debug_stats,
            duanyan_prog_repair_demo,
            duanyan_prog_repair_stats,
            duanyan_code_review_demo,
            duanyan_code_review_stats,
            duanyan_syntax_hl_demo,
            duanyan_syntax_hl_stats,
            duanyan_code_complete_demo,
            duanyan_code_complete_stats,
            duanyan_code_refactor_demo,
            duanyan_code_refactor_stats,
            duanyan_code_pipeline_analyze,
            duanyan_code_pipeline_full,
            duanyan_code_pipeline_stats,
            duanyan_code_analyze_panel,
            duanyan_moe_forward,
            duanyan_moe_stats,
            duanyan_onnx_export,
            duanyan_sparse_attention,
            duanyan_flash_attention,
            duanyan_flash_report,
            duanyan_speculative_generate,
            duanyan_spec_stats,
            duanyan_rope_attention,
            duanyan_rope_report,
            duanyan_model_merge,
            duanyan_multimodal,
            duanyan_multimodal_stats,
            duanyan_gqa_forward,
            duanyan_gqa_report,
            duanyan_act_cache_forward,
            duanyan_act_cache_stats,
            duanyan_icl_query,
            duanyan_icl_stats,
            duanyan_mod_forward,
            duanyan_mod_report,
            duanyan_rag_index,
            duanyan_rag_query,
            duanyan_contrastive_train,
            duanyan_contrastive2_stats,
            duanyan_rmsnorm_forward,
            duanyan_rmsnorm_report,
            duanyan_swiglu_forward,
            duanyan_swiglu_report,
            duanyan_beam_search_decode,
            duanyan_beam_search_stats,
            duanyan_sample_text,
            duanyan_sampling_config,
            duanyan_lr_step,
            duanyan_lr_curve,
            duanyan_alibi_forward,
            duanyan_alibi_report,
            duanyan_grad_clip_demo,
            duanyan_grad_clip_stats,
            duanyan_adam_simulate,
            duanyan_adam_stats,
            duanyan_token_merge,
            duanyan_token_merge_stats,
            duanyan_weight_tying_demo,
            duanyan_weight_tying_stats,
            duanyan_cross_attn_demo,
            duanyan_cross_attn_stats,
            duanyan_perplexity_eval,
            duanyan_perplexity_report,
            duanyan_rep_penalty_demo,
            duanyan_rep_penalty_stats,
            duanyan_layer_norm_demo,
            duanyan_layer_norm_compare,
            duanyan_streaming_attn_demo,
            duanyan_streaming_attn_compare,
            duanyan_pos_interp_demo,
            duanyan_pos_interp_compare,
            duanyan_grad_ckpt_demo,
            duanyan_grad_ckpt_compare,
            duanyan_prompt_tmpl_demo,
            duanyan_prompt_tmpl_compare,
                                    duanyan_seq_pack_demo,
            duanyan_seq_pack_compare,
            duanyan_attn_vis_demo,
            duanyan_attn_vis_stats,
            duanyan_mqa_demo,
            duanyan_mqa_stats,
            duanyan_temp_anneal_demo,
            duanyan_temp_anneal_compare,
            duanyan_early_stop_demo,
            duanyan_early_stop_stats,
            duanyan_grad_accum_demo, duanyan_grad_accum_stats,
            duanyan_swa_demo, duanyan_swa_compare,
            duanyan_token_freq_demo, duanyan_token_freq_stats,
            duanyan_dropout_demo, duanyan_dropout_stats,
            duanyan_weight_init_demo, duanyan_weight_init_stats,
            duanyan_batch_norm_demo, duanyan_batch_norm_compare,
            duanyan_mixup_demo, duanyan_mixup_stats,
            duanyan_focal_loss_demo, duanyan_focal_loss_stats,
            duanyan_stoch_depth_demo, duanyan_stoch_depth_stats,
            duanyan_label_smooth_demo, duanyan_label_smooth_stats,
            duanyan_pruning_demo, duanyan_pruning_stats,
            duanyan_grad_norm_demo, duanyan_grad_norm_stats,
            duanyan_warmup_demo, duanyan_warmup_stats,
            duanyan_ema_demo, duanyan_ema_stats,
            duanyan_spectral_norm_demo, duanyan_spectral_norm_stats,
            duanyan_ntk_rope_demo, duanyan_ntk_rope_stats,
            duanyan_multi_task_demo, duanyan_multi_task_stats,
            duanyan_wasserstein_demo, duanyan_wasserstein_stats,
            duanyan_self_consistency_demo, duanyan_self_consistency_stats,
            duanyan_contrastive_decoding_demo, duanyan_contrastive_decoding_stats,
            duanyan_token_unlearning_demo, duanyan_token_unlearning_stats,
            duanyan_paged_attn_demo, duanyan_paged_attn_stats,
            duanyan_spec_rejection_demo, duanyan_spec_rejection_stats,
            duanyan_dyn_batch_demo, duanyan_dyn_batch_stats,
            duanyan_gptq_demo, duanyan_gptq_stats,
            duanyan_bleu_demo, duanyan_bleu_stats,
            duanyan_cot_demo, duanyan_cot_stats,
            duanyan_inst_norm_demo, duanyan_inst_norm_stats,
            duanyan_rouge_demo, duanyan_rouge_stats,
            duanyan_tot_demo, duanyan_tot_stats,
            duanyan_kto_demo, duanyan_kto_stats,
            duanyan_mod_router_demo, duanyan_mod_router_stats,
            duanyan_sparse_moe_demo, duanyan_sparse_moe_stats,
            duanyan_bpe_demo, duanyan_bpe_stats,
            duanyan_dist_demo, duanyan_dist_stats,
            duanyan_reward_demo,
            duanyan_reward_stats,
            duanyan_adv_train_demo,
            duanyan_adv_train_stats,
            duanyan_nas_demo,
            duanyan_nas_stats,
            duanyan_data_aug_demo,
            duanyan_data_aug_stats,
            duanyan_curriculum_demo,
            duanyan_curriculum_stats,
            duanyan_watermark_demo,
            duanyan_watermark_stats,
            duanyan_group_norm_demo,
            duanyan_group_norm_stats,
            duanyan_kv_eviction_demo,
            duanyan_kv_eviction_stats,
            duanyan_bench_demo,
            duanyan_bench_stats,
            duanyan_raft_demo, duanyan_raft_stats,
            duanyan_federated_demo, duanyan_federated_stats,
            duanyan_kg_demo, duanyan_kg_stats,
            duanyan_prompt_tune_demo, duanyan_prompt_tune_stats,
            duanyan_constitut_demo, duanyan_constitut_stats,
            duanyan_active_learn_demo, duanyan_active_learn_stats,
            duanyan_mm_fusion_demo, duanyan_mm_fusion_stats,
            duanyan_meta_learn_demo, duanyan_meta_learn_stats,
            duanyan_continual_demo, duanyan_continual_stats,
            duanyan_sparse_gate_demo, duanyan_sparse_gate_stats,
            duanyan_inst_tune_demo, duanyan_inst_tune_stats,
            duanyan_cov_demo, duanyan_cov_stats,
            duanyan_data_loader_load,
        duanyan_data_loader_batch,
        duanyan_data_loader_stats,
        duanyan_data_pipeline_stats,
        duanyan_tokenized_ds_stats,
        duanyan_dist_train_stats,
        duanyan_mp_stats,
        duanyan_ckpt_stats,
        duanyan_compress_demo, duanyan_compress_stats,
            duanyan_self_play_demo, duanyan_self_play_stats,
            duanyan_ipo_demo, duanyan_ipo_stats,
            duanyan_ensemble_demo, duanyan_ensemble_stats,
            duanyan_contrastive2_demo, 
            duanyan_rag_pipeline_demo, duanyan_rag_pipeline_stats,
            duanyan_lora_adapter_demo,
            // KV Quant + Cont Batching (前端已调用但未注册)
            duanyan_kv_quant_demo, duanyan_kv_quant_stats,
            duanyan_cont_batching_demo, duanyan_cont_batching_stats,
            // Phase G: Board Management
            board_scan, board_get_all, board_get_pins,
            // Phase G: Serial Monitor
            serial_list_ports, serial_open, serial_close, serial_read, serial_write,
            // Phase G: Templates & TF Card
            template_list, template_create,
            tfcard_list, tfcard_deploy, tfcard_logs,
            // Phase G: Benchmark
            bench_run, bench_compare,
            // Phase H: Workspace
            workspace_init, workspace_load, workspace_status, workspace_build_all, workspace_add_package,
            // Phase H: Git
            git_status, git_log, git_diff, git_staged_diff,
            git_add, git_add_all, git_unstage,
            git_commit, git_push, git_pull,
            git_branches, git_checkout, git_create_branch,
            git_init, git_install_hooks,
            // Phase H: Lint
            lint_project, lint_file_content, lint_fix, lint_rules,
            // Phase H: CI/CD
            ci_build, ci_release, ci_generate_template, ci_archive,
            // DUANYAN Native Flasher
            flasher_detect_ports, flasher_chip_info, flasher_flash_bin,
            flasher_flash_data, flasher_erase,
            flasher_detect_toolchain, flasher_build, flasher_build_and_flash,
            flasher_compile_js, flasher_js_to_flash,
                        duanyan_smart_gen,
            deterministic_analyze,
            hardy_diagnose, hardy_complete, hardy_hover,
            flasher_full_pipeline,
            flasher_compile_to_bin,
        ])
        .run(tauri::generate_context!())
        .expect("DUANYAN IDE startup failed");
}


