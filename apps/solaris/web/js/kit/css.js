/*
 * Solaris — kit/css.js: a widget module loads its own stylesheet (PORTING.md §3: web/css/<area>/<widget>.css).
 *
 *   useCss("mixer/dock.css") -> Promise   adds <link rel=stylesheet href="css/mixer/dock.css"> once per path (a second
 *                                          call returns the same promise); resolves when it has loaded — await it
 *                                          before measuring anything the sheet sizes. A sheet that fails to load is
 *                                          reported on the console and resolves anyway (the module still mounts).
 */
const sheets = new Map();

export function useCss(path) {
  if (sheets.has(path)) return sheets.get(path);
  const link = document.createElement("link");
  link.rel = "stylesheet";
  link.href = new URL("../../css/" + path, import.meta.url).href;
  const p = new Promise((resolve) => {
    link.onload = () => resolve(link);
    link.onerror = () => { console.error("solaris: the stylesheet css/" + path + " did not load"); resolve(link); };
  });
  document.head.append(link);
  sheets.set(path, p);
  return p;
}
