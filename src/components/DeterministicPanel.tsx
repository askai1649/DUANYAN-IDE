import { useState } from "react";

interface Props {
  visible: boolean;
  source: string;
}

type IndustryTarget = "ic" | "ad" | "rt" | "av";

const TARGETS: { id: IndustryTarget; label: string; desc: string }[] = [
  { id: "ic", label: "工业控制", desc: "IEC 61131-3 / EtherCAT / SIL-3 / 运动控制" },
  { id: "ad", label: "自动驾驶", desc: "CAN FD / AUTOSAR / Fail-Operational" },
  { id: "rt", label: "嵌入式 RTOS", desc: "微内核 / BSP / TLA+ / Spin" },
  { id: "av", label: "航空电子", desc: "DO-178C / ARINC 653 / TMR" },
];

export default function DeterministicPanel({ visible, source }: Props) {
  const [target, setTarget] = useState<IndustryTarget>("ic");
  const [output, setOutput] = useState("");
  const [analyzing, setAnalyzing] = useState(false);
  const [error, setError] = useState("");

  if (!visible) return null;

  const runAnalysis = async () => {
    setAnalyzing(true);
    setError("");
    setOutput("");
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const result = await invoke<string>("deterministic_analyze", {
        source: source || "// empty",
        target,
      });
      setOutput(result);
    } catch (e: unknown) {
      setError(String(e));
    } finally {
      setAnalyzing(false);
    }
  };

  return (
    <div className="deterministic-panel" style={{ height: "100%", display: "flex", flexDirection: "column", padding: "4px 8px" }}>
      {/* Target selector */}
      <div style={{ display: "flex", gap: 6, marginBottom: 6, alignItems: "center", flexWrap: "wrap" }}>
        {TARGETS.map((t) => (
          <button
            key={t.id}
            onClick={() => setTarget(t.id)}
            title={t.desc}
            style={{
              padding: "2px 10px",
              fontSize: 11,
              borderRadius: 3,
              border: target === t.id ? "1px solid #fff" : "1px solid #555",
              background: target === t.id ? "#3a3a3a" : "#2a2a2a",
              color: target === t.id ? "#fff" : "#ccc",
              fontWeight: target === t.id ? "bold" : "normal",
              cursor: "pointer",
            }}
          >
            {t.label}
          </button>
        ))}
        <button
          onClick={runAnalysis}
          disabled={analyzing}
          style={{
            marginLeft: "auto",
            padding: "2px 14px",
            fontSize: 11,
            borderRadius: 3,
            border: "1px solid #555",
            background: analyzing ? "#333" : "#2a2a2a",
            color: "#fff",
            cursor: analyzing ? "wait" : "pointer",
          }}
        >
          {analyzing ? "分析中..." : "运行分析"}
        </button>
      </div>

      {/* Description */}
      <div style={{ fontSize: 10, color: "#ccc", marginBottom: 4 }}>
        {TARGETS.find((t) => t.id === target)?.desc}
      </div>

      {/* Error */}
      {error && (
        <div style={{ color: "#fff", fontWeight: "bold", fontSize: 11, marginBottom: 4, whiteSpace: "pre-wrap" }}>
          {error}
        </div>
      )}

      {/* Output */}
      <pre
        style={{
          flex: 1,
          overflow: "auto",
          fontSize: 11,
          lineHeight: 1.4,
          color: "#fff",
          background: "#1e1e1e",
          borderRadius: 4,
          padding: 8,
          margin: 0,
          whiteSpace: "pre-wrap",
          wordBreak: "break-all",
        }}
      >
        {output || (analyzing ? "正在分析确定性约束..." : "选择行业目标，点击「运行分析」")}
      </pre>
    </div>
  );
}
