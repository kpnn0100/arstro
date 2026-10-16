/*
 *  solaris_core — the keyboard's actions (R-SET-4).
 *
 *  Every shortcut is an ACTION with default keys, in an area. The service validates and persists a
 *  machine's own keys (`settings set key.<action>=…`) and publishes each action with its keys and
 *  defaults (`settings.keys`); the window looks a pressed combo up there. The song's three areas —
 *  Transport, Song, Lanes — are one keyboard, where a combo means one action; the piano roll's is
 *  its own, and shadows the song's while a roll has the keys (R-ROLL-8).
 */
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace solaris
{
    struct KeyAction
    {
        std::string id;        // "transport.play" — the setting is `key.<id>`
        std::string area;      // "Transport" · "Song" · "Lanes" · "Piano roll"
        std::string label;     // what the sheet shows
        std::string defaults;  // canonical combos, KeyCombo's spelling: "Ctrl+Shift+Z, Ctrl+Y"
    };

    /** The actions, in the order the sheet lists them. */
    const std::vector<KeyAction> &keyActions();
    const KeyAction *keyAction(const std::string &id);
    /** The keyboard an area's keys live on: "song" (Transport, Song, Lanes) or "roll". */
    std::string keyGroup(const std::string &area);
    /** An action's keys on this machine: its entry in `overrides` (the settings' `key.<id>` lines) when that
        reads, else its defaults — canonical combos, none for `none`. */
    std::vector<std::string> keysOf(const KeyAction &a, const std::vector<std::pair<std::string, std::string>> &overrides);
}
}
