import { useState, useEffect, useRef, useCallback } from "react";

interface PortInfo {
  name: string;
  description: string;
}

interface Props {
  visible: boolean;
}

export default function SerialMonitor({ visible }: Props) {
  const [ports, setPorts] = useState<PortInfo[]>([]);
  const [selectedPort, setSelectedPort] = useState("");
  const [baud, setBaud] = useState(115200);
  const [connected, setConnected] = useState(false);
  const [output, setOutput] = useState<string[]>([]);
  const [input, setInput] = useState("");
  const bodyRef = useRef<HTMLDivElement>(null);
  const pollRef = useRef<ReturnType<typeof setInterval> | null>(null);

  useEffect(() => {
    loadPorts();
  }, []);

  const loadPorts = async () => {
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const result = await invoke<PortInfo[]>("serial_list_ports");
      setPorts(result);
      if (result.length > 0 && !selectedPort) {
        setSelectedPort(result[0].name);
      }
    } catch (e) {
      console.error("Failed to list ports:", e);
    }
  };

  const handleConnect = useCallback(async () => {
    if (connected) {
      // Disconnect
      try {
        const { invoke } = await import("@tauri-apps/api/core");
        await invoke("serial_close");
        setConnected(false);
        if (pollRef.current) {
          clearInterval(pollRef.current);
          pollRef.current = null;
        }
        setOutput(prev => [...prev, "--- Disconnected ---"]);
      } catch (e) {
        console.error("Disconnect failed:", e);
      }
      return;
    }

    if (!selectedPort) return;

    try {
      const { invoke } = await import("@tauri-apps/api/core");
      await invoke("serial_open", { port: selectedPort, baud });
      setConnected(true);
      setOutput(prev => [...prev, `--- Connected to ${selectedPort} @ ${baud} ---`]);

      // Start polling
      pollRef.current = setInterval(async () => {
        try {
          const { invoke } = await import("@tauri-apps/api/core");
          const data = await invoke<string>("serial_read");
          if (data && data.length > 0) {
            setOutput(prev => {
              const next = [...prev, data];
              if (next.length > 1000) next.splice(0, next.length - 1000);
              return next;
            });
          }
        } catch {
          // Port may have been closed externally
        }
      }, 200);
    } catch (e) {
      setOutput(prev => [...prev, `[error] ${e}`]);
    }
  }, [connected, selectedPort, baud]);

  const handleSend = useCallback(async () => {
    if (!connected || !input.trim()) return;
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      await invoke("serial_write", { data: input + "\n" });
      setOutput(prev => [...prev, `> ${input}`]);
      setInput("");
    } catch (e) {
      setOutput(prev => [...prev, `[error] ${e}`]);
    }
  }, [connected, input]);

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === "Enter") {
      e.preventDefault();
      handleSend();
    }
  };

  useEffect(() => {
    if (bodyRef.current) {
      bodyRef.current.scrollTop = bodyRef.current.scrollHeight;
    }
  }, [output]);

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      if (pollRef.current) clearInterval(pollRef.current);
    };
  }, []);

  if (!visible) return null;

  return (
    <div className="serial-monitor">
      <div className="serial-toolbar">
        <select
          className="serial-port-select"
          value={selectedPort}
          onChange={(e) => setSelectedPort(e.target.value)}
          disabled={connected}
        >
          {ports.map((p) => (
            <option key={p.name} value={p.name}>{p.name} - {p.description}</option>
          ))}
        </select>
        <select
          className="serial-baud-select"
          value={baud}
          onChange={(e) => setBaud(Number(e.target.value))}
          disabled={connected}
        >
          {[9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600].map((b) => (
            <option key={b} value={b}>{b}</option>
          ))}
        </select>
        <button
          className={`serial-connect-btn ${connected ? "connected" : ""}`}
          onClick={handleConnect}
        >
          {connected ? "Disconnect" : "Connect"}
        </button>
        <button className="serial-refresh-btn" onClick={loadPorts} title="Refresh ports">
          Refresh
        </button>
        <button className="serial-clear-btn" onClick={() => setOutput([])}>
          Clear
        </button>
      </div>

      <div className="serial-output" ref={bodyRef}>
        {output.map((line, i) => (
          <div key={i} className={`serial-line ${line.startsWith("[error]") ? "error" : line.startsWith(">") ? "sent" : ""}`}>
            {line}
          </div>
        ))}
        {output.length === 0 && (
          <div className="serial-empty">Serial output will appear here...</div>
        )}
      </div>

      <div className="serial-input-row">
        <input
          className="serial-input"
          value={input}
          onChange={(e) => setInput(e.target.value)}
          onKeyDown={handleKeyDown}
          placeholder={connected ? "Type command and press Enter..." : "Connect first..."}
          disabled={!connected}
        />
        <button
          className="serial-send-btn"
          onClick={handleSend}
          disabled={!connected}
        >
          Send
        </button>
      </div>
    </div>
  );
}
