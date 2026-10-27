/*
 * Solaris — kit/popups.js: App's one ContextMenu (with its rename mode) and cosmo's ConfirmDialog, kept as WORDS
 * (App::openMenu, App::showConfirm, App::relabel — R-ACCESS-2) — what the shell hands every widget as ctx.menu /
 * ctx.confirm / ctx.prompt.
 *
 * Built on the borrowed kit (BORROW): ../ui/menu.js (cosmo_v2::ContextMenu), ../ui/confirm.js (ConfirmDialog),
 * imported dynamically — a kit that fails to load is said on the console, never fatal to the page.
 *
 *   createPopups(vm, { menuLayer, modalLayer }) -> {
 *     open(items, x, y)            -> Promise<menu|null>   items [{label, action}] — labels stored words (vm.say) or
 *                                                          data; x, y ROOT logical px. Modal while open (vm.view.menuOpen).
 *     openRename(name, x, y, onRename, {title})  -> Promise<menu|null>   the menu straight in rename mode
 *     confirm({title, message, buttons: [{label, primary, destructive, onClick}]}) -> Promise<index | -1>
 *                                  title / message / labels: stored words or data (App::showConfirm keeps them as
 *                                  words and relabels them in the language drawn)
 *     prompt({value, x, y, title}) -> Promise<string | null>   a name typed in place (the rename field; App's
 *                                  "Save as Template…" is exactly this — App.cpp:193-196)
 *     close()                      the open menu, if any
 *   }
 * A language switch while a menu or a dialog is open relabels it, never closes it (App::relabel).
 */
import { effect } from "../core/signal.js";

let menuMod = null, confirmMod = null;
const loadMenu = () => (menuMod ||= import("../ui/menu.js").catch((e) => { console.error("solaris: ui/menu.js (borrowed) failed to load:", e); return null; }));
const loadConfirm = () => (confirmMod ||= import("../ui/confirm.js").catch((e) => { console.error("solaris: ui/confirm.js (borrowed) failed to load:", e); return null; }));

export function createPopups(vm, { menuLayer, modalLayer }) {
  let menuWords = null;          // the open menu's labels, kept as words
  let dialog = null;             // {api, title, message, labels}

  // vm.view.menuOpen: the one menu is showing (ContextMenu is modal while open) — from open to its onClose
  // R-ACCESS-2: a switch of language relabels what is open
  effect(() => {
    void vm.lang.value;
    if (menuWords && menuMod) menuMod.then((mod) => { if (mod && menuWords) mod.relabel(menuLayer, menuWords.map((l) => vm.shown(l))); });
    if (dialog && dialog.api && dialog.api.relabel) dialog.api.relabel(vm.shown(dialog.title), vm.shown(dialog.message), dialog.labels.map((l) => vm.shown(l)));
  });

  async function open(items, x, y) {
    const mod = await loadMenu();
    if (!mod) return null;
    const words = items.map((it) => it.label);
    const shown = items.map((it) => ({ label: vm.shown(it.label), action: it.action }));
    menuWords = words;
    vm.view.menuOpen.value = true;
    return mod.openMenu(menuLayer, shown, x, y, {
      onClose: () => { if (menuWords === words) { menuWords = null; vm.view.menuOpen.value = false; } },
    });
  }

  async function openRename(name, x, y, onRename, { title } = {}) {
    const mod = await loadMenu();
    if (!mod) return null;
    menuWords = null;
    vm.view.menuOpen.value = true;
    return mod.openRename(menuLayer, name || "", x, y, (n) => onRename && onRename(n), {
      title: vm.shown(title || vm.say("Rename")),
      onClose: () => { if (!menuWords) vm.view.menuOpen.value = false; },
    });
  }

  function prompt({ value = "", x = 60, y = 30, title } = {}) {
    return new Promise((resolve) => {
      let answered = false;
      openRename(value, x, y, (n) => { answered = true; resolve(n); }, { title }).then((m) => {
        if (!m) { resolve(null); return; }
        const poll = () => { if (answered) return; if (!m.isOpen()) resolve(null); else setTimeout(poll, 120); };
        setTimeout(poll, 120);
      });
    });
  }

  async function close() {
    const mod = await loadMenu();
    if (mod) mod.closeMenu(menuLayer);
  }

  function confirm({ title = "", message = "", buttons = [] } = {}) {
    return loadConfirm().then((mod) => new Promise((resolve) => {
      if (!mod) { resolve(-1); return; }
      const labels = buttons.map((b) => b.label);
      const entry = { api: null, title, message, labels };
      dialog = entry;
      entry.api = mod.confirmDialog(modalLayer, {
        title: vm.shown(title), message: vm.shown(message),
        buttons: buttons.map((b) => ({ label: vm.shown(b.label), primary: !!b.primary, destructive: !!b.destructive, onClick: b.onClick })),
        onClose: (i) => { if (dialog === entry) dialog = null; resolve(i === null || i === undefined ? -1 : i); },
      });
    }));
  }

  return { open, openRename, prompt, confirm, close };
}
