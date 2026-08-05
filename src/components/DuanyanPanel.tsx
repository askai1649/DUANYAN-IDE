import { useState, useRef, useEffect } from "react";

export interface ChatMessage {
  role: "user" | "duanyan" | "system";
  content: string;
  timestamp: number;
}

interface Props {
  onClose: () => void;
}

export default function DuanyanPanel({ onClose }: Props) {
  const [messages, setMessages] = useState<ChatMessage[]>([
    {
      role: "system",
      content: "duanyan v2.8 [KV Cache + Grad Cache + Model Export]\n2-layer Transformer | 4 heads | 64d | Token Decoder | i8 Quant | Weight I/O\nType a prompt or use the toolbar.",
      timestamp: Date.now(),
    },
  ]);
  const [input, setInput] = useState("");
  const [isProcessing, setIsProcessing] = useState(false);
  const [clock, setClock] = useState(0);
  const [showToolbar, setShowToolbar] = useState(false);
  const [genSeed, setGenSeed] = useState("hello");
  const [genLen, setGenLen] = useState(50);
  const [trainEpochs, setTrainEpochs] = useState(10);
  const [useBackprop, setUseBackprop] = useState(true);
  const chatRef = useRef<HTMLDivElement>(null);
  const inputRef = useRef<HTMLTextAreaElement>(null);

  useEffect(() => {
    if (chatRef.current) {
      chatRef.current.scrollTop = chatRef.current.scrollHeight;
    }
  }, [messages]);

  const addMessage = (role: ChatMessage["role"], content: string) => {
    setMessages(prev => [...prev, { role, content, timestamp: Date.now() }]);
  };

  const invoke = async (cmd: string, args?: Record<string, unknown>): Promise<string> => {
    const { invoke } = await import("@tauri-apps/api/core");
    return await invoke<string>(cmd, args);
  };

  const handleSend = async () => {
    const text = input.trim();
    if (!text || isProcessing) return;
    addMessage("user", text);
    setInput("");
    setIsProcessing(true);
    try {
      const result = await invoke("duanyan_inference", { prompt: text });
      addMessage("duanyan", result);
      setClock(c => c + 1);
    } catch (e) {
      addMessage("duanyan", "[error] Inference failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleTrain = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    addMessage("system", "Training " + trainEpochs + " epochs...");
    try {
      const result = await invoke("duanyan_train", { epochs: trainEpochs });
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] Training failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleGenerate = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    addMessage("system", "Generating text from seed: '" + genSeed + "' (" + genLen + " chars)...");
    try {
      const result = await invoke("duanyan_generate", { seed: genSeed, maxLen: genLen });
      addMessage("duanyan", "Generated: " + result);
    } catch (e) {
      addMessage("duanyan", "[error] Generation failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleReplay = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke("duanyan_replay");
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] Replay failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleSaveWeights = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke("duanyan_save_weights", { path: "C:\\OPENSNAR\\duanyan\\weights.bin" });
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] Save failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleLoadWeights = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke("duanyan_load_weights", { path: "C:\\OPENSNAR\\duanyan\\weights.bin" });
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] Load failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleStatus = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke("duanyan_status");
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] Status failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleCodeGenerate = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    addMessage("system", "DUANYAN generating HardyScript code for: '" + genSeed + "' (max " + genLen + " chars)...");
    try {
      const result = await invoke("duanyan_generate_code", { prompt: genSeed, maxLen: genLen, verify: true });
      const data = result as unknown as { generated: string; compile_ok: boolean; compile_msg: string };
      const status = data.compile_ok ? "✓ Compile OK" : "✗ Compile Failed";
      addMessage("duanyan", "// Generated HardyScript Code\n" + data.generated + "\n\n// " + status + ": " + data.compile_msg);
    } catch (e) {
      addMessage("duanyan", "[error] Code generation failed: " + String(e));
    }
    setIsProcessing(false);
  };


  const handleGenerateDecoder = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    addMessage("system", "Decoder generating from: '" + genSeed + "' (max " + genLen + " tokens)...");
    try {
      const result = await invoke("duanyan_generate_decoder", { seed: genSeed, maxLen: genLen });
      addMessage("duanyan", "Decoder: " + result);
    } catch (e) {
      addMessage("duanyan", "[error] Decoder failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleToggleBackprop = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    const newVal = !useBackprop;
    try {
      const result = await invoke("duanyan_set_backprop", { enabled: newVal });
      addMessage("duanyan", result);
      setUseBackprop(newVal);
    } catch (e) {
      addMessage("duanyan", "[error] " + String(e));
    }
    setIsProcessing(false);
  };

  const handleQuantize = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke("duanyan_quantize");
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] Quantize failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleGenerateCached = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    addMessage("system", "KV-Cached generation from: '" + genSeed + "' (max " + genLen + " tokens)...");
    try {
      const result = await invoke("duanyan_generate_cached", { seed: genSeed, maxLen: genLen });
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] Cached gen failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleTrainCached = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    addMessage("system", "Cached training " + trainEpochs + " epochs...");
    try {
      const result = await invoke("duanyan_train_cached", { epochs: trainEpochs });
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] Cached training failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleExportArch = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke("duanyan_export_arch");
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] Export failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleExportJson = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke("duanyan_export_json");
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] JSON export failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleQuantReport = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke("duanyan_quant_report");
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] Quant report failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleF16Report = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke("duanyan_f16_report");
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] F16 report failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleRlhfTrain = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke("duanyan_rlhf_train");
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] RLHF training failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleDistillStep = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke("duanyan_distill_step");
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] Distillation failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleLoraTrain = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke("duanyan_lora_train");
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] LoRA training failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleLoraMerge = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke("duanyan_lora_merge");
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] LoRA merge failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleMoeForward = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const text = genSeed || "hello world";
      const result = await invoke("duanyan_moe_forward", { text });
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] MoE forward failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleMoeStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke("duanyan_moe_stats");
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] MoE stats failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleOnnxExport = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke("duanyan_onnx_export");
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] ONNX export failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleSparseAttn = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const text = genSeed || "hello world";
      const result = await invoke("duanyan_sparse_attention", { text, pattern: "window" });
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] Sparse attention failed: " + String(e));
    }
    setIsProcessing(false);
  };


  const handleFlashReport = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke("duanyan_flash_report");
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] Flash report failed: " + String(e));
    }
    setIsProcessing(false);
  };
  const handleFlashAttn = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const text = genSeed || "hello world";
      const result = await invoke("duanyan_flash_attention", { text, blockSize: 8 });
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] Flash attention failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleSpecDecode = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const text = genSeed || "hello";
      const result = await invoke("duanyan_speculative_generate", { text, maxLen: 20 });
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] Speculative decoding failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleSpecStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke("duanyan_spec_stats");
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] Spec stats failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleRoPE = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const text = genSeed || "hello world";
      const result = await invoke("duanyan_rope_attention", { text });
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] RoPE attention failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleRoPEReport = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke("duanyan_rope_report");
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] RoPE report failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleMerge = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke("duanyan_model_merge", { strategy: "slerp" });
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] Model merge failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleMultimodal = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const text = genSeed || "hello world";
      const result = await invoke("duanyan_multimodal", { text });
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] Multimodal failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleMultimodalStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke("duanyan_multimodal_stats");
      addMessage("duanyan", result);
    } catch (e) {
      addMessage("duanyan", "[error] Multimodal stats failed: " + String(e));
    }
    setIsProcessing(false);
  };

  const handleGQA = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const text = genSeed || 'hello world';
      const result = await invoke('duanyan_gqa_forward', { text });
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] GQA failed: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleGQAReport = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_gqa_report');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] GQA report failed: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleActCache = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const text = genSeed || 'hello world';
      const result = await invoke('duanyan_act_cache_forward', { text });
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] ActCache failed: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleActCacheStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_act_cache_stats');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] ActCache stats failed: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleICL = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const text = genSeed || 'hello world';
      const result = await invoke('duanyan_icl_query', { text });
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] ICL failed: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleICLStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_icl_stats');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] ICL stats failed: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleModForward = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_mod_forward', { text: input });
      addMessage('user', input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] MoD forward: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleModReport = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_mod_report');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] MoD report: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleRagIndex = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_rag_index', { text: input });
      addMessage('user', '[RAG Index] ' + input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] RAG index: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleRagQuery = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_rag_query', { text: input });
      addMessage('user', '[RAG Query] ' + input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] RAG query: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleContrastiveTrain = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_contrastive_train', { text: input });
      addMessage('user', '[Contrastive] ' + input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] Contrastive train: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleContrastive2Stats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_contrastive2_stats');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] Contrastive stats: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleRMSNormForward = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_rmsnorm_forward', { text: input });
      addMessage('user', input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] RMSNorm: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleRMSNormReport = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_rmsnorm_report');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] RMSNorm report: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleSwiGLUForward = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_swiglu_forward', { text: input });
      addMessage('user', input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] SwiGLU: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleSwiGLUReport = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_swiglu_report');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] SwiGLU report: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleBeamSearch = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_beam_search_decode', { text: input });
      addMessage('user', '[Beam] ' + input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] Beam Search: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleBeamSearchStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_beam_search_stats');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] Beam stats: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleSampleText = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_sample_text', { text: input });
      addMessage('user', '[Sample] ' + input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] Sampling: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleSamplingConfig = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_sampling_config');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] Config: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleLRStep = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_lr_step');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] LR step: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleLRCurve = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_lr_curve');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] LR curve: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleALiBiForward = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_alibi_forward', { text: input });
      addMessage('user', '[ALiBi] ' + input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] ALiBi: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleALiBiReport = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_alibi_report');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] ALiBi report: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleGradClipDemo = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_grad_clip_demo');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] GradClip: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleGradClipStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_grad_clip_stats');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] GradClip stats: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleAdamSimulate = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_adam_simulate', { steps: 100 });
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] Adam: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleAdamStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_adam_stats');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] Adam stats: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleTokenMerge = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_token_merge', { text: input });
      addMessage('user', '[ToMe] ' + input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] TokenMerge: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleTokenMergeStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_token_merge_stats');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] ToMe stats: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleWeightTyingDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_weight_tying_demo', { text: input });
      addMessage('user', '[WT] ' + input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] WeightTying: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleWeightTyingStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_weight_tying_stats');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] WT stats: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleCrossAttnDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_cross_attn_demo', { text: input });
      addMessage('user', '[CrossAttn] ' + input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] CrossAttn: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleCrossAttnStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_cross_attn_stats');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] CrossAttn stats: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handlePerplexityEval = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_perplexity_eval', { text: input });
      addMessage('user', '[PPL] ' + input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] Perplexity: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handlePerplexityReport = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_perplexity_report');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] PPL report: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleRepPenaltyDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_rep_penalty_demo', { text: input });
      addMessage('user', '[RepPenalty] ' + input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] RepPenalty: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleRepPenaltyStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_rep_penalty_stats');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] RepPenalty stats: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleLayerNormDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_layer_norm_demo', { text: input });
      addMessage('user', '[LayerNorm] ' + input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] LayerNorm: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleLayerNormCompare = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_layer_norm_compare');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] LayerNorm compare: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleStreamingAttnDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_streaming_attn_demo', { text: input });
      addMessage('user', '[StreamAttn] ' + input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] StreamAttn: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleStreamingAttnCompare = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_streaming_attn_compare');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] StreamAttn compare: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handlePosInterpDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_pos_interp_demo', { text: input });
      addMessage('user', '[PosInterp] ' + input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] PosInterp: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handlePosInterpCompare = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_pos_interp_compare');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] PosInterp compare: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleGradCkptDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_grad_ckpt_demo', { text: input });
      addMessage('user', '[GradCkpt] ' + input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] GradCkpt: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleGradCkptCompare = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_grad_ckpt_compare');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] GradCkpt compare: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handlePromptTmplDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_prompt_tmpl_demo', { text: input });
      addMessage('user', '[Prompt] ' + input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] Prompt: ' + String(e));
    }
    setIsProcessing(false);
  };


  const handleKvQuantDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_kv_quant_demo', { text: input }); addMessage('user', '[KVQuant] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] KVQuant: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleKvQuantStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_kv_quant_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] KVQuant stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handlePromptTmplCompare = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_prompt_tmpl_compare', { text: input });
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] Prompt compare: ' + String(e));
    }
    setIsProcessing(false);
  };



  const handleSeqPackDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_seq_pack_demo', { text: input });
      addMessage('user', '[SeqPack] ' + input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] SeqPack: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleSeqPackCompare = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_seq_pack_compare');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] SeqPack compare: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleAttnVisDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_attn_vis_demo', { text: input });
      addMessage('user', '[AttnVis] ' + input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] AttnVis: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleAttnVisStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_attn_vis_stats');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] AttnVis stats: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleMqaDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_mqa_demo', { text: input });
      addMessage('user', '[MQA] ' + input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] MQA: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleMqaStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_mqa_stats');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] MQA stats: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleTempAnnealDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_temp_anneal_demo', { text: input });
      addMessage('user', '[TempAnneal] ' + input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] TempAnneal: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleTempAnnealCompare = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_temp_anneal_compare');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] TempAnneal compare: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleEarlyStopDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_early_stop_demo', { text: input });
      addMessage('user', '[EarlyStop] ' + input);
      addMessage('duanyan', result);
      setInput('');
    } catch (e) {
      addMessage('duanyan', '[error] EarlyStop: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleEarlyStopStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try {
      const result = await invoke('duanyan_early_stop_stats');
      addMessage('duanyan', result);
    } catch (e) {
      addMessage('duanyan', '[error] EarlyStop stats: ' + String(e));
    }
    setIsProcessing(false);
  };

  const handleGradAccumDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_grad_accum_demo', { text: input }); addMessage('user', '[GradAccum] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] GradAccum: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleGradAccumStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_grad_accum_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSwaDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_swa_demo', { text: input }); addMessage('user', '[SWA] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] SWA: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSwaCompare = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_swa_compare')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleTokenFreqDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_token_freq_demo', { text: input }); addMessage('user', '[TokFreq] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] TokFreq: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleTokenFreqStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_token_freq_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };

  const handleDropoutDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_dropout_demo', { text: input }); addMessage('user', '[Dropout] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Dropout: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleDropoutStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_dropout_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleWeightInitDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_weight_init_demo', { text: input }); addMessage('user', '[WInit] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] WInit: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleWeightInitStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_weight_init_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleBatchNormDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_batch_norm_demo', { text: input }); addMessage('user', '[BatchNorm] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] BatchNorm: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleBatchNormCompare = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_batch_norm_compare')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };

  const handleMixupDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_mixup_demo', { text: input }); addMessage('user', '[MixUp] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] MixUp: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleMixupStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_mixup_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleFocalLossDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_focal_loss_demo', { text: input }); addMessage('user', '[FocalLoss] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] FocalLoss: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleFocalLossStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_focal_loss_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleStochDepthDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_stoch_depth_demo', { text: input }); addMessage('user', '[StochDepth] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] StochDepth: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleStochDepthStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_stoch_depth_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };

  const handleLabelSmoothDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_label_smooth_demo', { text: input }); addMessage('user', '[LSmooth] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] LSmooth: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleLabelSmoothStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_label_smooth_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handlePruningDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_pruning_demo', { text: input }); addMessage('user', '[Pruning] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Pruning: ' + String(e)); }
    setIsProcessing(false);
  };
  const handlePruningStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_pruning_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleGradNormDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_grad_norm_demo', { text: input }); addMessage('user', '[GradNorm] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] GradNorm: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleGradNormStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_grad_norm_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };

  const handleWarmupDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_warmup_demo', { text: input }); addMessage('user', '[Warmup] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Warmup: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleWarmupStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_warmup_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleEmaDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ema_demo', { text: input }); addMessage('user', '[EMA] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] EMA: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleEmaStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_ema_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSpectralNormDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_spectral_norm_demo', { text: input }); addMessage('user', '[SpecNorm] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] SpecNorm: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSpectralNormStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_spectral_norm_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };

  const handleNtkRopeDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ntk_rope_demo', { text: input }); addMessage('user', '[NTK-RoPE] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] NTK-RoPE: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleNtkRopeStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_ntk_rope_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleMultiTaskDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_multi_task_demo', { text: input }); addMessage('user', '[MTL] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] MTL: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleMultiTaskStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_multi_task_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleWassersteinDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_wasserstein_demo', { text: input }); addMessage('user', '[Wasserstein] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Wasserstein: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleWassersteinStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_wasserstein_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };

  const handleSelfConsistencyDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_self_consistency_demo', { text: input }); addMessage('user', '[SC-Dec] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] SC: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSelfConsistencyStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_self_consistency_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleContrastiveDecodingDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_contrastive_decoding_demo', { text: input }); addMessage('user', '[CD] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] CD: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleContrastiveDecodingStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_contrastive_decoding_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleTokenUnlearningDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_token_unlearning_demo', { text: input }); addMessage('user', '[Unlearn] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Unlearn: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleTokenUnlearningStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_token_unlearning_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };

  const handlePagedAttnDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_paged_attn_demo', { text: input }); addMessage('user', '[PagedAttn] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] PagedAttn: ' + String(e)); }
    setIsProcessing(false);
  };
  const handlePagedAttnStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_paged_attn_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSpecRejectionDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_spec_rejection_demo', { text: input }); addMessage('user', '[SpecRej] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] SpecRej: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSpecRejectionStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_spec_rejection_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleContBatchDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_dyn_batch_demo', { text: input }); addMessage('user', '[ContBatch] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] ContBatch: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleContBatchStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_dyn_batch_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };

  const handleGptqDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_gptq_demo', { text: input }); addMessage('user', '[GPTQ] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] GPTQ: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleGptqStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_gptq_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleBleuDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_bleu_demo', { text: input }); addMessage('user', '[BLEU] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] BLEU: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleBleuStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_bleu_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCotDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_cot_demo', { text: input }); addMessage('user', '[CoT] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] CoT: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCotStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_cot_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };

  const handleInstNormDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_inst_norm_demo', { text: input }); addMessage('user', '[InstNorm] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] InstNorm: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleInstNormStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_inst_norm_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleRougeDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_rouge_demo', { text: input }); addMessage('user', '[ROUGE] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] ROUGE: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleRougeStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_rouge_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleTotDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_tot_demo', { text: input }); addMessage('user', '[ToT] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] ToT: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleTotStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_tot_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };

  const handleKtoDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_kto_demo', { text: input }); addMessage('user', '[KTO] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] KTO: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleKtoStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_kto_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleModRouterDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_mod_router_demo', { text: input }); addMessage('user', '[MoD] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] MoD: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleModRouterStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_mod_router_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSparseMoEDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_sparse_moe_demo', { text: input }); addMessage('user', '[SMoE] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] SMoE: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSparseMoEStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_sparse_moe_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };

  const handleBpeDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_bpe_demo', { text: input }); addMessage('user', '[BPE] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] BPE: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleBpeStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_bpe_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleDistDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_dist_demo', { text: input }); addMessage('user', '[Dist] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Dist: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleDistStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_dist_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleRaftDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_raft_demo', { text: input }); addMessage('user', '[RAFT] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] RAFT: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleRaftStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_raft_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };

  const handleGroupNormDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_group_norm_demo', { text: input }); addMessage('user', '[GroupNorm] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] GroupNorm: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleGroupNormStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_group_norm_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleKVEvictDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_kv_eviction_demo', { text: input }); addMessage('user', '[KVEvict] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] KVEvict: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleKVEvictStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_kv_eviction_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleBenchDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_bench_demo', { text: input }); addMessage('user', '[Benchmark] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Benchmark: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleBenchStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_bench_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };

  const handleDataAugDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_data_aug_demo', { text: input }); addMessage('user', '[DataAug] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] DataAug: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleDataAugStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_data_aug_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCurriculumDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_curriculum_demo', { text: input }); addMessage('user', '[Curriculum] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Curriculum: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCurriculumStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_curriculum_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleWatermarkDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_watermark_demo', { text: input }); addMessage('user', '[Watermark] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Watermark: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleWatermarkStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_watermark_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };

  const handleRewardDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_reward_demo', { text: input }); addMessage('user', '[Reward] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Reward: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleRewardStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_reward_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleAdvTrainDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_adv_train_demo', { text: input }); addMessage('user', '[AdvTrain] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] AdvTrain: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleAdvTrainStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_adv_train_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleNASDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_nas_demo', { text: input }); addMessage('user', '[NAS] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] NAS: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleNASStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_nas_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };

  const handleFedDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_federated_demo', { text: input }); addMessage('user', '[Federated] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Federated: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleFedStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_federated_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleKGDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_kg_demo', { text: input }); addMessage('user', '[KG] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] KG: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleKGStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_kg_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handlePromptTuneDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_prompt_tune_demo', { text: input }); addMessage('user', '[PromptTune] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] PromptTune: ' + String(e)); }
    setIsProcessing(false);
  };
  const handlePromptTuneStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_prompt_tune_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };

  const handleConstitutDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_constitut_demo', { text: input }); addMessage('user', '[Constitutional] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Constitutional: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleConstitutStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_constitut_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleActiveLearnDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_active_learn_demo', { text: input }); addMessage('user', '[ActiveLearn] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] ActiveLearn: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleActiveLearnStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_active_learn_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleMMFusionDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_mm_fusion_demo', { text: input }); addMessage('user', '[MMFusion] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] MMFusion: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleMMFusionStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_mm_fusion_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };

  const handleMetaLearnDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_meta_learn_demo', { text: input }); addMessage('user', '[MetaLearn] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] MetaLearn: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleMetaLearnStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_meta_learn_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleContinualDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_continual_demo', { text: input }); addMessage('user', '[Continual] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Continual: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleContinualStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_continual_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSparseGateDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_sparse_gate_demo', { text: input }); addMessage('user', '[SparseGate] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] SparseGate: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSparseGateStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_sparse_gate_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };

  const handleInstTuneDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_inst_tune_demo', { text: input }); addMessage('user', '[InstTune] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] InstTune: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleInstTuneStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_inst_tune_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCovDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_cov_demo', { text: input }); addMessage('user', '[CoVerif] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] CoVerif: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCovStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_cov_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCompressDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_compress_demo', { text: input }); addMessage('user', '[Compress] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Compress: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCompressStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_compress_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };

  const handleSelfPlayDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_self_play_demo', { text: input }); addMessage('user', '[SelfPlay] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] SelfPlay: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSelfPlayStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_self_play_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleIPODemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ipo_demo', { text: input }); addMessage('user', '[IPO] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] IPO: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleIPOStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_ipo_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleEnsembleDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ensemble_demo', { text: input }); addMessage('user', '[Ensemble] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Ensemble: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleEnsembleStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_ensemble_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };

  const handleContrastive2Demo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_contrastive2_demo', { text: input }); addMessage('user', '[Contrastive] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Contrastive: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleRagPipelineDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_rag_pipeline_demo', { text: input }); addMessage('user', '[RAG] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] RAG: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleRagPipelineStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_rag_pipeline_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };
  const handleLoraAdapterDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_lora_adapter_demo', { text: input }); addMessage('user', '[LoRA] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] LoRA: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleLoraAdapterStats = async () => {
    if (isProcessing) return; setIsProcessing(true);
    try { addMessage('duanyan', await invoke('duanyan_lora_adapter_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
    setIsProcessing(false);
  };

  const handleGnnDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_gnn_demo', { text: input }); addMessage('user', '[GNN] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] GNN: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleGnnStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_gnn_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] GNN stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleDiffusionDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_diffusion_demo', { text: input }); addMessage('user', '[Diffusion] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Diffusion: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleDiffusionStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_diffusion_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Diffusion stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCausalDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_causal_demo', { text: input }); addMessage('user', '[Causal] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Causal: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCausalStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_causal_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Causal stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleVaeDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_vae_demo', { text: input }); addMessage('user', '[VAE] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] VAE: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleVaeStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_vae_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] VAE stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleBnnDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_bnn_demo', { text: input }); addMessage('user', '[BNN] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] BNN: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleBnnStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_bnn_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] BNN stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleNodeDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_node_demo', { text: input }); addMessage('user', '[Neural ODE] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Neural ODE: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleNodeStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_node_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Neural ODE stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleReservoirDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_reservoir_demo', { text: input }); addMessage('user', '[Reservoir] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Reservoir: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleReservoirStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_reservoir_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Reservoir stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCapsuleDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_capsule_demo', { text: input }); addMessage('user', '[Capsule] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Capsule: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCapsuleStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_capsule_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Capsule stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleEbmDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ebm_demo', { text: input }); addMessage('user', '[EBM] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] EBM: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleEbmStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ebm_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] EBM stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleHypernetDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_hypernet_demo', { text: input }); addMessage('user', '[Hypernet] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Hypernet: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleHypernetStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_hypernet_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Hypernet stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleFlowDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_flow_demo', { text: input }); addMessage('user', '[Flow] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Flow: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleFlowStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_flow_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Flow stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSnnDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_snn_demo', { text: input }); addMessage('user', '[SNN] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] SNN: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSnnStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_snn_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] SNN stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleWorldModelDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_world_model_demo', { text: input }); addMessage('user', '[WorldModel] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] WorldModel: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleWorldModelStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_world_model_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] WorldModel stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleNeuroSymDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_neuro_sym_demo', { text: input }); addMessage('user', '[NeuroSym] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] NeuroSym: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleNeuroSymStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_neuro_sym_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] NeuroSym stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSparseAeDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_sparse_ae_demo', { text: input }); addMessage('user', '[SparseAE] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] SparseAE: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSparseAeStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_sparse_ae_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] SparseAE stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleMemNetDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_mem_net_demo', { text: input }); addMessage('user', '[MemNet] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] MemNet: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleMemNetStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_mem_net_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] MemNet stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCbmDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_cbm_demo', { text: input }); addMessage('user', '[CBM] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] CBM: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCbmStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_cbm_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] CBM stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleHrrDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_hrr_demo', { text: input }); addMessage('user', '[HRR] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] HRR: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleHrrStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_hrr_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] HRR stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleLsmDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_lsm_demo', { text: input }); addMessage('user', '[LSM] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] LSM: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleLsmStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_lsm_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] LSM stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleNpsDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_nps_demo', { text: input }); addMessage('user', '[NPS] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] NPS: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleNpsStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_nps_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] NPS stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleNeuroCoreDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_neuro_core_demo', { text: input }); addMessage('user', '[NeuroCore] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] NeuroCore: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleNeuroCoreStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_neuro_core_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] NeuroCore stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleModelMergeDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ckpt_merge_demo', { text: input }); addMessage('user', '[Merge] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Merge: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleModelMergeStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ckpt_merge_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Merge stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleTtcDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ttc_demo', { text: input }); addMessage('user', '[TTC] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] TTC: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleTtcStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ttc_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] TTC stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleKdDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_kd_demo', { text: input }); addMessage('user', '[KD] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] KD: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleKdStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_kd_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] KD stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleSpecDecodeDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_draft_verify_demo', { text: input }); addMessage('user', '[SpecDec] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] SpecDec: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSpecDecodeStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_draft_verify_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] SpecDec stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleActPatchDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_act_patch_demo', { text: input }); addMessage('user', '[ActPatch] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] ActPatch: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleActPatchStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_act_patch_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] ActPatch stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSerDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ser_demo', { text: input }); addMessage('user', '[SER] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] SER: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSerStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ser_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] SER stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleMechInterpDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_mech_interp_demo', { text: input }); addMessage('user', '[MechInterp] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] MechInterp: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleMechInterpStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_mech_interp_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] MechInterp stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSymRegDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_sym_reg_demo', { text: input }); addMessage('user', '[SymReg] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] SymReg: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSymRegStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_sym_reg_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] SymReg stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleReasonTraceDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_reason_trace_demo', { text: input }); addMessage('user', '[ReasonTrace] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] ReasonTrace: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleReasonTraceStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_reason_trace_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] ReasonTrace stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleDpoDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_dpo_demo', { text: input }); addMessage('user', '[DPO] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] DPO: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleDpoStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_dpo_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] DPO stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleUqDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_uq_demo', { text: input }); addMessage('user', '[UQ] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] UQ: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleUqStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_uq_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] UQ stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleDataValDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_data_val_demo', { text: input }); addMessage('user', '[DataVal] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] DataVal: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleDataValStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_data_val_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] DataVal stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleMooDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_moo_demo', { text: input }); addMessage('user', '[MOO] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] MOO: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleMooStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_moo_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] MOO stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleTaskVecDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_task_vec_demo', { text: input }); addMessage('user', '[TaskVec] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] TaskVec: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleTaskVecStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_task_vec_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] TaskVec stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handlePromptOptDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_prompt_opt_demo', { text: input }); addMessage('user', '[PromptOpt] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] PromptOpt: ' + String(e)); }
    setIsProcessing(false);
  };
  const handlePromptOptStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_prompt_opt_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] PromptOpt stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleOnlineLearnDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_online_learn_demo', { text: input }); addMessage('user', '[Online] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Online: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleOnlineLearnStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_online_learn_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Online stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleBanditDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_bandit_demo', { text: input }); addMessage('user', '[Bandit] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Bandit: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleBanditStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_bandit_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Bandit stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleRewardShapeDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_reward_shape_demo', { text: input }); addMessage('user', '[RewShape] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] RewShape: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleRewardShapeStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_reward_shape_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] RewShape stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleActiveInfDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_active_inf_demo', { text: input }); addMessage('user', '[ActInf] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] ActiveInf: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleActiveInfStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_active_inf_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] ActiveInf stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleFedPersonalDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_fed_personal_demo', { text: input }); addMessage('user', '[FedPers] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] FedPers: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleFedPersonalStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_fed_personal_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] FedPers stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleModelCompDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_model_comp_demo', { text: input }); addMessage('user', '[ModComp] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] ModComp: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleModelCompStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_model_comp_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] ModComp stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleBayesOptDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_bayes_opt_demo', { text: input }); addMessage('user', '[BayesOpt] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] BayesOpt: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleBayesOptStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_bayes_opt_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] BayesOpt stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleDomainAdaptDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_domain_adapt_demo', { text: input }); addMessage('user', '[DomAdapt] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] DomAdapt: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleDomainAdaptStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_domain_adapt_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] DomAdapt stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSslDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ssl_demo', { text: input }); addMessage('user', '[SSL] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] SSL: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSslStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ssl_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] SSL stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleTransferDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_transfer_demo', { text: input }); addMessage('user', '[Transfer] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Transfer: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleTransferStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_transfer_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Transfer stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleFewShotDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_few_shot_demo', { text: input }); addMessage('user', '[FewShot] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] FewShot: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleFewShotStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_few_shot_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] FewShot stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleMetaRlDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_meta_rl_demo', { text: input }); addMessage('user', '[MetaRL] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] MetaRL: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleMetaRlStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_meta_rl_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] MetaRL stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleZeroShotDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_zero_shot_demo', { text: input }); addMessage('user', '[ZeroShot] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] ZeroShot: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleZeroShotStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_zero_shot_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] ZeroShot stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleTaskAdaptDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_task_adapt_demo', { text: input }); addMessage('user', '[TaskAdapt] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] TaskAdapt: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleTaskAdaptStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_task_adapt_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] TaskAdapt stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleRepMixupDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_rep_mixup_demo', { text: input }); addMessage('user', '[Mixup] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Mixup: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleRepMixupStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_rep_mixup_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Mixup stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleWeightShareDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_weight_share_demo', { text: input }); addMessage('user', '[WeightShare] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] WeightShare: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleWeightShareStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_weight_share_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] WeightShare stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleDisentangleDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_disentangle_demo', { text: input }); addMessage('user', '[Disentangle] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Disentangle: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleDisentangleStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_disentangle_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Disentangle stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleGradSurgeryDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_grad_surgery_demo', { text: input }); addMessage('user', '[GradSurgery] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] GradSurgery: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleGradSurgeryStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_grad_surgery_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] GradSurgery stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleEvoStratDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_evo_strat_demo', { text: input }); addMessage('user', '[EvoStrat] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] EvoStrat: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleEvoStratStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_evo_strat_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] EvoStrat stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleHyperOptDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_hyper_opt_demo', { text: input }); addMessage('user', '[HyperOpt] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] HyperOpt: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleHyperOptStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_hyper_opt_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] HyperOpt stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleMultiAgentDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_multi_agent_demo', { text: input }); addMessage('user', '[MARL] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] MARL: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleMultiAgentStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_multi_agent_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] MARL stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleImitationDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_imitation_demo', { text: input }); addMessage('user', '[Imitation] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Imitation: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleImitationStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_imitation_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Imitation stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleInverseRlDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_inverse_rl_demo', { text: input }); addMessage('user', '[InvRL] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] InvRL: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleInverseRlStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_inverse_rl_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] InvRL stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleNtkDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ntk_demo', { text: input }); addMessage('user', '[NTK] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] NTK: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleNtkStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ntk_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] NTK stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleOtDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ot_demo', { text: input }); addMessage('user', '[OT] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] OT: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleOtStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ot_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] OT stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleScalingDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_scaling_demo', { text: input }); addMessage('user', '[Scaling] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Scaling: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleScalingStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_scaling_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Scaling stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleModelEditDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_model_edit_demo', { text: input }); addMessage('user', '[ModelEdit] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] ModelEdit: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleModelEditStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_model_edit_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] ModelEdit stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleGrokDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_grok_demo', { text: input }); addMessage('user', '[Grok] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Grok: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleGrokStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_grok_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Grok stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleDoubleDescDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_double_desc_demo', { text: input }); addMessage('user', '[DoubleDesc] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] DoubleDesc: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleDoubleDescStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_double_desc_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] DoubleDesc stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSsmDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ssm_demo', { text: input }); addMessage('user', '[SSM] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] SSM: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSsmStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ssm_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] SSM stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleToolUseDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_tool_use_demo', { text: input }); addMessage('user', '[ToolUse] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] ToolUse: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleToolUseStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_tool_use_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] ToolUse stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSelfRefineDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_self_refine_demo', { text: input }); addMessage('user', '[SelfRefine] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] SelfRefine: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSelfRefineStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_self_refine_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] SelfRefine stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleReasonChainDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_reason_chain_demo', { text: input }); addMessage('user', '[CoT] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] CoT: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleReasonChainStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_reason_chain_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] CoT stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleRedTeamDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_red_team_demo', { text: input }); addMessage('user', '[RedTeam] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] RedTeam: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleRedTeamStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_red_team_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] RedTeam stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSafetyDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_safety_demo', { text: input }); addMessage('user', '[Safety] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Safety: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSafetyStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_safety_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Safety stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleJailbreakDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_jailbreak_demo', { text: input }); addMessage('user', '[Jailbreak] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Jailbreak: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleJailbreakStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_jailbreak_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Jailbreak stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleRingAttnDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ring_attn_demo', { text: input }); addMessage('user', '[RingAttn] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] RingAttn: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleRingAttnStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_ring_attn_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] RingAttn stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleArenaDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_arena_demo', { text: input }); addMessage('user', '[Arena] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Arena: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleArenaStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_arena_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Arena stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSynthDataDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_synth_data_demo', { text: input }); addMessage('user', '[SynthData] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] SynthData: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSynthDataStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_synth_data_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] SynthData stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleOrpoDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_orpo_demo', { text: input }); addMessage('user', '[ORPO] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] ORPO: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleOrpoStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_orpo_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] ORPO stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSimpoDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_simpo_demo', { text: input }); addMessage('user', '[SimPO] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] SimPO: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSimpoStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_simpo_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] SimPO stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleInfiniAttnDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_infini_attn_demo', { text: input }); addMessage('user', '[InfiniAttn] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] InfiniAttn: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleInfiniAttnStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_infini_attn_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] InfiniAttn stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleMedusaDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_medusa_demo', { text: input }); addMessage('user', '[Medusa] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Medusa: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleMedusaStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_medusa_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Medusa stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleEagleDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_eagle_demo', { text: input }); addMessage('user', '[EAGLE] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] EAGLE: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleEagleStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_eagle_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] EAGLE stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleChunkedDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_chunked_demo', { text: input }); addMessage('user', '[Chunked] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Chunked: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleChunkedStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_chunked_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Chunked stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleDoraDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_dora_demo', { text: input }); addMessage('user', '[DoRA] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] DoRA: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleDoraStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_dora_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] DoRA stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleTopKDemo = async () => {
  const handleKvQuantDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_kv_quant_demo', { text: input }); addMessage('user', '[KVQuant] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] KVQuant: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleKvQuantStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_kv_quant_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] KVQuant stats: ' + String(e)); }
    setIsProcessing(false);
  };
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_top_k_demo', { text: input }); addMessage('user', '[TopK] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] TopK: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleTopKStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_top_k_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] TopK stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleHyenaDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_hyena_demo', { text: input }); addMessage('user', '[Hyena] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Hyena: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleHyenaStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_hyena_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Hyena stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleRwkvDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_rwkv_demo', { text: input }); addMessage('user', '[RWKV] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] RWKV: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleRwkvStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_rwkv_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] RWKV stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handlePromptCompDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_prompt_comp_demo', { text: input }); addMessage('user', '[PCompress] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] PCompress: ' + String(e)); }
    setIsProcessing(false);
  };
  const handlePromptCompStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_prompt_comp_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] PCompress stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleFlashMlaDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_flash_mla_demo', { text: input }); addMessage('user', '[FlashMLA] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] FlashMLA: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleFlashMlaStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_flash_mla_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] FlashMLA stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleAgentPlanDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_agent_plan_demo', { text: input }); addMessage('user', '[Agent] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Agent: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleAgentPlanStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_agent_plan_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Agent stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSpecRejectDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_spec_reject_demo', { text: input }); addMessage('user', '[SpecRej] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] SpecRej: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSpecRejectStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_spec_reject_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] SpecRej stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handlePagedKvDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_paged_kv_demo', { text: input }); addMessage('user', '[PagedKV] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] PagedKV: ' + String(e)); }
    setIsProcessing(false);
  };
  const handlePagedKvStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_paged_kv_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] PagedKV stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleAgentMemDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_agent_mem_demo', { text: input }); addMessage('user', '[AgMem] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] AgMem: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleAgentMemStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_agent_mem_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] AgMem stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleDynBatchDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_dyn_batch_demo', { text: input }); addMessage('user', '[Batch] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Batch: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleDynBatchStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_dyn_batch_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Batch stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleVisEncDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_vis_enc_demo', { text: input }); addMessage('user', '[ViT] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] ViT: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleVisEncStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_vis_enc_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] ViT stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleTxt2ImgDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_txt2img_demo', { text: input }); addMessage('user', '[T2I] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] T2I: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleTxt2ImgStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_txt2img_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] T2I stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handlePromCacheDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_prom_cache_demo', { text: input }); addMessage('user', '[PCache] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] PCache: ' + String(e)); }
    setIsProcessing(false);
  };
  const handlePromCacheStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_prom_cache_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] PCache stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleImgCapDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_img_cap_demo', { text: input }); addMessage('user', '[Cap] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Cap: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleImgCapStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_img_cap_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Cap stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleObjDetDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_obj_det_demo', { text: input }); addMessage('user', '[Det] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Det: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleObjDetStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_obj_det_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Det stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSuperResDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_super_res_demo', { text: input }); addMessage('user', '[SR] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] SR: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSuperResStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_super_res_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] SR stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleStyleTransDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_style_trans_demo', { text: input }); addMessage('user', '[Style] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Style: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleStyleTransStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_style_trans_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Style stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleImgInpaintDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_img_inpaint_demo', { text: input }); addMessage('user', '[Inpaint] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Inpaint: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleImgInpaintStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_img_inpaint_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Inpaint stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleAudioEncDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_audio_enc_demo', { text: input }); addMessage('user', '[Audio] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Audio: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleAudioEncStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_audio_enc_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Audio stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleImgSegDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_img_seg_demo', { text: input }); addMessage('user', '[Seg] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Seg: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleImgSegStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_img_seg_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Seg stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleDepthEstDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_depth_est_demo', { text: input }); addMessage('user', '[Depth] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Depth: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleDepthEstStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_depth_est_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Depth stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleOptFlowDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_opt_flow_demo', { text: input }); addMessage('user', '[Flow] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Flow: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleOptFlowStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_opt_flow_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Flow stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handlePoseEstDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_pose_est_demo', { text: input }); addMessage('user', '[Pose] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Pose: ' + String(e)); }
    setIsProcessing(false);
  };
  const handlePoseEstStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_pose_est_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Pose stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSpeechRecogDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_speech_recog_demo', { text: input }); addMessage('user', '[ASR] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] ASR: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSpeechRecogStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_speech_recog_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] ASR stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleMusicGenDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_music_gen_demo', { text: input }); addMessage('user', '[Music] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Music: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleMusicGenStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_music_gen_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Music stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleCodeParserDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_code_parser_demo', { text: input }); addMessage('user', '[Parser] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Parser: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCodeParserStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_code_parser_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Parser stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCodeGenDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_code_gen_demo', { text: input }); addMessage('user', '[CodeGen] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] CodeGen: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCodeGenStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_code_gen_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] CodeGen stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCodeExecDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_code_exec_demo', { text: input }); addMessage('user', '[Exec] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Exec: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCodeExecStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_code_exec_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Exec stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleCodeDebugDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_code_debug_demo', { text: input }); addMessage('user', '[Debug] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Debug: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCodeDebugStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_code_debug_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Debug stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleProgRepairDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_prog_repair_demo', { text: input }); addMessage('user', '[Repair] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Repair: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleProgRepairStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_prog_repair_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Repair stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCodeReviewDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_code_review_demo', { text: input }); addMessage('user', '[Review] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Review: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCodeReviewStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_code_review_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Review stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handleSyntaxHlDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_syntax_hl_demo', { text: input }); addMessage('user', '[HL] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] HL: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleSyntaxHlStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_syntax_hl_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] HL stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCodeCompleteDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_code_complete_demo', { text: input }); addMessage('user', '[Complete] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Complete: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCodeCompleteStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_code_complete_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Complete stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCodeRefactorDemo = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_code_refactor_demo', { text: input }); addMessage('user', '[Refactor] ' + input); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Refactor: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleCodeRefactorStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_code_refactor_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Refactor stats: ' + String(e)); }
    setIsProcessing(false);
  };

  const handlePipelineAnalyze = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_code_pipeline_analyze', { text: input }); addMessage('user', '[Pipeline-Analyze] ' + input.substring(0, 50)); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Pipeline: ' + String(e)); }
    setIsProcessing(false);
  };
  const handlePipelineFull = async () => {
    if (isProcessing || !input.trim()) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_code_pipeline_full', { text: input }); addMessage('user', '[Pipeline-Full] ' + input.substring(0, 50)); addMessage('duanyan', r); setInput(''); }
    catch (e) { addMessage('duanyan', '[error] Pipeline full: ' + String(e)); }
    setIsProcessing(false);
  };
  const handlePipelineStats = async () => {
    if (isProcessing) return;
    setIsProcessing(true);
    try { const r = await invoke('duanyan_code_pipeline_stats'); addMessage('duanyan', r); }
    catch (e) { addMessage('duanyan', '[error] Pipeline stats: ' + String(e)); }
    setIsProcessing(false);
  };
  const handleDataLoaderStats = async () => {
    try { addMessage('duanyan', await invoke('duanyan_data_loader_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
  };
  const handleDataPipelineStats = async () => {
    try { addMessage('duanyan', await invoke('duanyan_data_pipeline_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
  };
  const handleTokenizedDsStats = async () => {
    try { addMessage('duanyan', await invoke('duanyan_tokenized_ds_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
  };
  const handleDistTrainStats = async () => {
    try { addMessage('duanyan', await invoke('duanyan_dist_train_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
  };
  const handleMpStats = async () => {
    try { addMessage('duanyan', await invoke('duanyan_mp_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
  };
  const handleCkptStats = async () => {
    try { addMessage('duanyan', await invoke('duanyan_ckpt_stats')); } catch (e) { addMessage('duanyan', '[error] ' + String(e)); }
  };








































  const handleClear = () => {
    setMessages([{ role: "system", content: "Session cleared.", timestamp: Date.now() }]);
  };

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === "Enter" && !e.shiftKey) {
      e.preventDefault();
      handleSend();
    }
  };

  return (
    <div className="duanyan-panel">
      <div className="duanyan-header">
        <div className="duanyan-header-left">
          <span className="duanyan-brand">duanyan</span>
          <span className="duanyan-version">v5.0</span>
          <span className={"duanyan-status-dot " + (isProcessing ? "thinking" : "idle")}></span>
          <span className="duanyan-status-text">{isProcessing ? "busy" : "ready"}</span>
        </div>
        <div className="duanyan-header-right">
          <button className={"duanyan-header-btn" + (showToolbar ? " active" : "")} onClick={() => setShowToolbar(p => !p)} title="Toggle toolbar">Tools</button>
          <button className="duanyan-header-btn" onClick={handleClear} title="Clear">Clear</button>
          <button className="duanyan-header-btn" onClick={onClose} title="Close">
            <svg width="10" height="10" viewBox="0 0 10 10">
              <line x1="0" y1="0" x2="10" y2="10" stroke="currentColor" strokeWidth="1.2"/>
              <line x1="10" y1="0" x2="0" y2="10" stroke="currentColor" strokeWidth="1.2"/>
            </svg>
          </button>
        </div>
      </div>

      <div className="duanyan-stats">
        <div className="duanyan-stat">
          <span className="stat-label">Model</span>
          <span className="stat-value">Transformer</span>
        </div>
        <div className="duanyan-stat">
          <span className="stat-label">Heads</span>
          <span className="stat-value">4</span>
        </div>
        <div className="duanyan-stat">
          <span className="stat-label">Layers</span>
          <span className="stat-value">2</span>
        </div>
        <div className="duanyan-stat">
          <span className="stat-label">Decoder</span>
          <span className="stat-value">Yes</span>
        </div>
        <div className="duanyan-stat">
          <span className="stat-label">Mode</span>
          <span className="stat-value">{useBackprop ? "BP" : "CLS"}</span>
        </div>
        <div className="duanyan-stat">
          <span className="stat-label">Clock</span>
          <span className="stat-value">{clock}</span>
        </div>
      </div>

      {showToolbar && (
        <div className="duanyan-toolbar">
          <div className="duanyan-toolbar-row">
            <button className="ql-tool-btn" onClick={handleTrain} disabled={isProcessing}>Train</button>
            <input type="number" className="ql-tool-input" value={trainEpochs} onChange={e => setTrainEpochs(Number(e.target.value) || 10)} min={1} max={100} title="Epochs" />
            <button className="ql-tool-btn" onClick={handleReplay} disabled={isProcessing}>Replay</button>
            <button className="ql-tool-btn" onClick={handleStatus} disabled={isProcessing}>Status</button>
          </div>
          <div className="duanyan-toolbar-row">
            <button className="ql-tool-btn" onClick={handleGenerate} disabled={isProcessing}>Generate</button>
            <input className="ql-tool-input ql-seed-input" value={genSeed} onChange={e => setGenSeed(e.target.value)} placeholder="seed" />
            <input type="number" className="ql-tool-input" value={genLen} onChange={e => setGenLen(Number(e.target.value) || 50)} min={10} max={200} title="Max length" />
          </div>
          <div className="duanyan-toolbar-row">
            <button className="ql-tool-btn" onClick={handleGenerateDecoder} disabled={isProcessing}>Decoder</button>
            <input className="ql-tool-input ql-seed-input" value={genSeed} onChange={e => setGenSeed(e.target.value)} placeholder="seed" />
            <input type="number" className="ql-tool-input" value={genLen} onChange={e => setGenLen(Number(e.target.value) || 50)} min={10} max={200} title="Max tokens" />
          </div>
          <div className="duanyan-toolbar-row">
            <button className="ql-tool-btn" onClick={handleGenerateCached} disabled={isProcessing}>KV-Gen</button>
            <button className="ql-tool-btn" onClick={handleTrainCached} disabled={isProcessing}>CacheTrain</button>
            <input className="ql-tool-input ql-seed-input" value={genSeed} onChange={e => setGenSeed(e.target.value)} placeholder="seed" />
          </div>
          <div className="duanyan-toolbar-row">
            <button className="ql-tool-btn" onClick={handleSaveWeights} disabled={isProcessing}>Save W</button>
            <button className="ql-tool-btn" onClick={handleLoadWeights} disabled={isProcessing}>Load W</button>
            <button className={"ql-tool-btn" + (useBackprop ? " active" : "")} onClick={handleToggleBackprop} disabled={isProcessing}>Backprop</button>
            <button className="ql-tool-btn" onClick={handleQuantize} disabled={isProcessing}>Quantize</button>
          </div>
          <div className="duanyan-toolbar-row">
            <button className="ql-tool-btn" onClick={handleExportArch} disabled={isProcessing}>Arch</button>
            <button className="ql-tool-btn" onClick={handleExportJson} disabled={isProcessing}>JSON</button>
            <button className="ql-tool-btn" onClick={handleQuantReport} disabled={isProcessing}>Q-Report</button>
          </div>
          <div className="duanyan-toolbar-row">
            <button className="ql-tool-btn" onClick={handleF16Report} disabled={isProcessing}>F16</button>
            <button className="ql-tool-btn" onClick={handleRlhfTrain} disabled={isProcessing}>RLHF</button>
            <button className="ql-tool-btn" onClick={handleDistillStep} disabled={isProcessing}>Distill</button>
          </div>
          <div className="duanyan-toolbar-row">
            <button className="ql-tool-btn" onClick={handleLoraTrain} disabled={isProcessing}>LoRA</button>
            <button className="ql-tool-btn" onClick={handleLoraMerge} disabled={isProcessing}>Merge</button>
            <button className="ql-tool-btn" onClick={handleMoeForward} disabled={isProcessing}>MoE</button>
            <button className="ql-tool-btn" onClick={handleMoeStats} disabled={isProcessing}>MoE-Stats</button>
            <button className="ql-tool-btn" onClick={handleOnnxExport} disabled={isProcessing}>ONNX</button>
          </div>
          <div className="duanyan-toolbar-row">
            <button className="ql-tool-btn" onClick={handleSparseAttn} disabled={isProcessing}>Sparse</button>
            <button className="ql-tool-btn" onClick={handleFlashAttn} disabled={isProcessing}>Flash</button>
            <button className="ql-tool-btn" onClick={handleFlashReport} disabled={isProcessing}>Flash-R</button>
            <button className="ql-tool-btn" onClick={handleSpecDecode} disabled={isProcessing}>SpecDec</button>
            <button className="ql-tool-btn" onClick={handleSpecStats} disabled={isProcessing}>Spec-Stat</button>
          </div>
          <div className="duanyan-toolbar-row">
            <button className="ql-tool-btn" onClick={handleRoPE} disabled={isProcessing}>RoPE</button>
            <button className="ql-tool-btn" onClick={handleRoPEReport} disabled={isProcessing}>RoPE-R</button>
            <button className="ql-tool-btn" onClick={handleMerge} disabled={isProcessing}>Merge</button>
            <button className="ql-tool-btn" onClick={handleMultimodal} disabled={isProcessing}>Vision</button>
            <button className="ql-tool-btn" onClick={handleMultimodalStats} disabled={isProcessing}>MM-Stat</button>
          </div>
          <div className="duanyan-toolbar-row">
            <button className="ql-tool-btn" onClick={handleGQA} disabled={isProcessing}>GQA</button>
            <button className="ql-tool-btn" onClick={handleGQAReport} disabled={isProcessing}>GQA-R</button>
            <button className="ql-tool-btn" onClick={handleActCache} disabled={isProcessing}>ActCache</button>
            <button className="ql-tool-btn" onClick={handleActCacheStats} disabled={isProcessing}>ActCache-S</button>
            <button className="ql-tool-btn" onClick={handleICL} disabled={isProcessing}>ICL</button>
            <button className="ql-tool-btn" onClick={handleICLStats} disabled={isProcessing}>ICL-Stat</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleModForward} disabled={isProcessing}>MoD</button>
            <button className="ql-tool-btn" onClick={handleModReport} disabled={isProcessing}>MoD-R</button>
            <button className="ql-tool-btn" onClick={handleRagIndex} disabled={isProcessing}>RAG-Idx</button>
            <button className="ql-tool-btn" onClick={handleRagQuery} disabled={isProcessing}>RAG-Q</button>
            <button className="ql-tool-btn" onClick={handleContrastiveTrain} disabled={isProcessing}>Contrast</button>
            <button className="ql-tool-btn" onClick={handleContrastive2Stats} disabled={isProcessing}>Contrast-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleRMSNormForward} disabled={isProcessing}>RMSN</button>
            <button className="ql-tool-btn" onClick={handleRMSNormReport} disabled={isProcessing}>RMSN-R</button>
            <button className="ql-tool-btn" onClick={handleSwiGLUForward} disabled={isProcessing}>SwiGLU</button>
            <button className="ql-tool-btn" onClick={handleSwiGLUReport} disabled={isProcessing}>SwiGLU-R</button>
            <button className="ql-tool-btn" onClick={handleBeamSearch} disabled={isProcessing}>Beam</button>
            <button className="ql-tool-btn" onClick={handleBeamSearchStats} disabled={isProcessing}>Beam-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleSampleText} disabled={isProcessing}>Sample</button>
            <button className="ql-tool-btn" onClick={handleSamplingConfig} disabled={isProcessing}>Sample-C</button>
            <button className="ql-tool-btn" onClick={handleLRStep} disabled={isProcessing}>LR-Step</button>
            <button className="ql-tool-btn" onClick={handleLRCurve} disabled={isProcessing}>LR-Curve</button>
            <button className="ql-tool-btn" onClick={handleALiBiForward} disabled={isProcessing}>ALiBi</button>
            <button className="ql-tool-btn" onClick={handleALiBiReport} disabled={isProcessing}>ALiBi-R</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleGradClipDemo} disabled={isProcessing}>GClip</button>
            <button className="ql-tool-btn" onClick={handleGradClipStats} disabled={isProcessing}>GClip-S</button>
            <button className="ql-tool-btn" onClick={handleAdamSimulate} disabled={isProcessing}>Adam</button>
            <button className="ql-tool-btn" onClick={handleAdamStats} disabled={isProcessing}>Adam-S</button>
            <button className="ql-tool-btn" onClick={handleTokenMerge} disabled={isProcessing}>ToMe</button>
            <button className="ql-tool-btn" onClick={handleTokenMergeStats} disabled={isProcessing}>ToMe-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleWeightTyingDemo} disabled={isProcessing}>WTie</button>
            <button className="ql-tool-btn" onClick={handleWeightTyingStats} disabled={isProcessing}>WTie-S</button>
            <button className="ql-tool-btn" onClick={handleCrossAttnDemo} disabled={isProcessing}>CrAttn</button>
            <button className="ql-tool-btn" onClick={handleCrossAttnStats} disabled={isProcessing}>CrAttn-S</button>
            <button className="ql-tool-btn" onClick={handlePerplexityEval} disabled={isProcessing}>PPL</button>
            <button className="ql-tool-btn" onClick={handlePerplexityReport} disabled={isProcessing}>PPL-R</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleRepPenaltyDemo} disabled={isProcessing}>RepPen</button>
            <button className="ql-tool-btn" onClick={handleRepPenaltyStats} disabled={isProcessing}>RepPen-S</button>
            <button className="ql-tool-btn" onClick={handleLayerNormDemo} disabled={isProcessing}>LNorm</button>
            <button className="ql-tool-btn" onClick={handleLayerNormCompare} disabled={isProcessing}>LNorm-C</button>
            <button className="ql-tool-btn" onClick={handleStreamingAttnDemo} disabled={isProcessing}>StrAttn</button>
            <button className="ql-tool-btn" onClick={handleStreamingAttnCompare} disabled={isProcessing}>StrAttn-C</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handlePosInterpDemo} disabled={isProcessing}>PI</button>
            <button className="ql-tool-btn" onClick={handlePosInterpCompare} disabled={isProcessing}>PI-C</button>
            <button className="ql-tool-btn" onClick={handleGradCkptDemo} disabled={isProcessing}>GCkpt</button>
            <button className="ql-tool-btn" onClick={handleGradCkptCompare} disabled={isProcessing}>GCkpt-C</button>
            <button className="ql-tool-btn" onClick={handlePromptTmplDemo} disabled={isProcessing}>Prompt</button>
            <button className="ql-tool-btn" onClick={handlePromptTmplCompare} disabled={isProcessing}>Prompt-C</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleKvQuantDemo} disabled={isProcessing}>KVQ</button>
            <button className="ql-tool-btn" onClick={handleKvQuantStats} disabled={isProcessing}>KVQ-S</button>
            <button className="ql-tool-btn" onClick={handleSeqPackDemo} disabled={isProcessing}>SPack</button>
            <button className="ql-tool-btn" onClick={handleSeqPackCompare} disabled={isProcessing}>SPack-C</button>
            <button className="ql-tool-btn" onClick={handleAttnVisDemo} disabled={isProcessing}>AttnVis</button>
            <button className="ql-tool-btn" onClick={handleAttnVisStats} disabled={isProcessing}>AttnVis-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleMqaDemo} disabled={isProcessing}>MQA</button>
            <button className="ql-tool-btn" onClick={handleMqaStats} disabled={isProcessing}>MQA-S</button>
            <button className="ql-tool-btn" onClick={handleTempAnnealDemo} disabled={isProcessing}>TAnnl</button>
            <button className="ql-tool-btn" onClick={handleTempAnnealCompare} disabled={isProcessing}>TAnnl-C</button>
            <button className="ql-tool-btn" onClick={handleEarlyStopDemo} disabled={isProcessing}>ES</button>
            <button className="ql-tool-btn" onClick={handleEarlyStopStats} disabled={isProcessing}>ES-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleGradAccumDemo} disabled={isProcessing}>GAccum</button>
            <button className="ql-tool-btn" onClick={handleGradAccumStats} disabled={isProcessing}>GAccum-S</button>
            <button className="ql-tool-btn" onClick={handleSwaDemo} disabled={isProcessing}>SWA</button>
            <button className="ql-tool-btn" onClick={handleSwaCompare} disabled={isProcessing}>SWA-C</button>
            <button className="ql-tool-btn" onClick={handleTokenFreqDemo} disabled={isProcessing}>TokFreq</button>
            <button className="ql-tool-btn" onClick={handleTokenFreqStats} disabled={isProcessing}>TokFreq-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleDropoutDemo} disabled={isProcessing}>Drop</button>
            <button className="ql-tool-btn" onClick={handleDropoutStats} disabled={isProcessing}>Drop-S</button>
            <button className="ql-tool-btn" onClick={handleWeightInitDemo} disabled={isProcessing}>WInit</button>
            <button className="ql-tool-btn" onClick={handleWeightInitStats} disabled={isProcessing}>WInit-S</button>
            <button className="ql-tool-btn" onClick={handleBatchNormDemo} disabled={isProcessing}>BNorm</button>
            <button className="ql-tool-btn" onClick={handleBatchNormCompare} disabled={isProcessing}>BNorm-C</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleMixupDemo} disabled={isProcessing}>MixUp</button>
            <button className="ql-tool-btn" onClick={handleMixupStats} disabled={isProcessing}>MixUp-S</button>
            <button className="ql-tool-btn" onClick={handleFocalLossDemo} disabled={isProcessing}>FLoss</button>
            <button className="ql-tool-btn" onClick={handleFocalLossStats} disabled={isProcessing}>FLoss-S</button>
            <button className="ql-tool-btn" onClick={handleStochDepthDemo} disabled={isProcessing}>SDepth</button>
            <button className="ql-tool-btn" onClick={handleStochDepthStats} disabled={isProcessing}>SDepth-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleLabelSmoothDemo} disabled={isProcessing}>LSmooth</button>
            <button className="ql-tool-btn" onClick={handleLabelSmoothStats} disabled={isProcessing}>LSmooth-S</button>
            <button className="ql-tool-btn" onClick={handlePruningDemo} disabled={isProcessing}>Prune</button>
            <button className="ql-tool-btn" onClick={handlePruningStats} disabled={isProcessing}>Prune-S</button>
            <button className="ql-tool-btn" onClick={handleGradNormDemo} disabled={isProcessing}>GNorm</button>
            <button className="ql-tool-btn" onClick={handleGradNormStats} disabled={isProcessing}>GNorm-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleWarmupDemo} disabled={isProcessing}>Warmup</button>
            <button className="ql-tool-btn" onClick={handleWarmupStats} disabled={isProcessing}>Warmup-S</button>
            <button className="ql-tool-btn" onClick={handleEmaDemo} disabled={isProcessing}>EMA</button>
            <button className="ql-tool-btn" onClick={handleEmaStats} disabled={isProcessing}>EMA-S</button>
            <button className="ql-tool-btn" onClick={handleSpectralNormDemo} disabled={isProcessing}>SNorm</button>
            <button className="ql-tool-btn" onClick={handleSpectralNormStats} disabled={isProcessing}>SNorm-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleNtkRopeDemo} disabled={isProcessing}>NTK</button>
            <button className="ql-tool-btn" onClick={handleNtkRopeStats} disabled={isProcessing}>NTK-S</button>
            <button className="ql-tool-btn" onClick={handleMultiTaskDemo} disabled={isProcessing}>MTL</button>
            <button className="ql-tool-btn" onClick={handleMultiTaskStats} disabled={isProcessing}>MTL-S</button>
            <button className="ql-tool-btn" onClick={handleWassersteinDemo} disabled={isProcessing}>WDist</button>
            <button className="ql-tool-btn" onClick={handleWassersteinStats} disabled={isProcessing}>WDist-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleSelfConsistencyDemo} disabled={isProcessing}>SC</button>
            <button className="ql-tool-btn" onClick={handleSelfConsistencyStats} disabled={isProcessing}>SC-S</button>
            <button className="ql-tool-btn" onClick={handleContrastiveDecodingDemo} disabled={isProcessing}>CD</button>
            <button className="ql-tool-btn" onClick={handleContrastiveDecodingStats} disabled={isProcessing}>CD-S</button>
            <button className="ql-tool-btn" onClick={handleTokenUnlearningDemo} disabled={isProcessing}>Unlearn</button>
            <button className="ql-tool-btn" onClick={handleTokenUnlearningStats} disabled={isProcessing}>Unlearn-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handlePagedAttnDemo} disabled={isProcessing}>PAttn</button>
            <button className="ql-tool-btn" onClick={handlePagedAttnStats} disabled={isProcessing}>PAttn-S</button>
            <button className="ql-tool-btn" onClick={handleSpecRejectionDemo} disabled={isProcessing}>SpecR</button>
            <button className="ql-tool-btn" onClick={handleSpecRejectionStats} disabled={isProcessing}>SpecR-S</button>
            <button className="ql-tool-btn" onClick={handleContBatchDemo} disabled={isProcessing}>CBatch</button>
            <button className="ql-tool-btn" onClick={handleContBatchStats} disabled={isProcessing}>CBatch-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleGptqDemo} disabled={isProcessing}>GPTQ</button>
            <button className="ql-tool-btn" onClick={handleGptqStats} disabled={isProcessing}>GPTQ-S</button>
            <button className="ql-tool-btn" onClick={handleBleuDemo} disabled={isProcessing}>BLEU</button>
            <button className="ql-tool-btn" onClick={handleBleuStats} disabled={isProcessing}>BLEU-S</button>
            <button className="ql-tool-btn" onClick={handleCotDemo} disabled={isProcessing}>CoT</button>
            <button className="ql-tool-btn" onClick={handleCotStats} disabled={isProcessing}>CoT-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleInstNormDemo} disabled={isProcessing}>INorm</button>
            <button className="ql-tool-btn" onClick={handleInstNormStats} disabled={isProcessing}>INorm-S</button>
            <button className="ql-tool-btn" onClick={handleRougeDemo} disabled={isProcessing}>ROUGE</button>
            <button className="ql-tool-btn" onClick={handleRougeStats} disabled={isProcessing}>ROUGE-S</button>
            <button className="ql-tool-btn" onClick={handleTotDemo} disabled={isProcessing}>ToT</button>
            <button className="ql-tool-btn" onClick={handleTotStats} disabled={isProcessing}>ToT-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleKtoDemo} disabled={isProcessing}>KTO</button>
            <button className="ql-tool-btn" onClick={handleKtoStats} disabled={isProcessing}>KTO-S</button>
            <button className="ql-tool-btn" onClick={handleModRouterDemo} disabled={isProcessing}>MoD</button>
            <button className="ql-tool-btn" onClick={handleModRouterStats} disabled={isProcessing}>MoD-S</button>
            <button className="ql-tool-btn" onClick={handleSparseMoEDemo} disabled={isProcessing}>SMoE</button>
            <button className="ql-tool-btn" onClick={handleSparseMoEStats} disabled={isProcessing}>SMoE-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleBpeDemo} disabled={isProcessing}>BPE</button>
            <button className="ql-tool-btn" onClick={handleBpeStats} disabled={isProcessing}>BPE-S</button>
            <button className="ql-tool-btn" onClick={handleDistDemo} disabled={isProcessing}>Dist</button>
            <button className="ql-tool-btn" onClick={handleDistStats} disabled={isProcessing}>Dist-S</button>
            <button className="ql-tool-btn" onClick={handleRaftDemo} disabled={isProcessing}>RAFT</button>
            <button className="ql-tool-btn" onClick={handleRaftStats} disabled={isProcessing}>RAFT-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleGroupNormDemo} disabled={isProcessing}>GroupNorm</button>
            <button className="ql-tool-btn" onClick={handleGroupNormStats} disabled={isProcessing}>GN-S</button>
            <button className="ql-tool-btn" onClick={handleKVEvictDemo} disabled={isProcessing}>KVEvict</button>
            <button className="ql-tool-btn" onClick={handleKVEvictStats} disabled={isProcessing}>KV-S</button>
            <button className="ql-tool-btn" onClick={handleBenchDemo} disabled={isProcessing}>Benchmark</button>
            <button className="ql-tool-btn" onClick={handleBenchStats} disabled={isProcessing}>BM-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleDataAugDemo} disabled={isProcessing}>DataAug</button>
            <button className="ql-tool-btn" onClick={handleDataAugStats} disabled={isProcessing}>DA-S</button>
            <button className="ql-tool-btn" onClick={handleCurriculumDemo} disabled={isProcessing}>Curriculum</button>
            <button className="ql-tool-btn" onClick={handleCurriculumStats} disabled={isProcessing}>CL-S</button>
            <button className="ql-tool-btn" onClick={handleWatermarkDemo} disabled={isProcessing}>Watermark</button>
            <button className="ql-tool-btn" onClick={handleWatermarkStats} disabled={isProcessing}>WM-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleRewardDemo} disabled={isProcessing}>Reward</button>
            <button className="ql-tool-btn" onClick={handleRewardStats} disabled={isProcessing}>RM-S</button>
            <button className="ql-tool-btn" onClick={handleAdvTrainDemo} disabled={isProcessing}>AdvTrain</button>
            <button className="ql-tool-btn" onClick={handleAdvTrainStats} disabled={isProcessing}>AT-S</button>
            <button className="ql-tool-btn" onClick={handleNASDemo} disabled={isProcessing}>NAS</button>
            <button className="ql-tool-btn" onClick={handleNASStats} disabled={isProcessing}>NAS-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleFedDemo} disabled={isProcessing}>Federated</button>
            <button className="ql-tool-btn" onClick={handleFedStats} disabled={isProcessing}>Fed-S</button>
            <button className="ql-tool-btn" onClick={handleKGDemo} disabled={isProcessing}>KG</button>
            <button className="ql-tool-btn" onClick={handleKGStats} disabled={isProcessing}>KG-S</button>
            <button className="ql-tool-btn" onClick={handlePromptTuneDemo} disabled={isProcessing}>PromptTune</button>
            <button className="ql-tool-btn" onClick={handlePromptTuneStats} disabled={isProcessing}>PT-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleConstitutDemo} disabled={isProcessing}>Constitut</button>
            <button className="ql-tool-btn" onClick={handleConstitutStats} disabled={isProcessing}>CT-S</button>
            <button className="ql-tool-btn" onClick={handleActiveLearnDemo} disabled={isProcessing}>ActiveLearn</button>
            <button className="ql-tool-btn" onClick={handleActiveLearnStats} disabled={isProcessing}>AL-S</button>
            <button className="ql-tool-btn" onClick={handleMMFusionDemo} disabled={isProcessing}>MMFusion</button>
            <button className="ql-tool-btn" onClick={handleMMFusionStats} disabled={isProcessing}>MM-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleMetaLearnDemo} disabled={isProcessing}>MetaLearn</button>
            <button className="ql-tool-btn" onClick={handleMetaLearnStats} disabled={isProcessing}>ML-S</button>
            <button className="ql-tool-btn" onClick={handleContinualDemo} disabled={isProcessing}>Continual</button>
            <button className="ql-tool-btn" onClick={handleContinualStats} disabled={isProcessing}>CL-S</button>
            <button className="ql-tool-btn" onClick={handleSparseGateDemo} disabled={isProcessing}>SparseGate</button>
            <button className="ql-tool-btn" onClick={handleSparseGateStats} disabled={isProcessing}>SG-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleInstTuneDemo} disabled={isProcessing}>InstTune</button>
            <button className="ql-tool-btn" onClick={handleInstTuneStats} disabled={isProcessing}>IT-S</button>
            <button className="ql-tool-btn" onClick={handleCovDemo} disabled={isProcessing}>CoVerif</button>
            <button className="ql-tool-btn" onClick={handleCovStats} disabled={isProcessing}>CV-S</button>
            <button className="ql-tool-btn" onClick={handleCompressDemo} disabled={isProcessing}>Compress</button>
            <button className="ql-tool-btn" onClick={handleCompressStats} disabled={isProcessing}>CP-S</button>
          </div>

          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleSelfPlayDemo} disabled={isProcessing}>SelfPlay</button>
            <button className="ql-tool-btn" onClick={handleSelfPlayStats} disabled={isProcessing}>SP-S</button>
            <button className="ql-tool-btn" onClick={handleIPODemo} disabled={isProcessing}>IPO</button>
            <button className="ql-tool-btn" onClick={handleIPOStats} disabled={isProcessing}>IPO-S</button>
            <button className="ql-tool-btn" onClick={handleEnsembleDemo} disabled={isProcessing}>Ensemble</button>
            <button className="ql-tool-btn" onClick={handleEnsembleStats} disabled={isProcessing}>EN-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleContrastive2Demo} disabled={isProcessing}>Contrast</button>
            <button className="ql-tool-btn" onClick={handleContrastive2Stats} disabled={isProcessing}>CT-S</button>
            <button className="ql-tool-btn" onClick={handleRagPipelineDemo} disabled={isProcessing}>RAG</button>
            <button className="ql-tool-btn" onClick={handleRagPipelineStats} disabled={isProcessing}>RAG-S</button>
            <button className="ql-tool-btn" onClick={handleLoraAdapterDemo} disabled={isProcessing}>LoRA</button>
            <button className="ql-tool-btn" onClick={handleLoraAdapterStats} disabled={isProcessing}>LoRA-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleGnnDemo} disabled={isProcessing}>GNN</button>
            <button className="ql-tool-btn" onClick={handleGnnStats} disabled={isProcessing}>GNN-S</button>
            <button className="ql-tool-btn" onClick={handleDiffusionDemo} disabled={isProcessing}>Diffusion</button>
            <button className="ql-tool-btn" onClick={handleDiffusionStats} disabled={isProcessing}>Diff-S</button>
            <button className="ql-tool-btn" onClick={handleCausalDemo} disabled={isProcessing}>Causal</button>
            <button className="ql-tool-btn" onClick={handleCausalStats} disabled={isProcessing}>Causal-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleVaeDemo} disabled={isProcessing}>VAE</button>
            <button className="ql-tool-btn" onClick={handleVaeStats} disabled={isProcessing}>VAE-S</button>
            <button className="ql-tool-btn" onClick={handleBnnDemo} disabled={isProcessing}>BNN</button>
            <button className="ql-tool-btn" onClick={handleBnnStats} disabled={isProcessing}>BNN-S</button>
            <button className="ql-tool-btn" onClick={handleNodeDemo} disabled={isProcessing}>NeurODE</button>
            <button className="ql-tool-btn" onClick={handleNodeStats} disabled={isProcessing}>ODE-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleReservoirDemo} disabled={isProcessing}>Reservoir</button>
            <button className="ql-tool-btn" onClick={handleReservoirStats} disabled={isProcessing}>Res-S</button>
            <button className="ql-tool-btn" onClick={handleCapsuleDemo} disabled={isProcessing}>Capsule</button>
            <button className="ql-tool-btn" onClick={handleCapsuleStats} disabled={isProcessing}>Cap-S</button>
            <button className="ql-tool-btn" onClick={handleEbmDemo} disabled={isProcessing}>EBM</button>
            <button className="ql-tool-btn" onClick={handleEbmStats} disabled={isProcessing}>EBM-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleHypernetDemo} disabled={isProcessing}>Hypernet</button>
            <button className="ql-tool-btn" onClick={handleHypernetStats} disabled={isProcessing}>Hyp-S</button>
            <button className="ql-tool-btn" onClick={handleFlowDemo} disabled={isProcessing}>Flow</button>
            <button className="ql-tool-btn" onClick={handleFlowStats} disabled={isProcessing}>Flow-S</button>
            <button className="ql-tool-btn" onClick={handleSnnDemo} disabled={isProcessing}>SNN</button>
            <button className="ql-tool-btn" onClick={handleSnnStats} disabled={isProcessing}>SNN-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleWorldModelDemo} disabled={isProcessing}>WorldMdl</button>
            <button className="ql-tool-btn" onClick={handleWorldModelStats} disabled={isProcessing}>WM-S</button>
            <button className="ql-tool-btn" onClick={handleNeuroSymDemo} disabled={isProcessing}>NeuroSym</button>
            <button className="ql-tool-btn" onClick={handleNeuroSymStats} disabled={isProcessing}>NS-S</button>
            <button className="ql-tool-btn" onClick={handleSparseAeDemo} disabled={isProcessing}>SparseAE</button>
            <button className="ql-tool-btn" onClick={handleSparseAeStats} disabled={isProcessing}>SAE-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleMemNetDemo} disabled={isProcessing}>MemNet</button>
            <button className="ql-tool-btn" onClick={handleMemNetStats} disabled={isProcessing}>MN-S</button>
            <button className="ql-tool-btn" onClick={handleCbmDemo} disabled={isProcessing}>CBM</button>
            <button className="ql-tool-btn" onClick={handleCbmStats} disabled={isProcessing}>CBM-S</button>
            <button className="ql-tool-btn" onClick={handleHrrDemo} disabled={isProcessing}>HRR</button>
            <button className="ql-tool-btn" onClick={handleHrrStats} disabled={isProcessing}>HRR-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleLsmDemo} disabled={isProcessing}>LSM</button>
            <button className="ql-tool-btn" onClick={handleLsmStats} disabled={isProcessing}>LSM-S</button>
            <button className="ql-tool-btn" onClick={handleNpsDemo} disabled={isProcessing}>NPS</button>
            <button className="ql-tool-btn" onClick={handleNpsStats} disabled={isProcessing}>NPS-S</button>
            <button className="ql-tool-btn" onClick={handleNeuroCoreDemo} disabled={isProcessing}>NeuroC</button>
            <button className="ql-tool-btn" onClick={handleNeuroCoreStats} disabled={isProcessing}>NC-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleModelMergeDemo} disabled={isProcessing}>Merge</button>
            <button className="ql-tool-btn" onClick={handleModelMergeStats} disabled={isProcessing}>Mrg-S</button>
            <button className="ql-tool-btn" onClick={handleTtcDemo} disabled={isProcessing}>TTC</button>
            <button className="ql-tool-btn" onClick={handleTtcStats} disabled={isProcessing}>TTC-S</button>
            <button className="ql-tool-btn" onClick={handleKdDemo} disabled={isProcessing}>KD</button>
            <button className="ql-tool-btn" onClick={handleKdStats} disabled={isProcessing}>KD-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleSpecDecodeDemo} disabled={isProcessing}>SpecDec</button>
            <button className="ql-tool-btn" onClick={handleSpecDecodeStats} disabled={isProcessing}>SD-S</button>
            <button className="ql-tool-btn" onClick={handleActPatchDemo} disabled={isProcessing}>ActPatch</button>
            <button className="ql-tool-btn" onClick={handleActPatchStats} disabled={isProcessing}>AP-S</button>
            <button className="ql-tool-btn" onClick={handleSerDemo} disabled={isProcessing}>SER</button>
            <button className="ql-tool-btn" onClick={handleSerStats} disabled={isProcessing}>SER-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleMechInterpDemo} disabled={isProcessing}>MechInt</button>
            <button className="ql-tool-btn" onClick={handleMechInterpStats} disabled={isProcessing}>MI-S</button>
            <button className="ql-tool-btn" onClick={handleSymRegDemo} disabled={isProcessing}>SymReg</button>
            <button className="ql-tool-btn" onClick={handleSymRegStats} disabled={isProcessing}>SR-S</button>
            <button className="ql-tool-btn" onClick={handleReasonTraceDemo} disabled={isProcessing}>RsnTrace</button>
            <button className="ql-tool-btn" onClick={handleReasonTraceStats} disabled={isProcessing}>RT-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleDpoDemo} disabled={isProcessing}>DPO</button>
            <button className="ql-tool-btn" onClick={handleDpoStats} disabled={isProcessing}>DPO-S</button>
            <button className="ql-tool-btn" onClick={handleUqDemo} disabled={isProcessing}>UQ</button>
            <button className="ql-tool-btn" onClick={handleUqStats} disabled={isProcessing}>UQ-S</button>
            <button className="ql-tool-btn" onClick={handleDataValDemo} disabled={isProcessing}>DataVal</button>
            <button className="ql-tool-btn" onClick={handleDataValStats} disabled={isProcessing}>DV-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleMooDemo} disabled={isProcessing}>MOO</button>
            <button className="ql-tool-btn" onClick={handleMooStats} disabled={isProcessing}>MOO-S</button>
            <button className="ql-tool-btn" onClick={handleTaskVecDemo} disabled={isProcessing}>TaskVec</button>
            <button className="ql-tool-btn" onClick={handleTaskVecStats} disabled={isProcessing}>TV-S</button>
            <button className="ql-tool-btn" onClick={handlePromptOptDemo} disabled={isProcessing}>PrmptOpt</button>
            <button className="ql-tool-btn" onClick={handlePromptOptStats} disabled={isProcessing}>PO-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleOnlineLearnDemo} disabled={isProcessing}>Online</button>
            <button className="ql-tool-btn" onClick={handleOnlineLearnStats} disabled={isProcessing}>OL-S</button>
            <button className="ql-tool-btn" onClick={handleBanditDemo} disabled={isProcessing}>Bandit</button>
            <button className="ql-tool-btn" onClick={handleBanditStats} disabled={isProcessing}>BD-S</button>
            <button className="ql-tool-btn" onClick={handleRewardShapeDemo} disabled={isProcessing}>RewShp</button>
            <button className="ql-tool-btn" onClick={handleRewardShapeStats} disabled={isProcessing}>RS-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleActiveInfDemo} disabled={isProcessing}>ActInf</button>
            <button className="ql-tool-btn" onClick={handleActiveInfStats} disabled={isProcessing}>AI-S</button>
            <button className="ql-tool-btn" onClick={handleFedPersonalDemo} disabled={isProcessing}>FedPers</button>
            <button className="ql-tool-btn" onClick={handleFedPersonalStats} disabled={isProcessing}>FP-S</button>
            <button className="ql-tool-btn" onClick={handleModelCompDemo} disabled={isProcessing}>ModComp</button>
            <button className="ql-tool-btn" onClick={handleModelCompStats} disabled={isProcessing}>MC-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleBayesOptDemo} disabled={isProcessing}>BayesOpt</button>
            <button className="ql-tool-btn" onClick={handleBayesOptStats} disabled={isProcessing}>BO-S</button>
            <button className="ql-tool-btn" onClick={handleDomainAdaptDemo} disabled={isProcessing}>DomAdpt</button>
            <button className="ql-tool-btn" onClick={handleDomainAdaptStats} disabled={isProcessing}>DA-S</button>
            <button className="ql-tool-btn" onClick={handleSslDemo} disabled={isProcessing}>SSL</button>
            <button className="ql-tool-btn" onClick={handleSslStats} disabled={isProcessing}>SSL-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleTransferDemo} disabled={isProcessing}>Transfer</button>
            <button className="ql-tool-btn" onClick={handleTransferStats} disabled={isProcessing}>TL-S</button>
            <button className="ql-tool-btn" onClick={handleFewShotDemo} disabled={isProcessing}>FewShot</button>
            <button className="ql-tool-btn" onClick={handleFewShotStats} disabled={isProcessing}>FS-S</button>
            <button className="ql-tool-btn" onClick={handleMetaRlDemo} disabled={isProcessing}>MetaRL</button>
            <button className="ql-tool-btn" onClick={handleMetaRlStats} disabled={isProcessing}>MR-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleZeroShotDemo} disabled={isProcessing}>ZeroShot</button>
            <button className="ql-tool-btn" onClick={handleZeroShotStats} disabled={isProcessing}>ZS-S</button>
            <button className="ql-tool-btn" onClick={handleTaskAdaptDemo} disabled={isProcessing}>TaskAdpt</button>
            <button className="ql-tool-btn" onClick={handleTaskAdaptStats} disabled={isProcessing}>TA-S</button>
            <button className="ql-tool-btn" onClick={handleRepMixupDemo} disabled={isProcessing}>Mixup</button>
            <button className="ql-tool-btn" onClick={handleRepMixupStats} disabled={isProcessing}>MU-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleWeightShareDemo} disabled={isProcessing}>WtShare</button>
            <button className="ql-tool-btn" onClick={handleWeightShareStats} disabled={isProcessing}>WS-S</button>
            <button className="ql-tool-btn" onClick={handleDisentangleDemo} disabled={isProcessing}>Disentgl</button>
            <button className="ql-tool-btn" onClick={handleDisentangleStats} disabled={isProcessing}>DE-S</button>
            <button className="ql-tool-btn" onClick={handleGradSurgeryDemo} disabled={isProcessing}>GradSurg</button>
            <button className="ql-tool-btn" onClick={handleGradSurgeryStats} disabled={isProcessing}>GS-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleEvoStratDemo} disabled={isProcessing}>EvoStrat</button>
            <button className="ql-tool-btn" onClick={handleEvoStratStats} disabled={isProcessing}>ES-S</button>
            <button className="ql-tool-btn" onClick={handleHyperOptDemo} disabled={isProcessing}>HyperOpt</button>
            <button className="ql-tool-btn" onClick={handleHyperOptStats} disabled={isProcessing}>HO-S</button>
            <button className="ql-tool-btn" onClick={handleMultiAgentDemo} disabled={isProcessing}>MARL</button>
            <button className="ql-tool-btn" onClick={handleMultiAgentStats} disabled={isProcessing}>MA-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleImitationDemo} disabled={isProcessing}>Imitate</button>
            <button className="ql-tool-btn" onClick={handleImitationStats} disabled={isProcessing}>IL-S</button>
            <button className="ql-tool-btn" onClick={handleInverseRlDemo} disabled={isProcessing}>InvRL</button>
            <button className="ql-tool-btn" onClick={handleInverseRlStats} disabled={isProcessing}>IR-S</button>
            <button className="ql-tool-btn" onClick={handleNtkDemo} disabled={isProcessing}>NTK</button>
            <button className="ql-tool-btn" onClick={handleNtkStats} disabled={isProcessing}>NTK-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleOtDemo} disabled={isProcessing}>OT</button>
            <button className="ql-tool-btn" onClick={handleOtStats} disabled={isProcessing}>OT-S</button>
            <button className="ql-tool-btn" onClick={handleScalingDemo} disabled={isProcessing}>Scaling</button>
            <button className="ql-tool-btn" onClick={handleScalingStats} disabled={isProcessing}>SL-S</button>
            <button className="ql-tool-btn" onClick={handleModelEditDemo} disabled={isProcessing}>ModEdit</button>
            <button className="ql-tool-btn" onClick={handleModelEditStats} disabled={isProcessing}>ME-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleGrokDemo} disabled={isProcessing}>Grok</button>
            <button className="ql-tool-btn" onClick={handleGrokStats} disabled={isProcessing}>GR-S</button>
            <button className="ql-tool-btn" onClick={handleDoubleDescDemo} disabled={isProcessing}>DblDesc</button>
            <button className="ql-tool-btn" onClick={handleDoubleDescStats} disabled={isProcessing}>DD-S</button>
            <button className="ql-tool-btn" onClick={handleSsmDemo} disabled={isProcessing}>SSM</button>
            <button className="ql-tool-btn" onClick={handleSsmStats} disabled={isProcessing}>SSM-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleToolUseDemo} disabled={isProcessing}>ToolUse</button>
            <button className="ql-tool-btn" onClick={handleToolUseStats} disabled={isProcessing}>TU-S</button>
            <button className="ql-tool-btn" onClick={handleSelfRefineDemo} disabled={isProcessing}>SelfRefn</button>
            <button className="ql-tool-btn" onClick={handleSelfRefineStats} disabled={isProcessing}>SR-S</button>
            <button className="ql-tool-btn" onClick={handleReasonChainDemo} disabled={isProcessing}>CoT</button>
            <button className="ql-tool-btn" onClick={handleReasonChainStats} disabled={isProcessing}>CoT-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleRedTeamDemo} disabled={isProcessing}>RedTeam</button>
            <button className="ql-tool-btn" onClick={handleRedTeamStats} disabled={isProcessing}>RT-S</button>
            <button className="ql-tool-btn" onClick={handleSafetyDemo} disabled={isProcessing}>Safety</button>
            <button className="ql-tool-btn" onClick={handleSafetyStats} disabled={isProcessing}>SF-S</button>
            <button className="ql-tool-btn" onClick={handleJailbreakDemo} disabled={isProcessing}>Jailbrk</button>
            <button className="ql-tool-btn" onClick={handleJailbreakStats} disabled={isProcessing}>JB-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleRingAttnDemo} disabled={isProcessing}>RingAttn</button>
            <button className="ql-tool-btn" onClick={handleRingAttnStats} disabled={isProcessing}>RA-S</button>
            <button className="ql-tool-btn" onClick={handleArenaDemo} disabled={isProcessing}>Arena</button>
            <button className="ql-tool-btn" onClick={handleArenaStats} disabled={isProcessing}>AR-S</button>
            <button className="ql-tool-btn" onClick={handleSynthDataDemo} disabled={isProcessing}>SynthData</button>
            <button className="ql-tool-btn" onClick={handleSynthDataStats} disabled={isProcessing}>SD-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleOrpoDemo} disabled={isProcessing}>ORPO</button>
            <button className="ql-tool-btn" onClick={handleOrpoStats} disabled={isProcessing}>OR-S</button>
            <button className="ql-tool-btn" onClick={handleSimpoDemo} disabled={isProcessing}>SimPO</button>
            <button className="ql-tool-btn" onClick={handleSimpoStats} disabled={isProcessing}>SP-S</button>
            <button className="ql-tool-btn" onClick={handleInfiniAttnDemo} disabled={isProcessing}>InfiniAt</button>
            <button className="ql-tool-btn" onClick={handleInfiniAttnStats} disabled={isProcessing}>IA-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleMedusaDemo} disabled={isProcessing}>Medusa</button>
            <button className="ql-tool-btn" onClick={handleMedusaStats} disabled={isProcessing}>MD-S</button>
            <button className="ql-tool-btn" onClick={handleEagleDemo} disabled={isProcessing}>EAGLE</button>
            <button className="ql-tool-btn" onClick={handleEagleStats} disabled={isProcessing}>EG-S</button>
            <button className="ql-tool-btn" onClick={handleChunkedDemo} disabled={isProcessing}>Chunked</button>
            <button className="ql-tool-btn" onClick={handleChunkedStats} disabled={isProcessing}>CK-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleDoraDemo} disabled={isProcessing}>DoRA</button>
            <button className="ql-tool-btn" onClick={handleDoraStats} disabled={isProcessing}>DR-S</button>
            <button className="ql-tool-btn" onClick={handleTopKDemo} disabled={isProcessing}>TopK</button>
            <button className="ql-tool-btn" onClick={handleTopKStats} disabled={isProcessing}>TK-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleHyenaDemo} disabled={isProcessing}>Hyena</button>
            <button className="ql-tool-btn" onClick={handleHyenaStats} disabled={isProcessing}>HY-S</button>
            <button className="ql-tool-btn" onClick={handleRwkvDemo} disabled={isProcessing}>RWKV</button>
            <button className="ql-tool-btn" onClick={handleRwkvStats} disabled={isProcessing}>RW-S</button>
            <button className="ql-tool-btn" onClick={handlePromptCompDemo} disabled={isProcessing}>PCompr</button>
            <button className="ql-tool-btn" onClick={handlePromptCompStats} disabled={isProcessing}>PC-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleFlashMlaDemo} disabled={isProcessing}>FlashMLA</button>
            <button className="ql-tool-btn" onClick={handleFlashMlaStats} disabled={isProcessing}>FM-S</button>
            <button className="ql-tool-btn" onClick={handleAgentPlanDemo} disabled={isProcessing}>Agent</button>
            <button className="ql-tool-btn" onClick={handleAgentPlanStats} disabled={isProcessing}>AG-S</button>
            <button className="ql-tool-btn" onClick={handleSpecRejectDemo} disabled={isProcessing}>SpecRej</button>
            <button className="ql-tool-btn" onClick={handleSpecRejectStats} disabled={isProcessing}>SR-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handlePagedKvDemo} disabled={isProcessing}>PagedKV</button>
            <button className="ql-tool-btn" onClick={handlePagedKvStats} disabled={isProcessing}>PK-S</button>
            <button className="ql-tool-btn" onClick={handleAgentMemDemo} disabled={isProcessing}>AgMem</button>
            <button className="ql-tool-btn" onClick={handleAgentMemStats} disabled={isProcessing}>AM-S</button>
            <button className="ql-tool-btn" onClick={handleDynBatchDemo} disabled={isProcessing}>Batch</button>
            <button className="ql-tool-btn" onClick={handleDynBatchStats} disabled={isProcessing}>CB-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleVisEncDemo} disabled={isProcessing}>ViT</button>
            <button className="ql-tool-btn" onClick={handleVisEncStats} disabled={isProcessing}>VE-S</button>
            <button className="ql-tool-btn" onClick={handleTxt2ImgDemo} disabled={isProcessing}>T2I</button>
            <button className="ql-tool-btn" onClick={handleTxt2ImgStats} disabled={isProcessing}>TI-S</button>
            <button className="ql-tool-btn" onClick={handlePromCacheDemo} disabled={isProcessing}>PCache</button>
            <button className="ql-tool-btn" onClick={handlePromCacheStats} disabled={isProcessing}>PC-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleImgCapDemo} disabled={isProcessing}>ImgCap</button>
            <button className="ql-tool-btn" onClick={handleImgCapStats} disabled={isProcessing}>IC-S</button>
            <button className="ql-tool-btn" onClick={handleObjDetDemo} disabled={isProcessing}>ObjDet</button>
            <button className="ql-tool-btn" onClick={handleObjDetStats} disabled={isProcessing}>OD-S</button>
            <button className="ql-tool-btn" onClick={handleSuperResDemo} disabled={isProcessing}>SupRes</button>
            <button className="ql-tool-btn" onClick={handleSuperResStats} disabled={isProcessing}>SR-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleStyleTransDemo} disabled={isProcessing}>Style</button>
            <button className="ql-tool-btn" onClick={handleStyleTransStats} disabled={isProcessing}>ST-S</button>
            <button className="ql-tool-btn" onClick={handleImgInpaintDemo} disabled={isProcessing}>Inpaint</button>
            <button className="ql-tool-btn" onClick={handleImgInpaintStats} disabled={isProcessing}>IP-S</button>
            <button className="ql-tool-btn" onClick={handleAudioEncDemo} disabled={isProcessing}>Audio</button>
            <button className="ql-tool-btn" onClick={handleAudioEncStats} disabled={isProcessing}>AE-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleImgSegDemo} disabled={isProcessing}>Seg</button>
            <button className="ql-tool-btn" onClick={handleImgSegStats} disabled={isProcessing}>SG-S</button>
            <button className="ql-tool-btn" onClick={handleDepthEstDemo} disabled={isProcessing}>Depth</button>
            <button className="ql-tool-btn" onClick={handleDepthEstStats} disabled={isProcessing}>DE-S</button>
            <button className="ql-tool-btn" onClick={handleOptFlowDemo} disabled={isProcessing}>Flow</button>
            <button className="ql-tool-btn" onClick={handleOptFlowStats} disabled={isProcessing}>OF-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handlePoseEstDemo} disabled={isProcessing}>Pose</button>
            <button className="ql-tool-btn" onClick={handlePoseEstStats} disabled={isProcessing}>PE-S</button>
            <button className="ql-tool-btn" onClick={handleSpeechRecogDemo} disabled={isProcessing}>ASR</button>
            <button className="ql-tool-btn" onClick={handleSpeechRecogStats} disabled={isProcessing}>AR-S</button>
            <button className="ql-tool-btn" onClick={handleMusicGenDemo} disabled={isProcessing}>Music</button>
            <button className="ql-tool-btn" onClick={handleMusicGenStats} disabled={isProcessing}>MG-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleCodeParserDemo} disabled={isProcessing}>Parse</button>
            <button className="ql-tool-btn" onClick={handleCodeParserStats} disabled={isProcessing}>PR-S</button>
            <button className="ql-tool-btn" onClick={handleCodeGenDemo} disabled={isProcessing}>CodeGen</button>
            <button className="ql-tool-btn" onClick={handleCodeGenStats} disabled={isProcessing}>CG-S</button>
            <button className="ql-tool-btn" onClick={handleCodeExecDemo} disabled={isProcessing}>Exec</button>
            <button className="ql-tool-btn" onClick={handleCodeExecStats} disabled={isProcessing}>EX-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleCodeDebugDemo} disabled={isProcessing}>Debug</button>
            <button className="ql-tool-btn" onClick={handleCodeDebugStats} disabled={isProcessing}>DB-S</button>
            <button className="ql-tool-btn" onClick={handleProgRepairDemo} disabled={isProcessing}>Repair</button>
            <button className="ql-tool-btn" onClick={handleProgRepairStats} disabled={isProcessing}>RP-S</button>
            <button className="ql-tool-btn" onClick={handleCodeReviewDemo} disabled={isProcessing}>Review</button>
            <button className="ql-tool-btn" onClick={handleCodeReviewStats} disabled={isProcessing}>RV-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleSyntaxHlDemo} disabled={isProcessing}>HL</button>
            <button className="ql-tool-btn" onClick={handleSyntaxHlStats} disabled={isProcessing}>HL-S</button>
            <button className="ql-tool-btn" onClick={handleCodeCompleteDemo} disabled={isProcessing}>Complete</button>
            <button className="ql-tool-btn" onClick={handleCodeCompleteStats} disabled={isProcessing}>CC-S</button>
            <button className="ql-tool-btn" onClick={handleCodeRefactorDemo} disabled={isProcessing}>Refactor</button>
            <button className="ql-tool-btn" onClick={handleCodeRefactorStats} disabled={isProcessing}>RF-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handlePipelineAnalyze} disabled={isProcessing}>Pipeline</button>
            <button className="ql-tool-btn" onClick={handlePipelineFull} disabled={isProcessing}>PL-Full</button>
            <button className="ql-tool-btn" onClick={handlePipelineStats} disabled={isProcessing}>PL-S</button>
          </div>
          <div className="ql-toolbar-row">
            <button className="ql-tool-btn" onClick={handleDataLoaderStats} disabled={isProcessing}>DL</button>
            <button className="ql-tool-btn" onClick={handleDataPipelineStats} disabled={isProcessing}>DP</button>
            <button className="ql-tool-btn" onClick={handleTokenizedDsStats} disabled={isProcessing}>TK</button>
            <button className="ql-tool-btn" onClick={handleDistTrainStats} disabled={isProcessing}>DT</button>
            <button className="ql-tool-btn" onClick={handleMpStats} disabled={isProcessing}>MP</button>
            <button className="ql-tool-btn" onClick={handleCkptStats} disabled={isProcessing}>CK</button>
          </div>

          <div className="ql-toolbar-row" style={{ borderTop: "1px solid #444", paddingTop: 4, marginTop: 4 }}>
            <button className="ql-tool-btn" style={{ background: "#2d5a27", color: "#8eff8e" }} onClick={handleCodeGenerate} disabled={isProcessing}>Hardy-Code</button>
            <input className="ql-tool-input ql-seed-input" value={genSeed} onChange={e => setGenSeed(e.target.value)} placeholder="describe code to generate..." style={{ flex: 1 }} />
            <input type="number" className="ql-tool-input" value={genLen} onChange={e => setGenLen(Number(e.target.value) || 50)} min={10} max={500} title="Max length" style={{ width: 60 }} />
          </div>

        </div>
      )}

      <div className="duanyan-chat" ref={chatRef}>
        {messages.map((msg, i) => (
          <div key={i} className={"duanyan-msg " + msg.role}>
            <div className="msg-role">{msg.role === "user" ? "You" : msg.role === "duanyan" ? "duanyan" : "System"}</div>
            <div className="msg-content">{msg.content}</div>
          </div>
        ))}
        {isProcessing && (
          <div className="duanyan-msg duanyan">
            <div className="msg-role">duanyan</div>
            <div className="msg-content thinking">Processing...</div>
          </div>
        )}
      </div>

      <div className="duanyan-input-area">
        <textarea ref={inputRef} className="duanyan-input" value={input} onChange={(e) => setInput(e.target.value)} onKeyDown={handleKeyDown} placeholder="Send a prompt to duanyan... (Enter)" rows={2} disabled={isProcessing} />
        <button className="duanyan-send-btn" onClick={handleSend} disabled={isProcessing || !input.trim()} title="Send (Enter)">
          <svg width="16" height="16" viewBox="0 0 16 16"><path d="M2 14L14 8L2 2L2 7L10 8L2 9L2 14Z" fill="currentColor"/></svg>
        </button>
      </div>
    </div>
  );
}


