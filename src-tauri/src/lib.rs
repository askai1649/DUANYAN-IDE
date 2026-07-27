//! SNAR IDE - Rust Backend
//!
//! Integrates SNARjs compiler, execution engine, debugger, and qianlu AI

use std::fs;
use std::path::Path;
use std::sync::Mutex;

mod bridge_protocol;
mod bridge_serial;
mod cluster_protocol;
mod cluster_serial;
mod board_detect;

use bridge_protocol::BridgeLog;
use bridge_serial::BridgeSerial;
use cluster_serial::ClusterSerial;

/// Persistent qianlu model state
struct QianluState {
    model: qianlu::QianluModel,
}

impl QianluState {
    fn new() -> Self {
        Self {
            model: qianlu::QianluModel::new(),
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
    let tokens = snarjs::lexer::tokenize(source)
        .map_err(|e| format!("Lexer error: {}", e))?;
    let ast = snarjs::parser::parse(&tokens)
        .map_err(|e| format!("Parser error: {}", e))?;
    let optimized = snarjs::optimizer::optimize(&ast);
    let instructions = snarjs::compiler::compile(&optimized)
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
    let tokens = snarjs::lexer::tokenize(source)
        .map_err(|e| format!("Lexer error: {}", e))?;
    let ast = snarjs::parser::parse(&tokens)
        .map_err(|e| format!("Parser error: {}", e))?;
    let optimized = snarjs::optimizer::optimize(&ast);
    let instructions = snarjs::compiler::compile(&optimized)
        .map_err(|e| format!("Compile error: {}", e))?;
    let machine_code: Vec<u32> = instructions.iter().map(|i| i.encode()).collect();

    let mut engine = snarjs::engine::Engine::new(machine_code);
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

/// Get SNARjs version info
#[tauri::command]
fn get_version() -> String {
    format!("SNARjs v{} | COUNPRE64 ISA", env!("CARGO_PKG_VERSION"))
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

/// Create a new SNAR project directory with snar.config.json and main.js.
#[tauri::command]
fn create_snar_project(project_dir: String, name: String) -> Result<String, String> {
    let base = std::path::Path::new(&project_dir);
    std::fs::create_dir_all(base).map_err(|e| format!("Create project dir failed: {}", e))?;
    let mut cfg = snarjs::config::ProjectConfig::default();
    cfg.name = name;
    let config_path = base.join("snar.config.json");
    std::fs::write(&config_path, cfg.to_json())
        .map_err(|e| format!("Write snar.config.json failed: {}", e))?;
    let main_path = base.join("main.js");
    if !main_path.exists() {
        std::fs::write(&main_path, "// New SNAR project\nlet x = 42;\nconsole.log(x);\n")
            .map_err(|e| format!("Write main.js failed: {}", e))?;
    }
    Ok(format!("Project created at {}", base.display()))
}

/// Compile current source and flash to hardware via SNARjs.
#[tauri::command]
fn build_and_flash(
    source: String,
    project_dir: String,
    stem: String,
    target: String,
) -> Result<String, String> {
    let base = std::path::Path::new(&project_dir);
    std::fs::create_dir_all(base).map_err(|e| format!("Create project dir failed: {}", e))?;
    let cfg = snarjs::config::ProjectConfig::load(base)?;
    let effective_target = if target.is_empty() { cfg.target.platform.clone() } else { target };
    snarjs::flash_source(&source, base, &stem, &effective_target)
}

// ========== qianlu AI Commands (persistent model via State) ==========

/// qianlu inference - process a prompt and return response
#[tauri::command]
fn qianlu_inference(
    state: tauri::State<'_, Mutex<QianluState>>,
    prompt: String,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let response = state.model.infer_text(&prompt);
    let status = state.model.get_learning_status();
    Ok(format!("{}\n\n--- qianlu stats ---\n{}", response, status.format()))
}

/// qianlu train - run training epochs
#[tauri::command]
fn qianlu_train(
    state: tauri::State<'_, Mutex<QianluState>>,
    epochs: usize,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let result = state.model.train(epochs);
    let stats = state.model.training_stats();
    Ok(format!("{}\n{}", result, stats))
}

/// qianlu generate - generate text from seed
#[tauri::command]
fn qianlu_generate(
    state: tauri::State<'_, Mutex<QianluState>>,
    seed: String,
    max_len: usize,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let generated = state.model.generate_text(&seed, max_len);
    Ok(generated)
}

/// qianlu replay - replay training history
#[tauri::command]
fn qianlu_replay(
    state: tauri::State<'_, Mutex<QianluState>>,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.replay())
}

/// qianlu save weights to file
#[tauri::command]
fn qianlu_save_weights(
    state: tauri::State<'_, Mutex<QianluState>>,
    path: String,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let weights = state.model.save_weights();
    let p = Path::new(&path);
    if let Some(parent) = p.parent() {
        fs::create_dir_all(parent).map_err(|e| format!("Cannot create dir: {}", e))?;
    }
    fs::write(p, &weights).map_err(|e| format!("Write failed: {}", e))?;
    let summary = state.model.weight_summary();
    Ok(format!("Saved {} bytes to {}\n{}", weights.len(), path, summary))
}

/// qianlu load weights from file
#[tauri::command]
fn qianlu_load_weights(
    state: tauri::State<'_, Mutex<QianluState>>,
    path: String,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let data = fs::read(&path).map_err(|e| format!("Read failed: {}", e))?;
    state.model.load_weights(&data)?;
    let summary = state.model.weight_summary();
    Ok(format!("Loaded {} bytes from {}\n{}", data.len(), path, summary))
}

/// qianlu get full status
#[tauri::command]
fn qianlu_status(
    state: tauri::State<'_, Mutex<QianluState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let status = state.model.get_learning_status();
    let training = state.model.training_stats();
    let weights = state.model.weight_summary();
    Ok(format!("{}\n\n{}\n\n{}", status.format(), training, weights))
}

/// qianlu generate via Transformer decoder (autoregressive)
#[tauri::command]
fn qianlu_generate_decoder(
    state: tauri::State<'_, Mutex<QianluState>>,
    seed: String,
    max_len: usize,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let generated = state.model.generate_decoder(&seed, max_len);
    Ok(generated)
}

/// qianlu toggle backprop training mode
#[tauri::command]
fn qianlu_set_backprop(
    state: tauri::State<'_, Mutex<QianluState>>,
    enabled: bool,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.set_backprop_mode(enabled))
}

/// qianlu generate with KV Cache acceleration
#[tauri::command]
fn qianlu_generate_cached(
    state: tauri::State<'_, Mutex<QianluState>>,
    seed: String,
    max_len: usize,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let generated = state.model.generate_cached(&seed, max_len);
    Ok(format!("KV-Cached: {}", generated))
}

/// qianlu train with cached forward pass
#[tauri::command]
fn qianlu_train_cached(
    state: tauri::State<'_, Mutex<QianluState>>,
    epochs: usize,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.train_cached(epochs))
}

/// qianlu export architecture description
#[tauri::command]
fn qianlu_export_arch(
    state: tauri::State<'_, Mutex<QianluState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.export_architecture())
}

/// qianlu export architecture as JSON
#[tauri::command]
fn qianlu_export_json(
    state: tauri::State<'_, Mutex<QianluState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.export_architecture_json())
}

/// qianlu full quantization report
#[tauri::command]
fn qianlu_quant_report(
    state: tauri::State<'_, Mutex<QianluState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.full_quantization_report())
}

/// qianlu RoPE attention
#[tauri::command]
fn qianlu_rope_attention(
    state: tauri::State<'_, Mutex<QianluState>>,
    text: String,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.rope_attention(&text))
}

/// qianlu RoPE report
#[tauri::command]
fn qianlu_rope_report(
    state: tauri::State<'_, Mutex<QianluState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.rope_report())
}

/// qianlu model merge demo
#[tauri::command]
fn qianlu_model_merge(
    state: tauri::State<'_, Mutex<QianluState>>,
    strategy: Option<String>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.model_merge_demo(strategy.as_deref().unwrap_or("linear")))
}

/// qianlu multimodal process
#[tauri::command]
fn qianlu_multimodal(
    state: tauri::State<'_, Mutex<QianluState>>,
    text: String,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.multimodal_process(&text))
}

/// qianlu multimodal stats
#[tauri::command]
fn qianlu_multimodal_stats(
    state: tauri::State<'_, Mutex<QianluState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.multimodal_stats())
}

/// qianlu sparse attention
#[tauri::command]
fn qianlu_sparse_attention(
    state: tauri::State<'_, Mutex<QianluState>>,
    text: String,
    pattern: String,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.sparse_attention(&text, &pattern))
}

/// qianlu flash attention
#[tauri::command]
fn qianlu_flash_attention(
    state: tauri::State<'_, Mutex<QianluState>>,
    text: String,
    block_size: Option<usize>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.flash_attention(&text, block_size.unwrap_or(8)))
}

/// qianlu flash report
#[tauri::command]
fn qianlu_flash_report(
    state: tauri::State<'_, Mutex<QianluState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.flash_report())
}

/// qianlu speculative decoding generation
#[tauri::command]
fn qianlu_speculative_generate(
    state: tauri::State<'_, Mutex<QianluState>>,
    text: String,
    max_len: Option<usize>,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.speculative_generate(&text, max_len.unwrap_or(20)))
}

/// qianlu speculative decoding stats
#[tauri::command]
fn qianlu_spec_stats(
    state: tauri::State<'_, Mutex<QianluState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.spec_decode_stats())
}

/// qianlu LoRA fine-tuning
#[tauri::command]
fn qianlu_lora_train(
    state: tauri::State<'_, Mutex<QianluState>>,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.lora_train())
}

/// qianlu LoRA merge
#[tauri::command]
fn qianlu_lora_merge(
    state: tauri::State<'_, Mutex<QianluState>>,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.lora_merge())
}

/// qianlu LoRA stats
#[tauri::command]
fn qianlu_lora_adapter_stats(
    state: tauri::State<'_, Mutex<QianluState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.lora_stats())
}

/// qianlu MoE forward pass
#[tauri::command]
fn qianlu_moe_forward(
    state: tauri::State<'_, Mutex<QianluState>>,
    text: String,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.moe_forward(&text))
}

/// qianlu MoE stats
#[tauri::command]
fn qianlu_moe_stats(
    state: tauri::State<'_, Mutex<QianluState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.moe_stats())
}

/// qianlu ONNX export report
#[tauri::command]
fn qianlu_onnx_export(
    state: tauri::State<'_, Mutex<QianluState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.onnx_export())
}

/// qianlu f16 half-precision report
#[tauri::command]
fn qianlu_f16_report(
    state: tauri::State<'_, Mutex<QianluState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.f16_report())
}

/// qianlu RLHF training
#[tauri::command]
fn qianlu_rlhf_train(
    state: tauri::State<'_, Mutex<QianluState>>,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.rlhf_train())
}

/// qianlu RLHF stats
#[tauri::command]
fn qianlu_rlhf_stats(
    state: tauri::State<'_, Mutex<QianluState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.rlhf_stats())
}

/// qianlu knowledge distillation step
#[tauri::command]
fn qianlu_distill_step(
    state: tauri::State<'_, Mutex<QianluState>>,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.distill_step())
}

/// qianlu distillation stats
#[tauri::command]
fn qianlu_distill_stats(
    state: tauri::State<'_, Mutex<QianluState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.distill_stats())
}

#[tauri::command]
fn qianlu_gqa_forward(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.gqa_forward(&text))
}

#[tauri::command]
fn qianlu_gqa_report(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.gqa_report())
}

#[tauri::command]
fn qianlu_act_cache_forward(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.act_cache_forward(&text))
}

#[tauri::command]
fn qianlu_act_cache_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.act_cache_stats())
}

#[tauri::command]
fn qianlu_icl_query(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.icl_query(&text))
}

#[tauri::command]
fn qianlu_icl_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.icl_stats())
}


#[tauri::command]
fn qianlu_mod_forward(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.mod_forward(&text))
}

#[tauri::command]
fn qianlu_mod_report(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.mod_report())
}

#[tauri::command]
fn qianlu_rag_index(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.rag_index(&text))
}

#[tauri::command]
fn qianlu_rag_query(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.rag_query(&text))
}

#[tauri::command]
fn qianlu_contrastive_train(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.contrastive_train(&text))
}

#[tauri::command]
fn qianlu_contrastive2_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.contrastive2_stats())
}


#[tauri::command]
fn qianlu_rmsnorm_forward(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.rmsnorm_forward(&text))
}

#[tauri::command]
fn qianlu_rmsnorm_report(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.rmsnorm_report())
}

#[tauri::command]
fn qianlu_swiglu_forward(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.swiglu_forward(&text))
}

#[tauri::command]
fn qianlu_swiglu_report(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.swiglu_report())
}

#[tauri::command]
fn qianlu_beam_search_decode(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.beam_search_decode(&text))
}

#[tauri::command]
fn qianlu_beam_search_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.beam_search_stats())
}


#[tauri::command]
fn qianlu_sample_text(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.sample_text(&text))
}

#[tauri::command]
fn qianlu_sampling_config(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.sampling_config())
}

#[tauri::command]
fn qianlu_lr_step(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.lr_step())
}

#[tauri::command]
fn qianlu_lr_curve(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.lr_curve())
}

#[tauri::command]
fn qianlu_alibi_forward(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.alibi_forward(&text))
}

#[tauri::command]
fn qianlu_alibi_report(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.alibi_report())
}


#[tauri::command]
fn qianlu_grad_clip_demo(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.grad_clip_demo())
}

#[tauri::command]
fn qianlu_grad_clip_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.grad_clip_stats())
}

#[tauri::command]
fn qianlu_adam_simulate(state: tauri::State<'_, Mutex<QianluState>>, steps: Option<usize>) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.adam_simulate(steps.unwrap_or(100)))
}

#[tauri::command]
fn qianlu_adam_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.adam_stats())
}

#[tauri::command]
fn qianlu_token_merge(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.token_merge_report(&text))
}

#[tauri::command]
fn qianlu_token_merge_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.token_merge_stats())
}


#[tauri::command]
fn qianlu_weight_tying_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.weight_tying_demo(&text))
}

#[tauri::command]
fn qianlu_weight_tying_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.weight_tying_stats())
}

#[tauri::command]
fn qianlu_cross_attn_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.cross_attn_demo(&text))
}

#[tauri::command]
fn qianlu_cross_attn_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.cross_attn_stats())
}

#[tauri::command]
fn qianlu_perplexity_eval(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.perplexity_eval(&text))
}

#[tauri::command]
fn qianlu_perplexity_report(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.perplexity_report())
}


#[tauri::command]
fn qianlu_rep_penalty_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.rep_penalty_demo(&text))
}

#[tauri::command]
fn qianlu_rep_penalty_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.rep_penalty_stats())
}

#[tauri::command]
fn qianlu_layer_norm_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.layer_norm_demo(&text))
}

#[tauri::command]
fn qianlu_layer_norm_compare(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.layer_norm_compare())
}

#[tauri::command]
fn qianlu_streaming_attn_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.streaming_attn_demo(&text))
}

#[tauri::command]
fn qianlu_streaming_attn_compare(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.streaming_attn_compare())
}


#[tauri::command]
fn qianlu_pos_interp_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.pos_interp_demo(&text))
}

#[tauri::command]
fn qianlu_pos_interp_compare(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.pos_interp_compare())
}

#[tauri::command]
fn qianlu_grad_ckpt_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.grad_ckpt_demo(&text))
}

#[tauri::command]
fn qianlu_grad_ckpt_compare(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.grad_ckpt_compare())
}

#[tauri::command]
fn qianlu_prompt_tmpl_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.prompt_tmpl_demo(&text))
}

#[tauri::command]
fn qianlu_prompt_tmpl_compare(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.prompt_tmpl_compare(&text))
}


#[tauri::command]
fn qianlu_kv_quant_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.kv_quant_demo(&text))
}

#[tauri::command]
fn qianlu_kv_quant_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.kv_quant_stats())
}

#[tauri::command]
fn qianlu_seq_pack_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.seq_pack_demo(&text))
}

#[tauri::command]
fn qianlu_seq_pack_compare(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.seq_pack_compare())
}

#[tauri::command]
fn qianlu_attn_vis_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.attn_vis_demo(&text))
}

#[tauri::command]
fn qianlu_attn_vis_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.attn_vis_stats())
}


#[tauri::command]
fn qianlu_mqa_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.mqa_demo(&text))
}

#[tauri::command]
fn qianlu_mqa_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.mqa_stats())
}

#[tauri::command]
fn qianlu_temp_anneal_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.temp_anneal_demo(&text))
}

#[tauri::command]
fn qianlu_temp_anneal_compare(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.temp_anneal_compare())
}

#[tauri::command]
fn qianlu_early_stop_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.early_stop_demo(&text))
}

#[tauri::command]
fn qianlu_early_stop_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.early_stop_stats())
}


#[tauri::command]
fn qianlu_grad_accum_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.grad_accum_demo(&text))
}
#[tauri::command]
fn qianlu_grad_accum_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.grad_accum_stats())
}
#[tauri::command]
fn qianlu_swa_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.swa_demo(&text))
}
#[tauri::command]
fn qianlu_swa_compare(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.swa_compare())
}
#[tauri::command]
fn qianlu_token_freq_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.token_freq_demo(&text))
}
#[tauri::command]
fn qianlu_token_freq_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.token_freq_stats())
}

#[tauri::command]
fn qianlu_dropout_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.dropout_demo(&text))
}
#[tauri::command]
fn qianlu_dropout_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.dropout_stats())
}
#[tauri::command]
fn qianlu_weight_init_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.weight_init_demo(&text))
}
#[tauri::command]
fn qianlu_weight_init_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.weight_init_stats())
}
#[tauri::command]
fn qianlu_batch_norm_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.batch_norm_demo(&text))
}
#[tauri::command]
fn qianlu_batch_norm_compare(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.batch_norm_compare())
}

#[tauri::command]
fn qianlu_mixup_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.mixup_demo(&text))
}
#[tauri::command]
fn qianlu_mixup_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.mixup_stats())
}
#[tauri::command]
fn qianlu_focal_loss_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.focal_loss_demo(&text))
}
#[tauri::command]
fn qianlu_focal_loss_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.focal_loss_stats())
}
#[tauri::command]
fn qianlu_stoch_depth_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.stoch_depth_demo(&text))
}
#[tauri::command]
fn qianlu_stoch_depth_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.stoch_depth_stats())
}

#[tauri::command]
fn qianlu_label_smooth_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.label_smooth_demo(&text))
}
#[tauri::command]
fn qianlu_label_smooth_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.label_smooth_stats())
}
#[tauri::command]
fn qianlu_pruning_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.pruning_demo(&text))
}
#[tauri::command]
fn qianlu_pruning_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.pruning_stats())
}
#[tauri::command]
fn qianlu_grad_norm_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.grad_norm_demo(&text))
}
#[tauri::command]
fn qianlu_grad_norm_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.grad_norm_stats())
}

#[tauri::command]
fn qianlu_warmup_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.warmup_demo(&text))
}
#[tauri::command]
fn qianlu_warmup_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.warmup_stats())
}
#[tauri::command]
fn qianlu_ema_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ema_demo(&text))
}
#[tauri::command]
fn qianlu_ema_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ema_stats())
}
#[tauri::command]
fn qianlu_spectral_norm_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.spectral_norm_demo(&text))
}
#[tauri::command]
fn qianlu_spectral_norm_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.spectral_norm_stats())
}

#[tauri::command]
fn qianlu_ntk_rope_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ntk_rope_demo(&text))
}
#[tauri::command]
fn qianlu_ntk_rope_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ntk_rope_stats())
}
#[tauri::command]
fn qianlu_multi_task_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.multi_task_demo(&text))
}
#[tauri::command]
fn qianlu_multi_task_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.multi_task_stats())
}
#[tauri::command]
fn qianlu_wasserstein_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.wasserstein_demo(&text))
}
#[tauri::command]
fn qianlu_wasserstein_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.wasserstein_stats())
}

#[tauri::command]
fn qianlu_self_consistency_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.self_consistency_demo(&text))
}
#[tauri::command]
fn qianlu_self_consistency_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.self_consistency_stats())
}
#[tauri::command]
fn qianlu_contrastive_decoding_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.contrastive_decoding_demo(&text))
}
#[tauri::command]
fn qianlu_contrastive_decoding_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.contrastive_decoding_stats())
}
#[tauri::command]
fn qianlu_token_unlearning_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.token_unlearning_demo(&text))
}
#[tauri::command]
fn qianlu_token_unlearning_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.token_unlearning_stats())
}

#[tauri::command]
fn qianlu_paged_attn_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.paged_attn_demo(&text))
}
#[tauri::command]
fn qianlu_paged_attn_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.paged_attn_stats())
}
#[tauri::command]
fn qianlu_spec_rejection_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.spec_rejection_demo(&text))
}
#[tauri::command]
fn qianlu_spec_rejection_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.spec_rejection_stats())
}
#[tauri::command]
fn qianlu_cont_batching_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.cont_batching_demo(&text))
}
#[tauri::command]
fn qianlu_cont_batching_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.cont_batching_stats())
}

#[tauri::command]
fn qianlu_gptq_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.gptq_demo(&text))
}
#[tauri::command]
fn qianlu_gptq_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.gptq_stats())
}
#[tauri::command]
fn qianlu_bleu_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.bleu_demo(&text))
}
#[tauri::command]
fn qianlu_bleu_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.bleu_stats())
}
#[tauri::command]
fn qianlu_cot_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.cot_demo(&text))
}
#[tauri::command]
fn qianlu_cot_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.cot_stats())
}

#[tauri::command]
fn qianlu_inst_norm_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.inst_norm_demo(&text))
}
#[tauri::command]
fn qianlu_inst_norm_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.inst_norm_stats())
}
#[tauri::command]
fn qianlu_rouge_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.rouge_demo(&text))
}
#[tauri::command]
fn qianlu_rouge_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.rouge_stats())
}
#[tauri::command]
fn qianlu_tot_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.tot_demo(&text))
}
#[tauri::command]
fn qianlu_tot_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.tot_stats())
}

#[tauri::command]
fn qianlu_kto_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.kto_demo(&text))
}
#[tauri::command]
fn qianlu_kto_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.kto_stats())
}
#[tauri::command]
fn qianlu_mod_router_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.mod_router_demo(&text))
}
#[tauri::command]
fn qianlu_mod_router_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.mod_router_stats())
}
#[tauri::command]
fn qianlu_sparse_moe_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.sparse_moe_demo(&text))
}
#[tauri::command]
fn qianlu_sparse_moe_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.sparse_moe_stats())
}

#[tauri::command]
fn qianlu_bpe_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.bpe_demo(&text))
}
#[tauri::command]
fn qianlu_bpe_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.bpe_stats())
}
#[tauri::command]
fn qianlu_dist_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.dist_demo(&text))
}
#[tauri::command]
fn qianlu_dist_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.dist_stats())
}



#[tauri::command]
fn qianlu_reward_demo(state: tauri::State<'_, Mutex<qianlu::QianluModel>>, text: String) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.reward_demo(&text))
}

#[tauri::command]
fn qianlu_reward_stats(state: tauri::State<'_, Mutex<qianlu::QianluModel>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.reward_stats())
}

#[tauri::command]
fn qianlu_adv_train_demo(state: tauri::State<'_, Mutex<qianlu::QianluModel>>, text: String) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.adv_train_demo(&text))
}

#[tauri::command]
fn qianlu_adv_train_stats(state: tauri::State<'_, Mutex<qianlu::QianluModel>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.adv_train_stats())
}

#[tauri::command]
fn qianlu_nas_demo(state: tauri::State<'_, Mutex<qianlu::QianluModel>>, text: String) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.nas_demo(&text))
}

#[tauri::command]
fn qianlu_nas_stats(state: tauri::State<'_, Mutex<qianlu::QianluModel>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.nas_stats())
}

#[tauri::command]
fn qianlu_data_aug_demo(state: tauri::State<'_, Mutex<qianlu::QianluModel>>, text: String) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.data_aug_demo(&text))
}

#[tauri::command]
fn qianlu_data_aug_stats(state: tauri::State<'_, Mutex<qianlu::QianluModel>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.data_aug_stats())
}

#[tauri::command]
fn qianlu_curriculum_demo(state: tauri::State<'_, Mutex<qianlu::QianluModel>>, text: String) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.curriculum_demo(&text))
}

#[tauri::command]
fn qianlu_curriculum_stats(state: tauri::State<'_, Mutex<qianlu::QianluModel>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.curriculum_stats())
}

#[tauri::command]
fn qianlu_watermark_demo(state: tauri::State<'_, Mutex<qianlu::QianluModel>>, text: String) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.watermark_demo(&text))
}

#[tauri::command]
fn qianlu_watermark_stats(state: tauri::State<'_, Mutex<qianlu::QianluModel>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.watermark_stats())
}

#[tauri::command]
fn qianlu_group_norm_demo(state: tauri::State<'_, Mutex<qianlu::QianluModel>>, text: String) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.group_norm_demo(&text))
}

#[tauri::command]
fn qianlu_group_norm_stats(state: tauri::State<'_, Mutex<qianlu::QianluModel>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.group_norm_stats())
}

#[tauri::command]
fn qianlu_kv_eviction_demo(state: tauri::State<'_, Mutex<qianlu::QianluModel>>, text: String) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.kv_eviction_demo(&text))
}

#[tauri::command]
fn qianlu_kv_eviction_stats(state: tauri::State<'_, Mutex<qianlu::QianluModel>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.kv_eviction_stats())
}

#[tauri::command]
fn qianlu_bench_demo(state: tauri::State<'_, Mutex<qianlu::QianluModel>>, text: String) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.bench_demo(&text))
}

#[tauri::command]
fn qianlu_bench_stats(state: tauri::State<'_, Mutex<qianlu::QianluModel>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.bench_stats())
}

#[tauri::command]
fn qianlu_raft_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.raft_demo(&text))
}
#[tauri::command]
fn qianlu_raft_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.raft_stats())
}
#[tauri::command]
fn qianlu_federated_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.federated_demo(&text))
}
#[tauri::command]
fn qianlu_federated_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.federated_stats())
}
#[tauri::command]
fn qianlu_kg_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.kg_demo(&text))
}
#[tauri::command]
fn qianlu_kg_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.kg_stats())
}
#[tauri::command]
fn qianlu_prompt_tune_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.prompt_tune_demo(&text))
}
#[tauri::command]
fn qianlu_prompt_tune_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.prompt_tune_stats())
}
#[tauri::command]
fn qianlu_constitut_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.constitut_demo(&text))
}
#[tauri::command]
fn qianlu_constitut_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.constitut_stats())
}
#[tauri::command]
fn qianlu_active_learn_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.active_learn_demo(&text))
}
#[tauri::command]
fn qianlu_active_learn_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.active_learn_stats())
}
#[tauri::command]
fn qianlu_mm_fusion_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.mm_fusion_demo(&text))
}
#[tauri::command]
fn qianlu_mm_fusion_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.mm_fusion_stats())
}
#[tauri::command]
fn qianlu_meta_learn_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.meta_learn_demo(&text))
}
#[tauri::command]
fn qianlu_meta_learn_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.meta_learn_stats())
}
#[tauri::command]
fn qianlu_continual_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.continual_demo(&text))
}
#[tauri::command]
fn qianlu_continual_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.continual_stats())
}
#[tauri::command]
fn qianlu_sparse_gate_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.sparse_gate_demo(&text))
}
#[tauri::command]
fn qianlu_sparse_gate_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.sparse_gate_stats())
}
#[tauri::command]
fn qianlu_inst_tune_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.inst_tune_demo(&text))
}
#[tauri::command]
fn qianlu_inst_tune_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.inst_tune_stats())
}
#[tauri::command]
fn qianlu_cov_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.cov_demo(&text))
}
#[tauri::command]
fn qianlu_cov_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.cov_stats())
}
#[tauri::command]
fn qianlu_data_loader_load(state: tauri::State<'_, std::sync::Mutex<QianluState>>, text: String, fmt: String) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.model.data_loader_load(&text, &fmt))
}
#[tauri::command]
fn qianlu_data_loader_batch(state: tauri::State<'_, std::sync::Mutex<QianluState>>) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.model.data_loader_batch())
}
#[tauri::command]
fn qianlu_data_loader_stats(state: tauri::State<'_, std::sync::Mutex<QianluState>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.model.data_loader_stats())
}
#[tauri::command]
fn qianlu_data_pipeline_stats(state: tauri::State<'_, std::sync::Mutex<QianluState>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.model.data_pipeline_stats())
}
#[tauri::command]
fn qianlu_tokenized_ds_stats(state: tauri::State<'_, std::sync::Mutex<QianluState>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.model.tokenized_ds_stats())
}
#[tauri::command]
fn qianlu_dist_train_stats(state: tauri::State<'_, std::sync::Mutex<QianluState>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.model.dist_train_stats())
}
#[tauri::command]
fn qianlu_mp_stats(state: tauri::State<'_, std::sync::Mutex<QianluState>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.model.mp_stats())
}
#[tauri::command]
fn qianlu_ckpt_stats(state: tauri::State<'_, std::sync::Mutex<QianluState>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.model.ckpt_stats())
}
#[tauri::command]
fn qianlu_compress_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.compress_demo(&text))
}
#[tauri::command]
fn qianlu_compress_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.compress_stats())
}
#[tauri::command]
fn qianlu_self_play_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.self_play_demo(&text))
}
#[tauri::command]
fn qianlu_self_play_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.self_play_stats())
}
#[tauri::command]
fn qianlu_ipo_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ipo_demo(&text))
}
#[tauri::command]
fn qianlu_ipo_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ipo_stats())
}
#[tauri::command]
fn qianlu_ensemble_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ensemble_demo(&text))
}
#[tauri::command]
fn qianlu_ensemble_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ensemble_stats())
}
#[tauri::command]
fn qianlu_contrastive2_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.contrastive2_demo(&text))
}
#[tauri::command]
fn qianlu_rag_pipeline_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.rag_pipeline_demo(&text))
}
#[tauri::command]
fn qianlu_rag_pipeline_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.rag_pipeline_stats())
}
#[tauri::command]
fn qianlu_lora_adapter_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.lora_adapter_demo(&text))
}
#[tauri::command]
fn qianlu_gnn_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.gnn_demo(&text))
}
#[tauri::command]
fn qianlu_gnn_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.gnn_stats())
}
#[tauri::command]
fn qianlu_diffusion_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.diffusion_demo(&text))
}
#[tauri::command]
fn qianlu_diffusion_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.diffusion_stats())
}
#[tauri::command]
fn qianlu_causal_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.causal_demo(&text))
}
#[tauri::command]
fn qianlu_causal_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.causal_stats())
}
#[tauri::command]
fn qianlu_vae_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.vae_demo(&text))
}
#[tauri::command]
fn qianlu_vae_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.vae_stats())
}
#[tauri::command]
fn qianlu_bnn_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.bnn_demo(&text))
}
#[tauri::command]
fn qianlu_bnn_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.bnn_stats())
}
#[tauri::command]
fn qianlu_node_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.node_demo(&text))
}
#[tauri::command]
fn qianlu_node_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.node_stats())
}
#[tauri::command]
fn qianlu_reservoir_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.reservoir_demo(&text))
}
#[tauri::command]
fn qianlu_reservoir_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.reservoir_stats())
}
#[tauri::command]
fn qianlu_capsule_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.capsule_demo(&text))
}
#[tauri::command]
fn qianlu_capsule_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.capsule_stats())
}
#[tauri::command]
fn qianlu_ebm_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ebm_demo(&text))
}
#[tauri::command]
fn qianlu_ebm_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ebm_stats())
}
#[tauri::command]
fn qianlu_hypernet_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.hypernet_demo(&text))
}
#[tauri::command]
fn qianlu_hypernet_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.hypernet_stats())
}
#[tauri::command]
fn qianlu_flow_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.flow_demo(&text))
}
#[tauri::command]
fn qianlu_flow_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.flow_stats())
}
#[tauri::command]
fn qianlu_snn_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.snn_demo(&text))
}
#[tauri::command]
fn qianlu_snn_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.snn_stats())
}
#[tauri::command]
fn qianlu_world_model_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.world_model_demo(&text))
}
#[tauri::command]
fn qianlu_world_model_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.world_model_stats())
}
#[tauri::command]
fn qianlu_neuro_sym_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.neuro_sym_demo(&text))
}
#[tauri::command]
fn qianlu_neuro_sym_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.neuro_sym_stats())
}
#[tauri::command]
fn qianlu_sparse_ae_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.sparse_ae_demo(&text))
}
#[tauri::command]
fn qianlu_sparse_ae_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.sparse_ae_stats())
}
#[tauri::command]
fn qianlu_mem_net_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.mem_net_demo(&text))
}
#[tauri::command]
fn qianlu_mem_net_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.mem_net_stats())
}
#[tauri::command]
fn qianlu_cbm_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.cbm_demo(&text))
}
#[tauri::command]
fn qianlu_cbm_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.cbm_stats())
}
#[tauri::command]
fn qianlu_hrr_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.hrr_demo(&text))
}
#[tauri::command]
fn qianlu_hrr_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.hrr_stats())
}
#[tauri::command]
fn qianlu_lsm_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.lsm_demo(&text))
}
#[tauri::command]
fn qianlu_lsm_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.lsm_stats())
}
#[tauri::command]
fn qianlu_nps_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.nps_demo(&text))
}
#[tauri::command]
fn qianlu_nps_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.nps_stats())
}
#[tauri::command]
fn qianlu_neuro_core_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.neuro_core_demo(&text))
}
#[tauri::command]
fn qianlu_neuro_core_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.neuro_core_stats())
}
#[tauri::command]
fn qianlu_ckpt_merge_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ckpt_merge_demo(&text))
}
#[tauri::command]
fn qianlu_ckpt_merge_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ckpt_merge_stats())
}
#[tauri::command]
fn qianlu_ttc_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ttc_demo(&text))
}
#[tauri::command]
fn qianlu_ttc_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ttc_stats())
}
#[tauri::command]
fn qianlu_kd_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.kd_demo(&text))
}
#[tauri::command]
fn qianlu_kd_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.kd_stats())
}
#[tauri::command]
fn qianlu_draft_verify_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.draft_verify_demo(&text))
}
#[tauri::command]
fn qianlu_draft_verify_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.draft_verify_stats())
}
#[tauri::command]
fn qianlu_act_patch_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.act_patch_demo(&text))
}
#[tauri::command]
fn qianlu_act_patch_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.act_patch_stats())
}
#[tauri::command]
fn qianlu_ser_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ser_demo(&text))
}
#[tauri::command]
fn qianlu_ser_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ser_stats())
}
#[tauri::command]
fn qianlu_mech_interp_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.mech_interp_demo(&text))
}
#[tauri::command]
fn qianlu_mech_interp_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.mech_interp_stats())
}
#[tauri::command]
fn qianlu_sym_reg_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.sym_reg_demo(&text))
}
#[tauri::command]
fn qianlu_sym_reg_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.sym_reg_stats())
}
#[tauri::command]
fn qianlu_reason_trace_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.reason_trace_demo(&text))
}
#[tauri::command]
fn qianlu_reason_trace_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.reason_trace_stats())
}
#[tauri::command]
fn qianlu_dpo_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.dpo_demo(&text))
}
#[tauri::command]
fn qianlu_dpo_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.dpo_stats())
}
#[tauri::command]
fn qianlu_uq_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.uq_demo(&text))
}
#[tauri::command]
fn qianlu_uq_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.uq_stats())
}
#[tauri::command]
fn qianlu_data_val_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.data_val_demo(&text))
}
#[tauri::command]
fn qianlu_data_val_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.data_val_stats())
}
#[tauri::command]
fn qianlu_moo_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.moo_demo(&text))
}
#[tauri::command]
fn qianlu_moo_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.moo_stats())
}
#[tauri::command]
fn qianlu_task_vec_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.task_vec_demo(&text))
}
#[tauri::command]
fn qianlu_task_vec_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.task_vec_stats())
}
#[tauri::command]
fn qianlu_prompt_opt_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.prompt_opt_demo(&text))
}
#[tauri::command]
fn qianlu_prompt_opt_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.prompt_opt_stats())
}
#[tauri::command]
fn qianlu_online_learn_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.online_learn_demo(&text))
}
#[tauri::command]
fn qianlu_online_learn_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.online_learn_stats())
}
#[tauri::command]
fn qianlu_bandit_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.bandit_demo(&text))
}
#[tauri::command]
fn qianlu_bandit_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.bandit_stats())
}
#[tauri::command]
fn qianlu_reward_shape_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.reward_shape_demo(&text))
}
#[tauri::command]
fn qianlu_reward_shape_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.reward_shape_stats())
}
#[tauri::command]
fn qianlu_active_inf_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.active_inf_demo(&text))
}
#[tauri::command]
fn qianlu_active_inf_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.active_inf_stats())
}
#[tauri::command]
fn qianlu_fed_personal_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.fed_personal_demo(&text))
}
#[tauri::command]
fn qianlu_fed_personal_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.fed_personal_stats())
}
#[tauri::command]
fn qianlu_model_comp_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.model_comp_demo(&text))
}
#[tauri::command]
fn qianlu_model_comp_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.model_comp_stats())
}
#[tauri::command]
fn qianlu_bayes_opt_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.bayes_opt_demo(&text))
}
#[tauri::command]
fn qianlu_bayes_opt_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.bayes_opt_stats())
}
#[tauri::command]
fn qianlu_domain_adapt_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.domain_adapt_demo(&text))
}
#[tauri::command]
fn qianlu_domain_adapt_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.domain_adapt_stats())
}
#[tauri::command]
fn qianlu_ssl_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ssl_demo(&text))
}
#[tauri::command]
fn qianlu_ssl_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ssl_stats())
}
#[tauri::command]
fn qianlu_transfer_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.transfer_demo(&text))
}
#[tauri::command]
fn qianlu_transfer_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.transfer_stats())
}
#[tauri::command]
fn qianlu_few_shot_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.few_shot_demo(&text))
}
#[tauri::command]
fn qianlu_few_shot_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.few_shot_stats())
}
#[tauri::command]
fn qianlu_meta_rl_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.meta_rl_demo(&text))
}
#[tauri::command]
fn qianlu_meta_rl_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.meta_rl_stats())
}
#[tauri::command]
fn qianlu_zero_shot_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.zero_shot_demo(&text))
}
#[tauri::command]
fn qianlu_zero_shot_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.zero_shot_stats())
}
#[tauri::command]
fn qianlu_task_adapt_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.task_adapt_demo(&text))
}
#[tauri::command]
fn qianlu_task_adapt_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.task_adapt_stats())
}
#[tauri::command]
fn qianlu_rep_mixup_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.rep_mixup_demo(&text))
}
#[tauri::command]
fn qianlu_rep_mixup_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.rep_mixup_stats())
}
#[tauri::command]
fn qianlu_weight_share_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.weight_share_demo(&text))
}
#[tauri::command]
fn qianlu_weight_share_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.weight_share_stats())
}
#[tauri::command]
fn qianlu_disentangle_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.disentangle_demo(&text))
}
#[tauri::command]
fn qianlu_disentangle_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.disentangle_stats())
}
#[tauri::command]
fn qianlu_grad_surgery_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.grad_surgery_demo(&text))
}
#[tauri::command]
fn qianlu_grad_surgery_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.grad_surgery_stats())
}
#[tauri::command]
fn qianlu_evo_strat_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.evo_strat_demo(&text))
}
#[tauri::command]
fn qianlu_evo_strat_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.evo_strat_stats())
}
#[tauri::command]
fn qianlu_hyper_opt_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.hyper_opt_demo(&text))
}
#[tauri::command]
fn qianlu_hyper_opt_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.hyper_opt_stats())
}
#[tauri::command]
fn qianlu_multi_agent_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.multi_agent_demo(&text))
}
#[tauri::command]
fn qianlu_multi_agent_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.multi_agent_stats())
}
#[tauri::command]
fn qianlu_imitation_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.imitation_demo(&text))
}
#[tauri::command]
fn qianlu_imitation_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.imitation_stats())
}
#[tauri::command]
fn qianlu_inverse_rl_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.inverse_rl_demo(&text))
}
#[tauri::command]
fn qianlu_inverse_rl_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.inverse_rl_stats())
}
#[tauri::command]
fn qianlu_ntk_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ntk_demo(&text))
}
#[tauri::command]
fn qianlu_ntk_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ntk_stats())
}
#[tauri::command]
fn qianlu_ot_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ot_demo(&text))
}
#[tauri::command]
fn qianlu_ot_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ot_stats())
}
#[tauri::command]
fn qianlu_scaling_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.scaling_demo(&text))
}
#[tauri::command]
fn qianlu_scaling_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.scaling_stats())
}
#[tauri::command]
fn qianlu_model_edit_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.model_edit_demo(&text))
}
#[tauri::command]
fn qianlu_model_edit_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.model_edit_stats())
}
#[tauri::command]
fn qianlu_grok_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.grok_demo(&text))
}
#[tauri::command]
fn qianlu_grok_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.grok_stats())
}
#[tauri::command]
fn qianlu_double_desc_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.double_desc_demo(&text))
}
#[tauri::command]
fn qianlu_double_desc_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.double_desc_stats())
}
#[tauri::command]
fn qianlu_ssm_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ssm_demo(&text))
}
#[tauri::command]
fn qianlu_ssm_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ssm_stats())
}
#[tauri::command]
fn qianlu_tool_use_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.tool_use_demo(&text))
}
#[tauri::command]
fn qianlu_tool_use_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.tool_use_stats())
}
#[tauri::command]
fn qianlu_self_refine_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.self_refine_demo(&text))
}
#[tauri::command]
fn qianlu_self_refine_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.self_refine_stats())
}
#[tauri::command]
fn qianlu_reason_chain_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.reason_chain_demo(&text))
}
#[tauri::command]
fn qianlu_reason_chain_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.reason_chain_stats())
}
#[tauri::command]
fn qianlu_red_team_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.red_team_demo(&text))
}
#[tauri::command]
fn qianlu_red_team_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.red_team_stats())
}
#[tauri::command]
fn qianlu_safety_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.safety_demo(&text))
}
#[tauri::command]
fn qianlu_safety_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.safety_stats())
}
#[tauri::command]
fn qianlu_jailbreak_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.jailbreak_demo(&text))
}
#[tauri::command]
fn qianlu_jailbreak_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.jailbreak_stats())
}
#[tauri::command]
fn qianlu_ring_attn_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ring_attn_demo(&text))
}
#[tauri::command]
fn qianlu_ring_attn_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ring_attn_stats())
}
#[tauri::command]
fn qianlu_arena_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.arena_demo(&text))
}
#[tauri::command]
fn qianlu_arena_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.arena_stats())
}
#[tauri::command]
fn qianlu_synth_data_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.synth_data_demo(&text))
}
#[tauri::command]
fn qianlu_synth_data_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.synth_data_stats())
}
#[tauri::command]
fn qianlu_orpo_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.orpo_demo(&text))
}
#[tauri::command]
fn qianlu_orpo_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.orpo_stats())
}
#[tauri::command]
fn qianlu_simpo_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.simpo_demo(&text))
}
#[tauri::command]
fn qianlu_simpo_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.simpo_stats())
}
#[tauri::command]
fn qianlu_infini_attn_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.infini_attn_demo(&text))
}
#[tauri::command]
fn qianlu_infini_attn_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.infini_attn_stats())
}
#[tauri::command]
fn qianlu_medusa_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.medusa_demo(&text))
}
#[tauri::command]
fn qianlu_medusa_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.medusa_stats())
}
#[tauri::command]
fn qianlu_eagle_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.eagle_demo(&text))
}
#[tauri::command]
fn qianlu_eagle_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.eagle_stats())
}
#[tauri::command]
fn qianlu_chunked_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.chunked_demo(&text))
}
#[tauri::command]
fn qianlu_chunked_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.chunked_stats())
}
#[tauri::command]
fn qianlu_dora_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.dora_demo(&text))
}
#[tauri::command]
fn qianlu_dora_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.dora_stats())
}
#[tauri::command]
fn qianlu_top_k_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.top_k_demo(&text))
}
#[tauri::command]
fn qianlu_top_k_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.top_k_stats())
}
#[tauri::command]
fn qianlu_hyena_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.hyena_demo(&text))
}
#[tauri::command]
fn qianlu_hyena_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.hyena_stats())
}
#[tauri::command]
fn qianlu_rwkv_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.rwkv_demo(&text))
}
#[tauri::command]
fn qianlu_rwkv_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.rwkv_stats())
}
#[tauri::command]
fn qianlu_prompt_comp_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.prompt_comp_demo(&text))
}
#[tauri::command]
fn qianlu_prompt_comp_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.prompt_comp_stats())
}
#[tauri::command]
fn qianlu_flash_mla_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.flash_mla_demo(&text))
}
#[tauri::command]
fn qianlu_flash_mla_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.flash_mla_stats())
}
#[tauri::command]
fn qianlu_agent_plan_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.agent_plan_demo(&text))
}
#[tauri::command]
fn qianlu_agent_plan_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.agent_plan_stats())
}
#[tauri::command]
fn qianlu_spec_reject_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.spec_reject_demo(&text))
}
#[tauri::command]
fn qianlu_spec_reject_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.spec_reject_stats())
}
#[tauri::command]
fn qianlu_paged_kv_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.paged_kv_demo(&text))
}
#[tauri::command]
fn qianlu_paged_kv_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.paged_kv_stats())
}
#[tauri::command]
fn qianlu_agent_mem_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.agent_mem_demo(&text))
}
#[tauri::command]
fn qianlu_agent_mem_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.agent_mem_stats())
}
#[tauri::command]
fn qianlu_dyn_batch_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.cont_batching_demo(&text))
}
#[tauri::command]
fn qianlu_dyn_batch_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.cont_batching_stats())
}
#[tauri::command]
fn qianlu_vis_enc_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.vis_enc_demo(&text))
}
#[tauri::command]
fn qianlu_vis_enc_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.vis_enc_stats())
}
#[tauri::command]
fn qianlu_txt2img_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.txt2img_demo(&text))
}
#[tauri::command]
fn qianlu_txt2img_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.txt2img_stats())
}
#[tauri::command]
fn qianlu_prom_cache_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.prom_cache_demo(&text))
}
#[tauri::command]
fn qianlu_prom_cache_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.prom_cache_stats())
}
#[tauri::command]
fn qianlu_img_cap_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.img_cap_demo(&text))
}
#[tauri::command]
fn qianlu_img_cap_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.img_cap_stats())
}
#[tauri::command]
fn qianlu_obj_det_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.obj_det_demo(&text))
}
#[tauri::command]
fn qianlu_obj_det_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.obj_det_stats())
}
#[tauri::command]
fn qianlu_super_res_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.super_res_demo(&text))
}
#[tauri::command]
fn qianlu_super_res_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.super_res_stats())
}
#[tauri::command]
fn qianlu_style_trans_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.style_trans_demo(&text))
}
#[tauri::command]
fn qianlu_style_trans_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.style_trans_stats())
}
#[tauri::command]
fn qianlu_img_inpaint_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.img_inpaint_demo(&text))
}
#[tauri::command]
fn qianlu_img_inpaint_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.img_inpaint_stats())
}
#[tauri::command]
fn qianlu_audio_enc_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.audio_enc_demo(&text))
}
#[tauri::command]
fn qianlu_audio_enc_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.audio_enc_stats())
}
#[tauri::command]
fn qianlu_img_seg_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.img_seg_demo(&text))
}
#[tauri::command]
fn qianlu_img_seg_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.img_seg_stats())
}
#[tauri::command]
fn qianlu_depth_est_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.depth_est_demo(&text))
}
#[tauri::command]
fn qianlu_depth_est_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.depth_est_stats())
}
#[tauri::command]
fn qianlu_opt_flow_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.opt_flow_demo(&text))
}
#[tauri::command]
fn qianlu_opt_flow_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.opt_flow_stats())
}
#[tauri::command]
fn qianlu_pose_est_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.pose_est_demo(&text))
}
#[tauri::command]
fn qianlu_pose_est_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.pose_est_stats())
}
#[tauri::command]
fn qianlu_speech_recog_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.speech_recog_demo(&text))
}
#[tauri::command]
fn qianlu_speech_recog_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.speech_recog_stats())
}
#[tauri::command]
fn qianlu_music_gen_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.music_gen_demo(&text))
}
#[tauri::command]
fn qianlu_music_gen_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.music_gen_stats())
}
#[tauri::command]
fn qianlu_code_parser_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.code_parser_demo(&text))
}
#[tauri::command]
fn qianlu_code_parser_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.code_parser_stats())
}
#[tauri::command]
fn qianlu_code_gen_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.code_gen_demo(&text))
}
#[tauri::command]
fn qianlu_code_gen_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.code_gen_stats())
}
#[tauri::command]
fn qianlu_code_exec_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.code_exec_demo(&text))
}
#[tauri::command]
fn qianlu_code_exec_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.code_exec_stats())
}
#[tauri::command]
fn qianlu_code_debug_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.code_debug_demo(&text))
}
#[tauri::command]
fn qianlu_code_debug_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.code_debug_stats())
}
#[tauri::command]
fn qianlu_prog_repair_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.prog_repair_demo(&text))
}
#[tauri::command]
fn qianlu_prog_repair_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.prog_repair_stats())
}
#[tauri::command]
fn qianlu_code_review_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.code_review_demo(&text))
}
#[tauri::command]
fn qianlu_code_review_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.code_review_stats())
}
#[tauri::command]
fn qianlu_syntax_hl_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.syntax_hl_demo(&text))
}
#[tauri::command]
fn qianlu_syntax_hl_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.syntax_hl_stats())
}
#[tauri::command]
fn qianlu_code_complete_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.code_complete_demo(&text))
}
#[tauri::command]
fn qianlu_code_complete_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.code_complete_stats())
}
#[tauri::command]
fn qianlu_code_refactor_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.code_refactor_demo(&text))
}
#[tauri::command]
fn qianlu_code_refactor_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.code_refactor_stats())
}
#[tauri::command]
fn qianlu_code_pipeline_analyze(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.code_pipeline_analyze(&text))
}
#[tauri::command]
fn qianlu_code_pipeline_full(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.code_pipeline_full(&text))
}
#[tauri::command]
fn qianlu_code_pipeline_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.code_pipeline_stats())
}
#[tauri::command]
fn qianlu_code_analyze_panel(state: tauri::State<'_, Mutex<QianluState>>, code: String, lang: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    let report = s.model.code_pipeline.analyze_for_language(&code, &lang);
    let formatted = s.model.code_pipeline.format_report(&report);
    Ok(formatted)
}
/// qianlu quantize model weights
#[tauri::command]
fn qianlu_quantize(
    state: tauri::State<'_, Mutex<QianluState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.quantize_model())
}

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
    let ports = snarjs::board::scan_serial_ports();
    ports.iter().map(|p| serde_json::json!({
        "name": p.name,
        "description": p.description,
    })).collect()
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

    // Create snar.config.json
    let mut cfg = snarjs::config::ProjectConfig::default();
    cfg.name = template_id.clone();
    fs::write(base.join("snar.config.json"), cfg.to_json())
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

/// List .qianlu model files in a directory
#[tauri::command]
fn tfcard_list(model_dir: String) -> Vec<ModelFileInfo> {
    let dir = Path::new(&model_dir);
    if !dir.exists() { return Vec::new(); }
    let mut files = Vec::new();
    if let Ok(entries) = fs::read_dir(dir) {
        for entry in entries.flatten() {
            let name = entry.file_name().to_string_lossy().to_string();
            if name.ends_with(".qianlu") {
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
    snarjs_flash: u32,
    c_flash: u32,
    snarjs_ram: u32,
    c_ram: u32,
    flash_overhead_pct: f64,
    ram_overhead_pct: f64,
    grade: String,
}

/// Run benchmark on current source
#[tauri::command]
fn bench_run(source: String, target: String) -> Result<BenchResult, String> {
    let tokens = snarjs::lexer::tokenize(&source)
        .map_err(|e| format!("Lexer error: {}", e))?;
    let ast = snarjs::parser::parse(&tokens)
        .map_err(|e| format!("Parser error: {}", e))?;
    let optimized = snarjs::optimizer::optimize(&ast);
    let instructions = snarjs::compiler::compile(&optimized)
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

/// Compare SNARjs output vs estimated equivalent C code
#[tauri::command]
fn bench_compare(source: String, target: String) -> Result<BenchComparison, String> {
    let tokens = snarjs::lexer::tokenize(&source)
        .map_err(|e| format!("Lexer error: {}", e))?;
    let ast = snarjs::parser::parse(&tokens)
        .map_err(|e| format!("Parser error: {}", e))?;
    let optimized = snarjs::optimizer::optimize(&ast);
    let instructions = snarjs::compiler::compile(&optimized)
        .map_err(|e| format!("Compile error: {}", e))?;

    let snarjs_flash = (instructions.len() as u32) * 4;
    let snarjs_ram = (instructions.len() as u32) * 4 + 256;

    // Estimate C equivalent: typically 60-80% of interpreter overhead
    let c_flash = (snarjs_flash as f64 * 0.7) as u32;
    let c_ram = (snarjs_ram as f64 * 0.6) as u32;

    let flash_overhead = if c_flash > 0 { ((snarjs_flash - c_flash) as f64 / c_flash as f64 * 100.0) } else { 0.0 };
    let ram_overhead = if c_ram > 0 { ((snarjs_ram - c_ram) as f64 / c_ram as f64 * 100.0) } else { 0.0 };

    let grade = if flash_overhead < 20.0 { "A" } else if flash_overhead < 40.0 { "B" } else if flash_overhead < 80.0 { "C" } else { "D" };

    Ok(BenchComparison {
        snarjs_flash,
        c_flash,
        snarjs_ram,
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
    let ws = snarjs::workspace::workspace_init(path, &name, monorepo)?;
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
    let ws = snarjs::workspace::WorkspaceConfig::load(path)?;
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
    let ws = snarjs::workspace::WorkspaceConfig::load(path)?;
    let statuses = ws.status(path);
    Ok(statuses.iter().map(|s| serde_json::json!({
        "name": s.name, "path": s.path, "target": s.target,
        "has_config": s.has_config, "has_entry": s.has_entry, "last_build": s.last_build,
    })).collect())
}

#[tauri::command]
fn workspace_build_all(dir: String) -> Result<String, String> {
    let path = std::path::Path::new(&dir);
    let mut ws = snarjs::workspace::WorkspaceConfig::load(path)?;
    let order = ws.resolve_build_order()?;
    Ok(format!("Build order: {}", order.join(" -> ")))
}

#[tauri::command]
fn workspace_add_package(dir: String, name: String, path: String, target: Option<String>) -> Result<(), String> {
    let ws_dir = std::path::Path::new(&dir);
    let mut ws = snarjs::workspace::WorkspaceConfig::load(ws_dir)?;
    ws.add_package(snarjs::workspace::PackageRef {
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
    let status = snarjs::git_ops::git_status(path)?;
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
    let entries = snarjs::git_ops::git_log(path, n)?;
    Ok(entries.iter().map(|e| serde_json::json!({
        "hash": e.hash, "author": e.author, "date": e.date, "message": e.message
    })).collect())
}

#[tauri::command]
fn git_diff(dir: String, file: Option<String>) -> Result<Vec<serde_json::Value>, String> {
    let path = std::path::Path::new(&dir);
    let diffs = snarjs::git_ops::git_diff(path, file.as_deref())?;
    Ok(diffs.iter().map(|d| serde_json::json!({
        "path": d.path,
        "hunks": d.hunks.iter().map(|h| serde_json::json!({
            "old_start": h.old_start, "old_count": h.old_count,
            "new_start": h.new_start, "new_count": h.new_count,
            "lines": h.lines.iter().map(|l| serde_json::json!({
                "kind": match l.kind {
                    snarjs::git_ops::DiffLineKind::Added => "Added",
                    snarjs::git_ops::DiffLineKind::Removed => "Removed",
                    snarjs::git_ops::DiffLineKind::Context => "Context",
                    snarjs::git_ops::DiffLineKind::Header => "Header",
                },
                "content": l.content, "old_lineno": l.old_lineno, "new_lineno": l.new_lineno
            })).collect::<Vec<_>>()
        })).collect::<Vec<_>>()
    })).collect())
}

#[tauri::command]
fn git_staged_diff(dir: String) -> Result<Vec<serde_json::Value>, String> {
    let path = std::path::Path::new(&dir);
    let diffs = snarjs::git_ops::git_staged_diff(path)?;
    Ok(diffs.iter().map(|d| serde_json::json!({
        "path": d.path,
        "hunks": d.hunks.iter().map(|h| serde_json::json!({
            "old_start": h.old_start, "old_count": h.old_count,
            "new_start": h.new_start, "new_count": h.new_count,
            "lines": h.lines.iter().map(|l| serde_json::json!({
                "kind": match l.kind {
                    snarjs::git_ops::DiffLineKind::Added => "Added",
                    snarjs::git_ops::DiffLineKind::Removed => "Removed",
                    snarjs::git_ops::DiffLineKind::Context => "Context",
                    snarjs::git_ops::DiffLineKind::Header => "Header",
                },
                "content": l.content, "old_lineno": l.old_lineno, "new_lineno": l.new_lineno
            })).collect::<Vec<_>>()
        })).collect::<Vec<_>>()
    })).collect())
}

#[tauri::command]
fn git_add(dir: String, files: Vec<String>) -> Result<(), String> {
    snarjs::git_ops::git_add(std::path::Path::new(&dir), &files)
}

#[tauri::command]
fn git_add_all(dir: String) -> Result<(), String> {
    snarjs::git_ops::git_add_all(std::path::Path::new(&dir))
}

#[tauri::command]
fn git_unstage(dir: String, files: Vec<String>) -> Result<(), String> {
    snarjs::git_ops::git_unstage(std::path::Path::new(&dir), &files)
}

#[tauri::command]
fn git_commit(dir: String, message: String) -> Result<String, String> {
    snarjs::git_ops::git_commit(std::path::Path::new(&dir), &message)
}

#[tauri::command]
fn git_push(dir: String, remote: Option<String>, branch: Option<String>) -> Result<String, String> {
    snarjs::git_ops::git_push(std::path::Path::new(&dir), remote.as_deref(), branch.as_deref())
}

#[tauri::command]
fn git_pull(dir: String) -> Result<String, String> {
    snarjs::git_ops::git_pull(std::path::Path::new(&dir))
}

#[tauri::command]
fn git_branches(dir: String) -> Result<Vec<serde_json::Value>, String> {
    let branches = snarjs::git_ops::git_branches(std::path::Path::new(&dir))?;
    Ok(branches.iter().map(|b| serde_json::json!({
        "name": b.name, "is_current": b.is_current, "is_remote": b.is_remote
    })).collect())
}

#[tauri::command]
fn git_checkout(dir: String, branch: String) -> Result<String, String> {
    snarjs::git_ops::git_checkout(std::path::Path::new(&dir), &branch)
}

#[tauri::command]
fn git_create_branch(dir: String, name: String) -> Result<String, String> {
    snarjs::git_ops::git_create_branch(std::path::Path::new(&dir), &name)
}

#[tauri::command]
fn git_init(dir: String) -> Result<String, String> {
    snarjs::git_ops::git_init(std::path::Path::new(&dir))
}

#[tauri::command]
fn git_install_hooks(dir: String) -> Result<(), String> {
    snarjs::git_ops::git_install_hooks(std::path::Path::new(&dir))
}

// =============================================================================
// Phase H: Lint Commands
// =============================================================================

#[tauri::command]
fn lint_project(dir: String) -> Result<serde_json::Value, String> {
    let path = std::path::Path::new(&dir);
    let config = snarjs::lint::LintConfig::load(path);
    let result = snarjs::lint::lint_project(path, &config);
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
    let config = snarjs::lint::LintConfig::default_config();
    let result = snarjs::lint::lint_file(std::path::Path::new(&file_path), &source, &config);
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
    let config = snarjs::lint::LintConfig::load(path);
    let js_files = find_js_files_for_lint(path);
    let mut fixed_count = 0;
    for file in &js_files {
        if let Ok(source) = std::fs::read_to_string(file) {
            if let Ok(fixed) = snarjs::lint::lint_fix(file, &source, &config) {
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
    let rules = snarjs::lint::builtin_rules();
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
    let config = snarjs::ci::CiConfig::default();
    let result = snarjs::ci::ci_build_all(path, &config);
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
    let info = snarjs::ci::ci_release(path, &version)?;
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
        snarjs::ci::ci_generate_github_action(path)?;
        return Ok("GitHub Actions template generated".to_string());
    }
    if gitlab {
        snarjs::ci::ci_generate_gitlab_ci(path)?;
        return Ok("GitLab CI template generated".to_string());
    }
    Err("Specify --github or --gitlab".to_string())
}

#[tauri::command]
fn ci_archive(dir: String, version: String) -> Result<String, String> {
    snarjs::ci::ci_archive(std::path::Path::new(&dir), &version)
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .manage(Mutex::new(QianluState::new()))
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
            qianlu_inference,
            qianlu_train,
            qianlu_generate,
            qianlu_replay,
            qianlu_save_weights,
            qianlu_load_weights,
            qianlu_status,
            qianlu_generate_decoder,
            qianlu_set_backprop,
            qianlu_quantize,
            qianlu_generate_cached,
            qianlu_train_cached,
            qianlu_export_arch,
            qianlu_export_json,
            qianlu_quant_report,
            qianlu_f16_report,
            qianlu_rlhf_train,
            qianlu_rlhf_stats,
            qianlu_distill_step,
            qianlu_distill_stats,
            qianlu_lora_train,
            qianlu_lora_merge,
            qianlu_lora_adapter_stats,
            qianlu_gnn_demo,
            qianlu_gnn_stats,
            qianlu_diffusion_demo,
            qianlu_diffusion_stats,
            qianlu_causal_demo,
            qianlu_causal_stats,
            qianlu_vae_demo,
            qianlu_vae_stats,
            qianlu_bnn_demo,
            qianlu_bnn_stats,
            qianlu_node_demo,
            qianlu_node_stats,
            qianlu_reservoir_demo,
            qianlu_reservoir_stats,
            qianlu_capsule_demo,
            qianlu_capsule_stats,
            qianlu_ebm_demo,
            qianlu_ebm_stats,
            qianlu_hypernet_demo,
            qianlu_hypernet_stats,
            qianlu_flow_demo,
            qianlu_flow_stats,
            qianlu_snn_demo,
            qianlu_snn_stats,
            qianlu_world_model_demo,
            qianlu_world_model_stats,
            qianlu_neuro_sym_demo,
            qianlu_neuro_sym_stats,
            qianlu_sparse_ae_demo,
            qianlu_sparse_ae_stats,
            qianlu_mem_net_demo,
            qianlu_mem_net_stats,
            qianlu_cbm_demo,
            qianlu_cbm_stats,
            qianlu_hrr_demo,
            qianlu_hrr_stats,
            qianlu_lsm_demo,
            qianlu_lsm_stats,
            qianlu_nps_demo,
            qianlu_nps_stats,
            qianlu_neuro_core_demo,
            qianlu_neuro_core_stats,
            qianlu_ckpt_merge_demo,
            qianlu_ckpt_merge_stats,
            qianlu_ttc_demo,
            qianlu_ttc_stats,
            qianlu_kd_demo,
            qianlu_kd_stats,
            qianlu_draft_verify_demo,
            qianlu_draft_verify_stats,
            qianlu_act_patch_demo,
            qianlu_act_patch_stats,
            qianlu_ser_demo,
            qianlu_ser_stats,
            qianlu_mech_interp_demo,
            qianlu_mech_interp_stats,
            qianlu_sym_reg_demo,
            qianlu_sym_reg_stats,
            qianlu_reason_trace_demo,
            qianlu_reason_trace_stats,
            qianlu_dpo_demo,
            qianlu_dpo_stats,
            qianlu_uq_demo,
            qianlu_uq_stats,
            qianlu_data_val_demo,
            qianlu_data_val_stats,
            qianlu_moo_demo,
            qianlu_moo_stats,
            qianlu_task_vec_demo,
            qianlu_task_vec_stats,
            qianlu_prompt_opt_demo,
            qianlu_prompt_opt_stats,
            qianlu_online_learn_demo,
            qianlu_online_learn_stats,
            qianlu_bandit_demo,
            qianlu_bandit_stats,
            qianlu_reward_shape_demo,
            qianlu_reward_shape_stats,
            qianlu_active_inf_demo,
            qianlu_active_inf_stats,
            qianlu_fed_personal_demo,
            qianlu_fed_personal_stats,
            qianlu_model_comp_demo,
            qianlu_model_comp_stats,
            qianlu_bayes_opt_demo,
            qianlu_bayes_opt_stats,
            qianlu_domain_adapt_demo,
            qianlu_domain_adapt_stats,
            qianlu_ssl_demo,
            qianlu_ssl_stats,
            qianlu_transfer_demo,
            qianlu_transfer_stats,
            qianlu_few_shot_demo,
            qianlu_few_shot_stats,
            qianlu_meta_rl_demo,
            qianlu_meta_rl_stats,
            qianlu_zero_shot_demo,
            qianlu_zero_shot_stats,
            qianlu_task_adapt_demo,
            qianlu_task_adapt_stats,
            qianlu_rep_mixup_demo,
            qianlu_rep_mixup_stats,
            qianlu_weight_share_demo,
            qianlu_weight_share_stats,
            qianlu_disentangle_demo,
            qianlu_disentangle_stats,
            qianlu_grad_surgery_demo,
            qianlu_grad_surgery_stats,
            qianlu_evo_strat_demo,
            qianlu_evo_strat_stats,
            qianlu_hyper_opt_demo,
            qianlu_hyper_opt_stats,
            qianlu_multi_agent_demo,
            qianlu_multi_agent_stats,
            qianlu_imitation_demo,
            qianlu_imitation_stats,
            qianlu_inverse_rl_demo,
            qianlu_inverse_rl_stats,
            qianlu_ntk_demo,
            qianlu_ntk_stats,
            qianlu_ot_demo,
            qianlu_ot_stats,
            qianlu_scaling_demo,
            qianlu_scaling_stats,
            qianlu_model_edit_demo,
            qianlu_model_edit_stats,
            qianlu_grok_demo,
            qianlu_grok_stats,
            qianlu_double_desc_demo,
            qianlu_double_desc_stats,
            qianlu_ssm_demo,
            qianlu_ssm_stats,
            qianlu_tool_use_demo,
            qianlu_tool_use_stats,
            qianlu_self_refine_demo,
            qianlu_self_refine_stats,
            qianlu_reason_chain_demo,
            qianlu_reason_chain_stats,
            qianlu_red_team_demo,
            qianlu_red_team_stats,
            qianlu_safety_demo,
            qianlu_safety_stats,
            qianlu_jailbreak_demo,
            qianlu_jailbreak_stats,
            qianlu_ring_attn_demo,
            qianlu_ring_attn_stats,
            qianlu_arena_demo,
            qianlu_arena_stats,
            qianlu_synth_data_demo,
            qianlu_synth_data_stats,
            qianlu_orpo_demo,
            qianlu_orpo_stats,
            qianlu_simpo_demo,
            qianlu_simpo_stats,
            qianlu_infini_attn_demo,
            qianlu_infini_attn_stats,
            qianlu_medusa_demo,
            qianlu_medusa_stats,
            qianlu_eagle_demo,
            qianlu_eagle_stats,
            qianlu_chunked_demo,
            qianlu_chunked_stats,
            qianlu_dora_demo,
            qianlu_dora_stats,
                                    qianlu_top_k_demo,
            qianlu_top_k_stats,
            qianlu_hyena_demo,
            qianlu_hyena_stats,
            qianlu_rwkv_demo,
            qianlu_rwkv_stats,
            qianlu_prompt_comp_demo,
            qianlu_prompt_comp_stats,
            qianlu_flash_mla_demo,
            qianlu_flash_mla_stats,
            qianlu_agent_plan_demo,
            qianlu_agent_plan_stats,
            qianlu_spec_reject_demo,
            qianlu_spec_reject_stats,
            qianlu_paged_kv_demo,
            qianlu_paged_kv_stats,
            qianlu_agent_mem_demo,
            qianlu_agent_mem_stats,
            qianlu_dyn_batch_demo,
            qianlu_dyn_batch_stats,
            qianlu_vis_enc_demo,
            qianlu_vis_enc_stats,
            qianlu_txt2img_demo,
            qianlu_txt2img_stats,
            qianlu_prom_cache_demo,
            qianlu_prom_cache_stats,
            qianlu_img_cap_demo,
            qianlu_img_cap_stats,
            qianlu_obj_det_demo,
            qianlu_obj_det_stats,
            qianlu_super_res_demo,
            qianlu_super_res_stats,
            qianlu_style_trans_demo,
            qianlu_style_trans_stats,
            qianlu_img_inpaint_demo,
            qianlu_img_inpaint_stats,
            qianlu_audio_enc_demo,
            qianlu_audio_enc_stats,
            qianlu_img_seg_demo,
            qianlu_img_seg_stats,
            qianlu_depth_est_demo,
            qianlu_depth_est_stats,
            qianlu_opt_flow_demo,
            qianlu_opt_flow_stats,
            qianlu_pose_est_demo,
            qianlu_pose_est_stats,
            qianlu_speech_recog_demo,
            qianlu_speech_recog_stats,
            qianlu_music_gen_demo,
            qianlu_music_gen_stats,
            qianlu_code_parser_demo,
            qianlu_code_parser_stats,
            qianlu_code_gen_demo,
            qianlu_code_gen_stats,
            qianlu_code_exec_demo,
            qianlu_code_exec_stats,
            qianlu_code_debug_demo,
            qianlu_code_debug_stats,
            qianlu_prog_repair_demo,
            qianlu_prog_repair_stats,
            qianlu_code_review_demo,
            qianlu_code_review_stats,
            qianlu_syntax_hl_demo,
            qianlu_syntax_hl_stats,
            qianlu_code_complete_demo,
            qianlu_code_complete_stats,
            qianlu_code_refactor_demo,
            qianlu_code_refactor_stats,
            qianlu_code_pipeline_analyze,
            qianlu_code_pipeline_full,
            qianlu_code_pipeline_stats,
            qianlu_code_analyze_panel,
            qianlu_moe_forward,
            qianlu_moe_stats,
            qianlu_onnx_export,
            qianlu_sparse_attention,
            qianlu_flash_attention,
            qianlu_flash_report,
            qianlu_speculative_generate,
            qianlu_spec_stats,
            qianlu_rope_attention,
            qianlu_rope_report,
            qianlu_model_merge,
            qianlu_multimodal,
            qianlu_multimodal_stats,
            qianlu_gqa_forward,
            qianlu_gqa_report,
            qianlu_act_cache_forward,
            qianlu_act_cache_stats,
            qianlu_icl_query,
            qianlu_icl_stats,
            qianlu_mod_forward,
            qianlu_mod_report,
            qianlu_rag_index,
            qianlu_rag_query,
            qianlu_contrastive_train,
            qianlu_contrastive2_stats,
            qianlu_rmsnorm_forward,
            qianlu_rmsnorm_report,
            qianlu_swiglu_forward,
            qianlu_swiglu_report,
            qianlu_beam_search_decode,
            qianlu_beam_search_stats,
            qianlu_sample_text,
            qianlu_sampling_config,
            qianlu_lr_step,
            qianlu_lr_curve,
            qianlu_alibi_forward,
            qianlu_alibi_report,
            qianlu_grad_clip_demo,
            qianlu_grad_clip_stats,
            qianlu_adam_simulate,
            qianlu_adam_stats,
            qianlu_token_merge,
            qianlu_token_merge_stats,
            qianlu_weight_tying_demo,
            qianlu_weight_tying_stats,
            qianlu_cross_attn_demo,
            qianlu_cross_attn_stats,
            qianlu_perplexity_eval,
            qianlu_perplexity_report,
            qianlu_rep_penalty_demo,
            qianlu_rep_penalty_stats,
            qianlu_layer_norm_demo,
            qianlu_layer_norm_compare,
            qianlu_streaming_attn_demo,
            qianlu_streaming_attn_compare,
            qianlu_pos_interp_demo,
            qianlu_pos_interp_compare,
            qianlu_grad_ckpt_demo,
            qianlu_grad_ckpt_compare,
            qianlu_prompt_tmpl_demo,
            qianlu_prompt_tmpl_compare,
                                    qianlu_seq_pack_demo,
            qianlu_seq_pack_compare,
            qianlu_attn_vis_demo,
            qianlu_attn_vis_stats,
            qianlu_mqa_demo,
            qianlu_mqa_stats,
            qianlu_temp_anneal_demo,
            qianlu_temp_anneal_compare,
            qianlu_early_stop_demo,
            qianlu_early_stop_stats,
            qianlu_grad_accum_demo, qianlu_grad_accum_stats,
            qianlu_swa_demo, qianlu_swa_compare,
            qianlu_token_freq_demo, qianlu_token_freq_stats,
            qianlu_dropout_demo, qianlu_dropout_stats,
            qianlu_weight_init_demo, qianlu_weight_init_stats,
            qianlu_batch_norm_demo, qianlu_batch_norm_compare,
            qianlu_mixup_demo, qianlu_mixup_stats,
            qianlu_focal_loss_demo, qianlu_focal_loss_stats,
            qianlu_stoch_depth_demo, qianlu_stoch_depth_stats,
            qianlu_label_smooth_demo, qianlu_label_smooth_stats,
            qianlu_pruning_demo, qianlu_pruning_stats,
            qianlu_grad_norm_demo, qianlu_grad_norm_stats,
            qianlu_warmup_demo, qianlu_warmup_stats,
            qianlu_ema_demo, qianlu_ema_stats,
            qianlu_spectral_norm_demo, qianlu_spectral_norm_stats,
            qianlu_ntk_rope_demo, qianlu_ntk_rope_stats,
            qianlu_multi_task_demo, qianlu_multi_task_stats,
            qianlu_wasserstein_demo, qianlu_wasserstein_stats,
            qianlu_self_consistency_demo, qianlu_self_consistency_stats,
            qianlu_contrastive_decoding_demo, qianlu_contrastive_decoding_stats,
            qianlu_token_unlearning_demo, qianlu_token_unlearning_stats,
            qianlu_paged_attn_demo, qianlu_paged_attn_stats,
            qianlu_spec_rejection_demo, qianlu_spec_rejection_stats,
            qianlu_dyn_batch_demo, qianlu_dyn_batch_stats,
            qianlu_gptq_demo, qianlu_gptq_stats,
            qianlu_bleu_demo, qianlu_bleu_stats,
            qianlu_cot_demo, qianlu_cot_stats,
            qianlu_inst_norm_demo, qianlu_inst_norm_stats,
            qianlu_rouge_demo, qianlu_rouge_stats,
            qianlu_tot_demo, qianlu_tot_stats,
            qianlu_kto_demo, qianlu_kto_stats,
            qianlu_mod_router_demo, qianlu_mod_router_stats,
            qianlu_sparse_moe_demo, qianlu_sparse_moe_stats,
            qianlu_bpe_demo, qianlu_bpe_stats,
            qianlu_dist_demo, qianlu_dist_stats,
            qianlu_reward_demo,
            qianlu_reward_stats,
            qianlu_adv_train_demo,
            qianlu_adv_train_stats,
            qianlu_nas_demo,
            qianlu_nas_stats,
            qianlu_data_aug_demo,
            qianlu_data_aug_stats,
            qianlu_curriculum_demo,
            qianlu_curriculum_stats,
            qianlu_watermark_demo,
            qianlu_watermark_stats,
            qianlu_group_norm_demo,
            qianlu_group_norm_stats,
            qianlu_kv_eviction_demo,
            qianlu_kv_eviction_stats,
            qianlu_bench_demo,
            qianlu_bench_stats,
            qianlu_raft_demo, qianlu_raft_stats,
            qianlu_federated_demo, qianlu_federated_stats,
            qianlu_kg_demo, qianlu_kg_stats,
            qianlu_prompt_tune_demo, qianlu_prompt_tune_stats,
            qianlu_constitut_demo, qianlu_constitut_stats,
            qianlu_active_learn_demo, qianlu_active_learn_stats,
            qianlu_mm_fusion_demo, qianlu_mm_fusion_stats,
            qianlu_meta_learn_demo, qianlu_meta_learn_stats,
            qianlu_continual_demo, qianlu_continual_stats,
            qianlu_sparse_gate_demo, qianlu_sparse_gate_stats,
            qianlu_inst_tune_demo, qianlu_inst_tune_stats,
            qianlu_cov_demo, qianlu_cov_stats,
            qianlu_data_loader_load,
        qianlu_data_loader_batch,
        qianlu_data_loader_stats,
        qianlu_data_pipeline_stats,
        qianlu_tokenized_ds_stats,
        qianlu_dist_train_stats,
        qianlu_mp_stats,
        qianlu_ckpt_stats,
        qianlu_compress_demo, qianlu_compress_stats,
            qianlu_self_play_demo, qianlu_self_play_stats,
            qianlu_ipo_demo, qianlu_ipo_stats,
            qianlu_ensemble_demo, qianlu_ensemble_stats,
            qianlu_contrastive2_demo, 
            qianlu_rag_pipeline_demo, qianlu_rag_pipeline_stats,
            qianlu_lora_adapter_demo,
            // KV Quant + Cont Batching (前端已调用但未注册)
            qianlu_kv_quant_demo, qianlu_kv_quant_stats,
            qianlu_cont_batching_demo, qianlu_cont_batching_stats,
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
        ])
        .run(tauri::generate_context!())
        .expect("SNAR IDE startup failed");
}


