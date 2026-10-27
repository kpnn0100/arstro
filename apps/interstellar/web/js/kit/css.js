/*
 * Interstellar — kit/css.js: a widget module's own stylesheet, loaded once.
 *
 *   useCss("css/cut/timeline.css") -> Promise<void>   (path relative to the page's root, web/)
 *
 * PORTING §3: one stylesheet per widget module (web/css/<area>/<widget>.css), loaded by the module
 * itself so the shell never lists another agent's files. Called at the top of `mount` (or at module
 * scope); a second call for the same path returns the first promise. The <link> goes into <head>
 * after tokens.css / base.css, so a widget's rules can use every token. Resolves once the sheet
 * applied (or failed: a missing sheet is logged, never fatal - the widget draws with what it has).
 * A path the installed page does not have (files.json) is not even requested.
 */
import { WEB, installed } from "./files.js";

const SHEETS = new Map();

export function useCss(path) {
  if (SHEETS.has(path)) return SHEETS.get(path);
  const p = installed(path).then((has) => new Promise((resolve) => {
    if (has === false) { console.warn("interstellar: no stylesheet " + path + " in the installed page"); resolve(); return; }
    const link = document.createElement("link");
    link.rel = "stylesheet";
    link.href = new URL(path, WEB).href;
    link.dataset.module = path;
    link.onload = () => resolve();
    link.onerror = () => { console.warn("interstellar: stylesheet " + path + " failed to load"); resolve(); };
    document.head.append(link);
  }));
  SHEETS.set(path, p);
  return p;
}
