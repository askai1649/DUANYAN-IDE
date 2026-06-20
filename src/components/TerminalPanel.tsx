import { useEffect, useRef } from "react";

interface Props {
  output: string[];
  onClear: () => void;
}

export default function TerminalPanel({ output, onClear }: Props) {
  const bodyRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (bodyRef.current) {
      bodyRef.current.scrollTop = bodyRef.current.scrollHeight;
    }
  }, [output]);

  return (
    <>
      <div className="terminal-header">
        <span>Output</span>
        <button onClick={onClear}>Clear</button>
      </div>
      <div className="terminal-body" ref={bodyRef}>
        {output.map((line, i) => (
          <div key={i} className={`terminal-line ${line.includes("[error]") ? "error" : ""}`}>
            {line}
          </div>
        ))}
      </div>
    </>
  );
}
