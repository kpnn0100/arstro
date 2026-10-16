#pragma once
/*
 *  solaris_core — the MIDI OUTPUT seam (R-CTRL-2): a control surface is told where its faders go, which lights are on
 *  and what its text says. The core names it; the host fills it with the ALSA sequencer. `send` takes whole MIDI
 *  messages, system exclusive included, as the wire spells them.
 */
#include <cstdint>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris
{
    class IMidiOut
    {
    public:
        virtual ~IMidiOut() = default;
        /** Connect to the port named `port` (a name from `listMidiInputs` — a surface's in and out share it). */
        virtual bool open(const std::string &port, std::string &err) = 0;
        virtual void send(const std::vector<uint8_t> &bytes) = 0;
        virtual void close() = 0;
    };
}
}
