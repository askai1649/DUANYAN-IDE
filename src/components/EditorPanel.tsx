import Editor from "@monaco-editor/react";
import type { OpenFile } from "../App";

interface Props {
  file: OpenFile | null;
  onChange: (path: string, content: string) => void;
}

export default function EditorPanel({ file, onChange }: Props) {
  if (!file) {
    return (
      <div className="editor-container">
        <div className="editor-empty">
          <div style={{ textAlign: "center" }}>
            <div style={{ fontSize: 48, marginBottom: 16, opacity: 0.3 }}>SNAR</div>
            <div style={{ fontSize: 16, marginBottom: 24, color: "#555" }}>
              SNAR IDE v0.1.0 — COUNPRE64 JavaScript Compiler
            </div>
            <div style={{ color: "#666", lineHeight: 2 }}>
              <div><b>Open Folder</b> — browse project files</div>
              <div><b>Ctrl+O</b> — open file</div>
              <div><b>Ctrl+S</b> — save file</div>
              <div><b>Ctrl+B</b> — compile to assembly</div>
              <div><b>Ctrl+R</b> — compile and run</div>
            </div>
            <div style={{ marginTop: 24, color: "#444", fontSize: 12 }}>
              Powered by SNARjs + Tauri + Monaco Editor
            </div>
          </div>
        </div>
      </div>
    );
  }

  return (
    <div className="editor-container">
      <Editor
        height="100%"
        language={file.language}
        value={file.content}
        theme="vs-dark"
        onChange={(value) => onChange(file.path, value || "")}
        options={{
          fontSize: 14,
          fontFamily: '"Source Code Pro", "Cascadia Code", "Fira Code", "DejaVu Sans Mono", monospace',
          minimap: { enabled: true },
          scrollBeyondLastLine: false,
          automaticLayout: true,
          tabSize: 2,
          wordWrap: "on",
          padding: { top: 8 },
          bracketPairColorization: { enabled: true },
          renderLineHighlight: "all",
          cursorBlinking: "smooth",
          smoothScrolling: true,
        }}
      />
    </div>
  );
}
