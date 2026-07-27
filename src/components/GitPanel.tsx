import { useState, useEffect, useCallback } from "react";
import { invoke } from "@tauri-apps/api/core";

interface FileChange {
  path: string;
  status: string;
}

interface GitStatus {
  branch: string;
  staged: FileChange[];
  unstaged: FileChange[];
  untracked: string[];
  ahead: number;
  behind: number;
}

interface GitLogEntry {
  hash: string;
  author: string;
  date: string;
  message: string;
}

interface BranchInfo {
  name: string;
  is_current: boolean;
  is_remote: boolean;
}

interface Props {
  projectDir: string;
  onLog: (msg: string) => void;
  onShowDiff: (file: string) => void;
}

export default function GitPanel({ projectDir, onLog, onShowDiff }: Props) {
  const [status, setStatus] = useState<GitStatus | null>(null);
  const [logs, setLogs] = useState<GitLogEntry[]>([]);
  const [branches, setBranches] = useState<BranchInfo[]>([]);
  const [commitMsg, setCommitMsg] = useState("");
  const [newBranch, setNewBranch] = useState("");
  const [showBranchInput, setShowBranchInput] = useState(false);
  const [loading, setLoading] = useState(false);

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const s = await invoke<GitStatus>("git_status", { dir: projectDir });
      setStatus(s);
      const l = await invoke<GitLogEntry[]>("git_log", { dir: projectDir, n: 10 });
      setLogs(l);
      const b = await invoke<BranchInfo[]>("git_branches", { dir: projectDir });
      setBranches(b);
    } catch (e) {
      onLog(`Git: ${e}`);
    }
    setLoading(false);
  }, [projectDir, onLog]);

  useEffect(() => {
    refresh();
  }, [refresh]);

  const handleStage = async (files: string[]) => {
    try {
      await invoke("git_add", { dir: projectDir, files });
      refresh();
    } catch (e) { onLog(`Stage failed: ${e}`); }
  };

  const handleStageAll = async () => {
    try {
      await invoke("git_add_all", { dir: projectDir });
      refresh();
    } catch (e) { onLog(`Stage all failed: ${e}`); }
  };

  const handleUnstage = async (files: string[]) => {
    try {
      await invoke("git_unstage", { dir: projectDir, files });
      refresh();
    } catch (e) { onLog(`Unstage failed: ${e}`); }
  };

  const handleCommit = async () => {
    if (!commitMsg.trim()) return;
    try {
      const out = await invoke<string>("git_commit", { dir: projectDir, message: commitMsg });
      onLog(`Committed: ${out}`);
      setCommitMsg("");
      refresh();
    } catch (e) { onLog(`Commit failed: ${e}`); }
  };

  const handlePush = async () => {
    try {
      const out = await invoke<string>("git_push", { dir: projectDir, remote: null, branch: null });
      onLog(`Push: ${out}`);
      refresh();
    } catch (e) { onLog(`Push failed: ${e}`); }
  };

  const handlePull = async () => {
    try {
      const out = await invoke<string>("git_pull", { dir: projectDir });
      onLog(`Pull: ${out}`);
      refresh();
    } catch (e) { onLog(`Pull failed: ${e}`); }
  };

  const handleCheckout = async (branch: string) => {
    try {
      await invoke("git_checkout", { dir: projectDir, branch });
      onLog(`Switched to ${branch}`);
      refresh();
    } catch (e) { onLog(`Checkout failed: ${e}`); }
  };

  const handleCreateBranch = async () => {
    if (!newBranch.trim()) return;
    try {
      await invoke("git_create_branch", { dir: projectDir, name: newBranch });
      onLog(`Created and switched to ${newBranch}`);
      setNewBranch("");
      setShowBranchInput(false);
      refresh();
    } catch (e) { onLog(`Create branch failed: ${e}`); }
  };

  const handleInit = async () => {
    try {
      await invoke("git_init", { dir: projectDir });
      onLog("Git repository initialized");
      refresh();
    } catch (e) { onLog(`Init failed: ${e}`); }
  };

  const handleInstallHooks = async () => {
    try {
      await invoke("git_install_hooks", { dir: projectDir });
      onLog("Pre-commit hook installed (runs snar lint on commit)");
    } catch (e) { onLog(`Hook install failed: ${e}`); }
  };

  if (loading && !status) {
    return <div className="git-panel"><div className="loading">Loading...</div></div>;
  }

  if (!status) {
    return (
      <div className="git-panel">
        <div className="git-empty">
          <h3>No Git Repository</h3>
          <button className="btn-primary" onClick={handleInit}>Initialize Git</button>
        </div>
      </div>
    );
  }

  return (
    <div className="git-panel">
      <div className="git-header">
        <div className="git-branch-info">
          <span className="branch-icon">&#x2387;</span>
          <select
            value={status.branch}
            onChange={(e) => handleCheckout(e.target.value)}
            className="branch-select"
          >
            {branches.filter(b => !b.is_remote).map(b => (
              <option key={b.name} value={b.name}>{b.name}</option>
            ))}
          </select>
          <button className="btn-xs" onClick={() => setShowBranchInput(!showBranchInput)}>+</button>
        </div>
        {(status.ahead > 0 || status.behind > 0) && (
          <div className="git-sync-info">
            {status.ahead > 0 && <span>&uarr;{status.ahead}</span>}
            {status.behind > 0 && <span>&darr;{status.behind}</span>}
          </div>
        )}
      </div>

      {showBranchInput && (
        <div className="git-new-branch">
          <input
            type="text" placeholder="New branch name" value={newBranch}
            onChange={(e) => setNewBranch(e.target.value)}
            onKeyDown={(e) => e.key === "Enter" && handleCreateBranch()}
          />
          <button className="btn-xs" onClick={handleCreateBranch}>Create</button>
        </div>
      )}

      <div className="git-actions">
        <button className="btn-sm" onClick={handlePush}>Push</button>
        <button className="btn-sm" onClick={handlePull}>Pull</button>
        <button className="btn-sm" onClick={handleInstallHooks} title="Install pre-commit hook">Hooks</button>
      </div>

      {/* Staged files */}
      <div className="git-section">
        <div className="git-section-header">
          <span>Staged ({status.staged.length})</span>
          <button className="btn-xs" onClick={handleStageAll}>Stage All</button>
        </div>
        <div className="git-file-list">
          {status.staged.map((f, i) => (
            <div key={i} className="git-file staged">
              <span className="file-status">{f.status}</span>
              <span className="file-path" onClick={() => onShowDiff(f.path)}>{f.path}</span>
              <button className="btn-xs" onClick={() => handleUnstage([f.path])}>-</button>
            </div>
          ))}
        </div>
      </div>

      {/* Unstaged files */}
      <div className="git-section">
        <div className="git-section-header">
          <span>Changes ({status.unstaged.length})</span>
        </div>
        <div className="git-file-list">
          {status.unstaged.map((f, i) => (
            <div key={i} className="git-file unstaged">
              <span className="file-status">{f.status}</span>
              <span className="file-path" onClick={() => onShowDiff(f.path)}>{f.path}</span>
              <button className="btn-xs" onClick={() => handleStage([f.path])}>+</button>
            </div>
          ))}
        </div>
      </div>

      {/* Untracked files */}
      {status.untracked.length > 0 && (
        <div className="git-section">
          <div className="git-section-header"><span>Untracked ({status.untracked.length})</span></div>
          <div className="git-file-list">
            {status.untracked.map((f, i) => (
              <div key={i} className="git-file untracked">
                <span className="file-status">?</span>
                <span className="file-path">{f}</span>
                <button className="btn-xs" onClick={() => handleStage([f])}>+</button>
              </div>
            ))}
          </div>
        </div>
      )}

      {/* Commit */}
      <div className="git-commit">
        <input
          type="text"
          placeholder="Commit message..."
          value={commitMsg}
          onChange={(e) => setCommitMsg(e.target.value)}
          onKeyDown={(e) => e.key === "Enter" && handleCommit()}
        />
        <button className="btn-primary btn-sm" onClick={handleCommit} disabled={!commitMsg.trim()}>
          Commit
        </button>
      </div>

      {/* Recent commits */}
      <div className="git-log">
        <h4>Recent Commits</h4>
        {logs.map((log, i) => (
          <div key={i} className="git-log-entry">
            <span className="log-hash">{log.hash.substring(0, 7)}</span>
            <span className="log-msg">{log.message}</span>
            <span className="log-date">{new Date(log.date).toLocaleDateString()}</span>
          </div>
        ))}
      </div>
    </div>
  );
}
