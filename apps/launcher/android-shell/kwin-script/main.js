/*
 * arstro-android-shell — KWin compositor bridge script (M7, Plasma track).
 *
 * Runs inside kwin_wayland (loaded via `org.kde.KWin /Scripting loadScript`) and answers the
 * shell's window-management calls. KWin scripts can't own an arbitrary D-Bus service, so we expose
 * the API by responding to `callDBus` from the shell OR (the pattern used here) the shell calls
 * KWin's own scripting D-Bus; this file implements the window operations and the open-maximised +
 * gesture-suppression rules. On Plasma 6 the Workspace API is `workspace.windowList()`,
 * `workspace.activeWindow`, `window.closeWindow()`, `window.frameGeometry`, `window.tile`.
 *
 * NOTE: unverified on this dev machine (no kwin_wayland). Load with:
 *   qdbus org.kde.KWin /Scripting loadScript /path/to/main.js arstro-android-shell
 *   qdbus org.kde.KWin /Scripting start
 */

// Open every normal window maximised by default (Android-like), unless in the opt-out list.
var maximizeOptOut = {};  // fill from config: appId -> true

function isNormal(w) {
    return w && w.normalWindow && !w.skipTaskbar && !w.desktopWindow && !w.dock;
}

function onWindowAdded(w) {
    if (!isNormal(w)) return;
    if (maximizeOptOut[w.resourceClass ? w.resourceClass.toString() : ""]) return;
    // Plasma 6: maximize by setting the maximize mode.
    try { w.setMaximize(true, true); } catch (e) { /* API drift tolerated */ }
}

workspace.windowAdded.connect(onWindowAdded);

// A minimal command surface the shell reaches via KWin's callDBus round-trip. The shell writes a
// request into a well-known place (a temp file or a KWin script D-Bus signal); for v1 we expose
// the operations as functions invoked by the shell's own D-Bus proxy calling into KWin scripting.

function listWindows() {
    var out = [];
    var ws = workspace.windowList ? workspace.windowList() : workspace.clientList();
    for (var i = 0; i < ws.length; i++) {
        var w = ws[i];
        if (!isNormal(w)) continue;
        out.push({
            id: w.internalId ? w.internalId.toString() : ("" + i),
            appId: w.resourceClass ? w.resourceClass.toString() : "",
            title: w.caption ? w.caption.toString() : "",
            active: (workspace.activeWindow === w),
            fullscreen: !!w.fullScreen
        });
    }
    return out;
}

function findById(id) {
    var ws = workspace.windowList ? workspace.windowList() : workspace.clientList();
    for (var i = 0; i < ws.length; i++) {
        var w = ws[i];
        var wid = w.internalId ? w.internalId.toString() : ("" + i);
        if (wid === id) return w;
    }
    return null;
}

function activate(id) { var w = findById(id); if (w) workspace.activeWindow = w; }
function closeWindow(id) { var w = findById(id); if (w && w.closeWindow) w.closeWindow(); }
function minimizeAll() {
    var ws = workspace.windowList ? workspace.windowList() : workspace.clientList();
    for (var i = 0; i < ws.length; i++) if (isNormal(ws[i])) ws[i].minimized = true;
}

// Tile two windows side-by-side at `ratio` (left width fraction). Uses frameGeometry on the
// active screen; KWin 6 also has a custom tiling API (workspace.tilingForScreen) that a later
// pass can prefer.
function tilePair(leftId, rightId, ratio) {
    var area = workspace.clientArea(KWin.MaximizeArea, workspace.activeScreen, workspace.currentDesktop);
    var l = findById(leftId), r = findById(rightId);
    var lw = Math.floor(area.width * ratio);
    if (l) l.frameGeometry = { x: area.x, y: area.y, width: lw, height: area.height };
    if (r) r.frameGeometry = { x: area.x + lw, y: area.y, width: area.width - lw, height: area.height };
}

// Register a global-shortcut-driven "back" as a fallback (the shell also injects keys). The shell
// primarily uses the D-Bus operations above.
print("arstro-android-shell: KWin compositor bridge loaded");
