interface Props {
  fileName?: string;
  language?: string;
  modified?: boolean;
  status?: string;
  target?: string;
  branch?: string;
  errors?: number;
  warnings?: number;
}

export default function StatusBar({ fileName, language, modified, status, target, branch, errors = 0, warnings = 0 }: Props) {
  return (
    <div className="statusbar">
      <div className="statusbar-left">
        <span>DUANYAN IDE v1.2.0</span>
        {status && <span className="statusbar-message">{status}</span>}
        {branch && (
          <span className="statusbar-item statusbar-branch" title={"Git 分支: " + branch}>
            <i className="codicon codicon-source-control" />
            {branch}
          </span>
        )}
        <span
          className={"statusbar-item statusbar-problems" + (errors > 0 ? " has-errors" : "")}
          title={`${errors} 个错误, ${warnings} 个警告`}
        >
          <i className="codicon codicon-error" />
          {errors}
          <i className="codicon codicon-warning" />
          {warnings}
        </span>
      </div>
      <div className="statusbar-right">
        {fileName && <span>{fileName}{modified ? " *" : ""}</span>}
        {language && <span>{language}</span>}
        <span>UTF-8</span>
        <span className="statusbar-target" title="目标板卡">
          <i className="codicon codicon-circuit-board" />
          {target || "COUNPRE64-FPGA"}
        </span>
      </div>
    </div>
  );
}
