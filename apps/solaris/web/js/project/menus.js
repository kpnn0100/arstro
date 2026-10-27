/*
 * Solaris — project/menus.js: what App.cpp fills and does that more than one widget reaches — the song bar's four
 * menus (App::buildMenus / refreshMenus, App.cpp:154-267), the template menu (pickTemplate), going Home behind the
 * unsaved-changes question (requestHome), the host's pickers in App's flows (onPickSongToOpen / ToCreate / Save /
 * File / Directory / Folder → ctx.pick), and the keyboard's song actions (songAction, pasteClip — App.cpp:849-949).
 * Wave-2 modules import it (it is the shell's, so a static import is safe): the song bar puts `songMenus` in its
 * MenuStrip; Home's buttons call `actions.newSong()` …; the shell's keys call `actions.songAction(id)`.
 *
 *   const A = appActions(vm, ctx)                — ctx is the caller's (any slot's: pick / confirm / menu / toast)
 *   A.newSong()            File › New Song… / Home's New song: the save picker → `project new "<p>.slp" [--template "<t>"]`
 *   A.openSong()           File › Open… / Home's Open song…: the open picker → `project open "<p>"`
 *   A.openRecent(path)     Home's card → `project open "<p>"`;  A.forgetRecent(path) → `recents remove "<p>"`
 *   A.pickTemplate(x, y)   File › New from Template… / Home's From template…: a menu of m.templates at (x, y)
 *   A.saveAs() · A.render() · A.renderStems() · A.collectAndSave() · A.saveAsTemplate()     File's pickers / lines
 *   A.exportSong() · A.relink() · A.conflicts() · A.openSettings() · A.openHistory()        the sheets
 *   A.requestHome()        the wordmark / File › Home: `project close`, behind "Unsaved changes" when the song is dirty
 *   A.playToggle() · A.recordToggle() · A.save()     the song bar's buttons (App.cpp:66-77)
 *   A.quantizeClip()       Song › Quantize Clip (the clicked clip's pattern at the lanes' snap step)
 *   A.songAction(id) -> bool   a keymap action of the "song" keyboard (App::songAction): true when it did something
 *   A.pasteClip() -> bool  Edit › Paste Clip / the paste key (App::pasteClip)
 *   A.pickSongToOpen() · A.pickSongToCreate() · A.pickSave(title, suggested) · A.pickFile(title) ·
 *   A.pickDirectory(title) · A.pickFolder()     -> Promise<path|null>  the host's GTK pickers' titles and buttons
 *
 *   songMenus(vm, A) -> [{title, items: [{label, action}]}]   File · Edit · Song · View, their titles and labels
 *       STORED WORDS (vm.say …; show them with vm.shown). It READS what App::refreshMenus keys on — the undo / redo
 *       labels, the dock / browser / session / video state, metronome, Show IDs, the keymap's hints — so called in an
 *       effect it re-runs on every model push — compare menusKey(menus) to rebuild only when a label moved (mMenuKey).
 *   menusKey(menus) -> string
 */
import { g as printfG } from "../kit/format.js";
import { keyHint } from "../kit/keys.js";

const STATE = new WeakMap(); // vm -> App's own: the template picked, the clips Copy kept, where the last paste went
function stateOf(vm) {
  let s = STATE.get(vm);
  if (!s) STATE.set(vm, (s = { newTemplate: "", pasteAt: -1, pasteNext: 0 }));
  return s;
}

export function appActions(vm, ctx) {
  const st = stateOf(vm);
  const say = vm.say, sayn = vm.sayn, q = vm.q;
  const model = () => vm.model.peek() || {};
  const dispatch = (line) => vm.cmd(line).then((r) => !!r);
  const toast = (text, err = true) => (ctx && ctx.toast ? ctx.toast(text, err) : vm.toast(text, err));

  // ---- the host's pickers (linux_main.cpp:378-450): their titles and buttons are the host's (English, as GTK's)
  const pick = (o) => (ctx && ctx.pick ? ctx.pick(o) : Promise.resolve(null));
  const A = {
    pickSongToOpen: () => pick({ mode: "open", title: "Open song", ok: "Open", kinds: ["song"] }),
    pickSongToCreate: () => pick({ mode: "save", title: "New song", ok: "Create", kinds: ["song"], name: "Untitled.slp" }),
    pickSave: (title, suggested) => pick({ mode: "save", title, ok: "Save", name: suggested, kinds: /\.slp$/.test(suggested) ? ["song"] : [] }),
    pickFile: (title) => pick({ mode: "open", title, ok: "Open", kinds: [] }),
    pickDirectory: (title) => pick({ mode: "folder", title, ok: "Search" }),
    pickFolder: () => pick({ mode: "folder", title: "Add a sample folder", ok: "Add" }),
  };

  /** App::newSongPicked: `.slp` appended when missing; from the template picked before the host asked — ONE line. */
  function newSongPicked(path) {
    let p = path;
    if (p.length < 4 || p.slice(-4) !== ".slp") p += ".slp";
    const from = st.newTemplate;
    st.newTemplate = "";
    return dispatch("project new " + q(p) + (from ? " --template " + q(from) : ""));
  }
  const createSong = () => A.pickSongToCreate().then((p) => (p ? newSongPicked(p) : false));

  Object.assign(A, {
    newSong() { st.newTemplate = ""; return createSong(); },
    openSong() { return A.pickSongToOpen().then((p) => (p ? dispatch("project open " + q(p)) : false)); },
    openRecent: (p) => dispatch("project open " + q(p)),
    forgetRecent: (p) => dispatch("recents remove " + q(p)),
    pickTemplate(x = 60, y = 30) {
      const m = model();
      const templates = m.templates || [];
      if (!templates.length) { toast(say("No templates yet — File › Save as Template… keeps a song as one")); return; }
      const items = templates.map((t) => ({
        label: t.name + (t.factory ? "  · " + say("factory") : ""),
        action: () => { st.newTemplate = t.name; createSong(); },
      }));
      return ctx.menu.open(items, x, y);
    },
    saveAs() {
      return A.pickSave(vm.tr("Save the song as"), (model().projectName || "") + ".slp").then((p) => (p ? dispatch("project save " + q(p)) : false));
    },
    render() {
      return A.pickSave(vm.tr("Render the mix"), (model().projectName || "") + ".wav").then((p) => (p ? dispatch("render --out " + q(p)) : false));
    },
    exportSong() { vm.openExport(); },
    async collectAndSave() {
      const r = await vm.cmd("project collect --save");
      if (!r) return false;
      const m = (await vm.modelAt(r.revision)) || {};
      const n = (m.missing || []).length;
      toast(n === 0 ? say("Collected into the song's folder, and saved")
                    : sayn(n, "Collected and saved — {1} file is still missing (File › Relink…)",
                           "Collected and saved — {1} files are still missing (File › Relink…)", n));
      return true;
    },
    relink() { vm.openRelink(); },
    renderStems() {
      const m = model();
      const ids = (m.strips || []).map((s) => s.id).join(",");
      if (!ids) return Promise.resolve(false);
      return A.pickSave(vm.tr("Render the mix and a stem per strip"), (m.projectName || "") + ".wav")
        .then((p) => (p ? dispatch("render --out " + q(p) + " --stems " + ids) : false));
    },
    saveAsTemplate() {
      return ctx.menu.openRename("", 60, 30, (n) => { if (n) dispatch("template save " + q(n)); }, { title: say("Rename") });
    },
    conflicts() { vm.openConflicts(); },
    openSettings() { vm.openSettings(); },
    openHistory() { vm.openHistory(); },
    /** App::requestHome. */
    requestHome() {
      const m = model();
      if (!m.dirty) return dispatch("project close");
      const name = m.projectName || "";
      return ctx.confirm({
        title: say("Unsaved changes"), message: say("Save \"{1}\" before going Home?", name),
        buttons: [
          { label: say("Cancel") },
          { label: say("Discard"), destructive: true, onClick: () => dispatch("project close") },
          { label: say("Save"), primary: true, onClick: () => dispatch("project save").then((ok) => ok && dispatch("project close")) },
        ],
      });
    },
    playToggle() { return dispatch(model().transport && model().transport.playing ? "transport stop --return" : "transport play"); },
    recordToggle() { return dispatch(model().transport && model().transport.recording ? "transport stop" : "transport record"); },
    save: () => dispatch("project save"),
    quantizeClip() {
      const m = model(), sel = vm.view.selectedClip.peek();
      for (const c of m.clips || []) {
        if (c.id !== sel || !c.pattern) continue;
        const step = Math.min(4, Math.max(1 / 32, vm.view.snapStep.peek()));
        dispatch("pattern quantize " + c.pattern + " --grid " + printfG(step, 10)); // a power-of-two step prints exactly
      }
    },
    /** App::pasteClip (R-CLIP-10/11). */
    pasteClip() {
      const m = model();
      const board = vm.view.clipboard.peek();
      if (!board.length) return false;
      let first = Infinity, last = -Infinity;
      for (const id of board) {
        const src = (m.clips || []).find((c) => c.id === id);
        if (!src) { toast(say("{1} is gone — copy a clip again", id)); vm.view.clipboard.value = []; return true; }
        first = Math.min(first, src.at);
        last = Math.max(last, src.at + src.length);
      }
      const pos = vm.position.peek();
      const at = st.pasteAt >= 0 && Math.abs(pos - st.pasteAt) < 1e-9 ? st.pasteNext : pos;
      vm.view.expectCopy.value = { instant: false }; // the copies are selected (they fade in: nothing showed them yet)
      const line = board.length === 1
        ? "clip duplicate " + board[0] + " --at " + vm.beatText(Math.round(at * 960) / 960)
        : "clips duplicate \"" + board.join(" ") + "\" --by " + vm.beatText(Math.round((at - first) * 960) / 960);
      dispatch(line).then((ok) => { if (ok) { st.pasteAt = pos; st.pasteNext = at + (last - first); } });
      return true;
    },
    /** App::songAction — true when it did something. */
    songAction(id) {
      if (id === "song.settings") { vm.openSettings(); return true; }
      if (!id || vm.screen.peek() !== "project") return false;
      if (id === "transport.play" || id === "transport.stopHere") {
        // play/stop goes back where the time bar was put; stop-here leaves it where it was heard (R-TIME-7/8)
        if (vm.playing.peek()) { dispatch(id === "transport.play" ? "transport stop --return" : "transport stop"); return true; }
        if (vm.view.rollKeys.peek()) { // R-ROLL-6: a roll's ▶ while it has the keys
          const roll = vm.rolls.get(vm.view.frontRoll.peek());
          if (roll && roll.togglePlay && roll.togglePlay()) return true;
        }
        dispatch("transport play");
        return true;
      }
      if (id === "transport.toStart") { dispatch("transport seek 0"); return true; }
      if (id === "song.save") { dispatch("project save"); return true; }
      if (id === "song.undo") { dispatch("undo"); return true; }
      if (id === "song.redo") { dispatch("redo"); return true; } // R-EDM-1
      if (id === "lanes.paste") return A.pasteClip();
      // R-CLIP-11: what is done to the selected clip is done to the selection — ONE line however many
      const sel = vm.view.selection.peek();
      if (!sel.length) return false;
      const ids = sel.join(" ");
      if (id === "lanes.delete") { dispatch(sel.length === 1 ? "clip delete " + ids : "clips delete \"" + ids + "\""); return true; }
      if (id === "lanes.duplicate") {
        vm.view.expectCopy.value = { instant: false }; // the copies are selected, so Duplicate again runs on after them
        dispatch(sel.length === 1 ? "clip duplicate " + ids : "clips duplicate \"" + ids + "\"");
        return true;
      }
      if (id === "lanes.split") { // R-EDIT-1: the clip clicked last
        dispatch("clip split " + vm.view.selectedClip.peek() + " --at " + vm.beatText(Math.round(vm.position.peek() * 960) / 960));
        return true;
      }
      if (id === "lanes.copy") {
        // R-CLIP-10: nothing is sent — the window keeps which clips; the paste key makes the copies
        vm.view.clipboard.value = sel.slice();
        st.pasteAt = -1;
        const paste = vm.keymap.hint("lanes.paste"), key = paste ? paste.slice(4, -1) : "";
        if (sel.length === 1) toast(paste ? say("Clip copied — {1} puts it at the time bar", key) : say("Clip copied — Paste puts it at the time bar"), false);
        else toast(paste ? say("{1} clips copied — {2} puts them at the time bar", sel.length, key) : say("{1} clips copied — Paste puts them at the time bar", sel.length), false);
        return true;
      }
      return false;
    },
  });
  return A;
}

/** What App::refreshMenus keys on (mMenuKey): the menus' words, as one string — rebuild the strip only when it moves. */
export function menusKey(menus) { return menus.map((mn) => mn.title + "\x1E" + mn.items.map((it) => it.label).join("\x1F")).join("\x1D"); }

/** The song bar's menus (App::buildMenus + refreshMenus): [{title, items: [{label, action}]}], words stored. */
export function songMenus(vm, A) {
  const m = vm.model.value || {};
  const s = m.settings || {};
  const hint = (id) => keyHint(m, id); // App::keyHint: the keymap's first key NOW (R-SET-4) — re-keyed, the hint follows
  const say = vm.say;
  const dock = vm.view.dockOpen.value, browser = vm.view.browserOpen.value, session = vm.view.sessionOpen.value;
  const video = vm.view.windows.value.some((w) => w.key === "video");
  return [
    { title: say("File"), items: [
      { label: say("New Song…"), action: () => A.newSong() },
      { label: say("Open…"), action: () => A.openSong() },
      { label: say("Save") + "            (Ctrl+S)", action: () => A.save() },
      { label: say("Save As…"), action: () => A.saveAs() },
      { label: say("Render…"), action: () => A.render() },
      { label: say("Export…"), action: () => A.exportSong() },
      { label: say("Collect and Save"), action: () => A.collectAndSave() },
      { label: say("Relink…"), action: () => A.relink() },
      { label: say("Render Stems…"), action: () => A.renderStems() },
      { label: say("Home"), action: () => A.requestHome() },
      { label: say("New from Template…"), action: () => A.pickTemplate(60, 30) },
      { label: say("Save as Template…"), action: () => A.saveAsTemplate() },
      { label: say("Conflicts…"), action: () => A.conflicts() },
    ] },
    { title: say("Edit"), items: [
      { label: (m.undoLabel ? say("Undo {1}", m.undoLabel) : say("Undo")) + hint("song.undo"), action: () => vm.cmd("undo") },
      { label: (m.redoLabel ? say("Redo {1}", m.redoLabel) : say("Redo")) + hint("song.redo"), action: () => vm.cmd("redo") },
      { label: say("Show History Tree…"), action: () => A.openHistory() },
      { label: say("Duplicate Clip") + hint("lanes.duplicate"), action: () => A.songAction("lanes.duplicate") },
      { label: say("Copy Clip") + hint("lanes.copy"), action: () => A.songAction("lanes.copy") },
      { label: say("Paste Clip") + hint("lanes.paste"), action: () => A.pasteClip() },
      { label: say("Delete Clip") + hint("lanes.delete"), action: () => A.songAction("lanes.delete") },
      { label: say("Split Clip at the Time Bar") + hint("lanes.split"), action: () => A.songAction("lanes.split") },
    ] },
    { title: say("Song"), items: [
      { label: say("Add Mixer"), action: () => vm.cmd("mixer add") },
      { label: say("Add Bus"), action: () => vm.cmd("strip add --kind bus") },
      { label: say("Add Audio Line"), action: () => vm.cmd("strip add --kind audio") },
      { label: say("Add Lane"), action: () => vm.cmd("lane add") },
      { label: say("Quantize Clip"), action: () => A.quantizeClip() },
    ] },
    { title: say("View"), items: [
      { label: dock ? say("Hide Mixer") : say("Show Mixer"), action: () => vm.setDockOpen(!vm.view.dockOpen.peek()) },
      { label: browser ? say("Hide Browser") : say("Show Browser"), action: () => vm.setBrowserOpen(!vm.view.browserOpen.peek()) },
      { label: s.metronome ? say("Metronome: On") : say("Metronome: Off"),
        action: () => vm.cmd("settings set metronome=" + (((vm.model.peek() || {}).settings || {}).metronome ? "off" : "on")) },
      { label: s.showIds ? say("Hide IDs") : say("Show IDs"),
        action: () => vm.cmd("settings set showIds=" + (((vm.model.peek() || {}).settings || {}).showIds ? "off" : "on")) },
      { label: say("Settings…") + "   (Ctrl+,)", action: () => A.openSettings() },
      { label: session ? say("Show Arrangement") : say("Show Session"), action: () => vm.setSessionOpen(!vm.view.sessionOpen.peek()) }, // R-LAUNCH-1
      { label: video ? say("Hide Video") : say("Show Video"), // R-VIDEO-1: the song's picture, in a window
        action: () => (vm.isWindowOpen("video") ? vm.closeWindow("video") : vm.openVideo()) },
    ] },
  ];
}
