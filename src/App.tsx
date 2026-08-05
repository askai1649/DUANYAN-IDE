import { useState, useEffect, useCallback } from "react";
import FileTree from "./components/FileTree";
import TabBar from "./components/TabBar";
import EditorPanel from "./components/EditorPanel";
import TerminalPanel from "./components/TerminalPanel";
import StatusBar from "./components/StatusBar";
import DuanyanPanel from "./components/DuanyanPanel";
import OpenSnarBridge from "./components/OpenSnarBridge";
import BoardPanel from "./components/BoardPanel";
import SerialMonitor from "./components/SerialMonitor";
import PinDiagram from "./components/PinDiagram";
import TemplateLibrary from "./components/TemplateLibrary";
import OnboardingWizard from "./components/OnboardingWizard";
import BenchmarkPanel from "./components/BenchmarkPanel";
import WorkspacePanel from "./components/WorkspacePanel";
import GitPanel from "./components/GitPanel";
import DiffView from "./components/DiffView";
import LintPanel from "./components/LintPanel";
import TFCardManager from "./components/TFCardManager";
import FlashPanel from "./components/FlashPanel";
import DeterministicPanel from "./components/DeterministicPanel";

export interface FileEntry {
  name: string;
  path: string;
  isDir: boolean;
  children?: FileEntry[];
}

export interface OpenFile {
  path: string;
  name: string;
  content: string;
  language: string;
  modified: boolean;
}

function App() {
  const [openFiles, setOpenFiles] = useState<OpenFile[]>([]);
  const [activeFile, setActiveFile] = useState<string | null>(null);
  const [terminalOutput, setTerminalOutput] = useState<string[]>([]);
  const [projectDir, setProjectDir] = useState<string | null>(null);
  const [saving, setSaving] = useState(false);
  const [maximized, setMaximized] = useState(false);
  const [showDuanyan, setShowDuanyan] = useState(false);
  const [showBridge, setShowBridge] = useState(false);
  const [showBoardPanel, setShowBoardPanel] = useState(false);
  const [showTemplates, setShowTemplates] = useState(false);
  const [showWizard, setShowWizard] = useState(false);
  const [showPinDiagram, setShowPinDiagram] = useState(false);
  const [showTFCard, setShowTFCard] = useState(false);
  const [selectedTarget, setSelectedTarget] = useState("COUNPRE64-FPGA");
  const [terminalTab, setTerminalTab] = useState<"output" | "serial" | "bench" | "lint" | "flash" | "rt">("output");
  const [sidebarTab, setSidebarTab] = useState<"files" | "workspace" | "git" | "lint">("files");
  const [diffFile, setDiffFile] = useState<string | null>(null);
  const [statusMessage, setStatusMessage] = useState("就绪");
    const [showSidebar, setShowSidebar] = useState(true);
  const [showTerminal, setShowTerminal] = useState(true);
  const [sidebarWidth, setSidebarWidth] = useState(240);
  const [terminalHeight, setTerminalHeight] = useState(150);
  const [duanyanWidth, setDuanyanWidth] = useState(380);

  // Window control handlers
  const handleMinimize = async () => {
    const { getCurrentWindow } = await import("@tauri-apps/api/window");
    getCurrentWindow().minimize();
  };
  const handleToggleMaximize = async () => {
    const { getCurrentWindow } = await import("@tauri-apps/api/window");
    const win = getCurrentWindow();
    await win.toggleMaximize();
    setMaximized(await win.isMaximized());
  };
  const handleClose = async () => {
    const { getCurrentWindow } = await import("@tauri-apps/api/window");
    getCurrentWindow().close();
  };

  // Check if first launch
  useEffect(() => {
    const onboarded = localStorage.getItem("duanyan-ide-onboarded");
    if (!onboarded) {
      setShowWizard(true);
    }
  }, []);

  const handleOpenFile = (path: string, name: string, content: string) => {
    const existing = openFiles.find(f => f.path === path);
    if (existing) {
      setActiveFile(path);
      return;
    }
    const ext = name.split(".").pop() || "";
    const langMap: Record<string, string> = {
      js: "javascript", ts: "typescript", jsx: "javascript", tsx: "typescript",
      rs: "rust", py: "python", v: "verilog", sv: "systemverilog",
      json: "json", md: "markdown", css: "css", html: "html",
      hardy: "javascript",
    };
    const newFile: OpenFile = {
      path, name, content,
      language: langMap[ext] || "plaintext",
      modified: false,
    };
    setOpenFiles(prev => [...prev, newFile]);
    setActiveFile(path);
  };

  const handleOpenFileViaDialog = useCallback(async () => {
    try {
      const { open } = await import("@tauri-apps/plugin-dialog");
      const { readTextFile } = await import("@tauri-apps/plugin-fs");
      const selected = await open({
        multiple: false,
        filters: [
          { name: "Code", extensions: ["js", "ts", "jsx", "tsx", "rs", "py", "v", "hardy"] },
          { name: "All", extensions: ["*"] },
        ],
      });
      if (selected) {
        const path = selected as string;
        const name = path.split(/[\\/]/).pop() || path;
        const content = await readTextFile(path);
        handleOpenFile(path, name, content);
      }
    } catch (e) {
      setTerminalOutput(prev => [...prev, `[error] Open failed: ${e}`]);
    }
  }, [openFiles]);

  const handleCloseTab = (path: string) => {
    setOpenFiles(prev => prev.filter(f => f.path !== path));
    if (activeFile === path) {
      const remaining = openFiles.filter(f => f.path !== path);
      setActiveFile(remaining.length > 0 ? remaining[remaining.length - 1].path : null);
    }
  };

  const handleContentChange = (path: string, content: string) => {
    setOpenFiles(prev => prev.map(f =>
      f.path === path ? { ...f, content, modified: true } : f
    ));
  };

  const handleSave = useCallback(async () => {
    const file = openFiles.find(f => f.path === activeFile);
    if (!file || !file.modified) return;
    setSaving(true);
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const result = await invoke<string>("save_file", { path: file.path, content: file.content });
      setTerminalOutput(prev => [...prev, result]);
      setOpenFiles(prev => prev.map(f =>
        f.path === file.path ? { ...f, modified: false } : f
      ));
    } catch (e) {
      setTerminalOutput(prev => [...prev, `[error] Save failed: ${e}`]);
    } finally {
      setSaving(false);
    }
  }, [openFiles, activeFile]);

  const handleRun = useCallback(async () => {
    const file = openFiles.find(f => f.path === activeFile);
    if (!file) return;
    setStatusMessage("Running...");
    setTerminalOutput(prev => [...prev, `> hardy run ${file.name}`]);
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const result = await invoke<string>("compile_and_run", { source: file.content });
      setTerminalOutput(prev => [...prev, result]);
      // Code analysis integrated into OUTPUT
      try {
        const lang = file.language === "python" ? "python"
          : file.language === "javascript" ? "javascript"
          : file.language === "go" ? "go"
          : file.language === "java" ? "java" : "rust";
        const analysis = await invoke<string>("duanyan_code_analyze_panel", { code: file.content, lang });
        const lines = analysis.split("\n").filter((l: string) => l.trim().length > 0);
        setTerminalOutput(prev => [...prev, "", "--- Code Analysis ---", ...lines, "--- End Analysis ---"]);
      } catch { /* analysis optional */ }
      setStatusMessage("Run complete");
    } catch (e) {
      setTerminalOutput(prev => [...prev, `[error] ${e}`]);
      setStatusMessage("Run failed");
    }
  }, [openFiles, activeFile]);

  const handleBuild = useCallback(async () => {
    const file = openFiles.find(f => f.path === activeFile);
    if (!file) return;
    setStatusMessage("Building...");
    setTerminalOutput(prev => [...prev, `> hardy build ${file.name} --target ${selectedTarget}`]);
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const result = await invoke<string>("compile_source", { source: file.content });
      setTerminalOutput(prev => [...prev, result]);
      setStatusMessage("Build complete");
      setTerminalTab("output");
    } catch (e) {
      setTerminalOutput(prev => [...prev, `[error] ${e}`]);
      setStatusMessage("Build failed");
    }
  }, [openFiles, activeFile, selectedTarget]);

  const handleFlash = useCallback(async () => {
    const file = openFiles.find(f => f.path === activeFile);
    if (!file) return;
    setStatusMessage("Flashing...");
    setTerminalOutput(prev => [...prev, `> hardy flash ${file.name} --target ${selectedTarget}`]);
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const projectDir = file.path.includes("\\") || file.path.includes("/")
        ? file.path.replace(/\\[^\\]*$/, "").replace(/\/[^/]*$/, "")
        : ".hardy-build";
      const stem = file.name.replace(/\.(js|ts)$/i, "");
      const targetMap: Record<string, string> = {
        "COUNPRE64-FPGA": "gowin",
        "ESP32-DevKit": "esp32",
        "ESP32-S3-DevKit": "esp32",
        "Arduino Uno": "arduino",
        "Arduino Nano": "arduino",
        "STM32-BluePill": "stm32",
        "Raspberry Pi Pico": "rp2040",
      };
      const flashTarget = targetMap[selectedTarget] || selectedTarget.toLowerCase().split("-")[0];
      const result = await invoke<string>("build_and_flash", {
        source: file.content,
        projectDir,
        stem,
        target: flashTarget,
      });
      setTerminalOutput(prev => [...prev, result]);
      setStatusMessage("Flash complete");
      setTerminalTab("output");
    } catch (e) {
      setTerminalOutput(prev => [...prev, `[error] ${e}`]);
      setStatusMessage("Flash failed");
    }
  }, [openFiles, activeFile, selectedTarget]);

  const handleNewProject = useCallback(async () => {
    setShowTemplates(true);
  }, []);

  const handleNewFile = useCallback(() => {
    const id = Date.now();
    const newFile: OpenFile = {
      path: `untitled-${id}.js`,
      name: `untitled-${id}.js`,
      content: "// DUANYAN IDE - new JavaScript file\n// Press Ctrl+R to compile and run\n\nlet x = 42;\nconsole.log(x);\n",
      language: "javascript",
      modified: true,
    };
    setOpenFiles(prev => [...prev, newFile]);
    setActiveFile(newFile.path);
  }, []);

  // Sidebar resize drag
  const handleSidebarResizeStart = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
    const startX = e.clientX;
    const startW = sidebarWidth;
    const onMove = (ev: MouseEvent) => {
      setSidebarWidth(Math.max(120, Math.min(500, startW + ev.clientX - startX)));
    };
    const onUp = () => {
      document.removeEventListener("mousemove", onMove);
      document.removeEventListener("mouseup", onUp);
      document.body.style.cursor = "";
      document.body.style.userSelect = "";
    };
    document.addEventListener("mousemove", onMove);
    document.addEventListener("mouseup", onUp);
    document.body.style.cursor = "col-resize";
    document.body.style.userSelect = "none";
  }, [sidebarWidth]);

  // Terminal resize drag
  const handleTerminalResizeStart = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
    const startY = e.clientY;
    const startH = terminalHeight;
    const onMove = (ev: MouseEvent) => {
      setTerminalHeight(Math.max(60, Math.min(500, startH - (ev.clientY - startY))));
    };
    const onUp = () => {
      document.removeEventListener("mousemove", onMove);
      document.removeEventListener("mouseup", onUp);
      document.body.style.cursor = "";
      document.body.style.userSelect = "";
    };
    document.addEventListener("mousemove", onMove);
    document.addEventListener("mouseup", onUp);
    document.body.style.cursor = "row-resize";
    document.body.style.userSelect = "none";
  }, [terminalHeight]);

  // Duanyan panel resize drag
  const handleDuanyanResizeStart = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
    const startX = e.clientX;
    const startW = duanyanWidth;
    const onMove = (ev: MouseEvent) => {
      setDuanyanWidth(Math.max(200, Math.min(700, startW - (ev.clientX - startX))));
    };
    const onUp = () => {
      document.removeEventListener("mousemove", onMove);
      document.removeEventListener("mouseup", onUp);
      document.body.style.cursor = "";
      document.body.style.userSelect = "";
    };
    document.addEventListener("mousemove", onMove);
    document.addEventListener("mouseup", onUp);
    document.body.style.cursor = "col-resize";
    document.body.style.userSelect = "none";
  }, [duanyanWidth]);

  // Keyboard shortcuts
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && e.key === "s") {
        e.preventDefault();
        handleSave();
      } else if ((e.ctrlKey || e.metaKey) && e.key === "r") {
        e.preventDefault();
        handleRun();
      } else if ((e.ctrlKey || e.metaKey) && e.shiftKey && e.key === "F") {
        e.preventDefault();
        handleFlash();
      } else if ((e.ctrlKey || e.metaKey) && e.key === "b") {
        e.preventDefault();
        handleBuild();
      } else if ((e.ctrlKey || e.metaKey) && e.key === "o") {
        e.preventDefault();
        handleOpenFileViaDialog();
      } else if ((e.ctrlKey || e.metaKey) && e.key === "n") {
        e.preventDefault();
        handleNewFile();
      } else if ((e.ctrlKey || e.metaKey) && e.key === "j") {
        e.preventDefault();
        setShowDuanyan(prev => !prev);
      } else if ((e.ctrlKey || e.metaKey) && e.shiftKey && e.key === "B") {
        e.preventDefault();
        setShowBridge(prev => !prev);
      } else if ((e.ctrlKey || e.metaKey) && e.key === "\\") {
        e.preventDefault();
        setShowSidebar(prev => !prev);
      } else if ((e.ctrlKey || e.metaKey) && e.key === "`") {
        e.preventDefault();
        setShowTerminal(prev => !prev);
      }
    };
    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, [handleSave, handleRun, handleBuild, handleFlash, handleOpenFileViaDialog, handleNewFile]);

  const currentFile = openFiles.find(f => f.path === activeFile) || null;

  return (
    <div className={"ide-container" + (!showSidebar ? " sidebar-hidden" : "")}>
      {/* Custom Title Bar */}
      <div className="titlebar" data-tauri-drag-region>
        <div className="titlebar-left" data-tauri-drag-region>
          <span className="brand">DUANYAN IDE</span>
          <button className="menu-btn" onClick={handleOpenFileViaDialog} title="Ctrl+O">File</button>
          <button className="menu-btn">Edit</button>
          <button className="menu-btn">View</button>
          <button className="menu-btn" onClick={handleBuild} title="Ctrl+B">Build</button>
          <button className="menu-btn" onClick={handleRun} title="Ctrl+R">Run</button>
          <button className="menu-btn flash-btn" onClick={handleFlash} title="Ctrl+Shift+F">Flash</button>
          <button className="menu-btn" onClick={handleSave} title="Ctrl+S">
            {saving ? "Saving..." : "Save"}
          </button>
          <button className="menu-btn" onClick={handleNewProject} title="New Project (Templates)">+ Project</button>
          <button className="menu-btn" onClick={() => setShowBoardPanel(p => !p)} title="Board Manager">Board</button>
          <button className="menu-btn" onClick={() => setShowPinDiagram(p => !p)} title="Pin Diagram">Pins</button>
          <button className="menu-btn" onClick={() => setShowTFCard(p => !p)} title="TF Card Manager">TF Card</button>
          <button className="menu-btn" onClick={() => setSidebarTab(sidebarTab === "workspace" ? "files" : "workspace")} title="Workspace">WS</button>
          <button className="menu-btn" onClick={() => setSidebarTab(sidebarTab === "git" ? "files" : "git")} title="Git">Git</button>
          <button className="menu-btn" onClick={() => setSidebarTab(sidebarTab === "lint" ? "files" : "lint")} title="Lint">Lint</button>
          <button className="menu-btn" onClick={handleNewFile} title="Ctrl+N">+ New</button>
        </div>
        <div className="titlebar-right-actions">
          {/* Sidebar toggle */}
          <button
            className={"layout-btn" + (showSidebar ? " active" : "")}
            onClick={() => setShowSidebar(p => !p)}
            title={"Toggle Explorer (Ctrl+\\)"}
          >
            <svg width="16" height="16" viewBox="0 0 16 16">
              <rect x="1" y="1" width="14" height="14" rx="1" fill="none" stroke="currentColor" strokeWidth="1.2"/>
              <line x1="6" y1="1" x2="6" y2="15" stroke="currentColor" strokeWidth="1.2" strokeDasharray={showSidebar ? "0" : "3 2"}/>
            </svg>
          </button>

          {/* Terminal/Output toggle */}
          <button
            className={"layout-btn" + (showTerminal ? " active" : "")}
            onClick={() => setShowTerminal(p => !p)}
            title={"Toggle Output (Ctrl+`)"}
          >
            <svg width="16" height="16" viewBox="0 0 16 16">
              <rect x="1" y="1" width="14" height="14" rx="1" fill="none" stroke="currentColor" strokeWidth="1.2"/>
              <line x1="1" y1="10" x2="15" y2="10" stroke="currentColor" strokeWidth="1.2" strokeDasharray={showTerminal ? "0" : "3 2"}/>
            </svg>
          </button>
          {/* duanyan AI panel toggle */}
          <button
            className={"layout-btn" + (showDuanyan ? " active" : "")}
            onClick={() => setShowDuanyan(prev => !prev)}
            title="Toggle duanyan AI Panel (Ctrl+J)"
          >
            <svg width="16" height="16" viewBox="0 0 16 16">
              <rect x="1" y="1" width="14" height="14" rx="1" fill="none" stroke="currentColor" strokeWidth="1.2"/>
              <line x1="10" y1="1" x2="10" y2="15" stroke="currentColor" strokeWidth="1.2" strokeDasharray={showDuanyan ? "0" : "3 2"}/>
            </svg>
          </button>

          {/* OpenSNAR Bridge panel toggle */}
          <button
            className={"layout-btn" + (showBridge ? " active" : "")}
            onClick={() => setShowBridge(prev => !prev)}
            title="Toggle OpenSNAR Bridge (Ctrl+Shift+B)"
          >
            <svg width="16" height="16" viewBox="0 0 16 16">
              <rect x="1" y="1" width="14" height="14" rx="1" fill="none" stroke="currentColor" strokeWidth="1.2"/>
              <circle cx="8" cy="8" r="3" fill="none" stroke="currentColor" strokeWidth="1.2"/>
              <line x1="8" y1="1" x2="8" y2="5" stroke="currentColor" strokeWidth="1.2"/>
              <line x1="8" y1="11" x2="8" y2="15" stroke="currentColor" strokeWidth="1.2"/>
            </svg>
          </button>

        </div>
        <div className="titlebar-controls">
          <button className="win-btn" onClick={handleMinimize} title="Minimize">
            <svg width="10" height="10" viewBox="0 0 10 10"><rect y="4" width="10" height="1" fill="currentColor"/></svg>
          </button>
          <button className="win-btn" onClick={handleToggleMaximize} title="Maximize">
            {maximized ? (
              <svg width="10" height="10" viewBox="0 0 10 10">
                <rect x="2" y="0" width="8" height="8" fill="none" stroke="currentColor" strokeWidth="1"/>
                <rect x="0" y="2" width="8" height="8" fill="var(--bg-secondary)" stroke="currentColor" strokeWidth="1"/>
              </svg>
            ) : (
              <svg width="10" height="10" viewBox="0 0 10 10"><rect width="10" height="10" fill="none" stroke="currentColor" strokeWidth="1"/></svg>
            )}
          </button>
          <button className="win-btn win-btn-close" onClick={handleClose} title="Close">
            <svg width="10" height="10" viewBox="0 0 10 10">
              <line x1="0" y1="0" x2="10" y2="10" stroke="currentColor" strokeWidth="1.2"/>
              <line x1="10" y1="0" x2="0" y2="10" stroke="currentColor" strokeWidth="1.2"/>
            </svg>
          </button>
        </div>
      </div>

      {/* Main area */}
      <div className="main-area">
        {showSidebar && (
          <div className="sidebar" style={{ width: sidebarWidth }}>
            {/* Sidebar Tabs */}
            <div className="sidebar-tabs">
              <button className={`sidebar-tab ${sidebarTab === "files" ? "active" : ""}`} onClick={() => setSidebarTab("files")}>Files</button>
              <button className={`sidebar-tab ${sidebarTab === "workspace" ? "active" : ""}`} onClick={() => setSidebarTab("workspace")}>WS</button>
              <button className={`sidebar-tab ${sidebarTab === "git" ? "active" : ""}`} onClick={() => setSidebarTab("git")}>Git</button>
              <button className={`sidebar-tab ${sidebarTab === "lint" ? "active" : ""}`} onClick={() => setSidebarTab("lint")}>Lint</button>
            </div>

            {sidebarTab === "files" && (
              <>
                <FileTree
                  projectDir={projectDir}
                  onOpenFile={handleOpenFile}
                  onSetProjectDir={setProjectDir}
                />
                {showBoardPanel && (
                  <BoardPanel
                    selectedTarget={selectedTarget}
                    onTargetChange={setSelectedTarget}
                    onClose={() => setShowBoardPanel(false)}
                  />
                )}
              </>
            )}
            {sidebarTab === "workspace" && projectDir && (
              <WorkspacePanel projectDir={projectDir} onLog={(m) => setTerminalOutput(p => [...p, m])} />
            )}
            {sidebarTab === "git" && projectDir && (
              <GitPanel
                projectDir={projectDir}
                onLog={(m) => setTerminalOutput(p => [...p, m])}
                onShowDiff={(f) => setDiffFile(f)}
              />
            )}
            {sidebarTab === "lint" && projectDir && (
              <LintPanel
                projectDir={projectDir}
                onLog={(m) => setTerminalOutput(p => [...p, m])}
                onJumpToLine={(_file, line) => {
                  // Could navigate to specific line in editor
                  setTerminalOutput(p => [...p, `Jump to line ${line}`]);
                }}
              />
            )}
          </div>
        )}
        {showSidebar && (
          <div className="resize-handle resize-handle-v" onMouseDown={handleSidebarResizeStart} />
        )}
        <div className="editor-terminal-wrapper">
          <div className="editor-area">
            <TabBar
              files={openFiles}
              activePath={activeFile}
              onSelect={setActiveFile}
              onClose={handleCloseTab}
            />
            <EditorPanel
              file={currentFile}
              onChange={handleContentChange}
            />
          </div>
          {showTerminal && (
            <div className="resize-handle resize-handle-h" onMouseDown={handleTerminalResizeStart} />
          )}
          {showTerminal && (
            <div className="terminal-panel" style={{ height: terminalHeight }}>
              {/* Terminal tabs */}
              <div className="terminal-tabs">
                <button
                  className={`terminal-tab ${terminalTab === "output" ? "active" : ""}`}
                  onClick={() => setTerminalTab("output")}
                >Output</button>
                <button
                  className={`terminal-tab ${terminalTab === "serial" ? "active" : ""}`}
                  onClick={() => setTerminalTab("serial")}
                >Serial</button>
                <button
                  className={`terminal-tab ${terminalTab === "bench" ? "active" : ""}`}
                  onClick={() => setTerminalTab("bench")}
                >Bench</button>
                <button
                  className={`terminal-tab ${terminalTab === "lint" ? "active" : ""}`}
                  onClick={() => setTerminalTab("lint")}
                >Lint</button>
                <button
                  className={`terminal-tab ${terminalTab === "flash" ? "active" : ""}`}
                  onClick={() => setTerminalTab("flash")}
                >⚡ Flash</button>
                <button
                  className={`terminal-tab ${terminalTab === "rt" ? "active" : ""}`}
                  onClick={() => setTerminalTab("rt")}
                >确定性</button>
              </div>
              {terminalTab === "output" && (
                <TerminalPanel output={terminalOutput} onClear={() => setTerminalOutput([])} />
              )}
              {terminalTab === "serial" && (
                <SerialMonitor visible={true} />
              )}
              {terminalTab === "bench" && (
                <BenchmarkPanel
                  visible={true}
                  source={currentFile?.content || ""}
                  target={selectedTarget}
                />
              )}
              {terminalTab === "lint" && projectDir && (
                <LintPanel
                  projectDir={projectDir}
                  onLog={(m) => setTerminalOutput(p => [...p, m])}
                  onJumpToLine={(_file, line) => {
                    setTerminalOutput(p => [...p, `Jump to line ${line}`]);
                  }}
                />
              )}
              {terminalTab === "flash" && (
                <FlashPanel />
              )}
              {terminalTab === "rt" && (
                <DeterministicPanel visible={true} source={currentFile?.content || ""} />
              )}
            </div>
          )}
        </div>
        {showDuanyan && (
          <div className="resize-handle resize-handle-v" onMouseDown={handleDuanyanResizeStart} />
        )}
        {showDuanyan && (
          <div className="duanyan-container" style={{ width: duanyanWidth }}>
            <DuanyanPanel onClose={() => setShowDuanyan(false)} />
          </div>
        )}
        {showBridge && (
          <div className="bridge-container">
            <OpenSnarBridge onClose={() => setShowBridge(false)} />
          </div>
        )}
        {showPinDiagram && (
          <div className="pin-diagram-panel">
            <PinDiagram boardName={selectedTarget} />
            <button className="pin-diagram-close" onClick={() => setShowPinDiagram(false)}>x</button>
          </div>
        )}
      </div>

      {/* Template Library Modal */}
      <TemplateLibrary
        visible={showTemplates}
        onClose={() => setShowTemplates(false)}
        onProjectCreated={handleOpenFile}
      />

      {/* Onboarding Wizard */}
      <OnboardingWizard
        visible={showWizard}
        onClose={() => setShowWizard(false)}
        onTargetChange={setSelectedTarget}
        onProjectCreated={handleOpenFile}
      />

      {/* DiffView Modal */}
      {diffFile && projectDir && (
        <div className="diff-modal">
          <DiffView projectDir={projectDir} file={diffFile} onClose={() => setDiffFile(null)} />
        </div>
      )}

      {/* TF Card Manager Modal */}
      {showTFCard && (
        <div className="tfcard-modal">
          <TFCardManager
            visible={true}
            onClose={() => setShowTFCard(false)}
            projectDir={projectDir}
            onLog={(m) => setTerminalOutput(p => [...p, m])}
          />
        </div>
      )}

      {/* Status bar */}
      <StatusBar
        fileName={currentFile?.name}
        language={currentFile?.language}
        modified={currentFile?.modified}
        status={statusMessage}
        target={selectedTarget}
      />
    </div>
  );
}

export default App;
