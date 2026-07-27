import { useState, useCallback } from "react";

interface ModelFile {
  name: string;
  size_kb: number;
  path: string;
}

interface Props {
  visible: boolean;
  onClose: () => void;
  projectDir: string | null;
  onLog: (msg: string) => void;
}

export default function TFCardManager({ visible, onClose, projectDir, onLog }: Props) {
  const [models, setModels] = useState<ModelFile[]>([]);
  const [logs, setLogs] = useState<string[]>([]);
  const [loading, setLoading] = useState(false);
  const [deploying, setDeploying] = useState("");
  const [tfCardPath, setTfCardPath] = useState("");
  const [activeTab, setActiveTab] = useState<"models" | "deploy" | "logs">("models");

  const handleRefresh = useCallback(async () => {
    if (!tfCardPath.trim()) return;
    setLoading(true);
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const result = await invoke<ModelFile[]>("tfcard_list", { modelDir: tfCardPath });
      setModels(result);
      onLog(`TF卡扫描完成: 发现 ${result.length} 个模型文件`);
    } catch (e) {
      onLog(`TF卡扫描失败: ${e}`);
    } finally {
      setLoading(false);
    }
  }, [tfCardPath, onLog]);

  const handleDeploy = useCallback(async (modelPath: string, modelName: string) => {
    if (!tfCardPath.trim()) return;
    setDeploying(modelName);
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const result = await invoke<string>("tfcard_deploy", {
        sourcePath: modelPath,
        destDir: tfCardPath,
      });
      onLog(`部署成功: ${result}`);
      // 刷新列表
      const updated = await invoke<ModelFile[]>("tfcard_list", { modelDir: tfCardPath });
      setModels(updated);
    } catch (e) {
      onLog(`部署失败: ${e}`);
    } finally {
      setDeploying("");
    }
  }, [tfCardPath, onLog]);

  const handleLoadLogs = useCallback(async () => {
    if (!tfCardPath.trim()) return;
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const result = await invoke<string[]>("tfcard_logs", { logDir: tfCardPath });
      setLogs(result);
      onLog(`加载了 ${result.length} 条日志`);
    } catch (e) {
      onLog(`加载日志失败: ${e}`);
    }
  }, [tfCardPath, onLog]);

  if (!visible) return null;

  const totalSizeKB = models.reduce((sum, m) => sum + m.size_kb, 0);

  return (
    <div className="tfcard-manager">
      <div className="tfcard-header">
        <span className="tfcard-title">TF Card Manager</span>
        <button className="tfcard-close" onClick={onClose}>x</button>
      </div>

      {/* TF Card Path */}
      <div className="tfcard-path-section">
        <label className="tfcard-label">TF Card Path</label>
        <div className="tfcard-path-row">
          <input
            className="tfcard-path-input"
            type="text"
            value={tfCardPath}
            onChange={(e) => setTfCardPath(e.target.value)}
            placeholder="/mnt/sdcard/models or E:\tfcard\models"
          />
          <button className="tfcard-refresh-btn" onClick={handleRefresh} disabled={loading || !tfCardPath.trim()}>
            {loading ? "..." : "Scan"}
          </button>
        </div>
      </div>

      {/* Tabs */}
      <div className="tfcard-tabs">
        <button
          className={`tfcard-tab ${activeTab === "models" ? "active" : ""}`}
          onClick={() => setActiveTab("models")}
        >Models ({models.length})</button>
        <button
          className={`tfcard-tab ${activeTab === "deploy" ? "active" : ""}`}
          onClick={() => setActiveTab("deploy")}
        >Deploy</button>
        <button
          className={`tfcard-tab ${activeTab === "logs" ? "active" : ""}`}
          onClick={() => { setActiveTab("logs"); handleLoadLogs(); }}
        >Logs</button>
      </div>

      {/* Models Tab */}
      {activeTab === "models" && (
        <div className="tfcard-models">
          {models.length === 0 && !loading && (
            <div className="tfcard-empty">
              {tfCardPath.trim() ? "No .qianlu models found" : "Enter TF card path and click Scan"}
            </div>
          )}
          {models.length > 0 && (
            <>
              <div className="tfcard-summary">
                {models.length} models, {totalSizeKB.toFixed(1)} KB total
              </div>
              <table className="tfcard-table">
                <thead>
                  <tr>
                    <th>Name</th>
                    <th>Size</th>
                    <th>Actions</th>
                  </tr>
                </thead>
                <tbody>
                  {models.map((m, i) => (
                    <tr key={i}>
                      <td className="tfcard-model-name">{m.name}</td>
                      <td className="tfcard-model-size">{m.size_kb.toFixed(1)} KB</td>
                      <td>
                        <button
                          className="tfcard-delete-btn"
                          title="Delete from TF card"
                          onClick={() => onLog(`Delete: ${m.name} (not yet implemented)`)}
                        >Del</button>
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </>
          )}
        </div>
      )}

      {/* Deploy Tab */}
      {activeTab === "deploy" && (
        <div className="tfcard-deploy">
          <div className="tfcard-deploy-desc">
            Deploy .qianlu model files to TF card for on-device inference.
          </div>
          {projectDir ? (
            <div className="tfcard-deploy-section">
              <label className="tfcard-label">Source Model Path</label>
              <input
                className="tfcard-path-input"
                type="text"
                id="deploy-source-path"
                placeholder="path/to/model.qianlu"
              />
              <button
                className="tfcard-deploy-btn"
                disabled={deploying !== ""}
                onClick={() => {
                  const input = document.getElementById("deploy-source-path") as HTMLInputElement;
                  if (input?.value.trim()) {
                    const name = input.value.split(/[/\\]/).pop() || "model";
                    handleDeploy(input.value.trim(), name);
                  }
                }}
              >
                {deploying ? `Deploying ${deploying}...` : "Deploy to TF Card"}
              </button>
            </div>
          ) : (
            <div className="tfcard-no-project">Open a project first to deploy models.</div>
          )}
        </div>
      )}

      {/* Logs Tab */}
      {activeTab === "logs" && (
        <div className="tfcard-logs">
          {logs.length === 0 ? (
            <div className="tfcard-empty">No inference logs found</div>
          ) : (
            <div className="tfcard-log-list">
              {logs.map((log, i) => (
                <div key={i} className="tfcard-log-entry">{log}</div>
              ))}
            </div>
          )}
        </div>
      )}
    </div>
  );
}
