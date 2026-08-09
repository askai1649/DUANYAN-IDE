// DUANYAN 研究演示命令区 (466 个 tauri command: inference/train/nas/rlhf/copilot 等)
// 2026-08 工程治理: 自 lib.rs 剥离, use super::* 继承根作用域
#[allow(unused_imports)]
use super::*;

/// duanyan inference - process a prompt and return response
#[tauri::command]
pub fn duanyan_inference(
    state: tauri::State<'_, Mutex<DuanyanState>>,
    prompt: String,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let response = state.model.infer_text(&prompt);
    let status = state.model.get_learning_status();
    Ok(format!("{}\n\n--- duanyan stats ---\n{}", response, status.format()))
}

/// duanyan train - run training epochs
#[tauri::command]
pub fn duanyan_train(
    state: tauri::State<'_, Mutex<DuanyanState>>,
    epochs: usize,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let result = state.model.train(epochs);
    let stats = state.model.training_stats();
    Ok(format!("{}\n{}", result, stats))
}

/// duanyan generate - generate text from seed
#[tauri::command]
pub fn duanyan_generate(
    state: tauri::State<'_, Mutex<DuanyanState>>,
    seed: String,
    max_len: usize,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let generated = state.model.generate_text(&seed, max_len);
    Ok(generated)
}

/// duanyan replay - replay training history
#[tauri::command]
pub fn duanyan_replay(
    state: tauri::State<'_, Mutex<DuanyanState>>,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.replay())
}

/// duanyan save weights to file
#[tauri::command]
pub fn duanyan_save_weights(
    state: tauri::State<'_, Mutex<DuanyanState>>,
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

/// duanyan load weights from file
#[tauri::command]
pub fn duanyan_load_weights(
    state: tauri::State<'_, Mutex<DuanyanState>>,
    path: String,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let data = fs::read(&path).map_err(|e| format!("Read failed: {}", e))?;
    state.model.load_weights(&data)?;
    let summary = state.model.weight_summary();
    Ok(format!("Loaded {} bytes from {}\n{}", data.len(), path, summary))
}

/// duanyan get full status
#[tauri::command]
pub fn duanyan_status(
    state: tauri::State<'_, Mutex<DuanyanState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let status = state.model.get_learning_status();
    let training = state.model.training_stats();
    let weights = state.model.weight_summary();
    Ok(format!("{}\n\n{}\n\n{}", status.format(), training, weights))
}

/// duanyan generate via Transformer decoder (autoregressive)
#[tauri::command]
pub fn duanyan_generate_decoder(
    state: tauri::State<'_, Mutex<DuanyanState>>,
    seed: String,
    max_len: usize,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let generated = state.model.generate_decoder(&seed, max_len);
    Ok(generated)
}

/// duanyan toggle backprop training mode
#[tauri::command]
pub fn duanyan_set_backprop(
    state: tauri::State<'_, Mutex<DuanyanState>>,
    enabled: bool,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.set_backprop_mode(enabled))
}

/// duanyan generate with KV Cache acceleration
#[tauri::command]
pub fn duanyan_generate_cached(
    state: tauri::State<'_, Mutex<DuanyanState>>,
    seed: String,
    max_len: usize,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    let generated = state.model.generate_cached(&seed, max_len);
    Ok(format!("KV-Cached: {}", generated))
}

/// duanyan train with cached forward pass
#[tauri::command]
pub fn duanyan_train_cached(
    state: tauri::State<'_, Mutex<DuanyanState>>,
    epochs: usize,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.train_cached(epochs))
}

/// duanyan export architecture description
#[tauri::command]
pub fn duanyan_export_arch(
    state: tauri::State<'_, Mutex<DuanyanState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.export_architecture())
}

/// duanyan export architecture as JSON
#[tauri::command]
pub fn duanyan_export_json(
    state: tauri::State<'_, Mutex<DuanyanState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.export_architecture_json())
}

/// duanyan generate HardyScript code from prompt (with compile verification)
#[tauri::command]
pub fn duanyan_generate_code(
    prompt: String,
    max_len: usize,
    verify: bool,
) -> Result<serde_json::Value, String> {
    use duanyan::copilot::{CopilotEngine, CopilotConfig, GenerationMode};

    let tokenizer = duanyan::tokenizer::Tokenizer::new();
    let mut config = CopilotConfig::small(tokenizer.vocab_size);
    config.use_preln = true;
    config.use_rope = true;

    let mut copilot = CopilotEngine::new(config);
    copilot.generation_mode = GenerationMode::Sample { temperature: 0.7, top_p: 0.9 };

    // Load HardyScript corpus for better code generation
    let corpus = duanyan::hardyscript_corpus::HardyscriptCorpusGen::new();
    for (p, c) in corpus.to_training_pairs() {
        let text = format!("{}{}", p, c);
        copilot.load_code(&text);
    }

    // Quick train
    copilot.train(2, 4);

    // Generate
    let generated = copilot.complete_code(&prompt, max_len.max(20));

    // Optionally verify with HardyScript compiler
    let mut compile_ok = false;
    let mut compile_msg = String::from("skipped");
    if verify {
        match hardyscript::lexer::tokenize(&generated) {
            Ok(tokens) => {
                match hardyscript::parser::parse(&tokens) {
                    Ok(ast) => {
                        match hardyscript::compiler::compile(&ast) {
                            Ok(instrs) => {
                                compile_ok = true;
                                compile_msg = format!("✓ {} instructions", instrs.len());
                            }
                            Err(e) => compile_msg = format!("✗ compiler: {}", e),
                        }
                    }
                    Err(e) => compile_msg = format!("✗ parser: {}", e),
                }
            }
            Err(e) => compile_msg = format!("✗ lexer: {}", e),
        }
    }

    Ok(serde_json::json!({
        "prompt": prompt,
        "generated": generated,
        "compile_ok": compile_ok,
        "compile_msg": compile_msg,
        "model_stats": copilot.model_stats(),
    }))
}

/// duanyan full quantization report
#[tauri::command]
pub fn duanyan_quant_report(
    state: tauri::State<'_, Mutex<DuanyanState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.full_quantization_report())
}

/// 锻言智能代码生成: 自然语言 → 可编译的 HardyScript 代码
#[tauri::command]
pub fn duanyan_smart_gen(user_input: String) -> Result<serde_json::Value, String> {
    let mut gen = duanyan::smart_gen::SmartGenerator::new();
    let result = gen.generate(&user_input);
    Ok(serde_json::json!({
        "code": result.code,
        "intent": result.intent_desc,
        "template": result.template_name,
        "compile_success": result.compile_success,
        "compile_stage": result.compile_stage,
        "instruction_count": result.instruction_count,
        "repair_attempts": result.repair_attempts,
        "errors": result.errors,
        "warnings": result.warnings,
        "log": result.pipeline_log,
    }))
}

/// duanyan RoPE attention
#[tauri::command]
pub fn duanyan_rope_attention(
    state: tauri::State<'_, Mutex<DuanyanState>>,
    text: String,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.rope_attention(&text))
}

/// duanyan RoPE report
#[tauri::command]
pub fn duanyan_rope_report(
    state: tauri::State<'_, Mutex<DuanyanState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.rope_report())
}

/// duanyan model merge demo
#[tauri::command]
pub fn duanyan_model_merge(
    state: tauri::State<'_, Mutex<DuanyanState>>,
    strategy: Option<String>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.model_merge_demo(strategy.as_deref().unwrap_or("linear")))
}

/// duanyan multimodal process
#[tauri::command]
pub fn duanyan_multimodal(
    state: tauri::State<'_, Mutex<DuanyanState>>,
    text: String,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.multimodal_process(&text))
}

/// duanyan multimodal stats
#[tauri::command]
pub fn duanyan_multimodal_stats(
    state: tauri::State<'_, Mutex<DuanyanState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.multimodal_stats())
}

/// duanyan sparse attention
#[tauri::command]
pub fn duanyan_sparse_attention(
    state: tauri::State<'_, Mutex<DuanyanState>>,
    text: String,
    pattern: String,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.sparse_attention(&text, &pattern))
}

/// duanyan flash attention
#[tauri::command]
pub fn duanyan_flash_attention(
    state: tauri::State<'_, Mutex<DuanyanState>>,
    text: String,
    block_size: Option<usize>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.flash_attention(&text, block_size.unwrap_or(8)))
}

/// duanyan flash report
#[tauri::command]
pub fn duanyan_flash_report(
    state: tauri::State<'_, Mutex<DuanyanState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.flash_report())
}

/// duanyan speculative decoding generation
#[tauri::command]
pub fn duanyan_speculative_generate(
    state: tauri::State<'_, Mutex<DuanyanState>>,
    text: String,
    max_len: Option<usize>,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.speculative_generate(&text, max_len.unwrap_or(20)))
}

/// duanyan speculative decoding stats
#[tauri::command]
pub fn duanyan_spec_stats(
    state: tauri::State<'_, Mutex<DuanyanState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.spec_decode_stats())
}

/// duanyan LoRA fine-tuning
#[tauri::command]
pub fn duanyan_lora_train(
    state: tauri::State<'_, Mutex<DuanyanState>>,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.lora_train())
}

/// duanyan LoRA merge
#[tauri::command]
pub fn duanyan_lora_merge(
    state: tauri::State<'_, Mutex<DuanyanState>>,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.lora_merge())
}

/// duanyan LoRA stats
#[tauri::command]
pub fn duanyan_lora_adapter_stats(
    state: tauri::State<'_, Mutex<DuanyanState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.lora_stats())
}

/// duanyan MoE forward pass
#[tauri::command]
pub fn duanyan_moe_forward(
    state: tauri::State<'_, Mutex<DuanyanState>>,
    text: String,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.moe_forward(&text))
}

/// duanyan MoE stats
#[tauri::command]
pub fn duanyan_moe_stats(
    state: tauri::State<'_, Mutex<DuanyanState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.moe_stats())
}

/// duanyan ONNX export report
#[tauri::command]
pub fn duanyan_onnx_export(
    state: tauri::State<'_, Mutex<DuanyanState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.onnx_export())
}

/// duanyan f16 half-precision report
#[tauri::command]
pub fn duanyan_f16_report(
    state: tauri::State<'_, Mutex<DuanyanState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.f16_report())
}

/// duanyan RLHF training
#[tauri::command]
pub fn duanyan_rlhf_train(
    state: tauri::State<'_, Mutex<DuanyanState>>,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.rlhf_train())
}

/// duanyan RLHF stats
#[tauri::command]
pub fn duanyan_rlhf_stats(
    state: tauri::State<'_, Mutex<DuanyanState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.rlhf_stats())
}

/// duanyan knowledge distillation step
#[tauri::command]
pub fn duanyan_distill_step(
    state: tauri::State<'_, Mutex<DuanyanState>>,
) -> Result<String, String> {
    let mut state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.distill_step())
}

/// duanyan distillation stats
#[tauri::command]
pub fn duanyan_distill_stats(
    state: tauri::State<'_, Mutex<DuanyanState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.distill_stats())
}

#[tauri::command]
pub fn duanyan_gqa_forward(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.gqa_forward(&text))
}

#[tauri::command]
pub fn duanyan_gqa_report(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.gqa_report())
}

#[tauri::command]
pub fn duanyan_act_cache_forward(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.act_cache_forward(&text))
}

#[tauri::command]
pub fn duanyan_act_cache_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.act_cache_stats())
}

#[tauri::command]
pub fn duanyan_icl_query(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.icl_query(&text))
}

#[tauri::command]
pub fn duanyan_icl_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.icl_stats())
}


#[tauri::command]
pub fn duanyan_mod_forward(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.mod_forward(&text))
}

#[tauri::command]
pub fn duanyan_mod_report(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.mod_report())
}

#[tauri::command]
pub fn duanyan_rag_index(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.rag_index(&text))
}

#[tauri::command]
pub fn duanyan_rag_query(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.rag_query(&text))
}

#[tauri::command]
pub fn duanyan_contrastive_train(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.contrastive_train(&text))
}

#[tauri::command]
pub fn duanyan_contrastive2_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.contrastive2_stats())
}


#[tauri::command]
pub fn duanyan_rmsnorm_forward(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.rmsnorm_forward(&text))
}

#[tauri::command]
pub fn duanyan_rmsnorm_report(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.rmsnorm_report())
}

#[tauri::command]
pub fn duanyan_swiglu_forward(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.swiglu_forward(&text))
}

#[tauri::command]
pub fn duanyan_swiglu_report(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.swiglu_report())
}

#[tauri::command]
pub fn duanyan_beam_search_decode(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.beam_search_decode(&text))
}

#[tauri::command]
pub fn duanyan_beam_search_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.beam_search_stats())
}


#[tauri::command]
pub fn duanyan_sample_text(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.sample_text(&text))
}

#[tauri::command]
pub fn duanyan_sampling_config(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.sampling_config())
}

#[tauri::command]
pub fn duanyan_lr_step(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.lr_step())
}

#[tauri::command]
pub fn duanyan_lr_curve(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.lr_curve())
}

#[tauri::command]
pub fn duanyan_alibi_forward(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.alibi_forward(&text))
}

#[tauri::command]
pub fn duanyan_alibi_report(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.alibi_report())
}


#[tauri::command]
pub fn duanyan_grad_clip_demo(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.grad_clip_demo())
}

#[tauri::command]
pub fn duanyan_grad_clip_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.grad_clip_stats())
}

#[tauri::command]
pub fn duanyan_adam_simulate(state: tauri::State<'_, Mutex<DuanyanState>>, steps: Option<usize>) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.adam_simulate(steps.unwrap_or(100)))
}

#[tauri::command]
pub fn duanyan_adam_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.adam_stats())
}

#[tauri::command]
pub fn duanyan_token_merge(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.token_merge_report(&text))
}

#[tauri::command]
pub fn duanyan_token_merge_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.token_merge_stats())
}


#[tauri::command]
pub fn duanyan_weight_tying_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.weight_tying_demo(&text))
}

#[tauri::command]
pub fn duanyan_weight_tying_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.weight_tying_stats())
}

#[tauri::command]
pub fn duanyan_cross_attn_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.cross_attn_demo(&text))
}

#[tauri::command]
pub fn duanyan_cross_attn_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.cross_attn_stats())
}

#[tauri::command]
pub fn duanyan_perplexity_eval(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.perplexity_eval(&text))
}

#[tauri::command]
pub fn duanyan_perplexity_report(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.perplexity_report())
}


#[tauri::command]
pub fn duanyan_rep_penalty_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.rep_penalty_demo(&text))
}

#[tauri::command]
pub fn duanyan_rep_penalty_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.rep_penalty_stats())
}

#[tauri::command]
pub fn duanyan_layer_norm_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.layer_norm_demo(&text))
}

#[tauri::command]
pub fn duanyan_layer_norm_compare(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.layer_norm_compare())
}

#[tauri::command]
pub fn duanyan_streaming_attn_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.streaming_attn_demo(&text))
}

#[tauri::command]
pub fn duanyan_streaming_attn_compare(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.streaming_attn_compare())
}


#[tauri::command]
pub fn duanyan_pos_interp_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.pos_interp_demo(&text))
}

#[tauri::command]
pub fn duanyan_pos_interp_compare(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.pos_interp_compare())
}

#[tauri::command]
pub fn duanyan_grad_ckpt_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.grad_ckpt_demo(&text))
}

#[tauri::command]
pub fn duanyan_grad_ckpt_compare(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.grad_ckpt_compare())
}

#[tauri::command]
pub fn duanyan_prompt_tmpl_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.prompt_tmpl_demo(&text))
}

#[tauri::command]
pub fn duanyan_prompt_tmpl_compare(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.prompt_tmpl_compare(&text))
}


#[tauri::command]
pub fn duanyan_kv_quant_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.kv_quant_demo(&text))
}

#[tauri::command]
pub fn duanyan_kv_quant_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.kv_quant_stats())
}

#[tauri::command]
pub fn duanyan_seq_pack_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.seq_pack_demo(&text))
}

#[tauri::command]
pub fn duanyan_seq_pack_compare(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.seq_pack_compare())
}

#[tauri::command]
pub fn duanyan_attn_vis_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.attn_vis_demo(&text))
}

#[tauri::command]
pub fn duanyan_attn_vis_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.attn_vis_stats())
}


#[tauri::command]
pub fn duanyan_mqa_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.mqa_demo(&text))
}

#[tauri::command]
pub fn duanyan_mqa_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.mqa_stats())
}

#[tauri::command]
pub fn duanyan_temp_anneal_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.temp_anneal_demo(&text))
}

#[tauri::command]
pub fn duanyan_temp_anneal_compare(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.temp_anneal_compare())
}

#[tauri::command]
pub fn duanyan_early_stop_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    Ok(s.model.early_stop_demo(&text))
}

#[tauri::command]
pub fn duanyan_early_stop_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap();
    Ok(s.model.early_stop_stats())
}


#[tauri::command]
pub fn duanyan_grad_accum_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.grad_accum_demo(&text))
}
#[tauri::command]
pub fn duanyan_grad_accum_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.grad_accum_stats())
}
#[tauri::command]
pub fn duanyan_swa_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.swa_demo(&text))
}
#[tauri::command]
pub fn duanyan_swa_compare(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.swa_compare())
}
#[tauri::command]
pub fn duanyan_token_freq_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.token_freq_demo(&text))
}
#[tauri::command]
pub fn duanyan_token_freq_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.token_freq_stats())
}

#[tauri::command]
pub fn duanyan_dropout_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.dropout_demo(&text))
}
#[tauri::command]
pub fn duanyan_dropout_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.dropout_stats())
}
#[tauri::command]
pub fn duanyan_weight_init_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.weight_init_demo(&text))
}
#[tauri::command]
pub fn duanyan_weight_init_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.weight_init_stats())
}
#[tauri::command]
pub fn duanyan_batch_norm_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.batch_norm_demo(&text))
}
#[tauri::command]
pub fn duanyan_batch_norm_compare(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.batch_norm_compare())
}

#[tauri::command]
pub fn duanyan_mixup_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.mixup_demo(&text))
}
#[tauri::command]
pub fn duanyan_mixup_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.mixup_stats())
}
#[tauri::command]
pub fn duanyan_focal_loss_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.focal_loss_demo(&text))
}
#[tauri::command]
pub fn duanyan_focal_loss_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.focal_loss_stats())
}
#[tauri::command]
pub fn duanyan_stoch_depth_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.stoch_depth_demo(&text))
}
#[tauri::command]
pub fn duanyan_stoch_depth_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.stoch_depth_stats())
}

#[tauri::command]
pub fn duanyan_label_smooth_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.label_smooth_demo(&text))
}
#[tauri::command]
pub fn duanyan_label_smooth_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.label_smooth_stats())
}
#[tauri::command]
pub fn duanyan_pruning_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.pruning_demo(&text))
}
#[tauri::command]
pub fn duanyan_pruning_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.pruning_stats())
}
#[tauri::command]
pub fn duanyan_grad_norm_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.grad_norm_demo(&text))
}
#[tauri::command]
pub fn duanyan_grad_norm_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.grad_norm_stats())
}

#[tauri::command]
pub fn duanyan_warmup_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.warmup_demo(&text))
}
#[tauri::command]
pub fn duanyan_warmup_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.warmup_stats())
}
#[tauri::command]
pub fn duanyan_ema_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ema_demo(&text))
}
#[tauri::command]
pub fn duanyan_ema_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ema_stats())
}
#[tauri::command]
pub fn duanyan_spectral_norm_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.spectral_norm_demo(&text))
}
#[tauri::command]
pub fn duanyan_spectral_norm_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.spectral_norm_stats())
}

#[tauri::command]
pub fn duanyan_ntk_rope_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ntk_rope_demo(&text))
}
#[tauri::command]
pub fn duanyan_ntk_rope_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ntk_rope_stats())
}
#[tauri::command]
pub fn duanyan_multi_task_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.multi_task_demo(&text))
}
#[tauri::command]
pub fn duanyan_multi_task_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.multi_task_stats())
}
#[tauri::command]
pub fn duanyan_wasserstein_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.wasserstein_demo(&text))
}
#[tauri::command]
pub fn duanyan_wasserstein_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.wasserstein_stats())
}

#[tauri::command]
pub fn duanyan_self_consistency_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.self_consistency_demo(&text))
}
#[tauri::command]
pub fn duanyan_self_consistency_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.self_consistency_stats())
}
#[tauri::command]
pub fn duanyan_contrastive_decoding_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.contrastive_decoding_demo(&text))
}
#[tauri::command]
pub fn duanyan_contrastive_decoding_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.contrastive_decoding_stats())
}
#[tauri::command]
pub fn duanyan_token_unlearning_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.token_unlearning_demo(&text))
}
#[tauri::command]
pub fn duanyan_token_unlearning_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.token_unlearning_stats())
}

#[tauri::command]
pub fn duanyan_paged_attn_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.paged_attn_demo(&text))
}
#[tauri::command]
pub fn duanyan_paged_attn_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.paged_attn_stats())
}
#[tauri::command]
pub fn duanyan_spec_rejection_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.spec_rejection_demo(&text))
}
#[tauri::command]
pub fn duanyan_spec_rejection_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.spec_rejection_stats())
}
#[tauri::command]
pub fn duanyan_cont_batching_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.cont_batching_demo(&text))
}
#[tauri::command]
pub fn duanyan_cont_batching_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.cont_batching_stats())
}

#[tauri::command]
pub fn duanyan_gptq_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.gptq_demo(&text))
}
#[tauri::command]
pub fn duanyan_gptq_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.gptq_stats())
}
#[tauri::command]
pub fn duanyan_bleu_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.bleu_demo(&text))
}
#[tauri::command]
pub fn duanyan_bleu_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.bleu_stats())
}
#[tauri::command]
pub fn duanyan_cot_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.cot_demo(&text))
}
#[tauri::command]
pub fn duanyan_cot_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.cot_stats())
}

#[tauri::command]
pub fn duanyan_inst_norm_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.inst_norm_demo(&text))
}
#[tauri::command]
pub fn duanyan_inst_norm_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.inst_norm_stats())
}
#[tauri::command]
pub fn duanyan_rouge_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.rouge_demo(&text))
}
#[tauri::command]
pub fn duanyan_rouge_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.rouge_stats())
}
#[tauri::command]
pub fn duanyan_tot_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.tot_demo(&text))
}
#[tauri::command]
pub fn duanyan_tot_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.tot_stats())
}

#[tauri::command]
pub fn duanyan_kto_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.kto_demo(&text))
}
#[tauri::command]
pub fn duanyan_kto_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.kto_stats())
}
#[tauri::command]
pub fn duanyan_mod_router_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.mod_router_demo(&text))
}
#[tauri::command]
pub fn duanyan_mod_router_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.mod_router_stats())
}
#[tauri::command]
pub fn duanyan_sparse_moe_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.sparse_moe_demo(&text))
}
#[tauri::command]
pub fn duanyan_sparse_moe_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.sparse_moe_stats())
}

#[tauri::command]
pub fn duanyan_bpe_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.bpe_demo(&text))
}
#[tauri::command]
pub fn duanyan_bpe_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.bpe_stats())
}
#[tauri::command]
pub fn duanyan_dist_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.dist_demo(&text))
}
#[tauri::command]
pub fn duanyan_dist_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.dist_stats())
}



#[tauri::command]
pub fn duanyan_reward_demo(state: tauri::State<'_, Mutex<duanyan::DuanyanModel>>, text: String) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.reward_demo(&text))
}

#[tauri::command]
pub fn duanyan_reward_stats(state: tauri::State<'_, Mutex<duanyan::DuanyanModel>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.reward_stats())
}

#[tauri::command]
pub fn duanyan_adv_train_demo(state: tauri::State<'_, Mutex<duanyan::DuanyanModel>>, text: String) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.adv_train_demo(&text))
}

#[tauri::command]
pub fn duanyan_adv_train_stats(state: tauri::State<'_, Mutex<duanyan::DuanyanModel>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.adv_train_stats())
}

#[tauri::command]
pub fn duanyan_nas_demo(state: tauri::State<'_, Mutex<duanyan::DuanyanModel>>, text: String) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.nas_demo(&text))
}

#[tauri::command]
pub fn duanyan_nas_stats(state: tauri::State<'_, Mutex<duanyan::DuanyanModel>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.nas_stats())
}

#[tauri::command]
pub fn duanyan_data_aug_demo(state: tauri::State<'_, Mutex<duanyan::DuanyanModel>>, text: String) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.data_aug_demo(&text))
}

#[tauri::command]
pub fn duanyan_data_aug_stats(state: tauri::State<'_, Mutex<duanyan::DuanyanModel>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.data_aug_stats())
}

#[tauri::command]
pub fn duanyan_curriculum_demo(state: tauri::State<'_, Mutex<duanyan::DuanyanModel>>, text: String) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.curriculum_demo(&text))
}

#[tauri::command]
pub fn duanyan_curriculum_stats(state: tauri::State<'_, Mutex<duanyan::DuanyanModel>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.curriculum_stats())
}

#[tauri::command]
pub fn duanyan_watermark_demo(state: tauri::State<'_, Mutex<duanyan::DuanyanModel>>, text: String) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.watermark_demo(&text))
}

#[tauri::command]
pub fn duanyan_watermark_stats(state: tauri::State<'_, Mutex<duanyan::DuanyanModel>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.watermark_stats())
}

#[tauri::command]
pub fn duanyan_group_norm_demo(state: tauri::State<'_, Mutex<duanyan::DuanyanModel>>, text: String) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.group_norm_demo(&text))
}

#[tauri::command]
pub fn duanyan_group_norm_stats(state: tauri::State<'_, Mutex<duanyan::DuanyanModel>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.group_norm_stats())
}

#[tauri::command]
pub fn duanyan_kv_eviction_demo(state: tauri::State<'_, Mutex<duanyan::DuanyanModel>>, text: String) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.kv_eviction_demo(&text))
}

#[tauri::command]
pub fn duanyan_kv_eviction_stats(state: tauri::State<'_, Mutex<duanyan::DuanyanModel>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.kv_eviction_stats())
}

#[tauri::command]
pub fn duanyan_bench_demo(state: tauri::State<'_, Mutex<duanyan::DuanyanModel>>, text: String) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.bench_demo(&text))
}

#[tauri::command]
pub fn duanyan_bench_stats(state: tauri::State<'_, Mutex<duanyan::DuanyanModel>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.bench_stats())
}

#[tauri::command]
pub fn duanyan_raft_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.raft_demo(&text))
}
#[tauri::command]
pub fn duanyan_raft_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.raft_stats())
}
#[tauri::command]
pub fn duanyan_federated_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.federated_demo(&text))
}
#[tauri::command]
pub fn duanyan_federated_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.federated_stats())
}
#[tauri::command]
pub fn duanyan_kg_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.kg_demo(&text))
}
#[tauri::command]
pub fn duanyan_kg_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.kg_stats())
}
#[tauri::command]
pub fn duanyan_prompt_tune_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.prompt_tune_demo(&text))
}
#[tauri::command]
pub fn duanyan_prompt_tune_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.prompt_tune_stats())
}
#[tauri::command]
pub fn duanyan_constitut_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.constitut_demo(&text))
}
#[tauri::command]
pub fn duanyan_constitut_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.constitut_stats())
}
#[tauri::command]
pub fn duanyan_active_learn_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.active_learn_demo(&text))
}
#[tauri::command]
pub fn duanyan_active_learn_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.active_learn_stats())
}
#[tauri::command]
pub fn duanyan_mm_fusion_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.mm_fusion_demo(&text))
}
#[tauri::command]
pub fn duanyan_mm_fusion_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.mm_fusion_stats())
}
#[tauri::command]
pub fn duanyan_meta_learn_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.meta_learn_demo(&text))
}
#[tauri::command]
pub fn duanyan_meta_learn_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.meta_learn_stats())
}
#[tauri::command]
pub fn duanyan_continual_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.continual_demo(&text))
}
#[tauri::command]
pub fn duanyan_continual_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.continual_stats())
}
#[tauri::command]
pub fn duanyan_sparse_gate_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.sparse_gate_demo(&text))
}
#[tauri::command]
pub fn duanyan_sparse_gate_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.sparse_gate_stats())
}
#[tauri::command]
pub fn duanyan_inst_tune_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.inst_tune_demo(&text))
}
#[tauri::command]
pub fn duanyan_inst_tune_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.inst_tune_stats())
}
#[tauri::command]
pub fn duanyan_cov_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.cov_demo(&text))
}
#[tauri::command]
pub fn duanyan_cov_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.cov_stats())
}
#[tauri::command]
pub fn duanyan_data_loader_load(state: tauri::State<'_, std::sync::Mutex<DuanyanState>>, text: String, fmt: String) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.model.data_loader_load(&text, &fmt))
}
#[tauri::command]
pub fn duanyan_data_loader_batch(state: tauri::State<'_, std::sync::Mutex<DuanyanState>>) -> Result<String, String> {
    let mut model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.model.data_loader_batch())
}
#[tauri::command]
pub fn duanyan_data_loader_stats(state: tauri::State<'_, std::sync::Mutex<DuanyanState>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.model.data_loader_stats())
}
#[tauri::command]
pub fn duanyan_data_pipeline_stats(state: tauri::State<'_, std::sync::Mutex<DuanyanState>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.model.data_pipeline_stats())
}
#[tauri::command]
pub fn duanyan_tokenized_ds_stats(state: tauri::State<'_, std::sync::Mutex<DuanyanState>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.model.tokenized_ds_stats())
}
#[tauri::command]
pub fn duanyan_dist_train_stats(state: tauri::State<'_, std::sync::Mutex<DuanyanState>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.model.dist_train_stats())
}
#[tauri::command]
pub fn duanyan_mp_stats(state: tauri::State<'_, std::sync::Mutex<DuanyanState>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.model.mp_stats())
}
#[tauri::command]
pub fn duanyan_ckpt_stats(state: tauri::State<'_, std::sync::Mutex<DuanyanState>>) -> Result<String, String> {
    let model = state.lock().map_err(|e| e.to_string())?;
    Ok(model.model.ckpt_stats())
}
#[tauri::command]
pub fn duanyan_compress_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.compress_demo(&text))
}
#[tauri::command]
pub fn duanyan_compress_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.compress_stats())
}
#[tauri::command]
pub fn duanyan_self_play_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.self_play_demo(&text))
}
#[tauri::command]
pub fn duanyan_self_play_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.self_play_stats())
}
#[tauri::command]
pub fn duanyan_ipo_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ipo_demo(&text))
}
#[tauri::command]
pub fn duanyan_ipo_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ipo_stats())
}
#[tauri::command]
pub fn duanyan_ensemble_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ensemble_demo(&text))
}
#[tauri::command]
pub fn duanyan_ensemble_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ensemble_stats())
}
#[tauri::command]
pub fn duanyan_contrastive2_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.contrastive2_demo(&text))
}
#[tauri::command]
pub fn duanyan_rag_pipeline_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.rag_pipeline_demo(&text))
}
#[tauri::command]
pub fn duanyan_rag_pipeline_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.rag_pipeline_stats())
}
#[tauri::command]
pub fn duanyan_lora_adapter_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.lora_adapter_demo(&text))
}
#[tauri::command]
pub fn duanyan_gnn_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.gnn_demo(&text))
}
#[tauri::command]
pub fn duanyan_gnn_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.gnn_stats())
}
#[tauri::command]
pub fn duanyan_diffusion_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.diffusion_demo(&text))
}
#[tauri::command]
pub fn duanyan_diffusion_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.diffusion_stats())
}
#[tauri::command]
pub fn duanyan_causal_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.causal_demo(&text))
}
#[tauri::command]
pub fn duanyan_causal_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.causal_stats())
}
#[tauri::command]
pub fn duanyan_vae_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.vae_demo(&text))
}
#[tauri::command]
pub fn duanyan_vae_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.vae_stats())
}
#[tauri::command]
pub fn duanyan_bnn_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.bnn_demo(&text))
}
#[tauri::command]
pub fn duanyan_bnn_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.bnn_stats())
}
#[tauri::command]
pub fn duanyan_node_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.node_demo(&text))
}
#[tauri::command]
pub fn duanyan_node_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.node_stats())
}
#[tauri::command]
pub fn duanyan_reservoir_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.reservoir_demo(&text))
}
#[tauri::command]
pub fn duanyan_reservoir_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.reservoir_stats())
}
#[tauri::command]
pub fn duanyan_capsule_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.capsule_demo(&text))
}
#[tauri::command]
pub fn duanyan_capsule_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.capsule_stats())
}
#[tauri::command]
pub fn duanyan_ebm_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ebm_demo(&text))
}
#[tauri::command]
pub fn duanyan_ebm_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ebm_stats())
}
#[tauri::command]
pub fn duanyan_hypernet_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.hypernet_demo(&text))
}
#[tauri::command]
pub fn duanyan_hypernet_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.hypernet_stats())
}
#[tauri::command]
pub fn duanyan_flow_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.flow_demo(&text))
}
#[tauri::command]
pub fn duanyan_flow_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.flow_stats())
}
#[tauri::command]
pub fn duanyan_snn_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.snn_demo(&text))
}
#[tauri::command]
pub fn duanyan_snn_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.snn_stats())
}
#[tauri::command]
pub fn duanyan_world_model_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.world_model_demo(&text))
}
#[tauri::command]
pub fn duanyan_world_model_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.world_model_stats())
}
#[tauri::command]
pub fn duanyan_neuro_sym_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.neuro_sym_demo(&text))
}
#[tauri::command]
pub fn duanyan_neuro_sym_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.neuro_sym_stats())
}
#[tauri::command]
pub fn duanyan_sparse_ae_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.sparse_ae_demo(&text))
}
#[tauri::command]
pub fn duanyan_sparse_ae_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.sparse_ae_stats())
}
#[tauri::command]
pub fn duanyan_mem_net_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.mem_net_demo(&text))
}
#[tauri::command]
pub fn duanyan_mem_net_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.mem_net_stats())
}
#[tauri::command]
pub fn duanyan_cbm_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.cbm_demo(&text))
}
#[tauri::command]
pub fn duanyan_cbm_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.cbm_stats())
}
#[tauri::command]
pub fn duanyan_hrr_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.hrr_demo(&text))
}
#[tauri::command]
pub fn duanyan_hrr_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.hrr_stats())
}
#[tauri::command]
pub fn duanyan_lsm_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.lsm_demo(&text))
}
#[tauri::command]
pub fn duanyan_lsm_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.lsm_stats())
}
#[tauri::command]
pub fn duanyan_nps_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.nps_demo(&text))
}
#[tauri::command]
pub fn duanyan_nps_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.nps_stats())
}
#[tauri::command]
pub fn duanyan_neuro_core_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.neuro_core_demo(&text))
}
#[tauri::command]
pub fn duanyan_neuro_core_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.neuro_core_stats())
}
#[tauri::command]
pub fn duanyan_ckpt_merge_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ckpt_merge_demo(&text))
}
#[tauri::command]
pub fn duanyan_ckpt_merge_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ckpt_merge_stats())
}
#[tauri::command]
pub fn duanyan_ttc_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ttc_demo(&text))
}
#[tauri::command]
pub fn duanyan_ttc_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ttc_stats())
}
#[tauri::command]
pub fn duanyan_kd_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.kd_demo(&text))
}
#[tauri::command]
pub fn duanyan_kd_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.kd_stats())
}
#[tauri::command]
pub fn duanyan_draft_verify_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.draft_verify_demo(&text))
}
#[tauri::command]
pub fn duanyan_draft_verify_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.draft_verify_stats())
}
#[tauri::command]
pub fn duanyan_act_patch_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.act_patch_demo(&text))
}
#[tauri::command]
pub fn duanyan_act_patch_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.act_patch_stats())
}
#[tauri::command]
pub fn duanyan_ser_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ser_demo(&text))
}
#[tauri::command]
pub fn duanyan_ser_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ser_stats())
}
#[tauri::command]
pub fn duanyan_mech_interp_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.mech_interp_demo(&text))
}
#[tauri::command]
pub fn duanyan_mech_interp_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.mech_interp_stats())
}
#[tauri::command]
pub fn duanyan_sym_reg_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.sym_reg_demo(&text))
}
#[tauri::command]
pub fn duanyan_sym_reg_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.sym_reg_stats())
}
#[tauri::command]
pub fn duanyan_reason_trace_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.reason_trace_demo(&text))
}
#[tauri::command]
pub fn duanyan_reason_trace_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.reason_trace_stats())
}
#[tauri::command]
pub fn duanyan_dpo_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.dpo_demo(&text))
}
#[tauri::command]
pub fn duanyan_dpo_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.dpo_stats())
}
#[tauri::command]
pub fn duanyan_uq_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.uq_demo(&text))
}
#[tauri::command]
pub fn duanyan_uq_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.uq_stats())
}
#[tauri::command]
pub fn duanyan_data_val_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.data_val_demo(&text))
}
#[tauri::command]
pub fn duanyan_data_val_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.data_val_stats())
}
#[tauri::command]
pub fn duanyan_moo_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.moo_demo(&text))
}
#[tauri::command]
pub fn duanyan_moo_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.moo_stats())
}
#[tauri::command]
pub fn duanyan_task_vec_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.task_vec_demo(&text))
}
#[tauri::command]
pub fn duanyan_task_vec_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.task_vec_stats())
}
#[tauri::command]
pub fn duanyan_prompt_opt_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.prompt_opt_demo(&text))
}
#[tauri::command]
pub fn duanyan_prompt_opt_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.prompt_opt_stats())
}
#[tauri::command]
pub fn duanyan_online_learn_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.online_learn_demo(&text))
}
#[tauri::command]
pub fn duanyan_online_learn_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.online_learn_stats())
}
#[tauri::command]
pub fn duanyan_bandit_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.bandit_demo(&text))
}
#[tauri::command]
pub fn duanyan_bandit_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.bandit_stats())
}
#[tauri::command]
pub fn duanyan_reward_shape_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.reward_shape_demo(&text))
}
#[tauri::command]
pub fn duanyan_reward_shape_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.reward_shape_stats())
}
#[tauri::command]
pub fn duanyan_active_inf_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.active_inf_demo(&text))
}
#[tauri::command]
pub fn duanyan_active_inf_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.active_inf_stats())
}
#[tauri::command]
pub fn duanyan_fed_personal_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.fed_personal_demo(&text))
}
#[tauri::command]
pub fn duanyan_fed_personal_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.fed_personal_stats())
}
#[tauri::command]
pub fn duanyan_model_comp_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.model_comp_demo(&text))
}
#[tauri::command]
pub fn duanyan_model_comp_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.model_comp_stats())
}
#[tauri::command]
pub fn duanyan_bayes_opt_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.bayes_opt_demo(&text))
}
#[tauri::command]
pub fn duanyan_bayes_opt_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.bayes_opt_stats())
}
#[tauri::command]
pub fn duanyan_domain_adapt_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.domain_adapt_demo(&text))
}
#[tauri::command]
pub fn duanyan_domain_adapt_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.domain_adapt_stats())
}
#[tauri::command]
pub fn duanyan_ssl_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ssl_demo(&text))
}
#[tauri::command]
pub fn duanyan_ssl_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ssl_stats())
}
#[tauri::command]
pub fn duanyan_transfer_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.transfer_demo(&text))
}
#[tauri::command]
pub fn duanyan_transfer_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.transfer_stats())
}
#[tauri::command]
pub fn duanyan_few_shot_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.few_shot_demo(&text))
}
#[tauri::command]
pub fn duanyan_few_shot_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.few_shot_stats())
}
#[tauri::command]
pub fn duanyan_meta_rl_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.meta_rl_demo(&text))
}
#[tauri::command]
pub fn duanyan_meta_rl_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.meta_rl_stats())
}
#[tauri::command]
pub fn duanyan_zero_shot_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.zero_shot_demo(&text))
}
#[tauri::command]
pub fn duanyan_zero_shot_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.zero_shot_stats())
}
#[tauri::command]
pub fn duanyan_task_adapt_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.task_adapt_demo(&text))
}
#[tauri::command]
pub fn duanyan_task_adapt_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.task_adapt_stats())
}
#[tauri::command]
pub fn duanyan_rep_mixup_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.rep_mixup_demo(&text))
}
#[tauri::command]
pub fn duanyan_rep_mixup_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.rep_mixup_stats())
}
#[tauri::command]
pub fn duanyan_weight_share_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.weight_share_demo(&text))
}
#[tauri::command]
pub fn duanyan_weight_share_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.weight_share_stats())
}
#[tauri::command]
pub fn duanyan_disentangle_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.disentangle_demo(&text))
}
#[tauri::command]
pub fn duanyan_disentangle_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.disentangle_stats())
}
#[tauri::command]
pub fn duanyan_grad_surgery_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.grad_surgery_demo(&text))
}
#[tauri::command]
pub fn duanyan_grad_surgery_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.grad_surgery_stats())
}
#[tauri::command]
pub fn duanyan_evo_strat_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.evo_strat_demo(&text))
}
#[tauri::command]
pub fn duanyan_evo_strat_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.evo_strat_stats())
}
#[tauri::command]
pub fn duanyan_hyper_opt_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.hyper_opt_demo(&text))
}
#[tauri::command]
pub fn duanyan_hyper_opt_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.hyper_opt_stats())
}
#[tauri::command]
pub fn duanyan_multi_agent_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.multi_agent_demo(&text))
}
#[tauri::command]
pub fn duanyan_multi_agent_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.multi_agent_stats())
}
#[tauri::command]
pub fn duanyan_imitation_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.imitation_demo(&text))
}
#[tauri::command]
pub fn duanyan_imitation_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.imitation_stats())
}
#[tauri::command]
pub fn duanyan_inverse_rl_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.inverse_rl_demo(&text))
}
#[tauri::command]
pub fn duanyan_inverse_rl_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.inverse_rl_stats())
}
#[tauri::command]
pub fn duanyan_ntk_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ntk_demo(&text))
}
#[tauri::command]
pub fn duanyan_ntk_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ntk_stats())
}
#[tauri::command]
pub fn duanyan_ot_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ot_demo(&text))
}
#[tauri::command]
pub fn duanyan_ot_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ot_stats())
}
#[tauri::command]
pub fn duanyan_scaling_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.scaling_demo(&text))
}
#[tauri::command]
pub fn duanyan_scaling_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.scaling_stats())
}
#[tauri::command]
pub fn duanyan_model_edit_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.model_edit_demo(&text))
}
#[tauri::command]
pub fn duanyan_model_edit_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.model_edit_stats())
}
#[tauri::command]
pub fn duanyan_grok_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.grok_demo(&text))
}
#[tauri::command]
pub fn duanyan_grok_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.grok_stats())
}
#[tauri::command]
pub fn duanyan_double_desc_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.double_desc_demo(&text))
}
#[tauri::command]
pub fn duanyan_double_desc_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.double_desc_stats())
}
#[tauri::command]
pub fn duanyan_ssm_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ssm_demo(&text))
}
#[tauri::command]
pub fn duanyan_ssm_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ssm_stats())
}
#[tauri::command]
pub fn duanyan_tool_use_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.tool_use_demo(&text))
}
#[tauri::command]
pub fn duanyan_tool_use_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.tool_use_stats())
}
#[tauri::command]
pub fn duanyan_self_refine_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.self_refine_demo(&text))
}
#[tauri::command]
pub fn duanyan_self_refine_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.self_refine_stats())
}
#[tauri::command]
pub fn duanyan_reason_chain_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.reason_chain_demo(&text))
}
#[tauri::command]
pub fn duanyan_reason_chain_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.reason_chain_stats())
}
#[tauri::command]
pub fn duanyan_red_team_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.red_team_demo(&text))
}
#[tauri::command]
pub fn duanyan_red_team_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.red_team_stats())
}
#[tauri::command]
pub fn duanyan_safety_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.safety_demo(&text))
}
#[tauri::command]
pub fn duanyan_safety_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.safety_stats())
}
#[tauri::command]
pub fn duanyan_jailbreak_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.jailbreak_demo(&text))
}
#[tauri::command]
pub fn duanyan_jailbreak_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.jailbreak_stats())
}
#[tauri::command]
pub fn duanyan_ring_attn_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.ring_attn_demo(&text))
}
#[tauri::command]
pub fn duanyan_ring_attn_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.ring_attn_stats())
}
#[tauri::command]
pub fn duanyan_arena_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.arena_demo(&text))
}
#[tauri::command]
pub fn duanyan_arena_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.arena_stats())
}
#[tauri::command]
pub fn duanyan_synth_data_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.synth_data_demo(&text))
}
#[tauri::command]
pub fn duanyan_synth_data_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.synth_data_stats())
}
#[tauri::command]
pub fn duanyan_orpo_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.orpo_demo(&text))
}
#[tauri::command]
pub fn duanyan_orpo_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.orpo_stats())
}
#[tauri::command]
pub fn duanyan_simpo_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.simpo_demo(&text))
}
#[tauri::command]
pub fn duanyan_simpo_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.simpo_stats())
}
#[tauri::command]
pub fn duanyan_infini_attn_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.infini_attn_demo(&text))
}
#[tauri::command]
pub fn duanyan_infini_attn_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.infini_attn_stats())
}
#[tauri::command]
pub fn duanyan_medusa_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.medusa_demo(&text))
}
#[tauri::command]
pub fn duanyan_medusa_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.medusa_stats())
}
#[tauri::command]
pub fn duanyan_eagle_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.eagle_demo(&text))
}
#[tauri::command]
pub fn duanyan_eagle_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.eagle_stats())
}
#[tauri::command]
pub fn duanyan_chunked_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.chunked_demo(&text))
}
#[tauri::command]
pub fn duanyan_chunked_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.chunked_stats())
}
#[tauri::command]
pub fn duanyan_dora_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.dora_demo(&text))
}
#[tauri::command]
pub fn duanyan_dora_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.dora_stats())
}
#[tauri::command]
pub fn duanyan_top_k_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.top_k_demo(&text))
}
#[tauri::command]
pub fn duanyan_top_k_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.top_k_stats())
}
#[tauri::command]
pub fn duanyan_hyena_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.hyena_demo(&text))
}
#[tauri::command]
pub fn duanyan_hyena_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.hyena_stats())
}
#[tauri::command]
pub fn duanyan_rwkv_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.rwkv_demo(&text))
}
#[tauri::command]
pub fn duanyan_rwkv_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.rwkv_stats())
}
#[tauri::command]
pub fn duanyan_prompt_comp_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.prompt_comp_demo(&text))
}
#[tauri::command]
pub fn duanyan_prompt_comp_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.prompt_comp_stats())
}
#[tauri::command]
pub fn duanyan_flash_mla_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.flash_mla_demo(&text))
}
#[tauri::command]
pub fn duanyan_flash_mla_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.flash_mla_stats())
}
#[tauri::command]
pub fn duanyan_agent_plan_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.agent_plan_demo(&text))
}
#[tauri::command]
pub fn duanyan_agent_plan_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.agent_plan_stats())
}
#[tauri::command]
pub fn duanyan_spec_reject_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.spec_reject_demo(&text))
}
#[tauri::command]
pub fn duanyan_spec_reject_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.spec_reject_stats())
}
#[tauri::command]
pub fn duanyan_paged_kv_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.paged_kv_demo(&text))
}
#[tauri::command]
pub fn duanyan_paged_kv_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.paged_kv_stats())
}
#[tauri::command]
pub fn duanyan_agent_mem_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.agent_mem_demo(&text))
}
#[tauri::command]
pub fn duanyan_agent_mem_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.agent_mem_stats())
}
#[tauri::command]
pub fn duanyan_dyn_batch_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.cont_batching_demo(&text))
}
#[tauri::command]
pub fn duanyan_dyn_batch_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.cont_batching_stats())
}
#[tauri::command]
pub fn duanyan_vis_enc_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.vis_enc_demo(&text))
}
#[tauri::command]
pub fn duanyan_vis_enc_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.vis_enc_stats())
}
#[tauri::command]
pub fn duanyan_txt2img_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.txt2img_demo(&text))
}
#[tauri::command]
pub fn duanyan_txt2img_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.txt2img_stats())
}
#[tauri::command]
pub fn duanyan_prom_cache_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.prom_cache_demo(&text))
}
#[tauri::command]
pub fn duanyan_prom_cache_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.prom_cache_stats())
}
#[tauri::command]
pub fn duanyan_img_cap_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.img_cap_demo(&text))
}
#[tauri::command]
pub fn duanyan_img_cap_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.img_cap_stats())
}
#[tauri::command]
pub fn duanyan_obj_det_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.obj_det_demo(&text))
}
#[tauri::command]
pub fn duanyan_obj_det_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.obj_det_stats())
}
#[tauri::command]
pub fn duanyan_super_res_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.super_res_demo(&text))
}
#[tauri::command]
pub fn duanyan_super_res_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.super_res_stats())
}
#[tauri::command]
pub fn duanyan_style_trans_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.style_trans_demo(&text))
}
#[tauri::command]
pub fn duanyan_style_trans_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.style_trans_stats())
}
#[tauri::command]
pub fn duanyan_img_inpaint_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.img_inpaint_demo(&text))
}
#[tauri::command]
pub fn duanyan_img_inpaint_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.img_inpaint_stats())
}
#[tauri::command]
pub fn duanyan_audio_enc_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.audio_enc_demo(&text))
}
#[tauri::command]
pub fn duanyan_audio_enc_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.audio_enc_stats())
}
#[tauri::command]
pub fn duanyan_img_seg_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.img_seg_demo(&text))
}
#[tauri::command]
pub fn duanyan_img_seg_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.img_seg_stats())
}
#[tauri::command]
pub fn duanyan_depth_est_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.depth_est_demo(&text))
}
#[tauri::command]
pub fn duanyan_depth_est_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.depth_est_stats())
}
#[tauri::command]
pub fn duanyan_opt_flow_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.opt_flow_demo(&text))
}
#[tauri::command]
pub fn duanyan_opt_flow_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.opt_flow_stats())
}
#[tauri::command]
pub fn duanyan_pose_est_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.pose_est_demo(&text))
}
#[tauri::command]
pub fn duanyan_pose_est_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.pose_est_stats())
}
#[tauri::command]
pub fn duanyan_speech_recog_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.speech_recog_demo(&text))
}
#[tauri::command]
pub fn duanyan_speech_recog_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.speech_recog_stats())
}
#[tauri::command]
pub fn duanyan_music_gen_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.music_gen_demo(&text))
}
#[tauri::command]
pub fn duanyan_music_gen_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.music_gen_stats())
}
#[tauri::command]
pub fn duanyan_code_parser_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.code_parser_demo(&text))
}
#[tauri::command]
pub fn duanyan_code_parser_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.code_parser_stats())
}
#[tauri::command]
pub fn duanyan_code_gen_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.code_gen_demo(&text))
}
#[tauri::command]
pub fn duanyan_code_gen_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.code_gen_stats())
}
#[tauri::command]
pub fn duanyan_code_exec_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.code_exec_demo(&text))
}
#[tauri::command]
pub fn duanyan_code_exec_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.code_exec_stats())
}
#[tauri::command]
pub fn duanyan_code_debug_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.code_debug_demo(&text))
}
#[tauri::command]
pub fn duanyan_code_debug_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.code_debug_stats())
}
#[tauri::command]
pub fn duanyan_prog_repair_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.prog_repair_demo(&text))
}
#[tauri::command]
pub fn duanyan_prog_repair_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.prog_repair_stats())
}
#[tauri::command]
pub fn duanyan_code_review_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.code_review_demo(&text))
}
#[tauri::command]
pub fn duanyan_code_review_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.code_review_stats())
}
#[tauri::command]
pub fn duanyan_syntax_hl_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.syntax_hl_demo(&text))
}
#[tauri::command]
pub fn duanyan_syntax_hl_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.syntax_hl_stats())
}
#[tauri::command]
pub fn duanyan_code_complete_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.code_complete_demo(&text))
}
#[tauri::command]
pub fn duanyan_code_complete_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.code_complete_stats())
}
#[tauri::command]
pub fn duanyan_code_refactor_demo(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.code_refactor_demo(&text))
}
#[tauri::command]
pub fn duanyan_code_refactor_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.code_refactor_stats())
}
#[tauri::command]
pub fn duanyan_code_pipeline_analyze(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.code_pipeline_analyze(&text))
}
#[tauri::command]
pub fn duanyan_code_pipeline_full(state: tauri::State<'_, Mutex<DuanyanState>>, text: String) -> Result<String, String> {
    let mut s = state.lock().unwrap(); Ok(s.model.code_pipeline_full(&text))
}
#[tauri::command]
pub fn duanyan_code_pipeline_stats(state: tauri::State<'_, Mutex<DuanyanState>>) -> Result<String, String> {
    let s = state.lock().unwrap(); Ok(s.model.code_pipeline_stats())
}
#[tauri::command]
pub fn duanyan_code_analyze_panel(state: tauri::State<'_, Mutex<DuanyanState>>, code: String, lang: String) -> Result<String, String> {
    let mut s = state.lock().unwrap();
    let report = s.model.code_pipeline.analyze_for_language(&code, &lang);
    let formatted = s.model.code_pipeline.format_report(&report);
    Ok(formatted)
}
/// duanyan quantize model weights
#[tauri::command]
pub fn duanyan_quantize(
    state: tauri::State<'_, Mutex<DuanyanState>>,
) -> Result<String, String> {
    let state = state.lock().map_err(|e| format!("Lock error: {}", e))?;
    Ok(state.model.quantize_model())
}
