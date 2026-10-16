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
#include <cmath>
#include <map>
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

    void SolarisService::stopMonitor()
    {
        mMonitor.reset(); // its thread joins; its engine goes with it
        mMonitorStrips.clear();
        drainMidi();
    }

    void SolarisService::startMonitor()
    {
        // armed, open, nothing playing: the strip heard through the song's mix — no clip, no automation, no metronome
        if (!mOpen || mArmed.empty() || !mMidiQ || (mPlayer && mPlayer->running())) return;
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
        mMonitor->setMidi(mMidiQ.get());
        mMonitor->setArmed(armedIn(mMonitorStrips));
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
            if (s->kind != "instrument") { err = ch + " is " + (s->kind == "bus" ? "a bus" : "an audio line") + ": only an instrument strip plays a keyboard"; return false; }
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
