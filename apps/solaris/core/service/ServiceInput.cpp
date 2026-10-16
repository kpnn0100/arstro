// SolarisService — the MIDI keyboard (R-MIDI-2): its ports, the armed strip, the input monitor. The host's MIDI
// thread pushes every message into ONE lock-free queue; the audio thread that runs drains it — the song's player
// while it plays, else the MONITOR: a player of its own over the song's mix with no clips and no automation, so an
// armed strip sounds as it will in the song (its rack, its sends, the master) while nothing plays. One consumer at
// a time: the monitor stops before the player starts and starts after it stops, and the queue is drained in
// between (what waited is stale).
#include "Compile.h"
#include "Engine.h"
#include "Format.h"
#include "SolarisService.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    namespace
    {
        std::vector<std::string> portsOf(const std::string &setting)
        {
            // all → every port (an empty list); none → no port (one that cannot match); else the names, comma-separated
            if (setting == "all") return {};
            if (setting == "none") return {"\x01none"};
            std::vector<std::string> out;
            std::string cur;
            std::stringstream ss(setting);
            while (std::getline(ss, cur, ','))
            {
                const size_t a = cur.find_first_not_of(' '), b = cur.find_last_not_of(' ');
                if (a != std::string::npos) out.push_back(cur.substr(a, b - a + 1));
            }
            return out;
        }
    }

    int SolarisService::armedIn(const std::vector<std::string> &strips) const
    {
        if (mArmed.empty()) return -1;
        const auto it = std::find(strips.begin(), strips.end(), mArmed);
        return it == strips.end() ? -1 : (int)(it - strips.begin());
    }

    void SolarisService::drainMidi()
    {
        if (!mMidiQ) return;
        MidiEvent e;
        while (mMidiQ->pop(e)) {}
    }

    bool SolarisService::ensureMidi(std::string &err)
    {
        if (!mHost.midiIn) { err = "this build has no MIDI input"; return false; }
        if (!mMidiQ) mMidiQ = std::make_shared<LockFreeQueue<MidiEvent>>(1024);
        if (mMidiIn && mMidiOpenFor == mSettings.midiInputs) return true;
        if (!mMidiIn) mMidiIn = mHost.midiIn();
        if (!mMidiIn) { err = "this build has no MIDI input"; return false; }
        auto q = mMidiQ;
        auto count = mMidiCount;
        if (!mMidiIn->open(portsOf(mSettings.midiInputs), [q, count](const MidiEvent &e) {
                // the host's thread: no lock — a full queue drops (nobody is draining it)
                q->push(e);
                count->fetch_add(1, std::memory_order_relaxed);
            }, err))
        {
            mMidiIn.reset();
            return false;
        }
        mMidiOpenFor = mSettings.midiInputs;
        return true;
    }

    // ── R-REC-1: the audio input ────────────────────────────────────────────────────────────────

    std::unique_ptr<IAudioIn> SolarisService::openInput(const Strip &s, int &channels, std::string &err)
    {
        // the strip's input port, on the device this machine's settings give it ("" the default input)
        if (!mHost.audioIn) { err = "this build cannot record audio (no capture)"; return nullptr; }
        const Port *pt = nullptr;
        for (const auto &x : mProject.ports)
            if (x.id == s.input) pt = &x;
        if (!pt) { err = s.id + " has no input — `set " + s.id + ".input=<port>`"; return nullptr; }
        channels = std::clamp(pt->channels, 1, 2);
        const std::string device = mSettings.portDevice(pt->name);
        if (mHost.audioInFor && mSettings.driver != "pulse") // R-DEV-9: the setting's driver first, PulseAudio the fallback
            if (auto in = mHost.audioInFor(mSettings.driver))
            {
                std::string why;
                if (in->open(device, mProject.header.sampleRate, channels, mSettings.bufferSize, why)) return in;
                emit(Event(Event::Kind::Info).with("text", "recording through PulseAudio: " + why));
            }
        auto in = mHost.audioIn();
        if (!in) { err = "this build cannot record audio (no capture)"; return nullptr; }
        if (!in->open(device, mProject.header.sampleRate, channels, mSettings.bufferSize, err)) return nullptr;
        return in;
    }

    bool SolarisService::monitorHears(const Strip &s, bool playing, bool recording) const
    {
        if (s.monitor == "on") return true;
        if (s.monitor == "off") return false;
        return !playing || recording; // auto: stopped, or recording — not while the song plays back
    }

    void SolarisService::attachInput(Player &pl, const std::vector<std::string> &strips, bool playing, bool recording)
    {
        // an armed audio line: its input read beside the output (before `start`), heard as its monitoring says
        mInputErr.clear();
        const Strip *s = mArmed.empty() ? nullptr : mProject.strip(mArmed);
        if (!s || s->kind != "audio") return;
        int channels = 2;
        auto in = openInput(*s, channels, mInputErr);
        if (!in) { emit(Event(Event::Kind::Info).with("text", "the input cannot be heard: " + mInputErr)); return; }
        pl.setInput(std::move(in), channels);
        pl.setMonitor(armedIn(strips), monitorHears(*s, playing, recording));
    }

    bool SolarisService::commitAudioTake(Player::Take take, double outLatency, double inLatency, long long graphLatency, std::string &err)
    {
        // R-REC-1: frame i of the take is what the song played at sample from + i − shift — it was heard Lo after it was
        // rendered (and the engine's own lag), and what was sung then arrived Li later
        mRecording = mRecordingAudio = false;
        Project &p = mProject;
        const double rate = p.header.sampleRate, spb = 60.0 / p.header.bpm * rate;
        const long long shift = std::llround((outLatency + inLatency) * rate) + graphLatency;
        // R-REC-2: the song looped while recording — a take of each pass over the loop
        if (mLoopTo > mLoopFrom && take.marks.size() > 1)
        {
            const double loopFrom = toTick(mLoopFrom), loopTo = toTick(mLoopTo);
            const long long A = std::llround(loopFrom * spb), L = std::llround(loopTo * spb) - A;
            std::map<int, std::pair<std::vector<float>, std::vector<float>>> passes;
            std::map<int, long long> covered;
            for (size_t m = 0; m < take.marks.size(); ++m)
            {
                const long long i0 = take.marks[m].first, i1 = m + 1 < take.marks.size() ? take.marks[m + 1].first : (long long)take.left.size();
                for (long long i = i0; i < i1; ++i)
                {
                    long long h = take.marks[m].second + (i - i0) - shift; // where it was HEARD
                    int pass = (int)m;
                    if (h < A && m > 0) { h += L; pass = (int)m - 1; } // just after the loop turned: the pass before's end
                    if (h < A || h >= A + L) continue;                // before the loop (a pre-roll): no pass of it
                    auto &buf = passes[pass];
                    if (buf.first.empty()) { buf.first.assign((size_t)L, 0.0f); buf.second.assign((size_t)L, 0.0f); }
                    buf.first[(size_t)(h - A)] = take.left[(size_t)i];
                    buf.second[(size_t)(h - A)] = take.right[(size_t)i];
                    ++covered[pass];
                }
            }
            std::vector<std::vector<std::vector<float>>> kept; // a pass that heard less than half the loop is dropped
            for (auto &kv : passes)
                if (covered[kv.first] * 2 >= L) kept.push_back({std::move(kv.second.first), std::move(kv.second.second)});
            if (kept.size() >= 2)
            {
                const Strip *s = p.strip(mRecordStrip);
                const std::string strip = s ? s->name : mRecordStrip;
                const auto slash = mPath.find_last_of('/');
                const std::string dir = (slash == std::string::npos ? std::string(".") : mPath.substr(0, slash)) + "/Recordings";
                std::set<std::string> there;
                std::vector<BrowserEntry> entries;
                std::string listErr;
                if (mHost.listDir && mHost.listDir(dir, entries, listErr))
                    for (const auto &e : entries) there.insert(e.name);
                for (const auto &cl : p.clips)
                {
                    there.insert(cl.src.substr(cl.src.find_last_of('/') + 1));
                    for (const auto &t : cl.takes) there.insert(t.src.substr(t.src.find_last_of('/') + 1));
                }
                if (!mHost.writeWav) { err = "this build cannot write a recording"; return false; }
                std::vector<Take> takes;
                int k = 1;
                for (const auto &ch : kept)
                {
                    while (there.count(strip + " " + std::to_string(k) + ".wav")) ++k;
                    const std::string name = strip + " " + std::to_string(k) + ".wav";
                    there.insert(name);
                    if (!mHost.writeWav(dir + "/" + name, ch, (int)rate, 32, err)) { if (err.empty()) err = "cannot write " + dir + "/" + name; return false; }
                    mPcm.erase(resolvePath(dir + "/" + name)); // a file of that name decoded before (an undone take) is not this one
                    takes.push_back(Take{relativePath(dir + "/" + name), {}}); // beside the song, as `clip add` keeps a source
                }
                Command cc;
                cc.kind = K::ClipAdd;
                cc.flags = {{"src", resolvePath(takes.back().src)}, {"strip", mRecordStrip}, {"at", canonicalBeats(loopFrom)}};
                if (!clipCommand(cc, err)) return false;
                const std::string clip = mOutput.substr(0, mOutput.find('\n'));
                Clip *cl = p.clip(clip);
                if (!cl) { err = "the take's clip is gone"; return false; }
                cl->takes = takes; // R-REC-2: every pass a take; the last plays where the comp does not say
                cl->length = toTick(loopTo - loopFrom);
                changed("clip.takes", clip);
                const std::string secs = canonicalNumber(std::round((double)L / rate * 1000.0) / 1000.0);
                mOutput = clip + "\nrecorded " + std::to_string(takes.size()) + " takes of " + secs + " s\n";
                emit(Event(Event::Kind::RecordFinished).with("clip", clip).with("notes", 0).with("seconds", secs).with("takes", (int)takes.size()));
                return true;
            }
            if (kept.size() == 1) // one whole pass: a clip of it, as a recording with no loop
            {
                take.left = std::move(kept[0][0]);
                take.right = std::move(kept[0][1]);
                take.from = A + shift; // so the take's first frame maps to the loop's start below
                take.marks.clear();
                mRecordFrom = loopFrom;
            }
        }
        const long long first = take.from - shift, n = (long long)take.left.size();
        double fromBeat = mRecordFrom, toBeat = -1.0;
        if (mPunchTo > mPunchFrom) { fromBeat = std::max(fromBeat, mPunchFrom); toBeat = mPunchTo; }
        const double at = toTick(fromBeat);                 // the clip's place, on the tick grid …
        const long long a = std::llround(at * spb);          // … and the take cut from that very sample
        const long long b = toBeat >= 0 ? std::llround(toBeat * spb) : first + n;
        std::vector<std::vector<float>> ch(2);
        if (take.from >= 0 && b > a)
            for (long long t = a; t < std::min(b, first + n); ++t)
            {
                const long long i = t - first; // before the first frame read: silence (the take began a hair late)
                ch[0].push_back(i >= 0 ? take.left[(size_t)i] : 0.0f);
                ch[1].push_back(i >= 0 ? take.right[(size_t)i] : 0.0f);
            }
        if (take.dropped > 0)
            emit(Event(Event::Kind::Info).with("text", "the recorder fell behind: " + std::to_string(take.dropped) + " frames of the input were lost"));
        if (ch[0].empty())
        {
            mOutput = "nothing was recorded\n";
            emit(Event(Event::Kind::RecordFinished).with("clip", "").with("notes", 0).with("seconds", "0"));
            return true;
        }
        // beside the song: <folder>/Recordings/<strip> <n>.wav — the next number no file there has
        const Strip *s = p.strip(mRecordStrip);
        const std::string strip = s ? s->name : mRecordStrip;
        const auto slash = mPath.find_last_of('/');
        const std::string dir = (slash == std::string::npos ? std::string(".") : mPath.substr(0, slash)) + "/Recordings";
        std::set<std::string> there;
        std::vector<BrowserEntry> entries;
        std::string listErr;
        if (mHost.listDir && mHost.listDir(dir, entries, listErr))
            for (const auto &e : entries) there.insert(e.name);
        for (const auto &cl : p.clips) there.insert(cl.src.substr(cl.src.find_last_of('/') + 1)); // an undone take's name too
        int k = 1;
        while (there.count(strip + " " + std::to_string(k) + ".wav")) ++k;
        const std::string name = strip + " " + std::to_string(k) + ".wav";
        if (!mHost.writeWav) { err = "this build cannot write a recording"; return false; }
        if (!mHost.writeWav(dir + "/" + name, ch, (int)rate, 32, err)) { if (err.empty()) err = "cannot write " + dir + "/" + name; return false; }
        mPcm.erase(resolvePath(dir + "/" + name)); // a file of that name decoded before (an undone take) is not this one
        Command cc;
        cc.kind = K::ClipAdd;
        cc.flags = {{"src", dir + "/" + name}, {"strip", mRecordStrip}, {"at", canonicalBeats(at)}}; // `clip add` keeps it relative to the song
        if (!clipCommand(cc, err)) return false;
        const std::string clip = mOutput.substr(0, mOutput.find('\n'));
        const std::string secs = canonicalNumber(std::round((double)ch[0].size() / rate * 1000.0) / 1000.0);
        mOutput = clip + "\nrecorded " + secs + " s into Recordings/" + name + "\n";
        emit(Event(Event::Kind::RecordFinished).with("clip", clip).with("notes", 0).with("seconds", secs));
        return true;
    }

    void SolarisService::stopMonitor()
    {
        mMonitor.reset(); // its thread joins; its engine goes with it
        mMonitorStrips.clear();
        drainMidi();
    }

    void SolarisService::startMonitor()
    {
        // armed, open, nothing playing: the strip heard through the song's mix — no clip, no automation, no metronome
        if (!mOpen || mArmed.empty() || (mPlayer && mPlayer->running())) return;
        const Strip *armed = mProject.strip(mArmed);
        const bool audio = armed && armed->kind == "audio"; // R-REC-1: its input, unless its monitoring is off
        if (audio ? (!mHost.audioIn || armed->monitor == "off" || armed->input.empty()) : !mMidiQ) return;
        stopMonitor();
        Project p = mProject;
        p.clips.clear();
        p.bindings.clear();
        CompileResult cr = compile(p, [this](const std::string &src) { return pcmFor(src); });
        auto eng = std::make_unique<engine::Engine>();
        std::string err;
        if (!eng->build(cr.graph, err)) return;
        std::vector<Player::Route> routes;
        for (size_t i = 0; i < eng->graph().ports.size(); ++i)
        {
            const std::string dev = mSettings.portDevice(eng->graph().ports[i].name);
            if (!dev.empty() && dev != mSettings.output) continue;
            routes.push_back(Player::Route{(int)i, 0});
        }
        std::string driver;
        auto out = openOutput(mProject.header.sampleRate, mSettings.bufferSize, driver, err);
        if (!out) { emit(Event(Event::Kind::Info).with("text", "the input monitor cannot play: " + err)); return; }
        mMonitorStrips = cr.stripIds;
        mMonitor = std::make_unique<Player>();
        if (audio) attachInput(*mMonitor, mMonitorStrips, false, false);
        else
        {
            mMonitor->setMidi(mMidiQ.get());
            mMonitor->setArmed(armedIn(mMonitorStrips));
        }
        mMonitor->start(std::move(out), std::move(eng), routes, 2, mSettings.bufferSize, 0, 0);
    }

    bool SolarisService::commitRecording(long long stoppedAt, std::string &err)
    {
        // R-MIDI-2: the take — ons paired with offs (an on again first ends the last), where each was HEARD; half a
        // beat early is the downbeat (a count-in's anticipation); starts on the `recordQuantize` grid
        mRecording = false;
        Project &p = mProject;
        const double spb = 60.0 / p.header.bpm * p.header.sampleRate, from = mRecordFrom;
        struct Open
        {
            double at = 0;
            int vel = 0;
            bool on = false;
        };
        std::array<Open, 128> open{};
        std::vector<Note> notes;
        auto close = [&](int pitch, double end) {
            Open &o = open[(size_t)pitch];
            if (!o.on) return;
            Note n;
            n.pitch = pitch;
            n.vel = std::clamp(o.vel, 1, 127);
            n.at = o.at;
            n.length = std::max(1.0 / kPpq, end - o.at);
            notes.push_back(n);
            o.on = false;
        };
        std::vector<ExprPoint> wheels; // R-MIDI-3: the keyboard's wheels, where they moved — thinned
        std::map<std::string, std::pair<double, double>> lastKept;   // lane → (beat, value)
        for (const auto &pl : mTaken)
        {
            const double b = (double)pl.at / spb;
            if (pl.kind != 0)
            {
                if (b < from - 0.5) continue;
                const std::string lane = pl.kind == 1 ? "bend" : "cc1";
                const auto it = lastKept.find(lane);
                // a point where the wheel moved by more than a hair, or a sixteenth on since the last kept
                if (it != lastKept.end() && std::fabs(pl.value - it->second.second) < 0.005 && b - it->second.first < 0.25) continue;
                lastKept[lane] = {b, pl.value};
                wheels.push_back(ExprPoint{lane, std::max(from, b), pl.value, {}});
                continue;
            }
            const int pitch = std::clamp(pl.pitch, 0, 127);
            close(pitch, b);
            if (pl.velocity > 0 && b >= from - 0.5) open[(size_t)pitch] = Open{std::max(from, b), pl.velocity, true};
        }
        // each wheel's LAST move is kept, however small — where it came to rest
        const Player::Played *lastOf[2] = {nullptr, nullptr};
        for (const auto &pl : mTaken)
            if (pl.kind == 1 || pl.kind == 2) lastOf[pl.kind - 1] = &pl;
        for (int k = 0; k < 2; ++k)
        {
            if (!lastOf[k] || (double)lastOf[k]->at / spb < from - 0.5) continue;
            const std::string lane = k == 0 ? "bend" : "cc1";
            const double b = std::max(from, (double)lastOf[k]->at / spb);
            const auto it = lastKept.find(lane);
            if (it == lastKept.end() || std::fabs(it->second.first - b) > 1e-9) wheels.push_back(ExprPoint{lane, b, lastOf[k]->value, {}});
        }
        for (int pitch = 0; pitch < 128; ++pitch) close(pitch, std::max(from, (double)stoppedAt / spb)); // still held: to the stop
        mTaken.clear();
        for (auto &w : wheels) w.at = toTick(w.at);
        if (notes.empty() && wheels.empty())
        {
            mOutput = "nothing was played\n";
            emit(Event(Event::Kind::RecordFinished).with("clip", "").with("notes", 0));
            return true;
        }
        const std::string &q = mSettings.recordQuantize;
        const double grid = q == "1/4" ? 1.0 : q == "1/8" ? 0.5 : q == "1/16" ? 0.25 : q == "1/32" ? 0.125 : 0.0;
        for (auto &n : notes)
        {
            if (grid > 0.0) n.at = std::max(0.0, std::round(n.at / grid) * grid);
            n.at = toTick(n.at);
            n.length = std::max(1.0 / kPpq, toTick(n.length));
        }
        std::stable_sort(notes.begin(), notes.end(), [](const Note &a, const Note &b) { return a.at < b.at || (a.at == b.at && a.pitch < b.pitch); });
        const double bar = std::max(1, std::atoi(p.header.sig.c_str()));
        // --overdub: into the strip's clip under the start (its pattern, wrapped where the clip repeats it)
        if (mOverdub)
            for (const auto &cl : p.clips)
            {
                if (cl.track != mRecordStrip || cl.pattern.empty()) continue;
                const double len = clipLengthBeats(p, cl);
                if (from < cl.at - 1e-9 || from >= cl.at + len - 1e-9) continue;
                Pattern *pt = nullptr;
                for (auto &x : p.patterns)
                    if (x.id == cl.pattern) pt = &x;
                if (!pt) break;
                std::map<std::pair<long long, int>, Note> merged; // a pitch struck where one already starts: the longer
                for (const auto &n : pt->notes) merged[{std::llround(n.at * kPpq), n.pitch}] = n;
                for (auto n : notes)
                {
                    double rel = n.at - cl.at;
                    if (pt->length > 0 && rel >= pt->length) rel = std::fmod(rel, pt->length);
                    n.at = toTick(std::max(0.0, rel));
                    const auto key = std::make_pair(std::llround(n.at * kPpq), n.pitch);
                    auto it = merged.find(key);
                    if (it == merged.end() || it->second.length < n.length) merged[key] = n;
                }
                pt->notes.clear();
                for (const auto &kv : merged) pt->notes.push_back(kv.second);
                std::stable_sort(pt->notes.begin(), pt->notes.end(), [](const Note &a, const Note &b) { return a.at < b.at || (a.at == b.at && a.pitch < b.pitch); });
                // R-MIDI-3: a wheel moved in the take writes its lane over the take's span — the old points there go
                std::map<std::string, std::pair<double, double>> span;
                std::vector<ExprPoint> moved;
                for (auto w : wheels)
                {
                    double rel = w.at - cl.at;
                    if (pt->length > 0 && rel >= pt->length) rel = std::fmod(rel, pt->length);
                    w.at = toTick(std::max(0.0, rel));
                    auto it = span.find(w.lane);
                    if (it == span.end()) span[w.lane] = {w.at, w.at};
                    else it->second = {std::min(it->second.first, w.at), std::max(it->second.second, w.at)};
                    moved.push_back(w);
                }
                pt->expr.erase(std::remove_if(pt->expr.begin(), pt->expr.end(), [&](const ExprPoint &x) {
                                   const auto it = span.find(x.lane);
                                   return it != span.end() && x.at >= it->second.first - 1e-9 && x.at <= it->second.second + 1e-9;
                               }), pt->expr.end());
                pt->expr.insert(pt->expr.end(), moved.begin(), moved.end());
                std::stable_sort(pt->expr.begin(), pt->expr.end(), [](const ExprPoint &a, const ExprPoint &b) { return a.lane < b.lane || (a.lane == b.lane && a.at < b.at); });
                changed("note.added", pt->id);
                mOutput = cl.id + "\n";
                emit(Event(Event::Kind::RecordFinished).with("clip", cl.id).with("notes", (int)notes.size()));
                return true;
            }
        // a new clip on its track, from the bar the take began in, whole bars long
        const double at = std::floor(from / bar + 1e-9) * bar;
        double end = at;
        Pattern pt;
        pt.id = p.nextId("pt");
        const Strip *s = p.strip(mRecordStrip);
        pt.name = (s ? s->name : mRecordStrip) + " take";
        for (auto n : notes)
        {
            n.at = toTick(n.at - at);
            pt.notes.push_back(n);
            end = std::max(end, at + n.at + n.length);
        }
        for (auto w : wheels) // R-MIDI-3: the wheels as the take's lanes
        {
            w.at = toTick(w.at - at);
            pt.expr.push_back(w);
            end = std::max(end, at + w.at);
        }
        std::stable_sort(pt.expr.begin(), pt.expr.end(), [](const ExprPoint &a, const ExprPoint &b) { return a.lane < b.lane || (a.lane == b.lane && a.at < b.at); });
        pt.length = std::max(bar, std::ceil((end - at) / bar - 1e-9) * bar);
        p.patterns.push_back(pt);
        changed("pattern.added", pt.id);
        Command cc;
        cc.kind = K::ClipAdd;
        cc.flags = {{"strip", mRecordStrip}, {"pattern", pt.id}, {"at", canonicalBeats(at)}};
        if (!clipCommand(cc, err)) return false;
        const std::string clip = mOutput.substr(0, mOutput.find('\n'));
        emit(Event(Event::Kind::RecordFinished).with("clip", clip).with("notes", (int)notes.size()));
        return true;
    }

    bool SolarisService::inputCommand(const Command &c, std::string &err)
    {
        switch (c.kind)
        {
        case K::PortAdd:
        {
            // R-DEV-3, R-REC-1: a logical port — the machine's settings say which device it is
            if (!requireOpen(err)) return false;
            Port pt;
            pt.id = mProject.nextId("prt");
            pt.name = c.arg(0);
            pt.dir = c.flag("dir", "out");
            if (pt.dir != "in" && pt.dir != "out") { err = "--dir is in or out"; return false; }
            double chans = 2;
            if (c.has("channels") && (!parseNumber(c.flag("channels"), chans) || (chans != 1 && chans != 2))) { err = "--channels is 1 or 2"; return false; }
            pt.channels = (int)chans;
            for (const auto &x : mProject.ports)
                if (x.name == pt.name) { err = "a port is already called `" + pt.name + "` (" + x.id + ")"; return false; }
            pt.order = (int)mProject.ports.size();
            mProject.ports.push_back(pt);
            changed("port.added", pt.id);
            mOutput = pt.id + "\n";
            return true;
        }
        case K::MidiDevices:
        {
            if (!mHost.listMidiInputs) { err = "this build has no MIDI input"; return false; }
            std::vector<MidiPort> ports;
            if (!mHost.listMidiInputs(ports, err)) return false;
            const std::vector<std::string> on = mMidiIn ? mMidiIn->listening() : std::vector<std::string>{};
            mModel.midiDevices.clear();
            mOutput.clear();
            for (const auto &pt : ports)
            {
                const bool listening = std::find(on.begin(), on.end(), pt.id) != on.end();
                mModel.midiDevices.push_back(MidiPortModel{pt.id, pt.name, listening});
                mOutput += pt.id + (listening ? "  (listening)" : "") + "\n";
            }
            mMidiPorts = mModel.midiDevices;
            if (ports.empty()) mOutput = "no MIDI inputs\n";
            return true;
        }
        case K::StripArm:
        {
            const std::string ch = c.arg(0);
            if (ch == "none")
            {
                mArmed.clear();
                if (mPlayer) mPlayer->setArmed(-1);
                stopMonitor();
                mModel.armed.clear();
                emit(Event(Event::Kind::StripArmed).with("strip", ""));
                return true;
            }
            if (!requireOpen(err)) return false;
            const Strip *s = mProject.strip(ch);
            if (!s) { err = "no strip `" + ch + "`"; return false; }
            if (s->kind == "bus") { err = ch + " is a bus: arm an instrument strip (the keyboard) or an audio line (its input)"; return false; }
            if (s->kind == "audio")
            {
                // R-REC-1: an audio line armed for its input — set here when --input names one (that part an edit)
                if (c.has("input"))
                {
                    std::string stored;
                    if (!setAddress(ch + ".input", c.flag("input"), stored, err)) return false;
                    mPending.push_back(Event(Event::Kind::ParamsChanged).with("address", ch + ".input").with("value", stored));
                    s = mProject.strip(ch);
                }
                if (s->input.empty())
                {
                    std::string ins;
                    for (const auto &x : mProject.ports)
                        if (x.dir == "in") ins += (ins.empty() ? "" : ", ") + x.id;
                    err = ch + " has no input: `strip arm " + ch + " --input <port>` (" + (ins.empty() ? std::string("the song has none — `port add <name> --dir in`") : "the song's: " + ins) + ")";
                    return false;
                }
                if (!mHost.audioIn) { err = "this build cannot record audio (no capture)"; return false; }
                if (mPlayer) mPlayer->setArmed(-1);
                mArmed = ch;
                mModel.armed = ch;
                if (!(mPlayer && mPlayer->running())) startMonitor(); // playing: its input is heard from the next play
                emit(Event(Event::Kind::StripArmed).with("strip", ch));
                return true;
            }
            if (c.has("input")) { err = "--input is an audio line's: " + ch + " plays a keyboard"; return false; }
            if (!ensureMidi(err)) return false;
            mArmed = ch;
            mModel.armed = ch;
            if (mPlayer && mPlayer->running()) mPlayer->setArmed(armedIn(mLiveStrips));
            else startMonitor();
            emit(Event(Event::Kind::StripArmed).with("strip", ch));
            return true;
        }
        default:
            return false;
        }
    }
}
}
