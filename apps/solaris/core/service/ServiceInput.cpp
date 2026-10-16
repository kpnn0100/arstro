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
        for (const auto &pl : mTaken)
        {
            const double b = (double)pl.at / spb;
            const int pitch = std::clamp(pl.pitch, 0, 127);
            close(pitch, b);
            if (pl.velocity > 0 && b >= from - 0.5) open[(size_t)pitch] = Open{std::max(from, b), pl.velocity, true};
        }
        for (int pitch = 0; pitch < 128; ++pitch) close(pitch, std::max(from, (double)stoppedAt / spb)); // still held: to the stop
        mTaken.clear();
        if (notes.empty())
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
