/**
 * DUANYAN 语言智能集成
 * 
 * 将 HardyScript 编译器的语义知识注入 Monaco Editor:
 * - 实时诊断 (红色/黄色波浪线)
 * - 上下文补全 (Ctrl+Space)
 * - 悬停信息 (WCET 提示)
 */
import { useEffect, useRef } from "react";

interface DiagItem {
  severity: string;
  line: number;
  col: number;
  message: string;
  code: string;
}

interface CompletionEntry {
  label: string;
  kind: string;
  detail: string;
  insertText: string;
}

interface HoverResult {
  title: string;
  signature: string;
  description: string;
  wcet: string;
}

let registered = false;

export function useHardyIntelligence(
  editorRef: React.MutableRefObject<any>,
  monacoRef: React.MutableRefObject<any>,
  target?: string
) {
  const diagTimer = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(() => {
    const editor = editorRef.current;
    const monaco = monacoRef.current;
    if (!editor || !monaco) return;

    const model = editor.getModel();
    if (!model) return;

    // === 实时诊断 (防抖 500ms) ===
    const runDiagnostics = async () => {
      const source = model.getValue();
      try {
        const { invoke } = await import("@tauri-apps/api/core");
        const json = await invoke<string>("hardy_diagnose", {
          source,
          target: target || null,
        });
        const diags: DiagItem[] = JSON.parse(json);
        const markers = diags.map((d) => ({
          severity:
            d.severity === "Error"
              ? monaco.MarkerSeverity.Error
              : d.severity === "Warning"
              ? monaco.MarkerSeverity.Warning
              : d.severity === "Info"
              ? monaco.MarkerSeverity.Info
              : monaco.MarkerSeverity.Hint,
          startLineNumber: d.line,
          startColumn: d.col,
          endLineNumber: d.line,
          endColumn: d.col + 80,
          message: `[${d.code}] ${d.message}`,
          source: "DUANYAN",
        }));
        monaco.editor.setModelMarkers(model, "hardy", markers);
      } catch {
        // Tauri not available (dev mode without backend)
      }
    };

    const onContentChange = () => {
      if (diagTimer.current) clearTimeout(diagTimer.current);
      diagTimer.current = setTimeout(runDiagnostics, 500);
    };

    const changeDisposable = model.onDidChangeContent(onContentChange);
    // Initial run
    runDiagnostics();

    // === 补全提供器 ===
    if (!registered) {
      registered = true;

      monaco.languages.registerCompletionItemProvider("javascript", {
        triggerCharacters: [".", "("],
        provideCompletionItems: async (mdl: any, position: any) => {
          const source = mdl.getValue();
          const line = position.lineNumber;
          const col = position.column - 1;
          try {
            const { invoke } = await import("@tauri-apps/api/core");
            const json = await invoke<string>("hardy_complete", {
              source,
              line,
              col,
              target: target || null,
            });
            const items: CompletionEntry[] = JSON.parse(json);
            const word = mdl.getWordUntilPosition(position);
            const range = {
              startLineNumber: line,
              endLineNumber: line,
              startColumn: word.startColumn,
              endColumn: word.endColumn,
            };
            return {
              suggestions: items.map((item) => ({
                label: item.label,
                kind: mapKind(monaco, item.kind),
                detail: item.detail,
                insertText: item.insertText,
                insertTextRules:
                  monaco.languages.CompletionItemInsertTextRule.InsertAsSnippet,
                range,
              })),
            };
          } catch {
            return { suggestions: [] };
          }
        },
      });

      // === 悬停提供器 ===
      monaco.languages.registerHoverProvider("javascript", {
        provideHover: async (mdl: any, position: any) => {
          const source = mdl.getValue();
          const line = position.lineNumber;
          const col = position.column;
          try {
            const { invoke } = await import("@tauri-apps/api/core");
            const json = await invoke<string>("hardy_hover", {
              source,
              line,
              col,
            });
            if (json === "null") return null;
            const info: HoverResult = JSON.parse(json);
            const word = mdl.getWordAtPosition(position);
            if (!word) return null;
            return {
              range: new monaco.Range(
                line,
                word.startColumn,
                line,
                word.endColumn
              ),
              contents: [
                { value: `**${info.title}**` },
                { value: `\`${info.signature}\`` },
                { value: info.description },
                ...(info.wcet
                  ? [{ value: `⏱ WCET: ${info.wcet}` }]
                  : []),
              ],
            };
          } catch {
            return null;
          }
        },
      });
    }

    return () => {
      changeDisposable.dispose();
      if (diagTimer.current) clearTimeout(diagTimer.current);
    };
  }, [editorRef.current, target]);
}

function mapKind(monaco: any, kind: string): number {
  switch (kind) {
    case "Keyword":
      return monaco.languages.CompletionItemKind.Keyword;
    case "Function":
      return monaco.languages.CompletionItemKind.Function;
    case "HardwareApi":
      return monaco.languages.CompletionItemKind.Method;
    case "MathApi":
      return monaco.languages.CompletionItemKind.Module;
    case "IndustryApi":
      return monaco.languages.CompletionItemKind.Interface;
    case "Variable":
      return monaco.languages.CompletionItemKind.Variable;
    case "Snippet":
      return monaco.languages.CompletionItemKind.Snippet;
    default:
      return monaco.languages.CompletionItemKind.Text;
  }
}
