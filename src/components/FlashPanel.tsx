import { useState, useCallback } from "react";

interface PortInfo {
  port_name: string;
  description: string;
  vid: number | null;
  pid: number | null;
  is_esp_device: boolean;
}

interface ChipInfo {
  chip: string;
  mac_address: string;
  flash_size: string;
  features: string[];
  crystal_frequency: string;
}

interface FlashResult {
  success: boolean;
  message: string;
  bytes_written: number;
  duration_ms: number;
}

interface ToolchainInfo {
  found: boolean;
  gcc_path: string;
  version: string;
}

interface BuildResult {
  success: boolean;
  bin_path: string;
  bin_size: number;
  message: string;
}

export default function FlashPanel() {
  const [ports, setPorts] = useState<PortInfo[]>([]);
  const [selectedPort, setSelectedPort] = useState("");
  const [chipInfo, setChipInfo] = useState<ChipInfo | null>(null);
  const [binPath, setBinPath] = useState("");
  const [address, setAddress] = useState("0x0");
  const [baud, setBaud] = useState("460800");
  const [log, setLog] = useState<string[]>([]);
  const [busy, setBusy] = useState(false);
  const [flashResult, setFlashResult] = useState<FlashResult | null>(null);
  const [toolchain, setToolchain] = useState<ToolchainInfo | null>(null);
  const [cFilePath, setCFilePath] = useState("");
  const [ldFilePath, setLdFilePath] = useState("");
  const [jsSource, setJsSource] = useState("");
  const [generatedC, setGeneratedC] = useState("");

  const addLog = (msg: string) => setLog(prev => [...prev, `[${new Date().toLocaleTimeString()}] ${msg}`]);

  const handleDetectPorts = useCallback(async () => {
    setBusy(true);
    addLog("扫描串口...");
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const result = await invoke<PortInfo[]>("flasher_detect_ports");
      setPorts(result);
      if (result.length > 0) {
        const esp = result.find(p => p.is_esp_device);
        setSelectedPort(esp ? esp.port_name : result[0].port_name);
        addLog(`发现 ${result.length} 个串口${esp ? ` (ESP: ${esp.port_name})` : ""}`);
      } else {
        addLog("未发现可用串口");
      }
    } catch (e) {
      addLog(`扫描失败: ${e}`);
    }
    setBusy(false);
  }, []);

  const handleChipInfo = useCallback(async () => {
    if (!selectedPort) { addLog("请先选择串口"); return; }
    setBusy(true);
    setChipInfo(null);
    addLog(`读取芯片信息 (${selectedPort})...`);
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const info = await invoke<ChipInfo>("flasher_chip_info", { port: selectedPort });
      setChipInfo(info);
      addLog(`芯片: ${info.chip} | MAC: ${info.mac_address} | Flash: ${info.flash_size}`);
    } catch (e) {
      addLog(`读取失败: ${e}`);
      addLog("提示: 按住 BOOT 键后按 RESET 进入下载模式");
    }
    setBusy(false);
  }, [selectedPort]);

  const handleSelectBin = useCallback(async () => {
    try {
      const { open } = await import("@tauri-apps/plugin-dialog");
      const selected = await open({
        filters: [{ name: "Firmware", extensions: ["bin", "hex", "elf"] }],
        title: "选择固件文件",
      });
      if (selected) {
        setBinPath(selected as string);
        addLog(`固件: ${selected}`);
      }
    } catch (e) {
      addLog(`选择文件失败: ${e}`);
    }
  }, []);

  const handleFlash = useCallback(async () => {
    if (!selectedPort) { addLog("请先选择串口"); return; }
    if (!binPath) { addLog("请先选择固件文件"); return; }
    setBusy(true);
    setFlashResult(null);
    const addr = parseInt(address, 16) || 0;
    const baudRate = parseInt(baud) || 460800;
    addLog(`开始烧录: ${binPath} → ${selectedPort} @ 0x${addr.toString(16)} (${baudRate} baud)`);
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const result = await invoke<FlashResult>("flasher_flash_bin", {
        port: selectedPort,
        binPath,
        address: addr,
        baud: baudRate,
      });
      setFlashResult(result);
      addLog(result.message);
    } catch (e) {
      addLog(`烧录失败: ${e}`);
      addLog("提示: 按住 BOOT 键后按 RESET 进入下载模式");
    }
    setBusy(false);
  }, [selectedPort, binPath, address, baud]);

  const handleErase = useCallback(async () => {
    if (!selectedPort) { addLog("请先选择串口"); return; }
    setBusy(true);
    addLog(`擦除 Flash (${selectedPort})...`);
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const result = await invoke<string>("flasher_erase", { port: selectedPort });
      addLog(result);
    } catch (e) {
      addLog(`擦除失败: ${e}`);
    }
    setBusy(false);
  }, [selectedPort]);

  const handleDetectToolchain = useCallback(async () => {
    addLog("检测 Xtensa GCC 工具链...");
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const info = await invoke<ToolchainInfo>("flasher_detect_toolchain");
      setToolchain(info);
      if (info.found) {
        addLog(`[OK] 工具链: ${info.version}`);
      } else {
        addLog("[FAIL] 未找到 xtensa-esp32s3-elf-gcc");
      }
    } catch (e) {
      addLog(`检测失败: ${e}`);
    }
  }, []);

  const handleSelectCFile = useCallback(async () => {
    try {
      const { open } = await import("@tauri-apps/plugin-dialog");
      const selected = await open({
        filters: [{ name: "C Source", extensions: ["c", "h"] }],
        title: "选择 C 源文件",
      });
      if (selected) {
        setCFilePath(selected as string);
        addLog(`C 源码: ${selected}`);
      }
    } catch (e) {
      addLog(`选择文件失败: ${e}`);
    }
  }, []);

  const handleSelectLd = useCallback(async () => {
    try {
      const { open } = await import("@tauri-apps/plugin-dialog");
      const selected = await open({
        filters: [{ name: "Linker Script", extensions: ["ld", "lds"] }],
        title: "选择链接脚本 (.ld)",
      });
      if (selected) {
        setLdFilePath(selected as string);
        addLog(`链接脚本: ${selected}`);
      }
    } catch (e) {
      addLog(`选择文件失败: ${e}`);
    }
  }, []);

  const handleBuildAndFlash = useCallback(async () => {
    if (!selectedPort) { addLog("请先选择串口"); return; }
    if (!cFilePath) { addLog("请先选择 C 源文件"); return; }
    setBusy(true);
    setFlashResult(null);
    const addr = parseInt(address, 16) || 0;
    const baudRate = parseInt(baud) || 460800;
    const outDir = cFilePath.replace(/\\[^\\]*$/, "").replace(/\/[^/]*$/, "") + "\\.build";
    addLog(`一键编译+烧录: ${cFilePath}`);
    addLog(`  → ${selectedPort} @ 0x${addr.toString(16)} (${baudRate} baud)`);
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const result = await invoke<FlashResult>("flasher_build_and_flash", {
        cSource: cFilePath,
        port: selectedPort,
        outputDir: outDir,
        address: addr,
        baud: baudRate,
        linkerScript: ldFilePath || null,
      });
      setFlashResult(result);
      addLog(`[OK] ${result.message}`);
    } catch (e) {
      addLog(`[FAIL] 失败: ${e}`);
    }
    setBusy(false);
  }, [selectedPort, cFilePath, ldFilePath, address, baud]);

  const handleCompileJs = useCallback(async () => {
    if (!jsSource.trim()) { addLog("请输入 JS 源码"); return; }
    addLog("HardyScript 编译: JS → C...");
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const result = await invoke<Record<string, unknown>>("flasher_compile_js", {
        jsSource,
        target: "esp32",
      });
      setGeneratedC(result.c_source as string);
      addLog(`[OK] C 代码生成成功 (${(result.c_source as string).length} chars)`);
    } catch (e) {
      addLog(`[FAIL] 编译失败: ${e}`);
    }
  }, [jsSource]);

  const handleJsToFlash = useCallback(async () => {
    if (!selectedPort) { addLog("请先选择串口"); return; }
    if (!jsSource.trim()) { addLog("请输入 JS 源码"); return; }
    setBusy(true);
    setFlashResult(null);
    const baudRate = parseInt(baud) || 460800;
    const outDir = "C:\\Users\\askai\\.duanyan\\build";
    addLog(`JS → C → gcc → ESP32-S3镜像 → flash 全自动管线`);
    addLog(`  → ${selectedPort} @ 0x10000 (${baudRate} baud)`);
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const result = await invoke<FlashResult>("flasher_full_pipeline", {
        jsSource,
        port: selectedPort,
        outputDir: outDir,
      });
      setFlashResult(result);
      addLog(`[OK] ${result.message}`);
    } catch (e) {
      addLog(`[FAIL] 失败: ${e}`);
    }
    setBusy(false);
  }, [selectedPort, jsSource, baud]);

  const handleJsToBin = useCallback(async () => {
    if (!jsSource.trim()) { addLog("请输入 JS 源码"); return; }
    setBusy(true);
    setFlashResult(null);
    const outDir = "C:\\Users\\askai\\.duanyan\\build";
    addLog("HardyScript 编译: JS → C → gcc → ESP32-S3 .bin (裸机后端)");
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const result = await invoke<{ bin_path: string; bin_size: number; message: string }>(
        "flasher_compile_to_bin",
        { jsSource, outputDir: outDir, target: "esp32s3-bare" },
      );
      setBinPath(result.bin_path);
      addLog(`[OK] ${result.message}`);
      addLog(`下一步: 选好串口 → 点上方“烧录”按钮 (地址 0x0 已预设)`);
    } catch (e) {
      addLog(`[FAIL] 编译失败: ${e}`);
    }
    setBusy(false);
  }, [jsSource]);

  return (
    <div style={{ padding: "12px", fontSize: "12px", color: "#fff", height: "100%", display: "flex", flexDirection: "column", gap: "8px" }}>
      <div style={{ fontWeight: "bold", fontSize: "13px", color: "#fff" }}>
        DUANYAN Flasher
        <span style={{ fontSize: "11px", color: "#ccc", marginLeft: "8px" }}>Native ESP32 Programmer</span>
      </div>

      {/* Port Selection Row */}
      <div style={{ display: "flex", gap: "6px", alignItems: "center" }}>
        <select
          value={selectedPort}
          onChange={e => setSelectedPort(e.target.value)}
          style={{ flex: 1, background: "#1e1e1e", color: "#fff", border: "1px solid #444", borderRadius: "3px", padding: "4px 8px" }}
        >
          <option value="">-- 选择串口 --</option>
          {ports.map(p => (
            <option key={p.port_name} value={p.port_name}>
              {p.port_name} {p.is_esp_device ? "★ ESP" : ""} - {p.description}
            </option>
          ))}
        </select>
        <button onClick={handleDetectPorts} disabled={busy} style={btnStyle}>扫描</button>
        <button onClick={handleChipInfo} disabled={busy} style={btnStyle}>芯片信息</button>
      </div>

      {/* Chip Info */}
      {chipInfo && (
        <div style={{ background: "#252526", border: "1px solid #555", borderRadius: "4px", padding: "8px" }}>
          <div>芯片: <b>{chipInfo.chip}</b></div>
          <div>MAC: <b>{chipInfo.mac_address}</b></div>
          <div>Flash: <b>{chipInfo.flash_size}</b> | 晶振: {chipInfo.crystal_frequency}</div>
          <div>特性: {chipInfo.features.join(", ")}</div>
        </div>
      )}

      {/* Flash Config */}
      <div style={{ display: "flex", gap: "6px", alignItems: "center" }}>
        <input
          value={binPath}
          readOnly
          placeholder="选择固件 .bin 文件..."
          style={{ flex: 1, background: "#1e1e1e", color: "#fff", border: "1px solid #444", borderRadius: "3px", padding: "4px 8px" }}
        />
        <button onClick={handleSelectBin} disabled={busy} style={btnStyle}>浏览</button>
      </div>

      <div style={{ display: "flex", gap: "6px", alignItems: "center" }}>
        <label style={{ color: "#fff" }}>地址:</label>
        <input value={address} onChange={e => setAddress(e.target.value)}
          style={{ width: "80px", background: "#1e1e1e", color: "#fff", border: "1px solid #444", borderRadius: "3px", padding: "4px" }} />
        <label style={{ color: "#fff" }}>波特率:</label>
        <select value={baud} onChange={e => setBaud(e.target.value)}
          style={{ background: "#1e1e1e", color: "#fff", border: "1px solid #444", borderRadius: "3px", padding: "4px" }}>
          <option value="115200">115200</option>
          <option value="230400">230400</option>
          <option value="460800">460800</option>
          <option value="921600">921600</option>
        </select>
        <div style={{ flex: 1 }} />
        <button onClick={handleFlash} disabled={busy} style={btnStyle}>
          {busy ? "烧录中..." : "烧录"}
        </button>
        <button onClick={handleErase} disabled={busy} style={btnStyle}>
          擦除
        </button>
      </div>

      {/* Result */}
      {flashResult && (
        <div style={{
          background: "#252526",
          border: "1px solid #555",
          borderRadius: "4px", padding: "6px 8px", color: "#fff"
        }}>
          {flashResult.success ? "✓ " : "✗ "}{flashResult.message}
        </div>
      )}

      {/* Build & Flash Section */}
      <div style={{ borderTop: "1px solid #333", paddingTop: "8px", marginTop: "4px" }}>
        <div style={{ display: "flex", gap: "6px", alignItems: "center", marginBottom: "6px" }}>
          <span style={{ color: "#fff", fontWeight: "bold" }}>编译+烧录</span>
          <button onClick={handleDetectToolchain} style={btnStyle}>检测工具链</button>
          {toolchain && (
            <span style={{ color: "#fff", fontWeight: "bold", fontSize: "11px" }}>
              {toolchain.found ? `[OK] ${toolchain.version}` : "[FAIL] 未找到"}
            </span>
          )}
        </div>
        <div style={{ display: "flex", gap: "6px", alignItems: "center", marginBottom: "6px" }}>
          <input value={cFilePath} readOnly placeholder="选择 C 源文件..."
            style={{ flex: 1, background: "#1e1e1e", color: "#fff", border: "1px solid #444", borderRadius: "3px", padding: "4px 8px" }} />
          <button onClick={handleSelectCFile} style={btnStyle}>浏览</button>
        </div>
        <div style={{ display: "flex", gap: "6px", alignItems: "center" }}>
          <input value={ldFilePath} readOnly placeholder="链接脚本 (.ld) 可选"
            style={{ flex: 1, background: "#1e1e1e", color: "#fff", border: "1px solid #444", borderRadius: "3px", padding: "4px 8px" }} />
          <button onClick={handleSelectLd} style={btnStyle}>浏览</button>
          <button onClick={handleBuildAndFlash} disabled={busy} style={btnStyle}>
            {busy ? "执行中..." : "编译+烧录"}
          </button>
        </div>
      </div>

      {/* JS → Flash Section */}
      <div style={{ borderTop: "1px solid #333", paddingTop: "8px", marginTop: "4px" }}>
        <div style={{ display: "flex", gap: "6px", alignItems: "center", marginBottom: "6px" }}>
          <span style={{ color: "#fff", fontWeight: "bold" }}>HardyScript JS → Flash</span>
          <button onClick={handleCompileJs} style={btnStyle}>仅编译 (JS→C)</button>
          <button onClick={handleJsToBin} disabled={busy} style={btnStyle}>
            {busy ? "编译中..." : "编译成 .bin"}
          </button>
          <button onClick={handleJsToFlash} disabled={busy} style={btnStyle}>
            {busy ? "执行中..." : "JS→Flash"}
          </button>
        </div>
        <textarea
          value={jsSource}
          onChange={(e) => setJsSource(e.target.value)}
          placeholder={'// 输入 HardyScript 代码\nimport { GPIO, Timer } from "hardy:hw";\nlet led = GPIO.output(2);\nwhile (true) {\n  led.high();\n  Timer.delay(500);\n  led.low();\n  Timer.delay(500);\n}'}
          style={{
            width: "100%", height: "80px", background: "#1a1a2e", color: "#fff",
            border: "1px solid #444", borderRadius: "4px", padding: "6px",
            fontFamily: "monospace", fontSize: "11px", resize: "vertical"
          }}
        />
        {generatedC && (
          <details style={{ marginTop: "4px" }}>
            <summary style={{ cursor: "pointer", color: "#fff", fontSize: "11px" }}>查看生成的 C 代码 ({generatedC.length} chars)</summary>
            <pre style={{
              background: "#111", padding: "6px", borderRadius: "4px",
              fontSize: "10px", maxHeight: "120px", overflow: "auto", color: "#ddd"
            }}>{generatedC}</pre>
          </details>
        )}
      </div>

      {/* Log */}
      <div style={{ flex: 1, overflow: "auto", background: "#111", borderRadius: "4px", padding: "6px", minHeight: "60px" }}>
        {log.map((line, i) => (
          <div key={i} style={{ color: "#fff", fontWeight: line.includes("失败") ? "bold" : "normal" }}>
            {line}
          </div>
        ))}
      </div>
    </div>
  );
}

const btnStyle: React.CSSProperties = {
  background: "#2a2a2a",
  color: "#fff",
  border: "1px solid #555",
  borderRadius: "3px",
  height: "26px",
  padding: "0 12px",
  cursor: "pointer",
  fontSize: "12px",
  display: "inline-flex",
  alignItems: "center",
  boxSizing: "border-box",
};
