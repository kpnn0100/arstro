/*
 * Interstellar — kit/files.js: where the page's files are, and which of them were installed.
 *
 *   WEB          the page's root URL (…/apps/interstellar/), from this module's own URL
 *   JS           the js/ folder - what slot paths ("edit/topbar.js") are relative to
 *   installed(p) -> Promise<boolean | null>   is `p` (relative to WEB: "js/edit/topbar.js",
 *                "css/edit/topbar.css") in the installed page? null = unknown (no list)
 *
 * `interstellar-cc ntwb install` writes web/files.json - every file it installed, relative to web/ -
 * so the slot loader and useCss() can tell "this widget is not ported yet" (a quiet placeholder, no
 * request at all) from "this widget failed to load" (a note, and the error on the console). Without
 * the list (the page served from a source tree) everything is tried and a missing module is a failure.
 */
export const WEB = new URL("../../", import.meta.url);
export const JS = new URL("../", import.meta.url);

let list = null;
function manifest() {
  if (!list) {
    list = fetch(new URL("files.json", WEB), { cache: "no-cache" })
      .then((r) => (r.ok ? r.json() : null))
      .then((j) => (j && Array.isArray(j.files) ? new Set(j.files) : null))
      .catch(() => null);
  }
  return list;
}

export async function installed(path) {
  const set = await manifest();
  return set ? set.has(path) : null;
}
