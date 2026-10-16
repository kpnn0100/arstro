// SolarisService — MIDI files (R-MIDI-1): a `.mid` made into patterns (and a clip, one line), a pattern or the whole
// song written as one. The bytes are the model's (`Midi.h`); the files are read and written here, as the `.slp` is.
#include "Compile.h"
#include "Format.h"
#include "Midi.h"
#include "SolarisService.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace arstro
{
namespace solaris
{
    namespace fs = std::filesystem;
    using K = Command::Kind;

    namespace
    {
        bool slurp(const std::string &path, std::string &out)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f) return false;
            std::stringstream ss;
            ss << f.rdbuf();
            out = ss.str();
            return true;
        }
        int beatsPerBar(const Project &p) { return std::max(1, std::atoi(p.header.sig.c_str())); }
        /** The General MIDI drum channel for a kit's strip, else a channel of its own (9 skipped). */
        int channelFor(const Project &p, const std::string &strip, int &next)
        {
            if (const Rack *rk = p.rack(strip); rk && !rk->devices.empty() && rk->devices.front().type == "drums") return 9;
            const int ch = next;
            next = next + 1 == 9 ? 10 : (next + 1) % 16;
            return ch;
        }
    }

    bool SolarisService::midiCommand(const Command &c, std::string &err)
    {
        Project &p = mProject;
        switch (c.kind)
        {
        case K::PatternImport:
        {
            std::string path = c.arg(0);
            if (!fs::path(path).is_absolute() && !mPath.empty() && fs::exists(resolvePath(path))) path = resolvePath(path);
            std::string bytes;
            if (!slurp(path, bytes)) { err = "cannot read " + c.arg(0); return false; }
            MidiFile mf;
            if (!parseMidi(bytes, mf, err)) { err = c.arg(0) + ": " + err; return false; }
            std::vector<int> withNotes;
            for (size_t i = 0; i < mf.tracks.size(); ++i)
                if (!mf.tracks[i].notes.empty()) withNotes.push_back((int)i);
            if (withNotes.empty()) { err = c.arg(0) + " has no notes"; return false; }
            const bool clip = c.has("lane") || c.has("instrument");
            std::vector<std::vector<int>> groups; // the tracks that make each pattern
            if (c.has("track"))
            {
                double x = 0;
                const int n = parseNumber(c.flag("track"), x) && x == std::floor(x) ? (int)x : 0;
                if (n < 1 || n > (int)withNotes.size())
                {
                    err = "--track is one of the file's " + std::to_string(withNotes.size()) + " tracks with notes (1…" + std::to_string(withNotes.size()) + ")";
                    return false;
                }
                groups = {{withNotes[(size_t)n - 1]}};
            }
            else if (clip) groups = {withNotes}; // a clip plays one pattern: every track with notes, merged
            else
                for (int i : withNotes) groups.push_back({i});
            const std::string stem = fs::path(path).stem().string();
            const double bar = beatsPerBar(p);
            std::vector<std::string> made;
            for (size_t gi = 0; gi < groups.size(); ++gi)
            {
                Pattern pt;
                pt.id = p.nextId("pt");
                const auto &first = mf.tracks[(size_t)groups[gi].front()];
                pt.name = c.has("name") ? c.flag("name")
                        : groups[gi].size() == 1 && !first.name.empty() ? first.name
                        : groups.size() > 1 ? stem + " " + std::to_string(gi + 1) : stem;
                std::map<std::pair<long long, int>, Note> byStart; // a pitch started twice at one tick is ONE note: the longer
                double end = 0.0;
                for (int ti : groups[gi])
                    for (const auto &n : mf.tracks[(size_t)ti].notes)
                    {
                        Note x;
                        x.pitch = std::clamp(n.pitch, 0, 127);
                        x.vel = std::clamp(n.vel, 1, 127);
                        x.at = toTick(midiBeats(mf, n.on));
                        x.length = std::max(1.0 / kPpq, toTick(midiBeats(mf, n.off) - midiBeats(mf, n.on))); // a duration on the tick, as the .slp holds it
                        const auto key = std::make_pair(std::llround(x.at * kPpq), x.pitch);
                        auto it = byStart.find(key);
                        if (it == byStart.end() || it->second.length < x.length) byStart[key] = x;
                        end = std::max(end, x.at + x.length);
                    }
                for (const auto &kv : byStart) pt.notes.push_back(kv.second);
                std::stable_sort(pt.notes.begin(), pt.notes.end(), [](const Note &a, const Note &b) { return a.at < b.at || (a.at == b.at && a.pitch < b.pitch); });
                pt.length = std::max(bar, std::ceil(end / bar - 1e-9) * bar); // whole bars that hold every note
                p.patterns.push_back(pt);
                changed("pattern.added", pt.id);
                made.push_back(pt.id);
            }
            mOutput = made.front() + "\n";
            if (clip)
            {
                // the clip by `clip add`'s own rules — a track's instrument, or a new one (R-BROWSE-3: one drop, one line)
                Command cc;
                cc.kind = K::ClipAdd;
                cc.flags = {{"pattern", made.front()}, {"at", c.flag("at", "0")}};
                if (c.has("lane")) cc.flags.emplace_back("lane", c.flag("lane"));
                if (c.has("instrument")) cc.flags.emplace_back("instrument", c.flag("instrument"));
                const std::string said = mOutput;
                if (!clipCommand(cc, err)) return false;
                mOutput = said; // the pattern first; the clip and anything else made are said by `made:`
            }
            return true;
        }
        case K::PatternExport:
        case K::ProjectExportMidi:
        {
            std::vector<MidiOutTrack> tracks;
            int next = 0;
            if (c.kind == K::PatternExport)
            {
                const Pattern *pt = p.pattern(c.arg(0));
                if (!pt) { err = "no pattern `" + c.arg(0) + "`"; return false; }
                std::string strip;
                for (const auto &cl : p.clips)
                    if (cl.pattern == pt->id) { strip = cl.track; break; }
                tracks.push_back(MidiOutTrack{pt->name, channelFor(p, strip, next), pt->notes});
            }
            else
            {
                // the song as it plays: each instrument strip a track; each note clip's pattern at the clip's place,
                // repeated to fill a clip longer than it, cut at the clip's end
                for (const auto &s : p.strips)
                {
                    if (s.kind != "instrument") continue;
                    MidiOutTrack t;
                    t.name = s.name;
                    for (const auto &cl : p.clips)
                    {
                        if (cl.track != s.id || cl.pattern.empty()) continue;
                        const Pattern *pt = p.pattern(cl.pattern);
                        if (!pt || pt->length <= 0) continue;
                        const double len = clipLengthBeats(p, cl);
                        for (double rep = 0.0; rep < len - 1e-9; rep += pt->length)
                            for (const auto &n : pt->notes)
                            {
                                if (rep + n.at >= len - 1e-9) continue;
                                Note x = n;
                                x.at = toTick(cl.at + rep + n.at);
                                x.length = std::min(n.length, len - (rep + n.at));
                                t.notes.push_back(x);
                            }
                    }
                    if (t.notes.empty()) continue;
                    std::stable_sort(t.notes.begin(), t.notes.end(), [](const Note &a, const Note &b) { return a.at < b.at; });
                    t.channel = channelFor(p, s.id, next);
                    tracks.push_back(std::move(t));
                }
                if (tracks.empty()) { err = "the song has no notes to export"; return false; }
            }
            const std::string out = c.kind == K::PatternExport ? c.arg(1) : c.arg(0);
            std::ofstream f(out, std::ios::binary);
            if (!f) { err = "cannot write " + out; return false; }
            f << writeMidi(p.header.name, p.header.bpm, p.header.sig, tracks);
            if (!f) { err = "cannot write " + out; return false; }
            mOutput = out + "\n";
            return true;
        }
        default:
            return false;
        }
    }
}
}
