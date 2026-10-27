/*
 * Solaris — kit/cursor.js: what the pointer looks like (app/Cursor.h, R-UI-14).
 *
 * The native App decides the cursor from the grip under the pointer, or the resize in hand, and the host shows it by
 * its CSS name (Cursor.h cursorName — GDK takes the same names). On the web a widget sets `style.cursor` on the
 * element of each grip (the browser's own hit-testing is ProjectScreen::cursorAt's walk), and while a resize is in
 * hand it HOLDS the cursor everywhere — the native's dragCursor(): the pointer may leave the grip while it drags.
 *
 *   Cursor.Default | ResizeH (↔ a clip's end, a note's end, the loop brace) | ResizeV (↕ the dock's edge, a velocity)
 *          | ResizeDiag (⤡ a window's corner)            — the CSS names
 *   holdCursor(Cursor.ResizeV) -> release()              while a drag is in hand (call release on its end)
 *
 * A modal (a menu, a dialog, a sheet) takes the pointer: the arrow over it (App::cursor) — the modal layers set
 * `cursor: default` on themselves.
 */
export const Cursor = Object.freeze({ Default: "default", ResizeH: "ew-resize", ResizeV: "ns-resize", ResizeDiag: "nwse-resize" });

let holds = 0;
export function holdCursor(name) {
  const root = document.documentElement;
  root.style.setProperty("--sl-hold-cursor", name);
  root.classList.add("sl-hold");
  ++holds;
  let done = false;
  return () => {
    if (done) return;
    done = true;
    if (--holds <= 0) { holds = 0; root.classList.remove("sl-hold"); }
  };
}
