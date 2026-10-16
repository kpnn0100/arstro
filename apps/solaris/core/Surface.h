#pragma once
/*
 *  solaris_core — a control surface (R-CTRL-2; docs/discussion.md Round 10): ONE model — eight channel strips (a fader,
 *  a pot, mute, solo, arm, select, a touch sense), a master fader, the transport keys, bank / channel shift — and two
 *  CODECS of it, Mackie Control and HUI. A codec is pure: a MIDI message in, an event out; the state the surface should
 *  show, against what it shows, in — the bytes that make it so, out. No device, no clock, no thread: tested as tables.
 */
#include "MidiIn.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris
{
    constexpr int kSurfaceStrips = 8;

    struct SurfaceEvent
    {
        enum Kind
        {
            None, Fader, Master, Pot, Mute, Solo, Arm, Select, Touch, Release, Play, Stop, Record, Rewind, Forward,
            BankLeft, BankRight, ChannelLeft, ChannelRight, Ping
        } kind = None;
        int strip = 0;      // 0 … 7 (a channel strip's events; Touch/Release: 8 = the master)
        double value = 0.0; // Fader, Master: 0 … 1 of travel; Pot: steps, + clockwise
    };

    struct SurfaceStrip
    {
        bool present = false;      // a strip of the song is here (else: down, dark, blank)
        double fader = 0.0;        // 0 … 1 of travel (the fader law's)
        double pan = 0.0;          // −1 … 1
        bool mute = false, solo = false, arm = false;
        std::string name;
        bool operator==(const SurfaceStrip &o) const
        {
            return present == o.present && fader == o.fader && pan == o.pan && mute == o.mute && solo == o.solo && arm == o.arm && name == o.name;
        }
    };

    struct SurfaceState
    {
        std::array<SurfaceStrip, kSurfaceStrips> strips{};
        double master = 0.0;
        bool playing = false, recording = false;
    };

    class SurfaceCodec
    {
    public:
        virtual ~SurfaceCodec() = default;
        /** One message from the surface → what it means (false: nothing this model knows). */
        virtual bool decode(const MidiEvent &m, SurfaceEvent &e) = 0;
        /** The bytes that make the surface show `now`, given it shows `was` (`all`: everything, as when it connects).
         *  A fader a hand is on (`touched[i]`, 8 the master) is left where the hand puts it. */
        virtual void encode(const SurfaceState &was, const SurfaceState &now, bool all, const std::array<bool, kSurfaceStrips + 1> &touched,
                            std::vector<uint8_t> &out) = 0;
        /** What it must be sent now and then to stay with us (HUI's ping); nothing for Mackie Control. */
        virtual void keepAlive(std::vector<uint8_t> &out) { (void)out; }
    };

    /** "mcu" (Mackie Control) or "hui"; null for anything else. */
    std::unique_ptr<SurfaceCodec> makeSurfaceCodec(const std::string &protocol);
    /** A pan's place on a pot's ring of eleven lights: 1 … 11, 6 the centre. */
    int ringPosition(double pan);
}
}
