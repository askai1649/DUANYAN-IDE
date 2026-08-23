import { useState, useEffect, useCallback, useMemo } from "react";
import "@vscode/codicons/dist/codicon.css";
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
import ActivityBar from "./components/ActivityBar";
import type { ActivityItem } from "./components/ActivityBar";
import MenuBar from "./components/MenuBar";
import type { MenuDef } from "./components/MenuBar";
import CommandPalette from "./components/CommandPalette";
import type { PaletteCommand } from "./components/CommandPalette";

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
  const [showTemplates, setShowTemplates] = useState(false);
  const [showWizard, setShowWizard] = useState(false);
  const [showTFCard, setShowTFCard] = useState(false);
  const [selectedTarget, setSelectedTarget] = useState("COUNPRE64-FPGA");
  const [terminalTab, setTerminalTab] = useState<"output" | "serial" | "bench" | "lint" | "rt">("output");
  const [sidebarTab, setSidebarTab] = useState<"files" | "workspace" | "git" | "hardware" | "deploy">("files");
  const [diffFile, setDiffFile] = useState<string | null>(null);
  const [statusMessage, setStatusMessage] = useState("就绪");
    const [showSidebar, setShowSidebar] = useState(true);
  const [showTerminal, setShowTerminal] = useState(true);
  const [sidebarWidth, setSidebarWidth] = useState(300);
  const [terminalHeight, setTerminalHeight] = useState(150);
  const [duanyanWidth, setDuanyanWidth] = useState(380);
  const [showPalette, setShowPalette] = useState(false);
  const [branch, setBranch] = useState<string | undefined>(undefined);

  // 三期: 状态栏 git 分支 — projectDir 变化时查询当前分支
  useEffect(() => {
    if (!projectDir) { setBranch(undefined); return; }
    let cancelled = false;
    (async () => {
      try {
        const { invoke } = await import("@tauri-apps/api/core");
        const branches = await invoke<{ name: string; is_current: boolean }[]>("git_branches", { dir: projectDir });
        if (!cancelled) setBranch(branches.find(b => b.is_current)?.name);
      } catch {
        if (!cancelled) setBranch(undefined);
      }
    })();
    return () => { cancelled = true; };
  }, [projectDir]);

  // 三期: 状态栏错误/警告计数 — 从输出日志流统计
  const { errorCount, warningCount } = useMemo(() => {
    let errors = 0, warnings = 0;
    for (const line of terminalOutput) {
      if (/\[error\]|\berror\b/i.test(line)) errors++;
      else if (/\bwarn(?:ing)?\b/i.test(line)) warnings++;
    }
    return { errorCount: errors, warningCount: warnings };
  }, [terminalOutput]);

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
      } else if ((e.ctrlKey || e.metaKey) && e.shiftKey && e.key.toLowerCase() === "p") {
        e.preventDefault();
        setShowPalette(prev => !prev);
      }
    };
    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, [handleSave, handleRun, handleBuild, handleFlash, handleOpenFileViaDialog, handleNewFile]);

  const currentFile = openFiles.find(f => f.path === activeFile) || null;

  // === 一期: Activity Bar / 菜单瘦身 / 命令面板 ===
  const showSidebarView = (tab: typeof sidebarTab) => {
    setSidebarTab(tab);
    setShowSidebar(true);
  };
  const showBottomTab = (tab: typeof terminalTab) => {
    setTerminalTab(tab);
    setShowTerminal(true);
  };

  const activityItems: ActivityItem[] = [
    { id: "files", icon: "files", title: "资源管理器" },
    { id: "workspace", icon: "briefcase", title: "工作区" },
    { id: "git", icon: "source-control", title: "源码管理 (Git)" },
    { id: "hardware", icon: "circuit-board", title: "硬件 (板卡/引脚/TF卡)" },
    { id: "deploy", icon: "rocket", title: "部署与烧录" },
  ];

  const handleActivitySelect = (id: string) => {
    // VSCode 行为: 点击当前激活图标 = 收起侧栏
    if (showSidebar && sidebarTab === id) { setShowSidebar(false); return; }
    showSidebarView(id as typeof sidebarTab);
  };

  const menus: MenuDef[] = [
    { title: "File", items: [
      { label: "新建文件", shortcut: "Ctrl+N", onClick: handleNewFile },
      { label: "打开文件...", shortcut: "Ctrl+O", onClick: handleOpenFileViaDialog },
      { label: "新建项目 (模板)...", onClick: handleNewProject },
      { separator: true },
      { label: "保存", shortcut: "Ctrl+S", onClick: handleSave },
    ]},
    { title: "View", items: [
      { label: "命令面板...", shortcut: "Ctrl+Shift+P", onClick: () => setShowPalette(true) },
      { separator: true },
      { label: "切换资源管理器", shortcut: "Ctrl+\\", onClick: () => setShowSidebar(p => !p) },
      { label: "切换底部面板", shortcut: "Ctrl+`", onClick: () => setShowTerminal(p => !p) },
      { label: "切换 duanyan AI 面板", shortcut: "Ctrl+J", onClick: () => setShowDuanyan(p => !p) },
      { label: "切换 OpenSNAR Bridge", shortcut: "Ctrl+Shift+B", onClick: () => setShowBridge(p => !p) },
    ]},
    { title: "Build", items: [
      { label: "编译", shortcut: "Ctrl+B", onClick: handleBuild },
      { separator: true },
      { label: `目标板卡: ${selectedTarget}`, onClick: () => showSidebarView("hardware") },
    ]},
    { title: "Run", items: [
      { label: "编译并运行", shortcut: "Ctrl+R", onClick: handleRun },
      { label: "烧录到设备", shortcut: "Ctrl+Shift+F", onClick: handleFlash },
      { separator: true },
      { label: "串口监视器", onClick: () => showBottomTab("serial") },
      { label: "性能基准", onClick: () => showBottomTab("bench") },
      { label: "确定性分析", onClick: () => showBottomTab("rt") },
    ]},
    { title: "Tools", items: [
      { label: "硬件视图 (板卡/引脚)", onClick: () => showSidebarView("hardware") },
      { label: "TF 卡管理", onClick: () => setShowTFCard(true) },
      { label: "部署与烧录", onClick: () => showSidebarView("deploy") },
      { separator: true },
      { label: "问题 (Lint)", onClick: () => showBottomTab("lint") },
    ]},
    { title: "Help", items: [
      { label: "入门向导", onClick: () => setShowWizard(true) },
      { label: "关于 DUANYAN IDE", onClick: () => setTerminalOutput(p => [...p, "DUANYAN IDE v1.2.0 — HardyScript + Tauri + Monaco"]) },
    ]},
  ];

  const paletteCommands: PaletteCommand[] = [
    { id: "new-file", label: "文件: 新建文件", shortcut: "Ctrl+N", icon: "new-file", run: handleNewFile },
    { id: "open-file", label: "文件: 打开文件...", shortcut: "Ctrl+O", icon: "folder-opened", run: handleOpenFileViaDialog },
    { id: "new-project", label: "项目: 新建项目 (模板)", icon: "new-folder", run: handleNewProject },
    { id: "save", label: "文件: 保存", shortcut: "Ctrl+S", icon: "save", run: handleSave },
    { id: "build", label: "构建: 编译", shortcut: "Ctrl+B", icon: "tools", run: handleBuild },
    { id: "run", label: "运行: 编译并运行", shortcut: "Ctrl+R", icon: "play", run: handleRun },
    { id: "flash", label: "烧录: 烧录到设备", shortcut: "Ctrl+Shift+F", icon: "rocket", run: handleFlash },
    { id: "board", label: "硬件: 板卡管理器", icon: "circuit-board", run: () => showSidebarView("hardware") },
    { id: "pins", label: "硬件: 引脚图", icon: "pin", run: () => showSidebarView("hardware") },
    { id: "tfcard", label: "硬件: TF 卡管理", icon: "sd-card", run: () => setShowTFCard(true) },
    { id: "serial", label: "视图: 串口监视器", icon: "plug", run: () => showBottomTab("serial") },
    { id: "bench", label: "视图: 性能基准", icon: "dashboard", run: () => showBottomTab("bench") },
    { id: "rt", label: "视图: 确定性分析", icon: "lock", run: () => showBottomTab("rt") },
    { id: "flash-panel", label: "视图: 部署与烧录", icon: "rocket", run: () => showSidebarView("deploy") },
    { id: "explorer", label: "视图: 资源管理器", icon: "files", run: () => showSidebarView("files") },
    { id: "workspace", label: "视图: 工作区", icon: "briefcase", run: () => showSidebarView("workspace") },
    { id: "git", label: "视图: 源码管理", icon: "source-control", run: () => showSidebarView("git") },
    { id: "lint", label: "视图: 问题 (Lint)", icon: "warning", run: () => showBottomTab("lint") },
    { id: "ai", label: "duanyan: 切换 AI 面板", shortcut: "Ctrl+J", icon: "sparkle", run: () => setShowDuanyan(p => !p) },
    { id: "bridge", label: "duanyan: OpenSNAR Bridge", shortcut: "Ctrl+Shift+B", icon: "broadcast", run: () => setShowBridge(p => !p) },
    { id: "toggle-sidebar", label: "视图: 切换资源管理器", shortcut: "Ctrl+\\", icon: "layout-sidebar-left", run: () => setShowSidebar(p => !p) },
    { id: "toggle-panel", label: "视图: 切换底部面板", shortcut: "Ctrl+`", icon: "layout-panel", run: () => setShowTerminal(p => !p) },
    { id: "wizard", label: "帮助: 入门向导", icon: "lightbulb", run: () => setShowWizard(true) },
  ];

  return (
    <div className={"ide-container" + (!showSidebar ? " sidebar-hidden" : "")}>
      {/* Custom Title Bar */}
      <div className="titlebar" data-tauri-drag-region>
        <div className="titlebar-left" data-tauri-drag-region>
          <span className="brand">DUANYAN IDE</span>
          <MenuBar menus={menus} />
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
        <ActivityBar
          items={activityItems}
          active={showSidebar ? sidebarTab : null}
          onSelect={handleActivitySelect}
        />
        {showSidebar && (
          <div className="sidebar" style={{ width: sidebarWidth }}>

            {sidebarTab === "files" && (
              <FileTree
                projectDir={projectDir}
                onOpenFile={handleOpenFile}
                onSetProjectDir={setProjectDir}
              />
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
            {sidebarTab === "hardware" && (
              <div className="hardware-view">
                <BoardPanel
                  selectedTarget={selectedTarget}
                  onTargetChange={setSelectedTarget}
                  onClose={() => showSidebarView("files")}
                />
                <PinDiagram boardName={selectedTarget} />
                <button className="hardware-tfcard-btn" onClick={() => setShowTFCard(true)}>
                  <i className="codicon codicon-sd-card" /> TF 卡管理
                </button>
              </div>
            )}
            {sidebarTab === "deploy" && (
              <div className="deploy-view">
                <FlashPanel />
              </div>
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
                ><i className="codicon codicon-warning" /> Problems</button>
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

      {/* 命令面板 (一期) */}
      <CommandPalette
        open={showPalette}
        commands={paletteCommands}
        onClose={() => setShowPalette(false)}
      />

      {/* Status bar */}
      <StatusBar
        fileName={currentFile?.name}
        language={currentFile?.language}
        modified={currentFile?.modified}
        status={statusMessage}
        target={selectedTarget}
        branch={branch}
        errors={errorCount}
        warnings={warningCount}
      />
    </div>
  );
}

export default App;
