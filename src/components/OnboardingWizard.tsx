import { useState, useEffect } from "react";

interface BoardInfo {
  name: string;
  description: string;
  mcu: string;
  ram_kb: number;
  flash_kb: number;
}

interface Props {
  visible: boolean;
  onClose: () => void;
  onTargetChange: (target: string) => void;
  onProjectCreated: (path: string, name: string, content: string) => void;
}

const STEPS = [
  { title: "Welcome", desc: "Welcome to SNAR IDE v1.0" },
  { title: "Select Board", desc: "Choose your target board" },
  { title: "Write Code", desc: "Create your first project" },
  { title: "Build & Flash", desc: "Compile and deploy" },
  { title: "Done!", desc: "You're ready to go" },
];

export default function OnboardingWizard({ visible, onClose, onTargetChange, onProjectCreated }: Props) {
  const [step, setStep] = useState(0);
  const [boards, setBoards] = useState<BoardInfo[]>([]);
  const [selectedBoard, setSelectedBoard] = useState("");

  useEffect(() => {
    if (visible) loadBoards();
  }, [visible]);

  const loadBoards = async () => {
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const result = await invoke<BoardInfo[]>("board_get_all");
      setBoards(result);
      if (result.length > 0) setSelectedBoard(result[0].name);
    } catch (e) {
      console.error("Failed to load boards:", e);
    }
  };

  const handleNext = () => {
    if (step < STEPS.length - 1) {
      setStep(step + 1);
    }
    if (step === 1 && selectedBoard) {
      onTargetChange(selectedBoard);
    }
  };

  const handleCreateProject = async () => {
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      const { open } = await import("@tauri-apps/plugin-dialog");
      const { readTextFile } = await import("@tauri-apps/plugin-fs");

      const baseDir = await open({ directory: true, multiple: false, title: "Select project directory" });
      if (!baseDir) return;

      const projectDir = `${baseDir}/my-first-snar-project`;
      await invoke("template_create", { templateId: "blink", projectDir });

      const mainPath = `${projectDir}/main.js`;
      const content = await readTextFile(mainPath);
      onProjectCreated(mainPath, "main.js", content);
    } catch (e) {
      console.error("Create project failed:", e);
    }
  };

  const handleFinish = () => {
    localStorage.setItem("snar-ide-onboarded", "true");
    onClose();
  };

  if (!visible) return null;

  return (
    <div className="wizard-overlay">
      <div className="wizard-modal">
        {/* Progress bar */}
        <div className="wizard-progress">
          {STEPS.map((s, i) => (
            <div key={i} className={`wizard-step-dot ${i <= step ? "active" : ""}`}>
              <div className="wizard-dot" />
              <span className="wizard-step-label">{s.title}</span>
            </div>
          ))}
        </div>

        {/* Step content */}
        <div className="wizard-content">
          {step === 0 && (
            <div className="wizard-step">
              <h2 className="wizard-step-title">Welcome to SNAR IDE</h2>
              <p className="wizard-step-text">
                SNAR IDE lets you write JavaScript to control hardware — from ESP32 to Arduino to custom FPGA boards.
              </p>
              <p className="wizard-step-text">
                This quick wizard will help you set up your first project in under 5 minutes.
              </p>
              <div className="wizard-features">
                <div className="wizard-feature">Write JS, deploy to hardware</div>
                <div className="wizard-feature">AI-powered with QianLu Mini</div>
                <div className="wizard-feature">One-click build & flash</div>
              </div>
            </div>
          )}

          {step === 1 && (
            <div className="wizard-step">
              <h2 className="wizard-step-title">Select Your Board</h2>
              <p className="wizard-step-text">Choose the development board you're using:</p>
              <div className="wizard-board-list">
                {boards.slice(0, 8).map((b) => (
                  <div
                    key={b.name}
                    className={`wizard-board-card ${selectedBoard === b.name ? "selected" : ""}`}
                    onClick={() => setSelectedBoard(b.name)}
                  >
                    <div className="wizard-board-name">{b.name}</div>
                    <div className="wizard-board-mcu">{b.mcu}</div>
                    <div className="wizard-board-specs">
                      {b.ram_kb >= 1024 ? `${(b.ram_kb/1024).toFixed(0)}MB` : `${b.ram_kb}KB`} RAM / {b.flash_kb >= 1024 ? `${(b.flash_kb/1024).toFixed(0)}MB` : `${b.flash_kb}KB`} Flash
                    </div>
                  </div>
                ))}
              </div>
            </div>
          )}

          {step === 2 && (
            <div className="wizard-step">
              <h2 className="wizard-step-title">Create Your First Project</h2>
              <p className="wizard-step-text">
                We'll create a simple LED blink project to get you started.
                Click the button below to create it.
              </p>
              <button className="wizard-create-btn" onClick={handleCreateProject}>
                Create Blink Project
              </button>
              <p className="wizard-step-hint">
                You'll be asked to choose a folder for your project.
              </p>
            </div>
          )}

          {step === 3 && (
            <div className="wizard-step">
              <h2 className="wizard-step-title">Build & Flash</h2>
              <p className="wizard-step-text">
                Now you can compile and flash your code to the board:
              </p>
              <div className="wizard-instructions">
                <div className="wizard-instruction">
                  <span className="wizard-key">Ctrl+B</span> Build (compile)
                </div>
                <div className="wizard-instruction">
                  <span className="wizard-key">Ctrl+Shift+F</span> Flash (deploy to board)
                </div>
                <div className="wizard-instruction">
                  <span className="wizard-key">Ctrl+R</span> Run (simulate)
                </div>
              </div>
            </div>
          )}

          {step === 4 && (
            <div className="wizard-step">
              <h2 className="wizard-step-title">You're All Set!</h2>
              <p className="wizard-step-text">
                Your SNAR IDE is ready. Start coding and deploy to hardware!
              </p>
              <div className="wizard-tips">
                <div className="wizard-tip">Press Ctrl+J to open the QianLu AI assistant</div>
                <div className="wizard-tip">Press Ctrl+Shift+B to open the OpenSNAR Bridge panel</div>
                <div className="wizard-tip">Explore project templates from the +Project button</div>
              </div>
            </div>
          )}
        </div>

        {/* Navigation */}
        <div className="wizard-nav">
          {step > 0 && (
            <button className="wizard-back-btn" onClick={() => setStep(step - 1)}>Back</button>
          )}
          <div className="wizard-nav-spacer" />
          {step < STEPS.length - 1 ? (
            <button className="wizard-next-btn" onClick={handleNext}>Next</button>
          ) : (
            <button className="wizard-finish-btn" onClick={handleFinish}>Get Started</button>
          )}
        </div>

        <button className="wizard-skip" onClick={handleFinish}>Skip</button>
      </div>
    </div>
  );
}
