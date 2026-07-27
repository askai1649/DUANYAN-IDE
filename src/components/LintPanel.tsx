import { useState, useEffect } from "react";
import { invoke } from "@tauri-apps/api/core";

interface LintDiagnostic {
  rule_id: string;
  severity: string;
  message: string;
  file: string;
  line: number;
  column: number;
  suggestion: string | null;
  has_fix: boolean;
}

interface LintResult {
  diagnostics: LintDiagnostic[];
  error_count: number;
  warning_count: number;
  fixable_count: number;
}

interface LintRule {
  id: string;
  description: string;
  severity: string;
  fixable: boolean;
}

interface Props {
  projectDir: string;
  onLog: (msg: string) => void;
  onJumpToLine: (file: string, line: number) => void;
}

export default function LintPanel({ projectDir, onLog, onJumpToLine }: Props) {
  const [result, setResult] = useState<LintResult | null>(null);
  const [rules, setRules] = useState<LintRule[]>([]);
  const [showRules, setShowRules] = useState(false);
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    loadRules();
  }, []);

  const loadRules = async () => {
    try {
      const r = await invoke<LintRule[]>("lint_rules");
      setRules(r);
    } catch (e) {
      console.error("Load rules failed:", e);
    }
  };

  const handleLint = async () => {
    setLoading(true);
    try {
      const r = await invoke<LintResult>("lint_project", { dir: projectDir });
      setResult(r);
      onLog(`Lint: ${r.error_count} errors, ${r.warning_count} warnings`);
    } catch (e) {
      onLog(`Lint failed: ${e}`);
    }
    setLoading(false);
  };

  const handleLintFile = async (source: string) => {
    setLoading(true);
    try {
      const r = await invoke<LintResult>("lint_file_content", { source, filePath: "current.js" });
      setResult(r);
    } catch (e) {
      onLog(`Lint file failed: ${e}`);
    }
    setLoading(false);
  };

  const handleFixAll = async () => {
    setLoading(true);
    try {
      const msg = await invoke<string>("lint_fix", { dir: projectDir });
      onLog(msg);
      handleLint(); // Re-lint after fix
    } catch (e) {
      onLog(`Fix failed: ${e}`);
    }
    setLoading(false);
  };

  const getSeverityClass = (severity: string) => {
    switch (severity) {
      case "Error": return "lint-error";
      case "Warning": return "lint-warning";
      default: return "lint-info";
    }
  };

  const getSeverityIcon = (severity: string) => {
    switch (severity) {
      case "Error": return "\u2716";
      case "Warning": return "\u26A0";
      default: return "\u2139";
    }
  };

  return (
    <div className="lint-panel">
      <div className="lint-header">
        <h3>Code Lint</h3>
        <div className="lint-actions">
          <button className="btn-sm" onClick={handleLint} disabled={loading}>
            {loading ? "Checking..." : "Lint Project"}
          </button>
          <button className="btn-sm" onClick={handleFixAll} disabled={loading}>
            Fix All
          </button>
          <button
            className={`btn-sm ${showRules ? "active" : ""}`}
            onClick={() => setShowRules(!showRules)}
          >
            Rules
          </button>
        </div>
      </div>

      {showRules && (
        <div className="lint-rules-list">
          <h4>Lint Rules ({rules.length})</h4>
          <table className="rules-table">
            <thead>
              <tr>
                <th>Rule</th>
                <th>Severity</th>
                <th>Fixable</th>
                <th>Description</th>
              </tr>
            </thead>
            <tbody>
              {rules.map((r) => (
                <tr key={r.id}>
                  <td className="rule-id">{r.id}</td>
                  <td className={`rule-severity ${r.severity.toLowerCase()}`}>{r.severity}</td>
                  <td>{r.fixable ? "\u2713" : "-"}</td>
                  <td>{r.description}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}

      {result && (
        <div className="lint-results">
          <div className="lint-summary">
            <span className="lint-errors">
              {getSeverityIcon("Error")} {result.error_count} errors
            </span>
            <span className="lint-warnings">
              {getSeverityIcon("Warning")} {result.warning_count} warnings
            </span>
            {result.fixable_count > 0 && (
              <span className="lint-fixable">
                {result.fixable_count} fixable
              </span>
            )}
          </div>

          <div className="lint-diagnostics">
            {result.diagnostics.map((d, i) => (
              <div
                key={i}
                className={`lint-diagnostic ${getSeverityClass(d.severity)}`}
                onClick={() => onJumpToLine(d.file, d.line)}
              >
                <span className="diag-icon">{getSeverityIcon(d.severity)}</span>
                <span className="diag-location">
                  {d.file}:{d.line}:{d.column}
                </span>
                <span className="diag-message">{d.message}</span>
                <span className="diag-rule">[{d.rule_id}]</span>
                {d.suggestion && (
                  <span className="diag-suggestion">{d.suggestion}</span>
                )}
              </div>
            ))}
            {result.diagnostics.length === 0 && (
              <div className="lint-clean">All checks passed!</div>
            )}
          </div>
        </div>
      )}
    </div>
  );
}
