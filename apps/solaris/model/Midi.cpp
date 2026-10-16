#include "Midi.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace arstro
{
namespace solaris
{
    namespace
    {
        struct Reader
        {
            const std::string &b;
            size_t at = 0;
            bool ok = true;
            explicit Reader(const std::string &bytes) : b(bytes) {}
            bool has(size_t n) const { return at + n <= b.size(); }
            unsigned u8()
            {
                if (!has(1)) { ok = false; return 0; }
                return (unsigned char)b[at++];
            }
            unsigned u16() { const unsigned a = u8(); return (a << 8) | u8(); }
            unsigned long u32() { const unsigned long a = u16(); return (a << 16) | u16(); }
            unsigned long vlq()
            {
                unsigned long v = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const unsigned c = u8();
                    v = (v << 7) | (c & 0x7F);
                    if (!(c & 0x80)) return v;
                }
                ok = false; // a variable-length number is at most four bytes
                return v;
            }
        };

        void put16(std::string &o, unsigned v) { o += (char)((v >> 8) & 0xFF); o += (char)(v & 0xFF); }
        void put32(std::string &o, unsigned long v) { put16(o, (unsigned)(v >> 16)); put16(o, (unsigned)(v & 0xFFFF)); }
        void putVlq(std::string &o, unsigned long v)
        {
            char buf[5];
            int n = 0;
            buf[n++] = (char)(v & 0x7F);
            while (v >>= 7) buf[n++] = (char)((v & 0x7F) | 0x80);
            while (n) o += buf[--n];
        }
    }

    bool parseMidi(const std::string &bytes, MidiFile &out, std::string &err)
    {
        out = MidiFile();
        Reader r(bytes);
        if (bytes.size() < 14 || bytes.compare(0, 4, "MThd") != 0) { err = "not a MIDI file (no MThd header)"; return false; }
        r.at = 4;
        const unsigned long hlen = r.u32();
        if (hlen < 6) { err = "a MIDI header shorter than 6 bytes"; return false; }
        out.format = (int)r.u16();
        const unsigned ntracks = r.u16();
        const unsigned div = r.u16();
        r.at = 8 + hlen;
        if (out.format > 2) { err = "MIDI format " + std::to_string(out.format) + " (0, 1 or 2 are read)"; return false; }
        if (div & 0x8000)
        {
            out.smpte = true;
            out.division = 0;
            out.fps = -(int)(signed char)(div >> 8);
            out.ticksPerFrame = (int)(div & 0xFF);
            if (out.fps <= 0 || out.ticksPerFrame <= 0) { err = "an SMPTE division with no frames"; return false; }
        }
        else
        {
            out.division = (int)div;
            if (out.division <= 0) { err = "a division of 0 ticks per quarter note"; return false; }
        }
        for (unsigned t = 0; t < ntracks && r.ok; ++t)
        {
            // skip any chunk that is not a track (the format allows them)
            while (r.has(8) && bytes.compare(r.at, 4, "MTrk") != 0)
            {
                r.at += 4;
                const unsigned long skip = r.u32();
                r.at += skip;
            }
            if (!r.has(8)) { err = "the file ends before its track " + std::to_string(t + 1); return false; }
            r.at += 4;
            const unsigned long len = r.u32();
            const size_t end = r.at + len;
            if (end > bytes.size()) { err = "track " + std::to_string(t + 1) + " runs past the end of the file"; return false; }
            MidiTrack track;
            std::map<std::pair<int, int>, std::vector<std::pair<long long, int>>> open; // (channel, pitch) → started notes, FIFO
            long long tick = 0;
            unsigned status = 0;
            while (r.at < end && r.ok)
            {
                tick += (long long)r.vlq();
                unsigned ev = r.u8();
                if (ev < 0x80)
                {
                    // running status: this byte is the first data byte
                    if (!status) { err = "track " + std::to_string(t + 1) + ": a data byte with no status"; return false; }
                    --r.at;
                    ev = status;
                }
                if (ev == 0xFF)
                {
                    const unsigned type = r.u8();
                    const unsigned long n = r.vlq();
                    if (!r.has(n)) { err = "track " + std::to_string(t + 1) + ": a meta event runs past the end"; return false; }
                    if (type == 0x03 && track.name.empty()) track.name = bytes.substr(r.at, n);
                    if (type == 0x51 && n == 3)
                    {
                        const double us = (double)(((unsigned char)bytes[r.at] << 16) | ((unsigned char)bytes[r.at + 1] << 8) | (unsigned char)bytes[r.at + 2]);
                        out.tempos.emplace_back(tick, us);
                    }
                    r.at += n;
                    if (type == 0x2F) break; // the end of the track
                    continue;
                }
                if (ev == 0xF0 || ev == 0xF7)
                {
                    r.at += r.vlq(); // system exclusive: skipped
                    continue;
                }
                status = ev;
                const unsigned kind = ev & 0xF0;
                const int ch = (int)(ev & 0x0F);
                const unsigned d1 = r.u8();
                const unsigned d2 = (kind == 0xC0 || kind == 0xD0) ? 0 : r.u8();
                if (kind == 0x90 && d2 > 0) open[{ch, (int)d1}].emplace_back(tick, (int)d2);
                else if (kind == 0x80 || (kind == 0x90 && d2 == 0))
                {
                    auto &q = open[{ch, (int)d1}];
                    if (q.empty()) continue; // an off with no on: nothing to end
                    MidiNote n;
                    n.pitch = (int)d1;
                    n.channel = ch;
                    n.on = q.front().first;
                    n.vel = q.front().second;
                    n.off = std::max(tick, n.on + 1);
                    q.erase(q.begin());
                    track.notes.push_back(n);
                }
            }
            if (!r.ok) { err = "track " + std::to_string(t + 1) + " is cut short"; return false; }
            // a note still sounding when its track ends ends there
            for (auto &kv : open)
                for (const auto &st : kv.second)
                {
                    MidiNote n;
                    n.channel = kv.first.first;
                    n.pitch = kv.first.second;
                    n.on = st.first;
                    n.vel = st.second;
                    n.off = std::max(tick, n.on + 1);
                    track.notes.push_back(n);
                }
            std::stable_sort(track.notes.begin(), track.notes.end(), [](const MidiNote &a, const MidiNote &b) { return a.on < b.on || (a.on == b.on && a.pitch < b.pitch); });
            r.at = end;
            out.tracks.push_back(std::move(track));
        }
        std::stable_sort(out.tempos.begin(), out.tempos.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        return true;
    }

    double midiBeats(const MidiFile &f, long long tick)
    {
        if (!f.smpte) return (double)tick / f.division;
        // SMPTE: ticks are time; beats come through the tempo map (120 bpm until the first tempo)
        const double tickSeconds = 1.0 / ((double)f.fps * f.ticksPerFrame);
        double beats = 0.0, us = 500000.0;
        long long from = 0;
        for (const auto &t : f.tempos)
        {
            if (t.first >= tick) break;
            beats += (double)(t.first - from) * tickSeconds / (us / 1e6);
            from = t.first;
            us = t.second;
        }
        return beats + (double)(tick - from) * tickSeconds / (us / 1e6);
    }

    std::string writeMidi(const std::string &songName, double bpm, const std::string &sig, const std::vector<MidiOutTrack> &tracks)
    {
        std::string o = "MThd";
        put32(o, 6);
        put16(o, 1);
        put16(o, (unsigned)(tracks.size() + 1));
        put16(o, 960);
        auto chunk = [&](const std::string &body) {
            o += "MTrk";
            put32(o, (unsigned long)body.size());
            o += body;
        };
        // the tempo track: its name, the meter and the tempo, at tick 0
        {
            std::string t;
            putVlq(t, 0);
            t += (char)0xFF; t += (char)0x03;
            putVlq(t, songName.size());
            t += songName;
            int num = 4, den = 4;
            std::sscanf(sig.c_str(), "%d/%d", &num, &den);
            int pow2 = 0;
            while ((1 << pow2) < den && pow2 < 7) ++pow2;
            putVlq(t, 0);
            t += (char)0xFF; t += (char)0x58; t += (char)0x04;
            t += (char)std::clamp(num, 1, 255); t += (char)pow2; t += (char)24; t += (char)8;
            const unsigned long us = (unsigned long)std::llround(60000000.0 / std::max(1.0, bpm));
            putVlq(t, 0);
            t += (char)0xFF; t += (char)0x51; t += (char)0x03;
            t += (char)((us >> 16) & 0xFF); t += (char)((us >> 8) & 0xFF); t += (char)(us & 0xFF);
            putVlq(t, 0);
            t += (char)0xFF; t += (char)0x2F; t += (char)0x00;
            chunk(t);
        }
        for (const auto &tr : tracks)
        {
            struct Ev { long long tick; int order; unsigned char status, d1, d2; };
            std::vector<Ev> evs;
            const unsigned char ch = (unsigned char)std::clamp(tr.channel, 0, 15);
            for (const auto &n : tr.notes)
            {
                const long long on = std::llround(n.at * 960.0), off = std::max(on + 1, (long long)std::llround((n.at + n.length) * 960.0));
                evs.push_back(Ev{on, 1, (unsigned char)(0x90 | ch), (unsigned char)std::clamp(n.pitch, 0, 127), (unsigned char)std::clamp(n.vel, 1, 127)});
                evs.push_back(Ev{off, 0, (unsigned char)(0x80 | ch), (unsigned char)std::clamp(n.pitch, 0, 127), 0});
            }
            // at one tick, offs before ons: a note repeated on the same pitch is two notes, not one held
            std::stable_sort(evs.begin(), evs.end(), [](const Ev &a, const Ev &b) { return a.tick < b.tick || (a.tick == b.tick && a.order < b.order); });
            std::string t;
            putVlq(t, 0);
            t += (char)0xFF; t += (char)0x03;
            putVlq(t, tr.name.size());
            t += tr.name;
            long long last = 0;
            for (const auto &e : evs)
            {
                putVlq(t, (unsigned long)(e.tick - last));
                last = e.tick;
                t += (char)e.status; t += (char)e.d1; t += (char)e.d2;
            }
            putVlq(t, 0);
            t += (char)0xFF; t += (char)0x2F; t += (char)0x00;
            chunk(t);
        }
        return o;
    }
}
}
