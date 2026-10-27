/*
 * Interstellar — kit/slot.js: one widget module, loaded into its box and isolated from the rest.
 *
 *   slot(host, path, vm, ctx, {name}) -> {state: Signal<"loading"|"absent"|"mounted"|"failed">,
 *                                         ready: Promise<state>, module}
 *
 *   host   the absolutely positioned <div> the screen owns (logical px); it may already hold a
 *          placeholder - any child with class `is-placeholder` - which is removed once the module
 *          mounted (and kept when it is absent or failed, so the frame still looks like the app's)
 *   path   the module, RELATIVE TO js/ ("edit/topbar.js", "grade/rack.js") - never to the caller
 *   name   the native widget's name for the note ("RackTree")
 *
 * The contract every widget module keeps (PORTING §3):  export function mount(host, vm, ctx)
 *
 * The module is imported dynamically inside try/catch: a module that fails to load or throws in
 * `mount` shows "<name> failed to load" in its box and logs why; the rest of the page keeps
 * working. A module the installed page does not have yet (web/files.json, written by `ntwb
 * install`) is not requested at all - the box keeps its quiet placeholder, with no 404 and no
 * console noise, which is what a wave of widgets landing one by one needs. `mount` may return a
 * disposer (or an object with destroy()); either way the screen tears a slot down with core/dom's
 * destroy(host), which runs every own(host, …) disposer under it.
 *
 * Never import another agent's widget module statically: talk through vm signals / ctx (the
 * cross-widget contract is in edit/screen.js's header). A widget that holds a sub-widget owned by
 * another module (the Grade inspector's scopes) mounts it with this same slot().
 */
import { signal } from "../core/signal.js";
import { h, own } from "../core/dom.js";
import { JS, installed } from "./files.js";

export function slot(host, path, vm, ctx, { name } = {}) {
  const state = signal("loading");
  const label = name || path.replace(/\.js$/, "");
  const out = { state, module: null, ready: null };
  out.ready = (async () => {
    const has = await installed("js/" + path);
    if (has === false) { state.value = "absent"; return state.peek(); }
    let mod;
    try {
      mod = await import(new URL(path, JS).href);
    } catch (e) {
      console.error(`interstellar: ${label} (${path}) failed to load:`, e);
      fail();
      return state.peek();
    }
    if (!host.isConnected && host.dataset.slotDead) return state.peek();   // torn down while loading
    try {
      if (typeof mod.mount !== "function") throw new Error(path + " exports no mount(host, vm, ctx)");
      const api = mod.mount(host, vm, ctx);
      if (typeof api === "function") own(host, api);
      else if (api && typeof api.destroy === "function") own(host, () => api.destroy());
      out.module = mod;
      for (const p of [...host.querySelectorAll(":scope > .is-placeholder")]) p.remove();
      state.value = "mounted";
    } catch (e) {
      console.error(`interstellar: ${label} (${path}) threw in mount:`, e);
      fail();
    }
    return state.peek();
  })();
  own(host, () => { host.dataset.slotDead = "1"; });
  function fail() {
    state.value = "failed";
    host.append(h("div.is-slot-fail", { text: label + " failed to load" }));
  }
  return out;
}
