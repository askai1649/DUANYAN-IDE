import { useState, useCallback } from "react";

interface BenchResult {
  flash_bytes: number;
  ram_bytes: number;
  instruction_count: number;
  grade: string;
  details: string;
}

interface BenchComparison {
  snarjs_flash: number;
  c_flash: number;
  snarjs_ram: number;
  c_ram: number;
  flash_overhead_pct: number;
  ram_overhead_pct: number;
  grade: string;
}

interface Props {
  visible: boolean;
  source: string;
  target: string;
}

const GRADE_COLORS: Record<string, string> = {
  A: "#4ec9b0",
  B: "#dcdcaa",
  C: "#ce9178",
  D: "#f44747",
};

export default function BenchmarkPanel({ visible, source, target }: Props) {
  const [result, setResult] = useState<BenchResult | null>(null);
  const [comparison, setComparison] = useState<BenchComparison | null>(null);
  const [running, setRunning] = useState(false);
  const [error, setError] = useState("");

  const handleRun = useCallback(async () => {
    if (!source.trim()) return;
    setRunning(true);
    setError("");
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const [benchResult, benchCompare] = await Promise.all([
        invoke<BenchResult>("bench_run", { source, target }),
        invoke<BenchComparison>("bench_compare", { source, target }),
      ]);
      setResult(benchResult);
      setComparison(benchCompare);
    } catch (e) {
      setError(String(e));
    } finally {
      setRunning(false);
    }
  }, [source, target]);

  if (!visible) return null;

  const formatKB = (bytes: number) => (bytes / 1024).toFixed(1);
  const gradeColor = (g: string) => GRADE_COLORS[g] || "#888";

  return (
    <div className="bench-panel">
      <div className="bench-header">
        <span className="bench-title">Benchmark</span>
        <button className="bench-run-btn" onClick={handleRun} disabled={running || !source.trim()}>
          {running ? "Running..." : "Run Benchmark"}
        </button>
      </div>

      {error && <div className="bench-error">{error}</div>}

      {!result && !running && (
        <div className="bench-empty">Click "Run Benchmark" to analyze your code</div>
      )}

      {result && (
        <div className="bench-results">
          {/* Grade */}
          <div className="bench-grade-section">
            <div className="bench-grade" style={{ color: gradeColor(result.grade) }}>
              {result.grade}
            </div>
            <div className="bench-grade-label">Grade</div>
          </div>

          {/* Metrics */}
          <div className="bench-metrics">
            <div className="bench-metric">
              <span className="bench-metric-value">{result.instruction_count}</span>
              <span className="bench-metric-label">Instructions</span>
            </div>
            <div className="bench-metric">
              <span className="bench-metric-value">{formatKB(result.flash_bytes)}KB</span>
              <span className="bench-metric-label">Flash</span>
            </div>
            <div className="bench-metric">
              <span className="bench-metric-value">{formatKB(result.ram_bytes)}KB</span>
              <span className="bench-metric-label">RAM</span>
            </div>
          </div>

          {/* Comparison */}
          {comparison && (
            <div className="bench-comparison">
              <div className="bench-comp-title">SNARjs vs C Comparison</div>

              <div className="bench-comp-row">
                <span className="bench-comp-label">Flash</span>
                <div className="bench-comp-bars">
                  <div className="bench-bar-container">
                    <div className="bench-bar bench-bar-snarjs"
                      style={{ width: `${Math.min(100, (comparison.snarjs_flash / Math.max(comparison.snarjs_flash, comparison.c_flash)) * 100)}%` }}>
                      SNARjs {formatKB(comparison.snarjs_flash)}KB
                    </div>
                  </div>
                  <div className="bench-bar-container">
                    <div className="bench-bar bench-bar-c"
                      style={{ width: `${Math.min(100, (comparison.c_flash / Math.max(comparison.snarjs_flash, comparison.c_flash)) * 100)}%` }}>
                      C (est.) {formatKB(comparison.c_flash)}KB
                    </div>
                  </div>
                </div>
                <span className="bench-comp-overhead">+{comparison.flash_overhead_pct.toFixed(0)}%</span>
              </div>

              <div className="bench-comp-row">
                <span className="bench-comp-label">RAM</span>
                <div className="bench-comp-bars">
                  <div className="bench-bar-container">
                    <div className="bench-bar bench-bar-snarjs"
                      style={{ width: `${Math.min(100, (comparison.snarjs_ram / Math.max(comparison.snarjs_ram, comparison.c_ram)) * 100)}%` }}>
                      SNARjs {formatKB(comparison.snarjs_ram)}KB
                    </div>
                  </div>
                  <div className="bench-bar-container">
                    <div className="bench-bar bench-bar-c"
                      style={{ width: `${Math.min(100, (comparison.c_ram / Math.max(comparison.snarjs_ram, comparison.c_ram)) * 100)}%` }}>
                      C (est.) {formatKB(comparison.c_ram)}KB
                    </div>
                  </div>
                </div>
                <span className="bench-comp-overhead">+{comparison.ram_overhead_pct.toFixed(0)}%</span>
              </div>
            </div>
          )}
        </div>
      )}
    </div>
  );
}
