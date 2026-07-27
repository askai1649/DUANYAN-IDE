import { useState, useEffect } from "react";

interface TemplateInfo {
  id: string;
  name: string;
  description: string;
  icon: string;
  category: string;
}

interface Props {
  visible: boolean;
  onClose: () => void;
  onProjectCreated: (path: string, name: string, content: string) => void;
}

export default function TemplateLibrary({ visible, onClose, onProjectCreated }: Props) {
  const [templates, setTemplates] = useState<TemplateInfo[]>([]);
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [projectName, setProjectName] = useState("");
  const [creating, setCreating] = useState(false);

  useEffect(() => {
    if (visible) loadTemplates();
  }, [visible]);

  const loadTemplates = async () => {
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const result = await invoke<TemplateInfo[]>("template_list");
      setTemplates(result);
    } catch (e) {
      console.error("Failed to load templates:", e);
    }
  };

  const handleCreate = async () => {
    if (!selectedId || !projectName.trim()) return;
    setCreating(true);
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const { open } = await import("@tauri-apps/plugin-dialog");
      const { readTextFile } = await import("@tauri-apps/plugin-fs");

      const baseDir = await open({ directory: true, multiple: false, title: "Select project directory" });
      if (!baseDir) { setCreating(false); return; }

      const projectDir = `${baseDir}/${projectName}`;
      await invoke("template_create", { templateId: selectedId, projectDir });

      const mainPath = `${projectDir}/main.js`;
      const content = await readTextFile(mainPath);
      onProjectCreated(mainPath, "main.js", content);
      onClose();
    } catch (e) {
      console.error("Create from template failed:", e);
    } finally {
      setCreating(false);
    }
  };

  if (!visible) return null;

  const categories = [...new Set(templates.map(t => t.category))];

  return (
    <div className="template-overlay">
      <div className="template-modal">
        <div className="template-modal-header">
          <span className="template-modal-title">Project Templates</span>
          <button className="template-close" onClick={onClose}>x</button>
        </div>

        <div className="template-grid">
          {categories.map(cat => (
            <div key={cat} className="template-category">
              <div className="template-cat-title">{cat}</div>
              <div className="template-cards">
                {templates.filter(t => t.category === cat).map(t => (
                  <div
                    key={t.id}
                    className={`template-card ${selectedId === t.id ? "selected" : ""}`}
                    onClick={() => { setSelectedId(t.id); setProjectName(t.id); }}
                  >
                    <div className="template-icon">{t.icon}</div>
                    <div className="template-name">{t.name}</div>
                    <div className="template-desc">{t.description}</div>
                  </div>
                ))}
              </div>
            </div>
          ))}
        </div>

        {selectedId && (
          <div className="template-create-section">
            <label className="template-label">Project Name</label>
            <input
              className="template-input"
              value={projectName}
              onChange={(e) => setProjectName(e.target.value)}
              placeholder="my-project"
            />
            <button
              className="template-create-btn"
              onClick={handleCreate}
              disabled={!projectName.trim() || creating}
            >
              {creating ? "Creating..." : "Create Project"}
            </button>
          </div>
        )}
      </div>
    </div>
  );
}
