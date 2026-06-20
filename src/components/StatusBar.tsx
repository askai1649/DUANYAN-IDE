interface Props {
  fileName?: string;
  language?: string;
  modified?: boolean;
}

export default function StatusBar({ fileName, language, modified }: Props) {
  return (
    <div className="statusbar">
      <div className="statusbar-left">
        <span>SNAR IDE v0.1.0</span>
      </div>
      <div className="statusbar-right">
        {fileName && <span>{fileName}{modified ? " *" : ""}</span>}
        {language && <span>{language}</span>}
        <span>UTF-8</span>
        <span>COUNPRE64</span>
      </div>
    </div>
  );
}
