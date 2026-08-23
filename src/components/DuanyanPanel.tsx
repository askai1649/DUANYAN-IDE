import { useState, useRef, useEffect } from "react";
import DuanyanHeader from "./DuanyanHeader";
import DuanyanToolbar from "./DuanyanToolbar";

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
      <DuanyanHeader
        isProcessing={isProcessing}
        showToolbar={showToolbar}
        onToggleToolbar={() => setShowToolbar(p => !p)}
        onClear={handleClear}
        onClose={onClose}
        useBackprop={useBackprop}
        clock={clock}
      />

      {showToolbar && (
        <DuanyanToolbar
          isProcessing={isProcessing}
          setIsProcessing={setIsProcessing}
          addMessage={addMessage}
          input={input}
          setInput={setInput}
          useBackprop={useBackprop}
          setUseBackprop={setUseBackprop}
        />
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
