import { useState, useEffect } from "react";
import { invoke } from "@tauri-apps/api/core";

interface DiffLine {
  kind: "Context" | "Added" | "Removed" | "Header";
  content: string;
  old_lineno: number | null;
  new_lineno: number | null;
}

interface DiffHunk {
  old_start: number;
  old_count: number;
  new_start: number;
  new_count: number;
  lines: DiffLine[];
}

interface FileDiff {
  path: string;
  hunks: DiffHunk[];
}

interface Props {
  projectDir: string;
  file: string | null;
  onClose: () => void;
}

export default function DiffView({ projectDir, file, onClose }: Props) {
  const [diffs, setDiffs] = useState<FileDiff[]>([]);
  const [loading, setLoading] = useState(false);
  const [viewMode, setViewMode] = useState<"diff" | "staged">("diff");

  useEffect(() => {
    loadDiff();
  }, [file, viewMode]);

  const loadDiff = async () => {
    setLoading(true);
    try {
      let result: FileDiff[];
      if (viewMode === "staged") {
        result = await invoke<FileDiff[]>("git_staged_diff", { dir: projectDir });
      } else {
        result = await invoke<FileDiff[]>("git_diff", { dir: projectDir, file });
      }
      setDiffs(result);
    } catch (e) {
      console.error("Diff load failed:", e);
    }
    setLoading(false);
  };

  const getLineClass = (kind: string) => {
    switch (kind) {
      case "Added": return "diff-line-added";
      case "Removed": return "diff-line-removed";
      case "Header": return "diff-line-header";
      default: return "diff-line-context";
    }
  };

  return (
    <div className="diff-view">
      <div className="diff-header">
        <div className="diff-tabs">
          <button
            className={`diff-tab ${viewMode === "diff" ? "active" : ""}`}
            onClick={() => setViewMode("diff")}
          >
            Working Tree
          </button>
          <button
            className={`diff-tab ${viewMode === "staged" ? "active" : ""}`}
            onClick={() => setViewMode("staged")}
          >
            Staged
          </button>
        </div>
        <button className="btn-xs" onClick={onClose}>&times;</button>
      </div>

      {loading ? (
        <div className="diff-loading">Loading diff...</div>
      ) : diffs.length === 0 ? (
        <div className="diff-empty">No changes to display</div>
      ) : (
        <div className="diff-content">
          {diffs.map((diff, fi) => (
            <div key={fi} className="diff-file">
              <div className="diff-file-header">
                <span className="diff-file-path">{diff.path}</span>
              </div>
              {diff.hunks.map((hunk, hi) => (
                <div key={hi} className="diff-hunk">
                  <div className="diff-hunk-header">
                    @@ -{hunk.old_start},{hunk.old_count} +{hunk.new_start},{hunk.new_count} @@
                  </div>
                  <table className="diff-table">
                    <tbody>
                      {hunk.lines.map((line, li) => (
                        <tr key={li} className={getLineClass(line.kind)}>
                          <td className="line-num old">
                            {line.old_lineno ?? ""}
                          </td>
                          <td className="line-num new">
                            {line.new_lineno ?? ""}
                          </td>
                          <td className="line-prefix">
                            {line.kind === "Added" ? "+" : line.kind === "Removed" ? "-" : " "}
                          </td>
                          <td className="line-content">{line.content}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              ))}
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
