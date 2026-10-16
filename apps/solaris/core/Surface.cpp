#include "Surface.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace solaris
{
    int ringPosition(double pan) { return 1 + (int)std::lround((std::clamp(pan, -1.0, 1.0) + 1.0) * 5.0); }

    namespace
    {
        int fader14(double travel) { return (int)std::lround(std::clamp(travel, 0.0, 1.0) * 16383.0); }
        void put(std::vector<uint8_t> &out, uint8_t a, uint8_t b, uint8_t c)
        {
            out.push_back(a);
            out.push_back(b);
            out.push_back(c);
        }
        std::string ascii(const std::string &s, size_t n)
        {
            // the surface's text is 7-bit: anything else a space; padded or cut to n
            std::string t;
            for (char ch : s)
            {
                if (t.size() == n) break;
                t += (ch >= 0x20 && ch < 0x7F) ? ch : ' ';
            }
            t.resize(n, ' ');
            return t;
        }

        /*
         *  Mackie Control. In: a fader is a 14-bit pitch bend on channel 1 … 8 (the master's on 9); a pot a relative CC
         *  16 … 23 (bit 6 set: counter-clockwise, the low six bits the steps); a button a note, velocity 127 pressed and 0
         *  let go — arm 0 … 7, solo 8 … 15, mute 16 … 23, select 24 … 31, bank ◀ ▶ 46 47, channel ◀ ▶ 48 49, rewind 91,
         *  fast forward 92, stop 93, play 94, record 95, touch 104 … 111 (the master 112). Out: the same pitch bends and
         *  notes (a light), a pot's ring as CC 48 … 55 (boost/cut mode, 1 … 11), the scribble strip's top line as SysEx
         *  F0 00 00 66 14 12 <offset> <text> F7 — seven characters a strip.
         */
        class Mcu : public SurfaceCodec
        {
        public:
            bool decode(const MidiEvent &m, SurfaceEvent &e) override
            {
                const int kind = m.status & 0xF0, ch = m.status & 0x0F;
                if (kind == 0xE0)
                {
                    e.value = (double)((int)m.d1 | ((int)m.d2 << 7)) / 16383.0;
                    if (ch < kSurfaceStrips) { e.kind = SurfaceEvent::Fader; e.strip = ch; return true; }
                    if (ch == 8) { e.kind = SurfaceEvent::Master; return true; }
                    return false;
                }
                if (kind == 0xB0 && m.d1 >= 0x10 && m.d1 < 0x10 + kSurfaceStrips)
                {
                    e.kind = SurfaceEvent::Pot;
                    e.strip = m.d1 - 0x10;
                    e.value = (m.d2 & 0x40) ? -(double)(m.d2 & 0x3F) : (double)(m.d2 & 0x3F);
                    return true;
                }
                if (kind != 0x90) return false;
                const int n = m.d1;
                const bool down = m.d2 > 0;
                if (n >= 0x68 && n <= 0x70) { e.kind = down ? SurfaceEvent::Touch : SurfaceEvent::Release; e.strip = n - 0x68; return true; }
                if (!down) return false; // a button counts when pressed
                if (n < 0x20)
                {
                    static const SurfaceEvent::Kind rows[4] = {SurfaceEvent::Arm, SurfaceEvent::Solo, SurfaceEvent::Mute, SurfaceEvent::Select};
                    e.kind = rows[n / 8];
                    e.strip = n % 8;
                    return true;
                }
                switch (n)
                {
                case 0x2E: e.kind = SurfaceEvent::BankLeft; return true;
                case 0x2F: e.kind = SurfaceEvent::BankRight; return true;
                case 0x30: e.kind = SurfaceEvent::ChannelLeft; return true;
                case 0x31: e.kind = SurfaceEvent::ChannelRight; return true;
                case 0x5B: e.kind = SurfaceEvent::Rewind; return true;
                case 0x5C: e.kind = SurfaceEvent::Forward; return true;
                case 0x5D: e.kind = SurfaceEvent::Stop; return true;
                case 0x5E: e.kind = SurfaceEvent::Play; return true;
                case 0x5F: e.kind = SurfaceEvent::Record; return true;
                default: return false;
                }
            }

            void encode(const SurfaceState &was, const SurfaceState &now, bool all, const std::array<bool, kSurfaceStrips + 1> &touched,
                        std::vector<uint8_t> &out) override
            {
                bool names = all;
                for (int i = 0; i < kSurfaceStrips; ++i)
                {
                    const SurfaceStrip &a = was.strips[(size_t)i], &b = now.strips[(size_t)i];
                    const double f = b.present ? b.fader : 0.0, fWas = a.present ? a.fader : 0.0;
                    if ((all || f != fWas) && !touched[(size_t)i])
                    {
                        const int v = fader14(f);
                        put(out, (uint8_t)(0xE0 | i), (uint8_t)(v & 0x7F), (uint8_t)(v >> 7));
                    }
                    auto light = [&](int note, bool on, bool onWas) {
                        if (all || on != onWas) put(out, 0x90, (uint8_t)note, on ? 0x7F : 0x00);
                    };
                    light(0x00 + i, b.present && b.arm, a.present && a.arm);
                    light(0x08 + i, b.present && b.solo, a.present && a.solo);
                    light(0x10 + i, b.present && b.mute, a.present && a.mute);
                    const int ring = b.present ? 0x10 | ringPosition(b.pan) : 0x00, ringWas = a.present ? 0x10 | ringPosition(a.pan) : 0x00;
                    if (all || ring != ringWas) put(out, 0xB0, (uint8_t)(0x30 + i), (uint8_t)ring);
                    names |= (b.present ? b.name : std::string()) != (a.present ? a.name : std::string());
                }
                if ((all || now.master != was.master) && !touched[kSurfaceStrips])
                {
                    const int v = fader14(now.master);
                    put(out, 0xE8, (uint8_t)(v & 0x7F), (uint8_t)(v >> 7));
                }
                if (all || now.playing != was.playing) put(out, 0x90, 0x5E, now.playing ? 0x7F : 0x00);
                if (all || now.recording != was.recording) put(out, 0x90, 0x5F, now.recording ? 0x7F : 0x00);
                if (names)
                {
                    // the top line: seven characters a strip, six of the name and a gap
                    std::string line;
                    for (const auto &s : now.strips) line += ascii(s.present ? s.name : std::string(), 6) + " ";
                    for (uint8_t b : {0xF0, 0x00, 0x00, 0x66, 0x14, 0x12, 0x00}) out.push_back(b);
                    for (char ch : line) out.push_back((uint8_t)ch);
                    out.push_back(0xF7);
                }
            }
        };

        /*
         *  HUI. In: a fader is a pair of CCs, 0 … 7 its high seven bits then 32 … 39 its low (14 bits); a switch is CC 15
         *  naming a ZONE then CC 47 naming a PORT (bit 6 set: pressed) — zones 0 … 7 the channel strips (port 0 the
         *  fader's touch, 1 select, 2 mute, 3 solo, 7 arm), zone 10 bank and channel (0 channel ◀, 1 bank ◀, 2 channel ▶,
         *  3 bank ▶), zone 14 the transport (1 rewind, 2 fast forward, 3 stop, 4 play, 5 record); a pot CC 64 … 71
         *  (bit 6 set: clockwise, the low six bits the steps). Out: the same fader pairs, a light as CC 12 <zone> then CC
         *  44 <port | 64 when lit>, a pot's ring CC 16 … 23 (1 … 11), a channel's four-character name as SysEx
         *  F0 00 00 66 05 00 10 <strip> <four characters> F7, and a ping (note 0, velocity 0) it answers.
         *  As the protocol is documented by those who read it from the wire; not yet checked against hardware.
         */
        class Hui : public SurfaceCodec
        {
        public:
            bool decode(const MidiEvent &m, SurfaceEvent &e) override
            {
                const int kind = m.status & 0xF0;
                if (kind == 0x90 && m.d1 == 0x00) { e.kind = SurfaceEvent::Ping; return true; } // its answer to our ping
                if (kind != 0xB0) return false;
                const int cc = m.d1, v = m.d2;
                if (cc < kSurfaceStrips) { mHigh[(size_t)cc] = v; return false; } // the high half: the low one completes it
                if (cc >= 0x20 && cc < 0x20 + kSurfaceStrips)
                {
                    const int i = cc - 0x20;
                    e.kind = SurfaceEvent::Fader;
                    e.strip = i;
                    e.value = (double)((mHigh[(size_t)i] << 7) | v) / 16383.0;
                    return true;
                }
                if (cc >= 0x40 && cc < 0x40 + kSurfaceStrips)
                {
                    e.kind = SurfaceEvent::Pot;
                    e.strip = cc - 0x40;
                    e.value = (v & 0x40) ? (double)(v & 0x3F) : -(double)(v & 0x3F);
                    return true;
                }
                if (cc == 0x0F) { mZone = v; return false; }
                if (cc != 0x2F) return false;
                const int port = v & 0x0F;
                const bool down = (v & 0x40) != 0;
                if (mZone < kSurfaceStrips)
                {
                    e.strip = mZone;
                    if (port == 0) { e.kind = down ? SurfaceEvent::Touch : SurfaceEvent::Release; return true; }
                    if (!down) return false;
                    switch (port)
                    {
                    case 1: e.kind = SurfaceEvent::Select; return true;
                    case 2: e.kind = SurfaceEvent::Mute; return true;
                    case 3: e.kind = SurfaceEvent::Solo; return true;
                    case 7: e.kind = SurfaceEvent::Arm; return true;
                    default: return false;
                    }
                }
                if (!down) return false;
                if (mZone == 0x0A)
                {
                    static const SurfaceEvent::Kind k[4] = {SurfaceEvent::ChannelLeft, SurfaceEvent::BankLeft, SurfaceEvent::ChannelRight, SurfaceEvent::BankRight};
                    if (port > 3) return false;
                    e.kind = k[port];
                    return true;
                }
                if (mZone == 0x0E)
                {
                    static const SurfaceEvent::Kind k[6] = {SurfaceEvent::None, SurfaceEvent::Rewind, SurfaceEvent::Forward, SurfaceEvent::Stop, SurfaceEvent::Play, SurfaceEvent::Record};
                    if (port < 1 || port > 5) return false;
                    e.kind = k[port];
                    return true;
                }
                return false;
            }

            void encode(const SurfaceState &was, const SurfaceState &now, bool all, const std::array<bool, kSurfaceStrips + 1> &touched,
                        std::vector<uint8_t> &out) override
            {
                auto light = [&](int zone, int port, bool on, bool onWas) {
                    if (!all && on == onWas) return;
                    put(out, 0xB0, 0x0C, (uint8_t)zone);
                    put(out, 0xB0, 0x2C, (uint8_t)(port | (on ? 0x40 : 0x00)));
                };
                for (int i = 0; i < kSurfaceStrips; ++i)
                {
                    const SurfaceStrip &a = was.strips[(size_t)i], &b = now.strips[(size_t)i];
                    const double f = b.present ? b.fader : 0.0, fWas = a.present ? a.fader : 0.0;
                    if ((all || f != fWas) && !touched[(size_t)i])
                    {
                        const int v = fader14(f);
                        put(out, 0xB0, (uint8_t)i, (uint8_t)(v >> 7));
                        put(out, 0xB0, (uint8_t)(0x20 + i), (uint8_t)(v & 0x7F));
                    }
                    light(i, 2, b.present && b.mute, a.present && a.mute);
                    light(i, 3, b.present && b.solo, a.present && a.solo);
                    light(i, 7, b.present && b.arm, a.present && a.arm);
                    const int ring = b.present ? ringPosition(b.pan) : 0, ringWas = a.present ? ringPosition(a.pan) : 0;
                    if (all || ring != ringWas) put(out, 0xB0, (uint8_t)(0x10 + i), (uint8_t)ring);
                    const std::string n = b.present ? b.name : std::string(), nWas = a.present ? a.name : std::string();
                    if (all || n != nWas)
                    {
                        for (uint8_t x : {0xF0, 0x00, 0x00, 0x66, 0x05, 0x00, 0x10}) out.push_back(x);
                        out.push_back((uint8_t)i);
                        for (char ch : ascii(n, 4)) out.push_back((uint8_t)ch);
                        out.push_back(0xF7);
                    }
                }
                light(0x0E, 4, now.playing, was.playing);
                light(0x0E, 5, now.recording, was.recording);
            }

            void keepAlive(std::vector<uint8_t> &out) override { put(out, 0x90, 0x00, 0x00); }

        private:
            std::array<int, kSurfaceStrips> mHigh{};
            int mZone = -1;
        };
    }

    std::unique_ptr<SurfaceCodec> makeSurfaceCodec(const std::string &protocol)
    {
        if (protocol == "mcu") return std::make_unique<Mcu>();
        if (protocol == "hui") return std::make_unique<Hui>();
        return nullptr;
    }
}
}
