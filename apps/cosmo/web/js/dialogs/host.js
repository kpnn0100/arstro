/*
 * Cosmo by arstro — host.js: the dialog host - the overlay pass where App draws its modals
 * (App.cpp: History, ContextMenu, Preset, Settings, Export, Confirm, Info over 0,0,W,H).
 *
 * It renders `vm.view.dialog` - this page's view state, `{name, ...props}` - with the module of
 * that name:
 *   {name: "settings"}                                    SettingsDialog
 *   {name: "confirm", title, message, buttons: [{label, primary?, destructive?, run?()}]}
 *                                                         ConfirmDialog (e.g. "Unsaved changes")
 *   {name: "prompt", title, placeholder, value, ok, run(text)}   the preset-name prompt (§5.6)
 *   {name: "info", node}                                  InfoDialog ("Image information")
 *   {name: "preset", mode: "save"|"export"|"import"}      PresetDialog (+ the name prompt)
 *   {name: "export"}                                      ExportDialog
 * Setting the signal to null closes the open one (animated); a dialog that closes itself sets
 * it back to null. `pick()` (picker.js) and `confirm()` (confirm.js) stack their own modals on
 * this host's layer - over whatever dialog is open - and resolve a promise instead.
 *
 * One host per page: the first mounted wins (screens.js mounts it over Home AND the editor, so
 * the scrim covers the whole window); a second mountDialogHost() for the same view-model - the
 * editor's own, when screens.js is present - is a no-op.
 */
import { h, own } from "../core/dom.js";
import { effect, untracked } from "../core/signal.js";

const HOSTS = new WeakMap();
const MODULES = {
  settings: ["./settings.js", "open"], confirm: ["./confirm.js", "open"], prompt: ["./confirm.js", "openPrompt"],
  info: ["./info.js", "open"], preset: ["./preset.js", "open"], export: ["./export.js", "open"],
};

export function hostOf(vm) {
  const h0 = HOSTS.get(vm);
  return h0 && h0.layer.isConnected ? h0 : null;
}

export function mountDialogHost(el, vm, ctx) {
  if (hostOf(vm)) return () => {};
  const layer = h("div.dg-host");
  el.append(layer);
  const host = { layer, vm, ctx: ctx || {}, current: null };
  HOSTS.set(vm, host);

  /** Called by a dialog when it closes itself: clear the view state if it still names it. */
  host.closed = (spec) => {
    if (host.current && host.current.spec === spec) host.current = null;
    if (vm.view.dialog.peek() === spec) vm.view.dialog.value = null;
  };

  const dispose = effect(() => {
    const spec = vm.view.dialog.value;
    untracked(() => {
      const cur = host.current;
      if (cur && cur.spec === spec) return;
      if (cur) { host.current = null; try { cur.api && cur.api.close(); } catch (e) { console.error(e); } cur.cancelled = true; }
      if (!spec) return;
      const m = MODULES[spec.name];
      if (!m) { console.warn("cosmo: no dialog named", spec.name); vm.view.dialog.value = null; return; }
      const entry = { spec, api: null, cancelled: false };
      host.current = entry;
      import(m[0]).then((mod) => {
        if (entry.cancelled || host.current !== entry) return;
        return Promise.resolve(mod[m[1]](host, spec)).then((api) => {
          if (entry.cancelled || host.current !== entry) { if (api) api.close(); return; }
          if (!api) { host.closed(spec); return; }          // the dialog decided not to open (e.g. no metadata)
          entry.api = api;
        });
      }).catch((e) => {
        console.error(e);
        if (host.ctx.toast) host.ctx.toast("This dialog failed to open: " + e.message, true);
        host.closed(spec);
      });
    });
  });
  own(layer, dispose);
  own(layer, () => { if (HOSTS.get(vm) === host) HOSTS.delete(vm); });
  return dispose;
}

/** A host for a page that has none (a picker called before any shell mounted one). */
export function ensureHost(vm) {
  let host = hostOf(vm);
  if (host) return host;
  const app = document.getElementById("app") || document.body;
  const holder = h("div.dg-fallback");
  app.append(holder);
  mountDialogHost(holder, vm, {});
  return hostOf(vm);
}
