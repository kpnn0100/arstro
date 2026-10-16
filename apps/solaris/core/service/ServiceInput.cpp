// SolarisService — the MIDI keyboard (R-MIDI-2): its ports, the armed strip, the input monitor. The host's MIDI
// thread pushes every message into ONE lock-free queue; the audio thread that runs drains it — the song's player
// while it plays, else the MONITOR: a player of its own over the song's mix with no clips and no automation, so an
// armed strip sounds as it will in the song (its rack, its sends, the master) while nothing plays. One consumer at
// a time: the monitor stops before the player starts and starts after it stops, and the queue is drained in
// between (what waited is stale).
#include "Compile.h"
#include "Engine.h"
#include "SolarisService.h"
#include <algorithm>
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
