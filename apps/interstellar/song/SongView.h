/*
 *  interstellar_song_ui — Solaris's OWN window for a hosted song, as Interstellar's Song page shows it
 *  (R-SONG-7). The other half of the one adapter between the two apps (SongHost.h is the core's half):
 *  it links Solaris's front end privately, so nothing that includes this file sees a Solaris header.
 *
 *  The window is Solaris's `App` — its song bar and transport, its lanes, its mixer dock, its device
 *  windows, its piano roll, its menus and its keymap — drawn from the hosted service's model and
 *  reporting every intent as a Solaris command line, which `dispatch` sends back as Interstellar's
 *  `song run <song> <line>`: each edit one step of the song's own history, journaled, and what an agent
 *  could have sent. Solaris's Home (and its window close) is `onBack` — back to Interstellar.
 *
 *  Solaris's accent (orange) is Solaris's while its window draws and takes input, and Interstellar's
 *  is put back after, so each app keeps its own colour in one window (cosmo's runtime accent slot).
 */
#pragma once
#include "../app/widgets/EmbeddedView.h"
#include <functional>
#include <memory>
#include <string>

namespace arstro
{
namespace interstellar
{
    class SongHost;

    struct SongViewHooks
    {
        /** One Solaris command line for this song (the host sends `song run <song> <line>`). */
        std::function<bool(const std::string &line, std::string &err)> dispatch;
        /** Solaris's Home or its window close: back to Interstellar. */
        std::function<void()> onBack;
        /** The host's dialogs and clipboard (Relink's Locate…, Search a Folder…, Copy Address). */
        std::function<void(const std::string &title, std::function<void(const std::string &)> done)> onPickFile, onPickDirectory;
        std::function<void(const std::string &text)> onCopy;
    };

    /** Solaris's window for `host` (which must outlive it — the service keeps a song until its project
     *  closes, and the App lets a window go when its song leaves the model). */
    std::shared_ptr<interstellar_v1::EmbeddedView> makeSongView(SongHost &host, SongViewHooks hooks);
}
}
