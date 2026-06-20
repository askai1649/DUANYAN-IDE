import { useState, useRef, useEffect } from "react";
import type { FileEntry } from "../App";

interface Props {
  projectDir: string | null;
  onOpenFile: (path: string, name: string, content: string) => void;
  onSetProjectDir: (dir: string) => void;
}

interface TreeNode extends FileEntry {
  children?: TreeNode[];
  loaded?: boolean;
}

interface CreatingState {
  parentPath: string;
  type: "file" | "dir";
}

// File icon colors by extension
const extColors: Record<string, string> = {
  js: "#f1e05a", ts: "#3178c6", jsx: "#f1e05a", tsx: "#3178c6",
  rs: "#dea584", py: "#3572a5", v: "#b2b7f8", sv: "#b2b7f8",
  json: "#cb8600", md: "#083fa1", css: "#563d7c", html: "#e34c26",
  snar: "#f1e05a", toml: "#9c4221", txt: "#cccccc",
};

function getFileColor(name: string): string {
  const ext = name.split(".").pop()?.toLowerCase() || "";
  return extColors[ext] || "#858585";
}

function sep(path: string): string {
  return path.includes("\\") ? "\\" : "/";
}

function joinPath(parent: string, name: string): string {
  return parent + sep(parent) + name;
}

function dirName(path: string): string {
  const parts = path.replace(/\\/g, "/").split("/");
  return parts[parts.length - 1] || path;
}

export default function FileTree({ projectDir, onOpenFile, onSetProjectDir }: Props) {
  const [entries, setEntries] = useState<TreeNode[]>([]);
  const [expanded, setExpanded] = useState<Set<string>>(new Set());
  const [creating, setCreating] = useState<CreatingState | null>(null);
  const [newName, setNewName] = useState("");
  const [selectedPath, setSelectedPath] = useState<string | null>(null);
  const inputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    if (creating && inputRef.current) {
      inputRef.current.focus();
    }
  }, [creating]);

  const readDirSafe = async (dirPath: string): Promise<TreeNode[]> => {
    try {
      const { readDir } = await import("@tauri-apps/plugin-fs");
      const items = await readDir(dirPath);
      return items
        .filter((i: any) => !i.name?.startsWith("."))
        .map((item: any) => ({
          name: item.name || "",
          path: item.path || "",
          isDir: !!item.isDirectory,
          loaded: false,
        }))
        .sort((a: TreeNode, b: TreeNode) => {
          if (a.isDir !== b.isDir) return a.isDir ? -1 : 1;
          return a.name.localeCompare(b.name);
        });
    } catch {
      return [];
    }
  };

  const handleOpenFolder = async () => {
    try {
      const { open } = await import("@tauri-apps/plugin-dialog");
      const selected = await open({ directory: true, multiple: false });
      if (selected) {
        const dir = selected as string;
        onSetProjectDir(dir);
        const items = await readDirSafe(dir);
        setEntries(items);
        setExpanded(new Set([dir]));
      }
    } catch (e) {
      console.error("Failed to open folder:", e);
    }
  };

  const handleNewProject = async () => {
    try {
      const { save } = await import("@tauri-apps/plugin-dialog");
      const { invoke } = await import("@tauri-apps/api/core");
      const chosen = await save({
        title: "New Project",
        defaultPath: "my-project",
        filters: [],
      });
      if (chosen) {
        const projectPath = (chosen as string).replace(/\.[^.]+$/, "");
        await invoke("create_dir", { path: projectPath });
        onSetProjectDir(projectPath);
        const items = await readDirSafe(projectPath);
        setEntries(items);
        setExpanded(new Set([projectPath]));
      }
    } catch (e) {
      console.error("New project failed:", e);
    }
  };

  const toggleDir = async (path: string) => {
    const isExpanding = !expanded.has(path);
    setExpanded(prev => {
      const next = new Set(prev);
      isExpanding ? next.add(path) : next.delete(path);
      return next;
    });
    if (isExpanding) {
      const children = await readDirSafe(path);
      setEntries(prev => injectChildren(prev, path, children));
    }
  };

  const handleClickFile = async (entry: TreeNode) => {
    setSelectedPath(entry.path);
    try {
      const { readTextFile } = await import("@tauri-apps/plugin-fs");
      const content = await readTextFile(entry.path);
      onOpenFile(entry.path, entry.name, content);
    } catch (e) {
      console.error("Failed to read file:", e);
    }
  };

  const refreshDir = async (dirPath: string) => {
    const children = await readDirSafe(dirPath);
    setEntries(prev => injectChildren(prev, dirPath, children));
  };

  // Determine target dir for new file/folder
  const getTargetDir = (): string => {
    if (selectedPath) {
      // If selected item is a dir, create inside it
      const findNode = (nodes: TreeNode[], path: string): TreeNode | null => {
        for (const n of nodes) {
          if (n.path === path) return n;
          if (n.children) {
            const found = findNode(n.children, path);
            if (found) return found;
          }
        }
        return null;
      };
      const node = findNode(entries, selectedPath);
      if (node?.isDir) {
        if (!expanded.has(node.path)) {
          setExpanded(prev => new Set(prev).add(node.path));
          refreshDir(node.path);
        }
        return node.path;
      }
      // If file, use parent dir
      const parentPath = selectedPath.substring(0, selectedPath.lastIndexOf(sep(selectedPath)));
      return parentPath || projectDir || "";
    }
    return projectDir || "";
  };

  const startCreate = (type: "file" | "dir") => {
    const targetDir = getTargetDir();
    setCreating({ parentPath: targetDir, type });
    setNewName("");
  };

  const confirmCreate = async () => {
    if (!creating || !newName.trim()) {
      setCreating(null);
      return;
    }
    const name = newName.trim();
    const fullPath = joinPath(creating.parentPath, name);
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      if (creating.type === "dir") {
        await invoke("create_dir", { path: fullPath });
      } else {
        // Template content based on extension
        const ext = name.split(".").pop()?.toLowerCase() || "";
        const templates: Record<string, string> = {
          js: "// New JavaScript file\n\n",
          ts: "// New TypeScript file\n\n",
          rs: "// New Rust file\nfn main() {\n    println!(\"Hello!\");\n}\n",
          py: "# New Python file\n\n",
          html: "<!DOCTYPE html>\n<html>\n<head>\n  <title>New Page</title>\n</head>\n<body>\n  \n</body>\n</html>\n",
          css: "/* New CSS file */\n\n",
          json: "{}\n",
          v: "// New Verilog file\nmodule (\n  \n);\nendmodule\n",
        };
        const content = templates[ext] || "";
        await invoke("create_file", { path: fullPath, content });
        // Auto-open the new file
        if (creating.type === "file") {
          onOpenFile(fullPath, name, content);
        }
      }
      // Refresh parent directory
      await refreshDir(creating.parentPath);
    } catch (e) {
      console.error("Create failed:", e);
    }
    setCreating(null);
    setNewName("");
  };

  const cancelCreate = () => {
    setCreating(null);
    setNewName("");
  };

  if (!projectDir) {
    return (
      <div className="file-tree">
        <div className="file-tree-header">
          <span>EXPLORER</span>
        </div>
        <div style={{ padding: "12px", display: "flex", flexDirection: "column", gap: "8px" }}>
          <button
            className="open-folder-btn"
            onClick={handleNewProject}
            style={{ background: "#ef6309", borderColor: "#ef6309", color: "#fff", fontWeight: 600 }}
          >
            New Project
          </button>
          <button
            className="open-folder-btn"
            onClick={handleOpenFolder}
            style={{ fontSize: "11px", color: "var(--text-secondary)" }}
          >
            Open Existing Folder
          </button>
        </div>
      </div>
    );
  }

  const rootName = dirName(projectDir);

  const renderEntry = (entry: TreeNode, depth: number) => {
    const isExpanded = expanded.has(entry.path);
    const indent = depth * 16;
    const isSelected = selectedPath === entry.path;
    const isCreatingHere = creating && creating.parentPath === entry.path;

    if (entry.isDir) {
      return (
        <div key={entry.path}>
          <div
            className={"tree-item dir" + (isSelected ? " selected" : "")}
            style={{ paddingLeft: indent + 8 }}
            onClick={() => toggleDir(entry.path)}
            onContextMenu={(e) => { e.preventDefault(); setSelectedPath(entry.path); }}
          >
            <span className="arrow">{isExpanded ? "\u25BE" : "\u25B8"}</span>
            <span className="icon folder-icon">{isExpanded ? "\uD83D\uDCC2" : "\uD83D\uDCC1"}</span>
            <span className="name">{entry.name}</span>
          </div>
          {isExpanded && (
            <div>
              {entry.children?.map(child => renderEntry(child, depth + 1))}
              {isCreatingHere && (
                <div className="tree-item creating" style={{ paddingLeft: (depth + 1) * 16 + 8 }}>
                  <span className="arrow">{"\u2022"}</span>
                  <span className="icon">{creating.type === "dir" ? "\uD83D\uDCC1" : "\uD83D\uDCC4"}</span>
                  <input
                    ref={inputRef}
                    className="create-input"
                    value={newName}
                    onChange={(e) => setNewName(e.target.value)}
                    onKeyDown={(e) => {
                      if (e.key === "Enter") confirmCreate();
                      if (e.key === "Escape") cancelCreate();
                    }}
                    onBlur={confirmCreate}
                    placeholder={creating.type === "dir" ? "folder name..." : "filename..."}
                  />
                </div>
              )}
            </div>
          )}
        </div>
      );
    }

    // File item
    return (
      <div key={entry.path}>
        <div
          className={"tree-item file" + (isSelected ? " selected" : "")}
          style={{ paddingLeft: indent + 8 }}
          onClick={() => handleClickFile(entry)}
          onContextMenu={(e) => { e.preventDefault(); setSelectedPath(entry.path); }}
        >
          <span className="arrow"></span>
          <span className="icon" style={{ color: getFileColor(entry.name) }}>{"\uD83D\uDCC4"}</span>
          <span className="name">{entry.name}</span>
        </div>
      </div>
    );
  };

  // Check if creating at root level
  const isCreatingAtRoot = creating && creating.parentPath === projectDir;

  return (
    <div className="file-tree">
      <div className="file-tree-header">
        <span>EXPLORER</span>
        <div className="file-tree-toolbar">
          <button className="toolbar-btn" title="New File" onClick={() => startCreate("file")}>+</button>
          <button className="toolbar-btn" title="New Folder" onClick={() => startCreate("dir")}>&#x1F4C1;</button>
          <button className="toolbar-btn" title="Refresh" onClick={() => { readDirSafe(projectDir).then(setEntries); }}>&#x21BB;</button>
          <button className="toolbar-btn" title="Collapse All" onClick={() => setExpanded(new Set())}>&#x2261;</button>
        </div>
      </div>

      {/* Root folder */}
      <div
        className={"tree-item dir root" + (expanded.has(projectDir) ? "" : "")}
        style={{ paddingLeft: 4 }}
        onClick={() => toggleDir(projectDir)}
      >
        <span className="arrow">{expanded.has(projectDir) ? "\u25BE" : "\u25B8"}</span>
        <span className="icon folder-icon">{expanded.has(projectDir) ? "\uD83D\uDCC2" : "\uD83D\uDCC1"}</span>
        <span className="name root-name">{rootName}</span>
      </div>

      {/* Root children */}
      {expanded.has(projectDir) && (
        <div>
          {entries.map(e => renderEntry(e, 1))}
          {isCreatingAtRoot && (
            <div className="tree-item creating" style={{ paddingLeft: 24 }}>
              <span className="arrow">{"\u2022"}</span>
              <span className="icon">{creating.type === "dir" ? "\uD83D\uDCC1" : "\uD83D\uDCC4"}</span>
              <input
                ref={inputRef}
                className="create-input"
                value={newName}
                onChange={(e) => setNewName(e.target.value)}
                onKeyDown={(e) => {
                  if (e.key === "Enter") confirmCreate();
                  if (e.key === "Escape") cancelCreate();
                }}
                onBlur={confirmCreate}
                placeholder={creating.type === "dir" ? "folder name..." : "filename..."}
              />
            </div>
          )}
        </div>
      )}
    </div>
  );
}

function injectChildren(tree: TreeNode[], parentPath: string, children: TreeNode[]): TreeNode[] {
  return tree.map(node => {
    if (node.path === parentPath) {
      return { ...node, children, loaded: true };
    }
    if (node.children) {
      return { ...node, children: injectChildren(node.children, parentPath, children) };
    }
    return node;
  });
}
