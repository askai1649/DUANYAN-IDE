import { useState, useEffect, useRef, useCallback } from "react";

export interface BridgeLogEntry {
  timestamp_ms: number;
  pkt_type: number;
  text: string;
  message?: BridgeMessage;
}

export type BridgeMessage =
  | { kind: "Log"; text: string }
  | { kind: "Result"; values: number[] }
  | { kind: "HardwareStatus"; pin_states: number[]; adc_values: number[]; uptime_ms: number }
  | { kind: "Exception"; code: number; text: string }
  | { kind: "Command"; text: string }
  | { kind: "Heartbeat"; uptime_ms: number }
  | { kind: "Raw"; bytes: number[] };

interface Props {
  onClose: () => void;
}

const TYPE_NAMES: Record<number, string> = {
  0x01: "LOG",
  0x02: "RESULT",
  0x03: "HW",
  0x04: "ERR",
  0x10: "CMD",
  0x11: "BEAT",
};

export default function OpenSnarBridge({ onClose }: Props) {
  const [ports, setPorts] = useState<string[]>([]);
  const [selectedPort, setSelectedPort] = useState("");
  const [baud, setBaud] = useState(115200);
  const [connected, setConnected] = useState(false);
  const [activeTab, setActiveTab] = useState<"console" | "hardware" | "cluster" | "settings">("console");
  const [logs, setLogs] = useState<BridgeLogEntry[]>([]);
  const [sendText, setSendText] = useState("");
  const [rawMode, setRawMode] = useState(false);
  const [hardware, setHardware] = useState({ pinStates: [0, 0, 0, 0], adcValues: [0, 0, 0, 0], uptime: 0 });
  const [captainPort, setCaptainPort] = useState("");
  const [clusterConnected, setClusterConnected] = useState(false);
  const [clusterMemberCount, setClusterMemberCount] = useState(0);
  const [clusterLogs, setClusterLogs] = useState<string>("");
  const [inputVector, setInputVector] = useState("0.1,0.2,0.3,0.4");
  const logRef = useRef<HTMLDivElement>(null);
  const intervalRef = useRef<number | null>(null);

  const invoke = useCallback(async (cmd: string, args?: Record<string, unknown>): Promise<unknown> => {
    const { invoke } = await import("@tauri-apps/api/core");
    return invoke(cmd, args);
  }, []);

  const refreshPorts = useCallback(async () => {
    try {
      const list = await invoke("bridge_list_ports") as string[];
      setPorts(list);
      if (list.length > 0 && !selectedPort) {
        setSelectedPort(list[0]);
      }
    } catch (e) {
      setLogs(prev => [...prev, { timestamp_ms: Date.now(), pkt_type: 0x04, text: `Port list error: ${e}` }]);
    }
  }, [invoke, selectedPort]);

  const connect = useCallback(async () => {
    if (!selectedPort) return;
    try {
      await invoke("bridge_connect", { port: selectedPort, baud });
      setConnected(true);
      setLogs(prev => [...prev, { timestamp_ms: Date.now(), pkt_type: 0x10, text: `Connected to ${selectedPort} @ ${baud}` }]);
    } catch (e) {
      setLogs(prev => [...prev, { timestamp_ms: Date.now(), pkt_type: 0x04, text: `Connect failed: ${e}` }]);
    }
  }, [invoke, selectedPort, baud]);

  const disconnect = useCallback(async () => {
    try {
      await invoke("bridge_disconnect");
      setConnected(false);
      setLogs(prev => [...prev, { timestamp_ms: Date.now(), pkt_type: 0x10, text: "Disconnected" }]);
    } catch (e) {
      setLogs(prev => [...prev, { timestamp_ms: Date.now(), pkt_type: 0x04, text: `Disconnect error: ${e}` }]);
    }
  }, [invoke]);

  const connectClusterCaptain = useCallback(async () => {
    if (!captainPort) return;
    try {
      await invoke("cluster_connect_captain", { port: captainPort, baud: 115200 });
      setClusterConnected(true);
      setClusterLogs(prev => prev + `[captain] connected on ${captainPort}\n`);
      const count = await invoke("cluster_discover", { maxShards: 8 }) as number;
      setClusterMemberCount(count);
      setClusterLogs(prev => prev + `[discover] ${count} member board(s) detected\n`);
    } catch (e) {
      setClusterLogs(prev => prev + `[captain error] ${e}\n`);
    }
  }, [invoke, captainPort]);

  const disconnectCluster = useCallback(async () => {
    try {
      await invoke("cluster_disconnect");
      setClusterConnected(false);
      setClusterMemberCount(0);
      setClusterLogs(prev => prev + "[cluster] disconnected\n");
    } catch (e) {
      setClusterLogs(prev => prev + `[cluster error] ${e}\n`);
    }
  }, [invoke]);

  const runClusterInference = useCallback(async () => {
    const values = inputVector.split(",").map(s => parseFloat(s.trim())).filter(n => !isNaN(n));
    if (values.length === 0) return;
    try {
      const result = await invoke("cluster_start_inference", { input: values });
      setClusterLogs(prev => prev + `[inference] ${result}\n`);
    } catch (e) {
      setClusterLogs(prev => prev + `[inference error] ${e}\n`);
    }
  }, [invoke, inputVector]);

  const sendCommand = useCallback(async () => {
    if (!sendText.trim()) return;
    try {
      await invoke("bridge_send_command", { cmd: sendText });
      setLogs(prev => [...prev, { timestamp_ms: Date.now(), pkt_type: 0x10, text: `> ${sendText}` }]);
      setSendText("");
    } catch (e) {
      setLogs(prev => [...prev, { timestamp_ms: Date.now(), pkt_type: 0x04, text: `Send error: ${e}` }]);
    }
  }, [invoke, sendText]);

  const readLogs = useCallback(async () => {
    try {
      const newLogs = await invoke("bridge_read_logs", { limit: 50 }) as BridgeLogEntry[];
      if (newLogs.length > 0) {
        setLogs(prev => {
          const merged = [...prev, ...newLogs];
          if (merged.length > 2000) {
            return merged.slice(merged.length - 2000);
          }
          return merged;
        });
        for (const log of newLogs) {
          if (log.message?.kind === "HardwareStatus") {
            const hw = log.message;
            setHardware({
              pinStates: hw.pin_states.concat(Array(4).fill(0)).slice(0, 4),
              adcValues: hw.adc_values.concat(Array(4).fill(0)).slice(0, 4),
              uptime: hw.uptime_ms,
            });
          }
        }
      }
    } catch (e) {
      // Silently ignore read errors to avoid spamming logs.
    }
  }, [invoke]);

  useEffect(() => {
    refreshPorts();
    intervalRef.current = window.setInterval(() => {
      if (connected) {
        readLogs();
      }
    }, 200);
    return () => {
      if (intervalRef.current) {
        window.clearInterval(intervalRef.current);
      }
    };
  }, [connected, refreshPorts, readLogs]);

  useEffect(() => {
    if (logRef.current) {
      logRef.current.scrollTop = logRef.current.scrollHeight;
    }
  }, [logs]);

  const formatTime = (ms: number) => {
    const d = new Date(ms);
    return d.toLocaleTimeString();
  };

  const typeLabel = (t: number) => TYPE_NAMES[t] || `0x${t.toString(16).padStart(2, "0")}`;

  return (
    <div className="bridge-panel">
      <div className="bridge-header">
        <div className="bridge-title-row">
          <span className="bridge-title">OpenSNAR Bridge</span>
          <button className="bridge-close" onClick={onClose}>x</button>
        </div>
        <div className="bridge-toolbar">
          <select
            className="bridge-select"
            value={selectedPort}
            onChange={e => setSelectedPort(e.target.value)}
            disabled={connected}
          >
            {ports.length === 0 && <option value="">No ports</option>}
            {ports.map(p => <option key={p} value={p}>{p}</option>)}
          </select>
          <select
            className="bridge-select"
            value={baud}
            onChange={e => setBaud(Number(e.target.value))}
            disabled={connected}
          >
            <option value={9600}>9600</option>
            <option value={19200}>19200</option>
            <option value={38400}>38400</option>
            <option value={57600}>57600</option>
            <option value={115200}>115200</option>
            <option value={921600}>921600</option>
          </select>
          <button className="bridge-btn" onClick={refreshPorts} disabled={connected}>Refresh</button>
          {!connected ? (
            <button className="bridge-btn bridge-btn-primary" onClick={connect}>Connect</button>
          ) : (
            <button className="bridge-btn bridge-btn-danger" onClick={disconnect}>Disconnect</button>
          )}
          <span className={`bridge-status ${connected ? "online" : "offline"}`}>
            {connected ? "online" : "offline"}
          </span>
        </div>
        <div className="bridge-tabs">
          <button className={activeTab === "console" ? "active" : ""} onClick={() => setActiveTab("console")}>Console</button>
          <button className={activeTab === "hardware" ? "active" : ""} onClick={() => setActiveTab("hardware")}>Hardware</button>
          <button className={activeTab === "cluster" ? "active" : ""} onClick={() => setActiveTab("cluster")}>Cluster</button>
          <button className={activeTab === "settings" ? "active" : ""} onClick={() => setActiveTab("settings")}>Settings</button>
        </div>
      </div>

      <div className="bridge-body">
        {activeTab === "console" && (
          <div className="bridge-console" ref={logRef}>
            {logs.length === 0 && <div className="bridge-empty">No messages yet.</div>}
            {logs.map((log, i) => (
              <div key={i} className={`bridge-log bridge-log-${typeLabel(log.pkt_type).toLowerCase()}`}>
                <span className="bridge-log-time">{formatTime(log.timestamp_ms)}</span>
                <span className="bridge-log-type">[{typeLabel(log.pkt_type)}]</span>
                <span className="bridge-log-text">{log.text}</span>
              </div>
            ))}
          </div>
        )}

        {activeTab === "hardware" && (
          <div className="bridge-hardware">
            <div className="bridge-hw-section">
              <h4>Pin States</h4>
              <div className="bridge-hw-grid">
                {hardware.pinStates.map((v, i) => (
                  <div key={i} className="bridge-hw-cell">
                    <span className="bridge-hw-label">GPIO{i}</span>
                    <span className={`bridge-hw-value ${v ? "high" : "low"}`}>{v}</span>
                  </div>
                ))}
              </div>
            </div>
            <div className="bridge-hw-section">
              <h4>ADC Values</h4>
              <div className="bridge-hw-grid">
                {hardware.adcValues.map((v, i) => (
                  <div key={i} className="bridge-hw-cell">
                    <span className="bridge-hw-label">ADC{i}</span>
                    <span className="bridge-hw-value">{v}</span>
                  </div>
                ))}
              </div>
            </div>
            <div className="bridge-hw-section">
              <h4>Uptime</h4>
              <div className="bridge-hw-value">{hardware.uptime} ms</div>
            </div>
          </div>
        )}

        {activeTab === "cluster" && (
          <div className="bridge-cluster">
            <div className="bridge-cluster-row">
              <div className="bridge-cluster-node">
                <h4>Captain FPGA</h4>
                <select className="bridge-select" value={captainPort} onChange={e => setCaptainPort(e.target.value)}>
                  {ports.map(p => <option key={p} value={p}>{p}</option>)}
                </select>
                <button className="bridge-btn bridge-btn-primary" onClick={connectClusterCaptain} disabled={clusterConnected}>Connect</button>
                <span className={`bridge-status ${clusterConnected ? "online" : "offline"}`}>
                  {clusterConnected ? "online" : "offline"}
                </span>
              </div>
              <div className="bridge-cluster-node">
                <h4>Chain Members</h4>
                <div className="bridge-cluster-count">{clusterMemberCount}</div>
                <span className="bridge-cluster-label">member board(s)</span>
              </div>
            </div>
            <button className="bridge-btn bridge-btn-danger" onClick={disconnectCluster} disabled={!clusterConnected}>Disconnect</button>
            <div className="bridge-cluster-inference">
              <h4>End-to-End Inference</h4>
              <input
                type="text"
                className="bridge-input"
                value={inputVector}
                onChange={e => setInputVector(e.target.value)}
                placeholder="Input vector: 0.1,0.2,0.3,..."
              />
              <button className="bridge-btn bridge-btn-primary" onClick={runClusterInference}>Run Inference</button>
            </div>
            <div className="bridge-cluster-logs">
              <pre>{clusterLogs}</pre>
            </div>
          </div>
        )}

        {activeTab === "settings" && (
          <div className="bridge-settings">
            <label className="bridge-setting">
              <input type="checkbox" checked={rawMode} onChange={e => setRawMode(e.target.checked)} />
              <span>Raw UART passthrough (send text without protocol frame)</span>
            </label>
            <p className="bridge-hint">
              Protocol: 8-byte header + payload. Magic = 0x4F53 ("OS").
            </p>
          </div>
        )}
      </div>

      <div className="bridge-footer">
        <input
          type="text"
          className="bridge-input"
          value={sendText}
          onChange={e => setSendText(e.target.value)}
          onKeyDown={e => e.key === "Enter" && sendCommand()}
          placeholder={rawMode ? "Send raw bytes as text..." : "Send command..."}
          disabled={!connected}
        />
        <button className="bridge-btn bridge-btn-primary" onClick={sendCommand} disabled={!connected}>Send</button>
      </div>
    </div>
  );
}
