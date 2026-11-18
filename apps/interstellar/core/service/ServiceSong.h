/*
 *  interstellar_core — INTERNAL: the hosted Solaris songs and what their clips play (R-SONG). Included by
 *  the service's own sources only (it needs the complete type where the service is built and destroyed).
 *
 *  Threads: the songs themselves (`slots`, `heard`, `versions`) are the UI thread's — opened, edited, saved,
 *  compiled there. What CROSSES threads is a compiled Program: the UI thread files it under its media
 *  ("song:<n>") and any reader — the player, a delivery render — builds its own engine from it, on its
 *  own thread. `programs` under `mu` is that crossing; nothing else is shared.
 */
#pragma once
#include "AudioSource.h"
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#if INTERSTELLAR_HAVE_SONG
#include "SongHost.h"
#endif

namespace arstro
{
namespace interstellar
{
    struct InterstellarService::SongState
    {
        /** One hosted song. */
        struct Slot
        {
#if INTERSTELLAR_HAVE_SONG
            std::unique_ptr<SongHost> host;
#endif
            std::string path;                        // the .slp, absolute
            unsigned long long fp = 0;               // its text's fingerprint when last looked at
            long long rev = -1;                      // the hosted model's revision then
            double fpAtMs = -1e18;                   // … when (a playing song moves its revision every pump)
            unsigned gen = 0;                        // bumped whenever the fingerprint moves: what a clip hears is stale
        };
        /** What a clip of a want (path | version | section) hears now: the program made at its song's `gen`. */
        struct Heard
        {
            bool made = false;
            unsigned gen = 0;
            std::string media;                       // "song:<n>" — empty when it could not be compiled
            std::string err;                         // … and why
        };
        /** A version's facts — its sections and lanes, as its clips draw them (R-SONG-8) — at a song's `gen`. */
        struct VersionFacts
        {
            unsigned gen = 0;
            bool made = false, ok = false;
#if INTERSTELLAR_HAVE_SONG
            SongHost::Facts facts;
#endif
        };

        std::map<std::string, Slot> slots;           // by absolute path — the UI thread's
        std::map<std::string, Heard> heard;          // by want — the UI thread's
        std::map<std::string, VersionFacts> versions; // by path | version — the UI thread's
        std::set<std::string> said;                  // want | gen failures already said (once each)
        unsigned long long nextMedia = 0;

        std::mutex mu;
#if INTERSTELLAR_HAVE_SONG
        std::map<std::string, std::shared_ptr<const SongHost::Program>> programs;   // media → program: any thread reads
#endif
    };
}
}
