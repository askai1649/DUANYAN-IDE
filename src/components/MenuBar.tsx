// MenuBar — VSCode 式下拉菜单栏
// 一期重构: 17 个平铺按钮瘦身为 6 个类别菜单, 长尾功能迁入命令面板

import { useEffect, useRef, useState } from "react";

export interface MenuItem {
  label?: string;
  shortcut?: string;
  onClick?: () => void;
  separator?: boolean;
}

export interface MenuDef {
  title: string;
  items: MenuItem[];
}

export default function MenuBar({ menus }: { menus: MenuDef[] }) {
  const [open, setOpen] = useState<string | null>(null);
  const ref = useRef<HTMLDivElement>(null);

  // 点击菜单外自动关闭
  useEffect(() => {
    if (!open) return;
    const onDown = (e: MouseEvent) => {
      if (ref.current && !ref.current.contains(e.target as Node)) setOpen(null);
    };
    document.addEventListener("mousedown", onDown);
    return () => document.removeEventListener("mousedown", onDown);
  }, [open]);

  return (
    <div className="menubar" ref={ref}>
      {menus.map(m => (
        <div key={m.title} className="menu-root">
          <button
            className={"menu-btn" + (open === m.title ? " open" : "")}
            onClick={() => setOpen(open === m.title ? null : m.title)}
            onMouseEnter={() => { if (open && open !== m.title) setOpen(m.title); }}
          >
            {m.title}
          </button>
          {open === m.title && (
            <div className="menu-dropdown">
              {m.items.map((it, i) =>
                it.separator ? (
                  <div key={i} className="menu-sep" />
                ) : (
                  <button
                    key={i}
                    className="menu-item"
                    onClick={() => { setOpen(null); it.onClick?.(); }}
                  >
                    <span className="menu-item-label">{it.label}</span>
                    {it.shortcut && <span className="menu-item-kbd">{it.shortcut}</span>}
                  </button>
                )
              )}
            </div>
          )}
        </div>
      ))}
    </div>
  );
}
