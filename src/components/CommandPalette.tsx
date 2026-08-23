// CommandPalette — Ctrl+Shift+P 命令面板
// 一期重构: 收纳全部长尾命令 (VSCode 核心交互)

import { useEffect, useMemo, useRef, useState } from "react";

export interface PaletteCommand {
  id: string;
  label: string;
  shortcut?: string;
  icon?: string; // codicon 类名后缀
  run: () => void;
}

interface Props {
  open: boolean;
  commands: PaletteCommand[];
  onClose: () => void;
}

export default function CommandPalette({ open, commands, onClose }: Props) {
  const [query, setQuery] = useState("");
  const [sel, setSel] = useState(0);
  const inputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    if (open) {
      setQuery("");
      setSel(0);
      setTimeout(() => inputRef.current?.focus(), 0);
    }
  }, [open]);

  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase();
    if (!q) return commands;
    return commands.filter(c => c.label.toLowerCase().includes(q));
  }, [query, commands]);

  useEffect(() => { setSel(0); }, [filtered.length]);

  if (!open) return null;

  const onKey = (e: React.KeyboardEvent) => {
    if (e.key === "ArrowDown") {
      e.preventDefault();
      setSel(s => Math.min(s + 1, Math.max(filtered.length - 1, 0)));
    } else if (e.key === "ArrowUp") {
      e.preventDefault();
      setSel(s => Math.max(s - 1, 0));
    } else if (e.key === "Enter") {
      e.preventDefault();
      const c = filtered[sel];
      if (c) { onClose(); c.run(); }
    } else if (e.key === "Escape") {
      onClose();
    }
  };

  return (
    <div className="cmd-palette-backdrop" onMouseDown={onClose}>
      <div className="cmd-palette" onMouseDown={e => e.stopPropagation()}>
        <input
          ref={inputRef}
          className="cmd-palette-input"
          placeholder="输入命令..."
          value={query}
          onChange={e => setQuery(e.target.value)}
          onKeyDown={onKey}
        />
        <div className="cmd-palette-list">
          {filtered.map((c, i) => (
            <button
              key={c.id}
              className={"cmd-item" + (i === sel ? " sel" : "")}
              onMouseEnter={() => setSel(i)}
              onClick={() => { onClose(); c.run(); }}
            >
              {c.icon && <i className={`codicon codicon-${c.icon} cmd-item-icon`} />}
              <span className="cmd-item-label">{c.label}</span>
              {c.shortcut && <span className="cmd-item-kbd">{c.shortcut}</span>}
            </button>
          ))}
          {filtered.length === 0 && <div className="cmd-empty">无匹配命令</div>}
        </div>
      </div>
    </div>
  );
}
