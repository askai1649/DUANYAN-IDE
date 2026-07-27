import { useState, useEffect } from "react";

interface PinInfo {
  number: number;
  label: string;
  function: string;
  description: string;
  side: string;
}

interface Props {
  boardName: string;
}

const FUNCTION_COLORS: Record<string, string> = {
  GPIO: "#888888",
  UART: "#4488cc",
  SPI: "#44aa44",
  I2C: "#ccaa22",
  ADC: "#ee8833",
  PWM: "#aa44cc",
  Power: "#dd3333",
  Ground: "#333333",
  Special: "#66aadd",
};

const FUNCTION_LABELS: Record<string, string> = {
  GPIO: "GPIO",
  UART: "UART",
  SPI: "SPI",
  I2C: "I2C",
  ADC: "ADC",
  PWM: "PWM",
  Power: "Power",
  Ground: "GND",
  Special: "Special",
};

export default function PinDiagram({ boardName }: Props) {
  const [pins, setPins] = useState<PinInfo[]>([]);
  const [selectedPin, setSelectedPin] = useState<PinInfo | null>(null);
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    if (!boardName) return;
    loadPins();
  }, [boardName]);

  const loadPins = async () => {
    setLoading(true);
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const result = await invoke<PinInfo[]>("board_get_pins", { board: boardName });
      setPins(result);
      setSelectedPin(null);
    } catch (e) {
      console.error("Failed to load pins:", e);
    } finally {
      setLoading(false);
    }
  };

  const leftPins = pins.filter(p => p.side === "left");
  const rightPins = pins.filter(p => p.side === "right");
  const maxRows = Math.max(leftPins.length, rightPins.length);

  const boardWidth = 220;
  const boardHeight = Math.max(180, maxRows * 24 + 40);
  const pinSpacing = 22;
  const startY = 30;

  if (loading) {
    return <div className="pin-diagram-loading">Loading pins...</div>;
  }

  if (pins.length === 0) {
    return <div className="pin-diagram-empty">Select a board to view pin diagram</div>;
  }

  return (
    <div className="pin-diagram">
      <div className="pin-diagram-header">
        <span className="pin-diagram-title">Pin Diagram - {boardName}</span>
      </div>

      {/* Legend */}
      <div className="pin-legend">
        {Object.entries(FUNCTION_LABELS).map(([key, label]) => (
          <span key={key} className="pin-legend-item">
            <span className="pin-legend-dot" style={{ background: FUNCTION_COLORS[key] }} />
            {label}
          </span>
        ))}
      </div>

      {/* SVG Board */}
      <svg width={boardWidth + 180} height={boardHeight + 20} className="pin-svg">
        {/* Board body */}
        <rect x={80} y={10} width={boardWidth - 160} height={boardHeight} rx={4}
          fill="#1a3a1a" stroke="#3a6a3a" strokeWidth={1.5} />

        {/* USB connector */}
        <rect x={80 + (boardWidth - 160) / 2 - 15} y={2} width={30} height={12} rx={2}
          fill="#555" stroke="#777" strokeWidth={1} />

        {/* Board label */}
        <text x={80 + (boardWidth - 160) / 2} y={boardHeight / 2 + 10}
          textAnchor="middle" fill="#5a8a5a" fontSize={10} fontWeight="bold">
          {boardName.split("-")[0]}
        </text>

        {/* Left pins */}
        {leftPins.map((pin, i) => {
          const y = startY + i * pinSpacing;
          const color = FUNCTION_COLORS[pin.function] || "#888";
          const isSelected = selectedPin?.number === pin.number;
          return (
            <g key={pin.number} onClick={() => setSelectedPin(pin)} style={{ cursor: "pointer" }}>
              {/* Pin pad */}
              <circle cx={76} cy={y} r={isSelected ? 6 : 4}
                fill={isSelected ? color : "#333"} stroke={color} strokeWidth={isSelected ? 2 : 1} />
              {/* Pin line */}
              <line x1={62} y1={y} x2={76} y2={y} stroke={color} strokeWidth={1} />
              {/* Label */}
              <text x={58} y={y + 3} textAnchor="end" fill={color} fontSize={9}
                fontFamily="'Source Code Pro', monospace">
                {pin.label}
              </text>
            </g>
          );
        })}

        {/* Right pins */}
        {rightPins.map((pin, i) => {
          const y = startY + i * pinSpacing;
          const color = FUNCTION_COLORS[pin.function] || "#888";
          const isSelected = selectedPin?.number === pin.number;
          const rightX = 80 + (boardWidth - 160);
          return (
            <g key={pin.number} onClick={() => setSelectedPin(pin)} style={{ cursor: "pointer" }}>
              <circle cx={rightX + 4} cy={y} r={isSelected ? 6 : 4}
                fill={isSelected ? color : "#333"} stroke={color} strokeWidth={isSelected ? 2 : 1} />
              <line x1={rightX + 4} y1={y} x2={rightX + 18} y2={y} stroke={color} strokeWidth={1} />
              <text x={rightX + 22} y={y + 3} textAnchor="start" fill={color} fontSize={9}
                fontFamily="'Source Code Pro', monospace">
                {pin.label}
              </text>
            </g>
          );
        })}
      </svg>

      {/* Selected pin detail */}
      {selectedPin && (
        <div className="pin-detail">
          <div className="pin-detail-row">
            <span className="pin-detail-label">Pin:</span>
            <span className="pin-detail-value">{selectedPin.label}</span>
          </div>
          <div className="pin-detail-row">
            <span className="pin-detail-label">Function:</span>
            <span className="pin-detail-value" style={{ color: FUNCTION_COLORS[selectedPin.function] }}>
              {selectedPin.function}
            </span>
          </div>
          <div className="pin-detail-row">
            <span className="pin-detail-label">Description:</span>
            <span className="pin-detail-value">{selectedPin.description}</span>
          </div>
        </div>
      )}
    </div>
  );
}
