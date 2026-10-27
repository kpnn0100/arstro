/*
 * Interstellar — edit/menus.js: the menus and App-level actions App.cpp fills for SEVERAL widgets.
 *
 * A port of the parts of app/App.cpp that more than one widget module calls. Every item is a command
 * line (vm.cmd) or a picker (ctx.pick) - the menus add no behaviour of their own (App.cpp:292). A menu
 * only ONE widget opens (the clip menu, the key menus, the plugin menu, the node / still menus, the
 * output spec's) is that widget's own to port, in its own module. Wave-2 modules import this one:
 *
 *   import { openRackContext, pickFootage, … } from "../edit/menus.js";
 *
 * MENUS (x, y / rect in ROOT logical px - ctx.point(e) converts a pointer event)
 *   openRackContext(vm, ctx, rackIndex, x, y)    App::openRackContext (App.cpp:233-290): right-click on a rack
 *                                                node - the rack tree row, the filmstrip cell, the source bin row.
 *                                                Selects the node first when it is outside the selection.
 *   openInputColourMenu(vm, ctx, bind, x, y)     App::openInputColourMenu (R-COLOR-2): rack menu, Colour menu
 *   openCaptureMenu(vm, ctx, rect)               App::openCaptureMenu (R-UI-11): the monitor's caption button
 *                                                (Grade) and the transport's - Copy Frame (when this page can
 *                                                write the clipboard) · Save Frame…
 *   openRelinkMenu(vm, ctx, x, y)                App::openRelinkMenu (R-MEDIA-3): File › Relink Media…
 *   openPlaceTimelineMenu(vm, ctx, track, t, x, y)   App::openPlaceTimelineMenu (R-EDT-4)
 *
 * ACTIONS
 *   requestHome(vm, ctx)                         `project close`, behind "Save changes?" when dirty
 *   pickFootage(vm, ctx)                         Add Footage… -> `rack add <paths…>`
 *   pickProjectToOpen / pickProjectToCreate / pickSaveAs / pickPresetToImport / pickStillToExport (vm, ctx)
 *   pickFrameToSave(vm, ctx, bind)               Save Frame… -> `capture --out <p> [--source <bind>]`
 *   pickLutToOpen(vm, ctx) / pickTimelineToImport / pickCaptionsToImport / pickFolder -> Promise<path|null>
 *   pickLutToSave(vm, ctx, suggested) / pickTimelineToExport / pickCaptionsToExport -> Promise<path|null>
 *   pickMediaToRelink(vm, ctx, fileName) -> Promise<path|null>
 * Each picker is linux_main.cpp's GTK chooser for the same hook (title, filter, suggested name, the
 * extension a save makes sure of), drawn by dialogs/picker.js over the machine's folders.
 */
import { q, bindName, seconds } from "../kit/cmd.js";

const BULLET = "•  ", SPACES = "     ";      // App.cpp's "\xE2\x80\xA2  " and the five spaces that line up with it
const baseName = (p) => { const i = String(p || "").lastIndexOf("/"); return i < 0 ? String(p || "") : String(p).slice(i + 1); };
const withExt = (ext, test) => (name) => (test(name) ? name : name + ext);

// ------------------------------------------------------------------ the rack node's menu
export async function openRackContext(vm, ctx, i, x, y) {
  const m = vm.model.peek();
  if (!m || i < 0 || i >= (m.rack || []).length) return null;
  const n = m.rack[i];
  if (!n.bindName) return null;
  const b = q(n.bindName);
  // right-clicking OUTSIDE the selection selects that node first; inside it the selection stays, so
  // "Group Selection" groups all of it - cosmo's rule
  if (!n.selected) await vm.cmd("rack select " + b, { sync: true });
  const m2 = vm.model.peek() || m;
  let selection = "", selected = 0;
  for (const r of m2.rack || []) if (r.selected && r.bindName) { selection += " " + q(r.bindName); ++selected; }
  const items = [];
  items.push({ label: "Add Footage...", action: () => pickFootage(vm, ctx) });
  items.push({ label: selected > 1 ? "Group Selection  (Ctrl+G)" : "Group  (Ctrl+G)", action: () => vm.cmd("rack group new") });
  if (n.group) items.push({ label: "Ungroup", action: () => vm.cmd("rack ungroup " + b) });
  if (!n.failed)
    items.push({ label: n.bypass ? "Enable Filter" : "Disable Filter", action: () => vm.cmd(`set ${n.bindName}.bypass=${n.bypass ? "0" : "1"}`) });
  // cosmo's inline rename: the same menu morphs into its field (ContextMenu::enterRenameMode)
  const rename = (typed) => {
    const name = bindName(typed);
    if (name && name !== n.bindName) vm.cmd("rack rename " + q(n.bindName) + " " + q(name));
  };
  items.push({ label: "Rename...", action: (menu) => {
    if (menu && menu.enterRenameMode) menu.enterRenameMode(n.bindName, rename);
    else ctx.menu.openRename(n.bindName, x, y, rename);
  } });
  if (!n.group && !n.failed) items.push({ label: "Duplicate as Variant  (Ctrl+D)", action: () => vm.cmd("rack duplicate " + b) });
  // R-MEDIA-3: a missing file, found where it is now
  if (!n.group && n.failed && !n.offlineWhy)
    items.push({ label: "Relink...", action: async () => {
      const p = await pickMediaToRelink(vm, ctx, baseName(n.media));
      if (p) vm.cmd("media relink " + b + " " + q(p));
    } });
  // R-MEDIA-2: a lighter file for the monitor
  if (!n.group && !n.failed && n.video) {
    const making = (m2.proxyJobs || []).some((j) => j.rackObj === n.rackObj && (j.state === "queued" || j.state === "running"));
    if (!making) items.push({ label: n.proxy ? "Make Proxy Again" : "Make Proxy", action: () => vm.cmd("proxy make " + b) });
    if (n.proxy || making) items.push({ label: making ? "Cancel Proxy" : "Remove Proxy", action: () => vm.cmd("proxy remove " + b) });
  }
  if (!n.group) {
    // R-COLOR-2: what the source IS - the list opens in place of this menu
    let now = "Rec.709";
    for (const c of m2.colourInputs || []) if (c.id === n.input) now = c.label;
    items.push({ label: `Input Colour (${now})...`, action: () => openInputColourMenu(vm, ctx, n.bindName, x, y) });
    // R-COLOR-5/6: an input LUT in, the source's colour out
    items.push({ label: n.lut ? "Change Input LUT..." : "Input LUT...", action: async () => {
      const p = await pickLutToOpen(vm, ctx);
      if (p) vm.cmd("set " + q(n.bindName + ".lut=" + p));
    } });
    if (n.lut) items.push({ label: "Remove Input LUT", action: () => vm.cmd("set " + q(n.bindName + ".lut=none")) });
    if (!n.failed)
      items.push({ label: "Export LUT...", action: async () => {
        const p = await pickLutToSave(vm, ctx, n.bindName + ".cube");
        if (p) vm.cmd("lut export " + b + " --out " + q(p));
      } });
  }
  if (!n.failed) items.push({ label: "Copy Grade", action: () => vm.cmd("grade copy " + b) });
  if (m2.hasGradeClipboard)
    items.push({ label: selected > 1 ? "Paste Grade to Selection" : "Paste Grade", action: () => vm.cmd("grade paste" + selection) });
  if (!n.group && n.usedBy === 0) items.push({ label: "Remove from Rack", action: () => vm.cmd("rack remove " + b) });
  return ctx.menu.open(items, x, y);
}

/** App::openInputColourMenu (App.cpp:443-457): the source's input colour, the current one marked. */
export function openInputColourMenu(vm, ctx, bind, x, y) {
  const m = vm.model.peek() || {};
  let current = "rec709", group = false;
  for (const r of m.rack || []) if (r.bindName === bind) { current = r.input; group = r.group; }
  if (group) return null;
  const items = (m.colourInputs || []).map((c) => ({
    label: (c.id === current ? BULLET : SPACES) + c.label,
    action: () => vm.cmd("set " + q(bind) + ".input=" + c.id),
  }));
  return ctx.menu.open(items, x, y);
}

/** App::openCaptureMenu (App.cpp:559-573): under the button; Copy Frame only where the clipboard works
 *  (the native offers it only when the host binds copyFrame). `rect` = the button, root px. */
export function openCaptureMenu(vm, ctx, rect) {
  const bind = vm.captureBind();
  const items = [];
  if (vm.canCopyFrame()) items.push({ label: "Copy Frame", action: () => vm.copyFrame(bind) });
  items.push({ label: "Save Frame...", action: () => pickFrameToSave(vm, ctx, bind) });
  return ctx.menu.open(items, rect.x, rect.y + rect.h);
}

/** App::openRelinkMenu (App.cpp:976-1006): every offline source - each found by hand, or all by a folder. */
export function openRelinkMenu(vm, ctx, x, y) {
  const m = vm.model.peek() || {};
  const items = [];
  let missing = 0;
  for (const n of m.rack || []) {
    if (n.group || !n.failed) continue;
    const file = baseName(n.media);
    const name = n.cosmoName || n.bindName;
    if (n.offlineWhy) { items.push({ label: name + " — " + n.offlineWhy }); continue; }   // a decoder is missing
    ++missing;
    items.push({ label: `Locate ${name} (${file})...`, action: async () => {
      const p = await pickMediaToRelink(vm, ctx, file);
      if (p) vm.cmd("media relink " + q(n.bindName) + " " + q(p));
    } });
  }
  if (missing > 0)
    items.push({ label: "Search a Folder for " + (missing > 1 ? "All..." : "It..."), action: async () => {
      const dir = await pickFolder(vm, ctx);
      if (dir) vm.cmd("media relink --search " + q(dir));
    } });
  if (!items.length) items.push({ label: "No media is offline" });
  return ctx.menu.open(items, x, y);
}

/** App::openPlaceTimelineMenu (App.cpp:1009-1023): the timelines that can go inside this one. */
export function openPlaceTimelineMenu(vm, ctx, track, t, x, y) {
  const m = vm.model.peek() || {};
  const fps = m.fps > 0 ? m.fps : 24;
  const items = (m.timelines || []).filter((tl) => tl.placeable).map((tl) => ({
    label: " ".repeat((tl.depth || 0) * 2) + tl.name,
    action: () => vm.cmd(`clip add --track ${q(track)} --src ${q(tl.name)} --in 0 --at ${seconds(t, fps)}`),
  }));
  return items.length ? ctx.menu.open(items, x, y) : null;
}

// ------------------------------------------------------------------ App-level actions
/** App::requestHome (App.cpp:1025-1034): `project close`, behind cosmo's ConfirmDialog when dirty. */
export function requestHome(vm, ctx) {
  const m = vm.model.peek() || {};
  if (!m.dirty) return vm.cmd("project close");
  return ctx.confirm({
    title: "Save changes to " + (m.projectName || "this project") + "?",
    message: "Edits since the last save are lost if you close without saving.",
    buttons: [{ label: "Cancel" },
              { label: "Don't save", destructive: true, onClick: () => vm.cmd("project close") },
              { label: "Save", primary: true, onClick: async () => { if ((await vm.cmd("project save")).ok) vm.cmd("project close"); } }],
  });
}

// ------------------------------------------------------------------ the pickers (linux_main.cpp's choosers)
export async function pickFootage(vm, ctx) {
  const paths = await ctx.pick({ mode: "files", title: "Add footage", kinds: ["video", "image"], ok: "Add" });
  if (paths && paths.length) vm.footagePicked(paths);
}
export async function pickProjectToOpen(vm, ctx) {
  const p = await ctx.pick({ mode: "open", title: "Open project", kinds: ["project"], ok: "Open" });
  if (p) vm.openProject(p);
}
export async function pickProjectToCreate(vm, ctx) {
  const p = await ctx.pick({ mode: "save", title: "New project", kinds: ["project"], name: "Untitled.isp", ok: "Create",
                             ensure: withExt(".isp", (n) => n.endsWith(".isp")) });
  if (p) vm.newProject(p);
}
export async function pickSaveAs(vm, ctx) {
  const cur = (vm.model.peek() || {}).projectPath || "";
  const p = await ctx.pick({ mode: "save", title: "Save project as", kinds: ["project"], name: cur ? baseName(cur) : "Untitled.isp", ok: "Save",
                             ensure: withExt(".isp", (n) => n.endsWith(".isp")) });
  if (p) vm.saveAsPicked(p);
}
export async function pickPresetToImport(vm, ctx) {
  const p = await ctx.pick({ mode: "open", title: "Import preset", kinds: ["preset"], ok: "Import" });
  if (p) vm.presetImportPicked(p);
}
const pngSave = (title, ok, name) => ({ mode: "save", title, kinds: ["image"], name, ok, ensure: withExt(".png", (n) => n.endsWith(".png")) });
export async function pickStillToExport(vm, ctx) {
  const p = await ctx.pick(pngSave("Export still", "Export", "still.png"));
  if (p) vm.stillExportPicked(p);
}
export async function pickFrameToSave(vm, ctx, bind) {
  const p = await ctx.pick(pngSave("Save frame", "Save", "frame.png"));
  if (p) vm.frameSavePicked(p, bind);
}
export const pickLutToOpen = (vm, ctx) => ctx.pick({ mode: "open", title: "Choose a LUT", kinds: ["lut"], ok: "Open" });
export const pickLutToSave = (vm, ctx, suggested) => ctx.pick({ mode: "save", title: "Export LUT", kinds: ["lut"], name: suggested, ok: "Export",
  ensure: withExt(".cube", (n) => n.endsWith(".cube") || n.endsWith(".CUBE")) });
const hasExt = (n) => /\.[^./]+$/.test(n);
export const pickTimelineToImport = (vm, ctx) => ctx.pick({ mode: "open", title: "Import timeline", kinds: ["timeline"], ok: "Open" });
export const pickTimelineToExport = (vm, ctx, suggested) => ctx.pick({ mode: "save", title: "Export timeline", kinds: ["timeline"], name: suggested,
  ok: "Export", ensure: withExt(".fcpxml", hasExt) });
export const pickCaptionsToImport = (vm, ctx) => ctx.pick({ mode: "open", title: "Import captions", kinds: ["captions"], ok: "Open" });
export const pickCaptionsToExport = (vm, ctx, suggested) => ctx.pick({ mode: "save", title: "Export captions", kinds: ["captions"], name: suggested,
  ok: "Export", ensure: withExt(".srt", hasExt) });
export const pickMediaToRelink = (vm, ctx, fileName) => ctx.pick({ mode: "open", title: "Locate " + fileName, kinds: ["video", "image", "audio", "file"], ok: "Open" });
export const pickFolder = (vm, ctx) => ctx.pick({ mode: "folder", title: "Search a folder for missing media", ok: "Search" });
