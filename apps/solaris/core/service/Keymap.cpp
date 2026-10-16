#include "Keymap.h"
#include "KeyCombo.h"

namespace arstro
{
namespace solaris
{
    const std::vector<KeyAction> &keyActions()
    {
        // the keys the app had before they were settings (R-TIME-7/8, R-EDM-1, R-ROLL-8) are the defaults
        static const std::vector<KeyAction> k = {
            {"transport.play", "Transport", "Play / stop — back to where the time bar was put", "Space"},
            {"transport.stopHere", "Transport", "Stop here", "Ctrl+Space"},
            {"transport.toStart", "Transport", "To the start", "Enter"},
            {"song.save", "Song", "Save", "Ctrl+S"},
            {"song.undo", "Song", "Undo", "Ctrl+Z"},
            {"song.redo", "Song", "Redo", "Ctrl+Shift+Z, Ctrl+Y"},
            {"song.settings", "Song", "Settings", "Ctrl+Comma"},
            {"lanes.delete", "Lanes", "Delete the clip", "Delete, Backspace"},
            {"lanes.duplicate", "Lanes", "Duplicate the clip", "Ctrl+D"},
            {"roll.selectAll", "Piano roll", "Select all", "Ctrl+A"},
            {"roll.deselect", "Piano roll", "Deselect", "Escape"},
            {"roll.delete", "Piano roll", "Delete", "Delete, Backspace"},
            {"roll.duplicate", "Piano roll", "Duplicate", "Ctrl+D"},
            {"roll.copy", "Piano roll", "Copy", "Ctrl+C"},
            {"roll.paste", "Piano roll", "Paste", "Ctrl+V"},
            {"roll.up", "Piano roll", "Up a semitone", "Up"},
            {"roll.down", "Piano roll", "Down a semitone", "Down"},
            {"roll.octaveUp", "Piano roll", "Up an octave", "Shift+Up"},
            {"roll.octaveDown", "Piano roll", "Down an octave", "Shift+Down"},
            {"roll.left", "Piano roll", "Earlier by the snap", "Left"},
            {"roll.right", "Piano roll", "Later by the snap", "Right"},
        };
        return k;
    }

    const KeyAction *keyAction(const std::string &id)
    {
        for (const auto &a : keyActions())
            if (a.id == id) return &a;
        return nullptr;
    }

    std::string keyGroup(const std::string &area) { return area == "Piano roll" ? "roll" : "song"; }

    std::vector<std::string> keysOf(const KeyAction &a, const std::vector<std::pair<std::string, std::string>> &overrides)
    {
        std::vector<std::string> keys;
        std::string err;
        for (const auto &o : overrides)
            if (o.first == a.id && parseCombos(o.second, keys, err)) return keys; // a hand-edited line that does not read: the defaults
        parseCombos(a.defaults, keys, err);
        return keys;
    }
}
}
