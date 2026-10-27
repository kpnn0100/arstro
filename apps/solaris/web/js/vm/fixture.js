/*
 * Solaris — vm/fixture.js: a native shot's VIEW state put into this page's view-model, so the page draws what the
 * window drew (the parity check: tools/fixture_host.mjs serves the page with a FAKE NTWB that delivers the shot's exact
 * model; `window.__ntwbFixture.view` is the native App's view state — `solaris_app_shots --model-json`).
 *
 *   applyNativeView(vm, view, shell)   called by main.js once the fixture is there (a real session never has one)
 *     App-level state (here):  uiScale · the toast · the sheets (settingsOpen, exportOpen, relinkOpen, conflictsOpen)
 *                              · historyOpen · focus {key, held} · project.{browserOpen, dockOpen, sessionOpen,
 *                              windows: ["dev:dv_2", "roll:pt_1", "auto:au_1", "loudness", "video"]}
 *     every other key is a WIDGET's: vm.nativeView (Signal<view | null>) holds the whole object — a module reads its
 *     own part when it mounts (project.browserTab, project.dockTab, project.dockOnMatrix, project.timeline.{pxPerBeat,
 *     leftBeat, selectedClip, selectedClips}, menuOpen (the song bar's open menu, −1 none), settingsSection,
 *     contextMenu.{open, items}, confirmOpen …) and puts itself there. It is never set in a real session.
 */
export function applyNativeView(vm, view, shell) {
  if (!view) return;
  vm.nativeView.value = view;
  vm.batch(() => {
    if (typeof view.uiScale === "number") vm.view.uiScale.value = Math.round(view.uiScale); // this page only: not kept
    const p = view.project || {};
    if (typeof p.browserOpen === "boolean") vm.view.browserOpen.value = p.browserOpen;
    if (typeof p.dockOpen === "boolean") vm.view.dockOpen.value = p.dockOpen;
    if (typeof p.sessionOpen === "boolean") vm.view.sessionOpen.value = p.sessionOpen;
    if (Array.isArray(p.windows)) {
      vm.view.windows.value = p.windows.map((key) => {
        const [pre, id] = key.includes(":") ? [key.slice(0, key.indexOf(":")), key.slice(key.indexOf(":") + 1)] : [key, ""];
        const kind = { dev: "device", roll: "roll", auto: "automation" }[pre] || pre;
        return { key, kind, id, clip: "" };
      });
    }
    if (p.timeline) {
      if (Array.isArray(p.timeline.selectedClips)) vm.view.selection.value = p.timeline.selectedClips.slice();
      if (typeof p.timeline.selectedClip === "string") vm.view.selectedClip.value = p.timeline.selectedClip;
    }
    vm.view.sheet.value = view.settingsOpen ? "settings" : view.exportOpen ? "export" : view.relinkOpen ? "relink" : view.conflictsOpen ? "conflicts" : null;
    vm.view.history.value = !!view.historyOpen;
    if (view.focus) { vm.view.focus.key.value = view.focus.key || ""; vm.view.focus.on.value = !!view.focus.held; }
  });
  if (view.toast && view.toast.amount > 0 && view.toast.text && shell) shell.toast(view.toast.text, view.toast.refusal !== false);
}
