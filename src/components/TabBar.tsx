import type { OpenFile } from "../App";

interface Props {
  files: OpenFile[];
  activePath: string | null;
  onSelect: (path: string) => void;
  onClose: (path: string) => void;
}

export default function TabBar({ files, activePath, onSelect, onClose }: Props) {
  if (files.length === 0) return null;
  return (
    <div className="tab-bar">
      {files.map(f => (
        <div
          key={f.path}
          className={`tab ${f.path === activePath ? "active" : ""} ${f.modified ? "modified" : ""}`}
          onClick={() => onSelect(f.path)}
        >
          <span className="dot" />
          <span>{f.name}</span>
          <span className="close" onClick={(e) => { e.stopPropagation(); onClose(f.path); }}>×</span>
        </div>
      ))}
    </div>
  );
}
