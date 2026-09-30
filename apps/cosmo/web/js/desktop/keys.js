/*
 * Cosmo by arstro — desktop/keys.js: the editor's keyboard.
 *
 * Ports the host's key handler (linux_main.cpp:1385-1497) and App::key (App.cpp:1798-1819), in
 * the native's order:
 *   1. a focused text field (a dialog's input) keeps its keys;
 *   2. an in-app text layer (the group-rename field, pushKeyLayer) takes EVERY key while open;
 *   3. the Confirm / Info / Export dialogs (and any dialog but Settings / Preset, which never
 *      route keys natively) own the keyboard - C's dialog host handles them;
 *   4. only the editor screen has shortcuts (Home, Loading and the transitions swallow them);
 *   5. ← / → step through the cells of the shown group (App::stepSelection) and scroll the new
 *      cell into view; Ctrl+Z undo, Ctrl+Shift+Z / Ctrl+Y redo, Ctrl+S save, Ctrl+Shift+S save
 *      as, O open, S quick export, Delete / KP_Delete delete the selection.
 * Nothing else is bound (no Esc for menus, no zoom keys, no Tab focus) and keys auto-repeat.
 *
 * Deviation: S (quick full-resolution export of one image) has no command yet (CORE GAP #13)
 * and says so.
 */
const layers = [];

/** An in-app text field takes the keyboard while it is open: fn(event) -> true = consumed. */
export function pushKeyLayer(fn) {
  layers.push(fn);
  return () => { const i = layers.lastIndexOf(fn); if (i >= 0) layers.splice(i, 1); };
}

const KEYS_PASS_DIALOGS = new Set(["settings", "preset"]);
// C's modal stack (pickers, confirms) - loaded if present; a missing module means no modals.
let modalOpen = () => false;
import("../ui/modal.js").then((m) => { if (m.modalOpen) modalOpen = m.modalOpen; }).catch(() => {});

/** The cell App::stepSelection lands on, walking from `from` (a node) when given. */
function stepTarget(vm, dir, from) {
  const list = vm.cells.peek();
  if (!list.length) return null;
  const sel = vm.selectedIds.peek();
  let cur = from != null ? list.findIndex((c) => c.node === from) : -1;
  if (cur < 0) cur = list.findIndex((c) => sel.has(c.node));
  if (cur < 0) { const t = vm.editTarget.peek(); cur = t ? list.findIndex((c) => c.node === t.node) : -1; }
  const next = cur < 0 ? (dir > 0 ? 0 : list.length - 1) : cur + dir;
  return next >= 0 && next < list.length && next !== cur ? list[next].node : null;
}

export function installKeys(vm, ed) {
  // Auto-repeat steps faster than the model answers: the next step walks from the one still in
  // flight (a gesture in flight outranks the model), not from the selection the model last sent.
  let pending = null;                              // {node, at}
  function onKey(e) {
    const a = document.activeElement;
    if (a && (a.tagName === "INPUT" || a.tagName === "TEXTAREA" || a.isContentEditable)) return;
    if (layers.length) { if (layers[layers.length - 1](e)) e.preventDefault(); return; }
    const dlg = vm.view.dialog.peek();
    if (dlg && !KEYS_PASS_DIALOGS.has(dlg.name)) return;
    if (modalOpen()) return;
    if (vm.screen.peek() !== "editor" || !ed.editor()) return;
    if (ed.ctx && ed.ctx.screen && ed.ctx.screen() !== "editor") return;   // Home, or a transition
    const k = e.key, lower = k.length === 1 ? k.toLowerCase() : k;
    const ctrl = e.ctrlKey || e.metaKey;
    const act = ed.editor().act;
    let done = true;
    if (k === "ArrowLeft" || k === "ArrowRight") {
      const dir = k === "ArrowRight" ? 1 : -1;
      if (pending && (vm.selectedIds.peek().has(pending.node) || performance.now() - pending.at > 1500)) pending = null;
      const node = stepTarget(vm, dir, pending && pending.node);
      if (node !== null) {
        pending = { node, at: performance.now() };
        vm.select(node).catch(() => { pending = null; });
        ed.editor().reveal(node);
      }
    } else if (ctrl && lower === "z") { if (e.shiftKey) act.redo(); else act.undo(); }
    else if (ctrl && lower === "y") act.redo();
    else if (ctrl && lower === "s") { if (e.shiftKey) act.saveAs(); else act.save(); }
    else if (!ctrl && lower === "o") act.open();
    else if (!ctrl && lower === "s") act.quickExport();
    else if (k === "Delete") act.remove();
    else done = false;
    if (done) e.preventDefault();
  }
  window.addEventListener("keydown", onKey);
  return () => window.removeEventListener("keydown", onKey);
}
