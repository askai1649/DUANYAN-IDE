import { useState, useRef, useEffect, useCallback } from "react";

interface Props {
  onClose: () => void;
}

function parseReport(raw: string) {
  if (!raw) return null;
  const lines = raw.split("\n");
  const sections = {
    language: "", parseInfo: "",
    diagnostics: [] as {severity: string; line: number; message: string}[],
    reviewScore: 0, reviewFindings: [] as {category: string; line: number; description: string}[],
    patches: [] as {line: number; original: string; fixed: string; rule: string}[],
    refactorSuggestions: [] as string[],
    completions: [] as string[],
    execOutput: "",
  };
  let inExec = false; const execLines: string[] = [];
  for (const line of lines) {
    if (line.startsWith("Language: ")) sections.language = line.slice(10).trim();
    if (line.startsWith("Parse: ")) sections.parseInfo = line.slice(7).trim();
    const dm = line.match(/^\s+\[(\w+)\] L(\d+): (.+)$/);
    if (dm) {
      const sev = dm[1];
      if (["ERROR","WARN","INFO","CRITICAL"].includes(sev)) {
        sections.diagnostics.push({severity: sev, line: parseInt(dm[2]), message: dm[3]});
      } else {
        sections.reviewFindings.push({category: sev, line: parseInt(dm[2]), description: dm[3]});
      }
    }
    const sm = line.match(/Score: ([\d.]+)\/100/);
    if (sm) sections.reviewScore = parseFloat(sm[1]);
    const pm = line.match(/^\s+L(\d+) \[(\w+)\]: -(.+)$/);
    if (pm) sections.patches.push({line: parseInt(pm[1]), original: pm[3], fixed: "", rule: pm[2]});
    const fm = line.match(/^\s+\+(.+)$/);
    if (fm && sections.patches.length > 0) {
      const last = sections.patches[sections.patches.length-1];
      if (!last.fixed) last.fixed = fm[1];
    }
    if (line === "[EXEC]") { inExec = true; continue; }
    if (inExec) { if (line.startsWith("===")) inExec = false; else execLines.push(line); }
  }
  sections.execOutput = execLines.join("\n");
  return sections;
}

export default function CodeAnalysisPanel({ onClose }: Props) {
  const [code, setCode] = useState("fn process(data: Vec<u8>) -> String {\n    let result = data[0].clone();\n    if data.len() == 0 {\n        panic!(\"empty\");\n    }\n    dbg!(result);\n    result.to_string()\n}");
  const [language, setLanguage] = useState("rust");
  const [sections, setSections] = useState<ReturnType<typeof parseReport>>(null);
  const [isAnalyzing, setIsAnalyzing] = useState(false);
  const [activeTab, setActiveTab] = useState<"diagnostics"|"review"|"repair"|"refactor"|"exec">("diagnostics");
  const textareaRef = useRef<HTMLTextAreaElement>(null);
  const lineNumRef = useRef<HTMLDivElement>(null);
  const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  // Debounce auto-analyze (300ms)
  const debouncedAnalyze = useCallback((currentCode: string) => {
    if (debounceRef.current) clearTimeout(debounceRef.current);
    if (!currentCode.trim()) return;
    debounceRef.current = setTimeout(async () => {
      try {
        const { invoke } = await import("@tauri-apps/api/core");
        const r = await invoke<string>("qianlu_code_analyze_panel", { code: currentCode, lang: language });
        setSections(parseReport(r));
      } catch (e) { /* silent */ }
    }, 300);
  }, [language]);

  useEffect(() => {
    debouncedAnalyze(code);
    return () => { if (debounceRef.current) clearTimeout(debounceRef.current); };
  }, [code, debouncedAnalyze]);

  const invoke = async (cmd: string, args?: Record<string,unknown>) => {
    const { invoke } = await import("@tauri-apps/api/core");
    return await invoke<string>(cmd, args);
  };

  const runAnalysis = async (full: boolean) => {
    if (isAnalyzing || !code.trim()) return;
    setIsAnalyzing(true);
    try {
      const r = full
        ? await invoke("qianlu_code_pipeline_full", { text: code })
        : await invoke("qianlu_code_analyze_panel", { code, lang: language });
      setSections(parseReport(r));
    } catch (e) { console.error(e); }
    setIsAnalyzing(false);
  };

  const handleApplyPatch = (patch: {line:number;original:string;fixed:string}) => {
    const lines = code.split("\n");
    if (patch.line > 0 && patch.line <= lines.length) {
      lines[patch.line-1] = lines[patch.line-1].replace(patch.original, patch.fixed);
      setCode(lines.join("\n"));
    }
  };

  const handleScroll = () => {
    if (textareaRef.current && lineNumRef.current) lineNumRef.current.scrollTop = textareaRef.current.scrollTop;
  };

  const lineCount = code.split("\n").length;
  const sevColor = (s: string) => s === "ERROR" || s === "CRITICAL" ? "#ef4444" : s === "WARN" ? "#f59e0b" : "#6b7280";
  const scoreColor = (s: number) => s >= 80 ? "#22c55e" : s >= 60 ? "#f59e0b" : "#ef4444";
  const errCnt = sections?.diagnostics.filter(d=>d.severity==="ERROR"||d.severity==="CRITICAL").length ?? 0;
  const warnCnt = sections?.diagnostics.filter(d=>d.severity==="WARN").length ?? 0;

  const tabBtn = (key: string, label: string) => (
    <button key={key} onClick={()=>setActiveTab(key as typeof activeTab)} style={{
      background: activeTab===key?"#2d2d4a":"transparent", color: activeTab===key?"#e0e0e0":"#9ca3af",
      border:"none", borderBottom: activeTab===key?"2px solid #8b5cf6":"2px solid transparent",
      padding:"8px 12px", cursor:"pointer", fontSize:11,
    }}>{label}</button>
  );

  return (
    <div style={{position:"fixed",inset:0,zIndex:1000,background:"#1a1a2e",color:"#e0e0e0",display:"flex",flexDirection:"column",fontFamily:"'Source Code Pro','Cascadia Code','Fira Code','DejaVu Sans Mono',monospace"}}>
      {/* Header bar */}
      <div style={{display:"flex",alignItems:"center",gap:8,padding:"8px 16px",borderBottom:"1px solid #2d2d4a",background:"#16162a"}}>
        <span style={{fontWeight:"bold",fontSize:14,color:"#8b5cf6"}}>Code Analysis</span>
        <select value={language} onChange={e=>setLanguage(e.target.value)} style={{background:"#2d2d4a",color:"#e0e0e0",border:"1px solid #3d3d5a",borderRadius:4,padding:"4px 8px",fontSize:12}}>
          <option value="rust">Rust</option><option value="python">Python</option><option value="javascript">JavaScript</option><option value="go">Go</option><option value="java">Java</option>
        </select>
        <button onClick={()=>runAnalysis(false)} disabled={isAnalyzing} style={{background:isAnalyzing?"#3d3d5a":"#8b5cf6",color:"#fff",border:"none",borderRadius:4,padding:"4px 12px",cursor:"pointer",fontSize:12}}>{isAnalyzing?"...":"Analyze"}</button>
        <button onClick={()=>runAnalysis(true)} disabled={isAnalyzing} style={{background:isAnalyzing?"#3d3d5a":"#6366f1",color:"#fff",border:"none",borderRadius:4,padding:"4px 12px",cursor:"pointer",fontSize:12}}>Full Pipeline</button>
        <div style={{flex:1}}/>
        {sections && <span style={{fontSize:12,color:"#9ca3af"}}><span style={{color:"#22c55e",marginRight:4}}>LIVE</span>{sections.language} | {sections.parseInfo} | Score: <span style={{color:scoreColor(sections.reviewScore),fontWeight:"bold"}}>{sections.reviewScore.toFixed(1)}</span></span>}
        <button onClick={onClose} style={{background:"none",border:"none",color:"#9ca3af",cursor:"pointer",fontSize:18,lineHeight:1,padding:"0 4px"}}>&times;</button>
      </div>
      {/* Main split */}
      <div style={{flex:1,display:"flex",overflow:"hidden"}}>
        {/* Left: Code input */}
        <div style={{width:"45%",display:"flex",flexDirection:"column",borderRight:"1px solid #2d2d4a"}}>
          <div style={{padding:"6px 12px",background:"#16162a",borderBottom:"1px solid #2d2d4a",fontSize:11,color:"#9ca3af"}}>Source Code ({lineCount} lines)</div>
          <div style={{flex:1,display:"flex",overflow:"hidden"}}>
            <div ref={lineNumRef} style={{width:40,background:"#12122a",color:"#555",padding:"8px 4px",textAlign:"right",fontSize:12,lineHeight:"18px",overflow:"hidden",userSelect:"none"}}>
              {Array.from({length:lineCount},(_,i)=><div key={i}>{i+1}</div>)}
            </div>
            <textarea ref={textareaRef} value={code} onChange={e=>setCode(e.target.value)} onScroll={handleScroll} spellCheck={false}
              style={{flex:1,background:"#1e1e3a",color:"#e0e0e0",border:"none",outline:"none",resize:"none",padding:8,fontSize:12,lineHeight:"18px",fontFamily:"'Source Code Pro','Cascadia Code','Fira Code','DejaVu Sans Mono',monospace",tabSize:4}}/>
          </div>
        </div>
        {/* Right: Results */}
        <div style={{width:"55%",display:"flex",flexDirection:"column"}}>
          <div style={{display:"flex",background:"#16162a",borderBottom:"1px solid #2d2d4a"}}>
            {tabBtn("diagnostics",`Diagnostics (${errCnt+warnCnt})`)}
            {tabBtn("review",`Review (${sections?.reviewFindings.length??0})`)}
            {tabBtn("repair",`Repair (${sections?.patches.length??0})`)}
            {tabBtn("refactor",`Refactor`)}
            {tabBtn("exec","Exec")}
          </div>
          <div style={{flex:1,overflow:"auto",padding:12}}>
            {!sections && <div style={{color:"#555",textAlign:"center",marginTop:40,fontSize:13}}>Click "Analyze" or "Full Pipeline"</div>}
            {sections && activeTab==="diagnostics" && (
              <div>
                <div style={{marginBottom:8,fontSize:12,color:"#9ca3af"}}>{sections.diagnostics.length===0?"No issues found":`${sections.diagnostics.length} diagnostics`}</div>
                {sections.diagnostics.map((d,i)=>(
                  <div key={i} style={{padding:"6px 8px",marginBottom:4,borderRadius:4,background:"#1e1e3a",borderLeft:`3px solid ${sevColor(d.severity)}`,fontSize:12}}>
                    <span style={{color:sevColor(d.severity),fontWeight:"bold",marginRight:8}}>[{d.severity}]</span>
                    <span style={{color:"#6b7280"}}>L{d.line}: </span>{d.message}
                  </div>
                ))}
              </div>
            )}
            {sections && activeTab==="review" && (
              <div>
                <div style={{display:"flex",alignItems:"center",gap:12,marginBottom:12}}>
                  <span style={{fontSize:32,fontWeight:"bold",color:scoreColor(sections.reviewScore)}}>{sections.reviewScore.toFixed(1)}</span>
                  <span style={{color:"#9ca3af",fontSize:13}}>/100</span>
                </div>
                {sections.reviewFindings.map((f,i)=>(
                  <div key={i} style={{padding:"6px 8px",marginBottom:4,borderRadius:4,background:"#1e1e3a",fontSize:12}}>
                    <span style={{color:"#8b5cf6",marginRight:8}}>[{f.category}]</span>
                    <span style={{color:"#6b7280"}}>L{f.line}: </span>{f.description}
                  </div>
                ))}
              </div>
            )}
            {sections && activeTab==="repair" && (
              <div>
                <div style={{marginBottom:8,fontSize:12,color:"#9ca3af"}}>{sections.patches.length===0?"No repairs":`${sections.patches.length} patches`}</div>
                {sections.patches.map((p,i)=>(
                  <div key={i} style={{padding:8,marginBottom:8,borderRadius:4,background:"#1e1e3a",fontSize:12}}>
                    <div style={{color:"#9ca3af",marginBottom:4}}>L{p.line} [{p.rule}]</div>
                    <div style={{color:"#ef4444",textDecoration:"line-through",marginBottom:2}}>- {p.original}</div>
                    <div style={{color:"#22c55e",marginBottom:6}}>+ {p.fixed}</div>
                    {p.fixed && <button onClick={()=>handleApplyPatch(p)} style={{background:"#22c55e20",color:"#22c55e",border:"1px solid #22c55e40",borderRadius:3,padding:"2px 8px",cursor:"pointer",fontSize:10}}>Apply Fix</button>}
                  </div>
                ))}
              </div>
            )}
            {sections && activeTab==="refactor" && (
              <div>
                <pre style={{background:"#12122a",padding:12,borderRadius:4,fontSize:12,whiteSpace:"pre-wrap",lineHeight:1.5,color:"#f59e0b"}}>
                  {sections.refactorSuggestions.length===0?"No refactoring suggestions":sections.refactorSuggestions.join("\n")}
                </pre>
              </div>
            )}
            {sections && activeTab==="exec" && (
              <div>
                <div style={{marginBottom:8,fontSize:12,color:"#9ca3af"}}>Execution Output</div>
                <pre style={{background:"#12122a",padding:12,borderRadius:4,fontSize:12,whiteSpace:"pre-wrap",lineHeight:1.5,color:sections.execOutput.includes("[ERR]")?"#ef4444":"#22c55e"}}>
                  {sections.execOutput||"No execution output (code may not be directly executable)"}
                </pre>
                {sections.execOutput && (
                  <div style={{marginTop:12}}>
                    <div style={{fontSize:12,color:"#9ca3af",marginBottom:6}}>Variable States</div>
                    <div style={{background:"#12122a",borderRadius:4,padding:8}}>
                      {sections.execOutput.split("\n").filter(l=>l.startsWith("> ")).map((l,i)=>(
                        <div key={i} style={{fontSize:11,padding:"3px 6px",borderBottom:"1px solid #2d2d4a",color:"#e0e0e0"}}>
                          <span style={{color:"#8b5cf6"}}>{">"}</span> {l.slice(2)}
                        </div>
                      ))}
                      {sections.execOutput.split("\n").filter(l=>l.startsWith("> ")).length===0 && (
                        <div style={{fontSize:11,color:"#555",padding:"4px 6px"}}>No variable outputs</div>
                      )}
                    </div>
                  </div>
                )}
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}
