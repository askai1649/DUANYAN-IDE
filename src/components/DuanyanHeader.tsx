// DuanyanHeader — AI 面板头部 + 可折叠模型指标徽章行
// 三期: Secondary Sidebar 化 — 指标默认折叠为一行徽章, 点击展开完整网格
import { useState } from "react";

interface Props {
  isProcessing: boolean;
  showToolbar: boolean;
  onToggleToolbar: () => void;
  onClear: () => void;
  onClose: () => void;
  useBackprop: boolean;
  clock: number;
}

export default function DuanyanHeader({ isProcessing, showToolbar, onToggleToolbar, onClear, onClose, useBackprop, clock }: Props) {
  const [statsOpen, setStatsOpen] = useState(false);

  return (
    <>
      <div className="duanyan-header">
        <div className="duanyan-header-left">
          <span className="duanyan-brand">duanyan</span>
          <span className="duanyan-version">v5.0</span>
          <span className={"duanyan-status-dot " + (isProcessing ? "thinking" : "idle")}></span>
          <span className="duanyan-status-text">{isProcessing ? "busy" : "ready"}</span>
        </div>
        <div className="duanyan-header-right">
          <button className={"duanyan-header-btn" + (showToolbar ? " active" : "")} onClick={onToggleToolbar} title="Toggle toolbar">Tools</button>
          <button className="duanyan-header-btn" onClick={onClear} title="Clear">Clear</button>
          <button className="duanyan-header-btn" onClick={onClose} title="Close">
            <svg width="10" height="10" viewBox="0 0 10 10">
              <line x1="0" y1="0" x2="10" y2="10" stroke="currentColor" strokeWidth="1.2"/>
              <line x1="10" y1="0" x2="0" y2="10" stroke="currentColor" strokeWidth="1.2"/>
            </svg>
          </button>
        </div>
      </div>

      {/* 指标徽章行: 默认折叠, 点击展开 */}
      <button
        className="duanyan-stats-badge"
        onClick={() => setStatsOpen(o => !o)}
        title={statsOpen ? "折叠模型指标" : "展开模型指标"}
      >
        <i className={"codicon codicon-" + (statsOpen ? "chevron-down" : "chevron-right")} />
        <span className="badge-item">Transformer</span>
        <span className="badge-sep">·</span>
        <span className="badge-item">4 heads</span>
        <span className="badge-sep">·</span>
        <span className="badge-item">2 layers</span>
        <span className="badge-sep">·</span>
        <span className="badge-item">Decoder</span>
        <span className="badge-sep">·</span>
        <span className="badge-item">{useBackprop ? "BP" : "CLS"}</span>
        <span className="badge-sep">·</span>
        <span className="badge-item">clk {clock}</span>
      </button>
      {statsOpen && (
        <div className="duanyan-stats">
          <div className="duanyan-stat">
            <span className="stat-label">Model</span>
            <span className="stat-value">Transformer</span>
          </div>
          <div className="duanyan-stat">
            <span className="stat-label">Heads</span>
            <span className="stat-value">4</span>
          </div>
          <div className="duanyan-stat">
            <span className="stat-label">Layers</span>
            <span className="stat-value">2</span>
          </div>
          <div className="duanyan-stat">
            <span className="stat-label">Decoder</span>
            <span className="stat-value">Yes</span>
          </div>
          <div className="duanyan-stat">
            <span className="stat-label">Mode</span>
            <span className="stat-value">{useBackprop ? "BP" : "CLS"}</span>
          </div>
          <div className="duanyan-stat">
            <span className="stat-label">Clock</span>
            <span className="stat-value">{clock}</span>
          </div>
        </div>
      )}
    </>
  );
}
