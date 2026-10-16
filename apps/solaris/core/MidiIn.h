/*
 *  solaris_core — the MIDI INPUT seam (R-MIDI-2). The core names it; the host fills it with the ALSA sequencer.
 *
 *  A keyboard's bytes arrive on the host's thread and go straight into ONE lock-free queue the service owns
 *  (`push` never blocks); whichever audio thread runs — the song's player, or the input monitor while stopped —
 *  drains it at the start of each block, so a key is heard a block later, never a UI frame later. The sustain
 *  pedal is the consumer's (`SustainPedal`, pure and allocation-free): a note let go while it is down is held
 *  until it comes up.
 */
#pragma once
#include <bitset>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris
{
    /** One MIDI message, as the wire spells it: a status byte and its two data bytes. */
    struct MidiEvent
    {
        uint8_t status = 0, d1 = 0, d2 = 0;
        int kind() const { return status & 0xF0; }
        bool noteOn() const { return kind() == 0x90 && d2 > 0; }
        bool noteOff() const { return kind() == 0x80 || (kind() == 0x90 && d2 == 0); }
    };

    /** A MIDI input port this machine has (`midi devices`). */
    struct MidiPort
    {
        std::string id;   // what the host connects by — its name (ALSA's client:port numbers move between boots)
        std::string name;
    };

    class IMidiIn
    {
    public:
        virtual ~IMidiIn() = default;
        /** Listen to `ports` (ids from `listMidiInputs`; empty = every port there is), each message to `sink`
         *  on the host's thread. */
        virtual bool open(const std::vector<std::string> &ports, std::function<void(const MidiEvent &)> sink, std::string &err) = 0;
        virtual void close() = 0;
        /** The ports it listens to now. */
        virtual std::vector<std::string> listening() const = 0;
    };

    /** R-MIDI-2: the sustain pedal (CC 64), on the consumer's thread. `off` says whether a note-off goes through
     *  now; `up` hands back every note it held; a held pitch struck again is let go first (a fresh attack). */
    class SustainPedal
    {
    public:
        void reset() { mDown = false; mHeld.reset(); }
        bool down() const { return mDown; }
        /** A note-off: true = send it now; false = held for the pedal. */
        bool off(int pitch)
        {
            if (!mDown || pitch < 0 || pitch > 127) return true;
            mHeld.set((size_t)pitch);
            return false;
        }
        /** A note-on: true when that pitch was held — send its off first. */
        bool on(int pitch)
        {
            if (pitch < 0 || pitch > 127 || !mHeld.test((size_t)pitch)) return false;
            mHeld.reset((size_t)pitch);
            return true;
        }
        /** The pedal moved (value ≥ 64 = down). Going up, `release(pitch)` for every note it held. */
        template <class F> void pedal(int value, F release)
        {
            const bool down = value >= 64;
            if (mDown && !down)
                for (int p = 0; p < 128; ++p)
                    if (mHeld.test((size_t)p)) { mHeld.reset((size_t)p); release(p); }
            mDown = down;
        }

    private:
        bool mDown = false;
        std::bitset<128> mHeld;
    };
}
}
