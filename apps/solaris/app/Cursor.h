/*
 *  solaris_ui — Cursor: what the pointer looks like (R-UI-14).
 *
 *  The App decides — from the grip under the pointer, or the resize in hand — and the host only shows it
 *  (law 12): `App::cursor()` after every pointer event, mapped to the OS's own cursor by its CSS name.
 */
#pragma once

namespace arstro
{
namespace solaris_ui
{
    enum class Cursor
    {
        Default = 0,
        ResizeH,    // ↔ a clip's end, a note's end, the pattern's end, the loop brace's ends
        ResizeV,    // ↕ the dock's top edge, a note's velocity
        ResizeDiag, // ⤡ a window's corner
    };

    /** The CSS cursor name (GDK's `gdk_cursor_new_from_name` takes these). */
    inline const char *cursorName(Cursor c)
    {
        switch (c)
        {
        case Cursor::ResizeH: return "ew-resize";
        case Cursor::ResizeV: return "ns-resize";
        case Cursor::ResizeDiag: return "nwse-resize";
        default: return "default";
        }
    }
}
}
