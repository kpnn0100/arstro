/*
 * Solaris — kit/slot.js: a widget module mounted into its box, never trusted to exist or to work (PORTING.md §3).
 *
 *   slot(host, path, vm, ctx, { name, placeholder }) -> { ready, dispose() }
 *     host         the box (an absolutely positioned <div> the caller owns and lays out)
 *     path         the module, relative to web/js — "mixer/dock.js"; it exports mount(host, vm, ctx)
 *     name         the native widget's name, for the placeholder and the failure note ("MixerDock")
 *     placeholder  (el) => void — draws the quiet stand-in INTO `el` (the native's panel colours), shown until the
 *                  module mounts, and kept when there is no such module yet
 *     ready        Promise<"mounted" | "absent" | "failed">
 *     dispose()    destroy(host) (core/dom.js runs every disposer the module registered with own()) and empty it
 *
 * Not there yet (web/files.json — the installed tree, written by `ntwb install` — does not list it): the placeholder
 * stays, quietly, and nothing is fetched (no 404 in anyone's console). A module that fails to load or throws in
 * mount: "<name> failed to load" in its box, the error on the console, and the rest of the page keeps working.
 * Without files.json (a page served from the source tree) the module is simply imported.
 */
import { destroy } from "../core/dom.js";

let FILES = null;
/** The installed tree's files (web/files.json), or null when the page has no list. */
export function installedFiles() {
  if (!FILES) {
    FILES = fetch(new URL("../../files.json", import.meta.url), { cache: "no-store" })
      .then((r) => (r.ok ? r.json() : null))
      .then((a) => (Array.isArray(a) ? new Set(a) : null))
      .catch(() => null);
  }
  return FILES;
}
/** Whether web/js/<path> is installed (true when there is no list to ask). */
export async function hasModule(path) {
  const files = await installedFiles();
  return !files || files.has("js/" + path);
}

export function slot(host, path, vm, ctx, { name = path, placeholder } = {}) {
  let disposed = false;
  const ph = document.createElement("div");
  ph.className = "sl-ph";
  ph.dataset.widget = name;
  try { if (placeholder) placeholder(ph); } catch (e) { console.error(e); }
  const label = document.createElement("div");
  label.className = "sl-ph-name";
  label.textContent = name;
  ph.append(label);
  host.append(ph);
  host.dataset.slot = path;

  const fail = (e) => {
    console.error(`solaris: ${name} (js/${path}) failed to load:`, e);
    label.textContent = name + " failed to load";
    ph.classList.add("sl-ph-failed");
    if (!ph.isConnected) host.append(ph);
    return "failed";
  };

  const ready = (async () => {
    if (!(await hasModule(path))) return "absent";
    let mod;
    try { mod = await import(new URL("../" + path, import.meta.url).href); }
    catch (e) { return disposed ? "failed" : fail(e); }
    if (disposed) return "failed";
    if (typeof mod.mount !== "function") return fail(new Error("it exports no mount(host, vm, ctx)"));
    ph.remove();
    try { await mod.mount(host, vm, ctx); }
    catch (e) { try { destroy(host); } catch { /* the module's own */ } host.replaceChildren(); return fail(e); }
    host.dataset.mounted = "1";
    return "mounted";
  })();

  return {
    ready,
    dispose() {
      disposed = true;
      try { destroy(host); } catch (e) { console.error(e); }
      host.replaceChildren();
    },
  };
}
