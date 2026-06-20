//! SNAR IDE - Rust Backend
//!
//! Integrates SNARjs compiler, execution engine, debugger, and qianlu AI

use std::fs;
use std::path::Path;
use std::sync::Mutex;

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
fn qianlu_cont_batch_demo(state: tauri::State<'_, Mutex<QianluState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.cont_batch_demo(&text))
}
#[tauri::command]
fn qianlu_cont_batch_stats(state: tauri::State<'_, Mutex<QianluState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.cont_batch_stats())
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
/// qianlu quantize model weights
#[tauri::command]
fn qianlu_quantize(
    state: tauri::State<'_, Mutex<QianluState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.quantize_model())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .manage(Mutex::new(QianluState::new()))
        .invoke_handler(tauri::generate_handler![
            compile_source,
            compile_and_run,
            get_version,
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
            qianlu_kv_quant_demo,
            qianlu_kv_quant_stats,
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
            qianlu_cont_batch_demo, qianlu_cont_batch_stats,
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
            qianlu_compress_demo, qianlu_compress_stats,
            qianlu_self_play_demo, qianlu_self_play_stats,
            qianlu_ipo_demo, qianlu_ipo_stats,
            qianlu_ensemble_demo, qianlu_ensemble_stats,
            qianlu_contrastive2_demo, 
            qianlu_rag_pipeline_demo, qianlu_rag_pipeline_stats,
            qianlu_lora_adapter_demo, 
        ])
        .run(tauri::generate_context!())
        .expect("SNAR IDE startup failed");
}
