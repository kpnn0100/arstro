/*
 *  solaris_ui — KeyInput: what the window hands the App for one key press (R-SET-4; D-9).
 *
 *  Pure, so it is tested with no display: an X keysym in (GDK_KEY_* are the same numbers), the key's
 *  code out — KeyCombo's table, the codes a keymap combo names. The host reads the code from the
 *  key's UNSHIFTED keysym, so Shift+1 is the combo "Shift+1", never "!"; and what the press types
 *  goes after it as a Text event, so a field being typed in (a rename, the tempo) gets the character.
 */
#pragma once
#include <cstdint>

namespace arstro
{
namespace solaris_ui
{
    /** The App's key code for a keysym, or 0 for a key no combo can name (a modifier alone, a media key). */
    inline int keyCodeOfKeysym(uint32_t ks)
    {
        if (ks >= 'a' && ks <= 'z') return (int)(ks - 'a' + 'A');
        if ((ks >= 'A' && ks <= 'Z') || (ks >= '0' && ks <= '9')) return (int)ks;
        if (ks >= 0xffbe && ks <= 0xffc9) return 112 + (int)(ks - 0xffbe); // F1 … F12
        switch (ks)
        {
        case 0xff08: return 8;                // BackSpace
        case 0xff09: case 0xfe20: return 9;   // Tab, ISO_Left_Tab
        case 0xff0d: case 0xff8d: return 13;  // Return, KP_Enter
        case 0xff1b: return 27;               // Escape
        case 0x20: return 32;                 // space
        case 0xff55: return 33;               // Page_Up
        case 0xff56: return 34;               // Page_Down
        case 0xff57: return 35;               // End
        case 0xff50: return 36;               // Home
        case 0xff51: return 37;               // Left
        case 0xff52: return 38;               // Up
        case 0xff53: return 39;               // Right
        case 0xff54: return 40;               // Down
        case 0xff63: return 45;               // Insert
        case 0xffff: case 0xff9f: return 46;  // Delete, KP_Delete
        case ';': return 186;
        case '=': return 187;
        case ',': return 188;
        case '-': return 189;
        case '.': return 190;
        case '/': return 191;
        case '`': return 192;
        case '[': return 219;
        case '\\': return 220;
        case ']': return 221;
        case '\'': return 222;
        default: return 0;
        }
    }

    /** Whether a press also TYPES (a Text event after its Down): a printable character with no Ctrl or Alt held. */
    inline bool keyTypes(uint32_t unicode, bool ctrl, bool alt) { return !ctrl && !alt && unicode >= 0x20 && unicode != 0x7f; }
}
}
