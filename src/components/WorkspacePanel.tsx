import { useState, useEffect } from "react";
import { invoke } from "@tauri-apps/api/core";

interface PackageRef {
  name: string;
  path: string;
  target: string | null;
  dependencies: string[];
}

interface PackageStatus {
  name: string;
  path: string;
  target: string;
  has_config: boolean;
  has_entry: boolean;
  last_build: string | null;
}

interface WorkspaceConfig {
  name: string;
  version: string;
  packages: PackageRef[];
  shared_libs: string[];
  default_target: string;
  build_order: string[];
}

interface Props {
  projectDir: string;
  onLog: (msg: string) => void;
}

export default function WorkspacePanel({ projectDir, onLog }: Props) {
  const [ws, setWs] = useState<WorkspaceConfig | null>(null);
  const [statuses, setStatuses] = useState<PackageStatus[]>([]);
  const [loading, setLoading] = useState(false);
  const [newPkgName, setNewPkgName] = useState("");
  const [newPkgTarget, setNewPkgTarget] = useState("");
  const [showAddForm, setShowAddForm] = useState(false);

  useEffect(() => {
    loadWorkspace();
  }, [projectDir]);

  const loadWorkspace = async () => {
    setLoading(true);
    try {
      const config = await invoke<WorkspaceConfig>("workspace_load", { dir: projectDir });
      setWs(config);
      const statusList = await invoke<PackageStatus[]>("workspace_status", { dir: projectDir });
      setStatuses(statusList);
    } catch (e) {
      onLog(`Workspace: ${e}`);
    }
    setLoading(false);
  };

  const handleInit = async () => {
    try {
      const config = await invoke<WorkspaceConfig>("workspace_init", {
        dir: projectDir,
        name: "workspace",
        monorepo: true,
      });
      setWs(config);
      onLog("Workspace initialized!");
    } catch (e) {
      onLog(`Init failed: ${e}`);
    }
  };

  const handleBuildAll = async () => {
    onLog("Building all workspace packages...");
    try {
      const result = await invoke<string>("workspace_build_all", { dir: projectDir });
      onLog(result);
      loadWorkspace();
    } catch (e) {
      onLog(`Build failed: ${e}`);
    }
  };

  const handleAddPackage = async () => {
    if (!newPkgName.trim()) return;
    try {
      await invoke("workspace_add_package", {
        dir: projectDir,
        name: newPkgName,
        path: `packages/${newPkgName}`,
        target: newPkgTarget || null,
      });
      setNewPkgName("");
      setNewPkgTarget("");
      setShowAddForm(false);
      onLog(`Added package: ${newPkgName}`);
      loadWorkspace();
    } catch (e) {
      onLog(`Add failed: ${e}`);
    }
  };

  if (loading) {
    return <div className="workspace-panel"><div className="loading">Loading...</div></div>;
  }

  if (!ws) {
    return (
      <div className="workspace-panel">
        <div className="workspace-empty">
          <h3>No Workspace Found</h3>
          <p>Initialize a workspace to manage multiple projects.</p>
          <button className="btn-primary" onClick={handleInit}>
            Initialize Workspace
          </button>
        </div>
      </div>
    );
  }

  return (
    <div className="workspace-panel">
      <div className="workspace-header">
        <h3>{ws.name} <span className="version">v{ws.version}</span></h3>
        <div className="workspace-actions">
          <button className="btn-sm" onClick={handleBuildAll} title="Build All">
            Build All
          </button>
          <button className="btn-sm" onClick={() => setShowAddForm(!showAddForm)} title="Add Package">
            + Add
          </button>
          <button className="btn-sm" onClick={loadWorkspace} title="Refresh">
            Refresh
          </button>
        </div>
      </div>

      {showAddForm && (
        <div className="workspace-add-form">
          <input
            type="text"
            placeholder="Package name"
            value={newPkgName}
            onChange={(e) => setNewPkgName(e.target.value)}
          />
          <select value={newPkgTarget} onChange={(e) => setNewPkgTarget(e.target.value)}>
            <option value="">Default target</option>
            <option value="esp32">ESP32</option>
            <option value="arduino-avr">Arduino AVR</option>
            <option value="stm32">STM32</option>
            <option value="counpre64">COUNPRE64</option>
            <option value="rpi">Raspberry Pi</option>
          </select>
          <button className="btn-sm" onClick={handleAddPackage}>Create</button>
        </div>
      )}

      <div className="workspace-packages">
        {statuses.map((pkg) => (
          <div key={pkg.name} className="workspace-package">
            <div className="pkg-header">
              <span className="pkg-name">{pkg.name}</span>
              <span className="pkg-target">{pkg.target}</span>
            </div>
            <div className="pkg-details">
              <span className="pkg-path">{pkg.path}</span>
              <div className="pkg-status-icons">
                <span title={pkg.has_config ? "Config OK" : "No config"} className={pkg.has_config ? "ok" : "warn"}>
                  {pkg.has_config ? "\u2713" : "\u2717"} cfg
                </span>
                <span title={pkg.has_entry ? "Entry OK" : "No entry"} className={pkg.has_entry ? "ok" : "warn"}>
                  {pkg.has_entry ? "\u2713" : "\u2717"} entry
                </span>
                <span className={pkg.last_build ? "ok" : "neutral"}>
                  {pkg.last_build || "not built"}
                </span>
              </div>
            </div>
            {ws.packages.find(p => p.name === pkg.name)?.dependencies?.length! > 0 && (
              <div className="pkg-deps">
                deps: {ws.packages.find(p => p.name === pkg.name)!.dependencies.join(", ")}
              </div>
            )}
          </div>
        ))}
      </div>

      {ws.build_order.length > 0 && (
        <div className="workspace-build-order">
          <h4>Build Order</h4>
          <div className="build-order-list">
            {ws.build_order.map((name, i) => (
              <span key={name} className="build-order-item">
                {i > 0 && <span className="arrow">&rarr;</span>}
                {name}
              </span>
            ))}
          </div>
        </div>
      )}
    </div>
  );
}
