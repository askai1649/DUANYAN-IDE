import { useState, useEffect, useCallback } from "react";

interface BoardInfo {
  name: string;
  description: string;
  mcu: string;
  frequency_mhz: number;
  ram_kb: number;
  flash_kb: number;
  gpio_count: number;
  uart_count: number;
  adc_channels: number;
  pwm_channels: number;
  spi_count: number;
  i2c_count: number;
}

interface DetectedBoard {
  port: string;
  description: string;
  board: BoardInfo | null;
  vid: number | null;
  pid: number | null;
}

interface Props {
  selectedTarget: string;
  onTargetChange: (target: string) => void;
  onClose: () => void;
}

export default function BoardPanel({ selectedTarget, onTargetChange, onClose }: Props) {
  const [boards, setBoards] = useState<BoardInfo[]>([]);
  const [detected, setDetected] = useState<DetectedBoard[]>([]);
  const [selectedBoard, setSelectedBoard] = useState<BoardInfo | null>(null);
  const [scanning, setScanning] = useState(false);

  useEffect(() => {
    loadBoards();
  }, []);

  const loadBoards = async () => {
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const all = await invoke<BoardInfo[]>("board_get_all");
      setBoards(all);
      // Set initial selection
      if (!selectedTarget && all.length > 0) {
        onTargetChange(all[0].name);
        setSelectedBoard(all[0]);
      }
    } catch (e) {
      console.error("Failed to load boards:", e);
    }
  };

  const handleScan = useCallback(async () => {
    setScanning(true);
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const result = await invoke<DetectedBoard[]>("board_scan");
      setDetected(result);
    } catch (e) {
      console.error("Scan failed:", e);
    } finally {
      setScanning(false);
    }
  }, []);

  const handleSelectBoard = (board: BoardInfo) => {
    setSelectedBoard(board);
    onTargetChange(board.name);
  };

  const formatKB = (kb: number) => {
    if (kb >= 1024) return `${(kb / 1024).toFixed(1)}MB`;
    return `${kb}KB`;
  };

  return (
    <div className="board-panel">
      <div className="board-panel-header">
        <span className="board-panel-title">Board Manager</span>
        <button className="board-close" onClick={onClose}>x</button>
      </div>

      {/* Target selector */}
      <div className="board-target-section">
        <label className="board-label">Target Board</label>
        <select
          className="board-select"
          value={selectedTarget}
          onChange={(e) => {
            onTargetChange(e.target.value);
            const b = boards.find(b => b.name === e.target.value);
            if (b) setSelectedBoard(b);
          }}
        >
          {boards.map((b) => (
            <option key={b.name} value={b.name}>{b.name}</option>
          ))}
        </select>
      </div>

      {/* USB scan */}
      <div className="board-scan-section">
        <button className="board-scan-btn" onClick={handleScan} disabled={scanning}>
          {scanning ? "Scanning..." : "Scan USB"}
        </button>
        {detected.length > 0 && (
          <div className="board-detected-list">
            {detected.map((d, i) => (
              <div key={i} className="board-detected-item">
                <span className="board-port">{d.port}</span>
                <span className="board-desc">{d.board?.name || d.description || "Unknown"}</span>
              </div>
            ))}
          </div>
        )}
        {detected.length === 0 && !scanning && (
          <div className="board-no-devices">No USB devices detected</div>
        )}
      </div>

      {/* Board specs */}
      {selectedBoard && (
        <div className="board-specs">
          <div className="board-specs-title">{selectedBoard.name}</div>
          <div className="board-specs-desc">{selectedBoard.description}</div>
          <div className="board-specs-grid">
            <div className="board-spec-item">
              <span className="spec-label">MCU</span>
              <span className="spec-value">{selectedBoard.mcu}</span>
            </div>
            <div className="board-spec-item">
              <span className="spec-label">Clock</span>
              <span className="spec-value">{selectedBoard.frequency_mhz}MHz</span>
            </div>
            <div className="board-spec-item">
              <span className="spec-label">RAM</span>
              <span className="spec-value">{formatKB(selectedBoard.ram_kb)}</span>
            </div>
            <div className="board-spec-item">
              <span className="spec-label">Flash</span>
              <span className="spec-value">{formatKB(selectedBoard.flash_kb)}</span>
            </div>
            <div className="board-spec-item">
              <span className="spec-label">GPIO</span>
              <span className="spec-value">{selectedBoard.gpio_count}</span>
            </div>
            <div className="board-spec-item">
              <span className="spec-label">UART</span>
              <span className="spec-value">{selectedBoard.uart_count}</span>
            </div>
            <div className="board-spec-item">
              <span className="spec-label">ADC</span>
              <span className="spec-value">{selectedBoard.adc_channels}</span>
            </div>
            <div className="board-spec-item">
              <span className="spec-label">PWM</span>
              <span className="spec-value">{selectedBoard.pwm_channels}</span>
            </div>
            <div className="board-spec-item">
              <span className="spec-label">SPI</span>
              <span className="spec-value">{selectedBoard.spi_count}</span>
            </div>
            <div className="board-spec-item">
              <span className="spec-label">I2C</span>
              <span className="spec-value">{selectedBoard.i2c_count}</span>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
