interface Props {
  fileName?: string;
  language?: string;
  modified?: boolean;
  status?: string;
  target?: string;
}

export default function StatusBar({ fileName, language, modified, status, target }: Props) {
  return (
    <div className="statusbar">
      <div className="statusbar-left">
        <span>DUANYAN IDE v1.2.0</span>
        {status && <span className="statusbar-message">{status}</span>}
      </div>
      <div className="statusbar-right">
        {fileName && <span>{fileName}{modified ? " *" : ""}</span>}
        {language && <span>{language}</span>}
        <span>UTF-8</span>
        <span className="statusbar-target">{target || "COUNPRE64-FPGA"}</span>
      </div>
    </div>
  );
}
