// SolarisService — a control surface (R-CTRL-2; docs/discussion.md Round 10). Mackie Control or HUI on a port of its
// own, in and out. What it moves goes through the grammar as a drag would (landed as the MIDI controls' lines are);
// what it shows is the model as it stands, sent as a diff of what it was last sent — a fader a hand is on left to it.
#include "FaderLaw.h"
#include "Format.h"
#include "SolarisService.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>

namespace arstro
{
namespace solaris
{
    void SolarisService::stopSurface()
    {
        if (mSurfaceIn) mSurfaceIn->close();
        if (mSurfaceOut) mSurfaceOut->close();
        mSurfaceIn.reset();
        mSurfaceOut.reset();
        mSurfaceCodec.reset();
        mSurfaceQ.reset();
    }

    void SolarisService::startSurface()
    {
        const bool was = mSurfaceIn != nullptr;
        stopSurface();
        mSurfaceFor = mSettings.surface + "|" + mSettings.surfacePort;
        mSurfaceWhy.clear();
        mSurfaceBank = 0;
        auto said = [&] {
            refreshModel();
            emit(Event(Event::Kind::SurfaceChanged).with("protocol", mSettings.surface).with("connected", mSurfaceIn ? "1" : "0")
                     .with("bank", (long long)mSurfaceBank).with("why", mSurfaceWhy));
        };
        if (mSettings.surface == "none") { if (was) said(); return; }
        std::string err;
        if (mSettings.surfacePort.empty()) mSurfaceWhy = "no port — `settings set surfacePort=<port>`";
        else if (!mHost.midiIn || !mHost.midiOut) mSurfaceWhy = "this build has no MIDI output";
        else
        {
            mSurfaceCodec = makeSurfaceCodec(mSettings.surface);
            mSurfaceQ = std::make_shared<LockFreeQueue<MidiEvent>>(1024);
            auto in = mHost.midiIn();
            auto out = mHost.midiOut();
            auto q = mSurfaceQ;
            if (!in || !in->open({mSettings.surfacePort}, [q](const MidiEvent &e) { q->push(e); }, err)) mSurfaceWhy = "its port will not listen: " + err;
            else if (!out || !out->open(mSettings.surfacePort, err)) { in->close(); mSurfaceWhy = "its port will not take output: " + err; }
            else
            {
                mSurfaceIn = std::move(in);
                mSurfaceOut = std::move(out);
                mSurfaceAll = true; // everything, as it connects — and HUI's ping at once
                mSurfacePing = 0.0;
                mSurfaceTouched.fill(false);
                mSurfaceShown = SurfaceState();
            }
        }
        if (!mSurfaceIn) mSurfaceCodec.reset();
        // the keyboard, listening again without the surface's port
        if (mMidiIn && mMidiOpenFor != midiFor())
        {
            std::string e;
            ensureMidi(e);
        }
        said();
    }

    SurfaceState SolarisService::surfaceState() const
    {
        SurfaceState s;
        if (!mOpen) return s;
        for (int i = 0; i < kSurfaceStrips; ++i)
        {
            const int k = mSurfaceBank + i;
            if (k >= (int)mModel.strips.size()) break;
            const StripModel &m = mModel.strips[(size_t)k];
            SurfaceStrip &x = s.strips[(size_t)i];
            x.present = true;
            x.fader = faderPos(m.gain);
            x.pan = m.pan;
            x.mute = m.mute;
            x.solo = m.solo;
            x.arm = mArmed == m.id;
            x.name = m.name;
        }
        s.master = faderPos(mModel.masterGain);
        s.playing = mModel.transport.playing;
        s.recording = mModel.transport.recording;
        return s;
    }

    void SolarisService::surfacePump()
    {
        if (mSurfaceFor != mSettings.surface + "|" + mSettings.surfacePort) startSurface(); // the settings changed (or the first pump)
        if (!mSurfaceIn) return;
        // what it moved — lines, as a drag's
        std::map<std::string, std::string> moved;
        std::map<std::string, double> pans; // a pot turned twice in one pump turns from where the first left it
        const int count = (int)mModel.strips.size();
        int bank = mSurfaceBank;
        MidiEvent m;
        SurfaceEvent e;
        while (mSurfaceQ->pop(m))
        {
            if (!mSurfaceCodec->decode(m, e)) continue;
            const int k = mSurfaceBank + e.strip;
            const StripModel *s = (k >= 0 && k < count) ? &mModel.strips[(size_t)k] : nullptr;
            switch (e.kind)
            {
            case SurfaceEvent::Fader:
                if (s && mOpen) moved[s->id + ".gain"] = canonicalNumber(faderDb(e.value));
                break;
            case SurfaceEvent::Master:
                if (mOpen) moved["project.masterGain"] = canonicalNumber(std::min(12.0, faderDb(e.value)));
                break;
            case SurfaceEvent::Pot:
                if (s && mOpen)
                {
                    const std::string a = s->id + ".pan";
                    const double from = pans.count(a) ? pans[a] : s->pan;
                    const double to = std::clamp(std::round((from + 0.02 * e.value) * 100.0) / 100.0, -1.0, 1.0);
                    pans[a] = to;
                    moved[a] = canonicalNumber(to);
                }
                break;
            case SurfaceEvent::Mute: if (s) mControlLines.push_back("set " + s->id + ".mute=" + (s->mute ? "false" : "true")); break;
            case SurfaceEvent::Solo: if (s) mControlLines.push_back("set " + s->id + ".solo=" + (s->solo ? "false" : "true")); break;
            case SurfaceEvent::Arm: if (s) mControlLines.push_back("strip arm " + (mArmed == s->id ? std::string("none") : s->id)); break;
            case SurfaceEvent::Touch: if (e.strip >= 0 && e.strip <= kSurfaceStrips) mSurfaceTouched[(size_t)e.strip] = true; break;
            case SurfaceEvent::Release:
                if (e.strip >= 0 && e.strip <= kSurfaceStrips)
                {
                    mSurfaceTouched[(size_t)e.strip] = false;
                    if (e.strip < kSurfaceStrips) mSurfaceShown.strips[(size_t)e.strip].fader = -1.0; // let go: where the song has it
                    else mSurfaceShown.master = -1.0;
                }
                break;
            case SurfaceEvent::Play: mControlLines.push_back("transport play"); break;
            case SurfaceEvent::Stop: mControlLines.push_back("transport stop"); break;
            case SurfaceEvent::Record: mControlLines.push_back("transport record"); break;
            case SurfaceEvent::Rewind:
            case SurfaceEvent::Forward:
            {
                const double bar = std::max(1, std::atoi(mProject.header.sig.c_str())), at = mModel.transport.position;
                const double to = e.kind == SurfaceEvent::Forward ? std::floor(at / bar + 1e-9) * bar + bar : std::max(0.0, std::ceil(at / bar - 1e-9) * bar - bar);
                mControlLines.push_back("transport seek " + canonicalNumber(to));
                break;
            }
            case SurfaceEvent::BankLeft: bank = std::max(0, bank - kSurfaceStrips); break;
            case SurfaceEvent::BankRight: if (bank + kSurfaceStrips < count) bank += kSurfaceStrips; break;
            case SurfaceEvent::ChannelLeft: bank = std::max(0, bank - 1); break;
            case SurfaceEvent::ChannelRight: if (bank + 1 < count) bank += 1; break;
            default: break;
            }
        }
        if (!moved.empty())
        {
            std::string line = "set";
            for (const auto &x : moved) line += " " + x.first + "=" + x.second;
            mControlLines.push_back(line);
        }
        if (bank != mSurfaceBank)
        {
            mSurfaceBank = bank; // another eight: their faders, lights and names follow below
            refreshModel();
            emit(Event(Event::Kind::SurfaceChanged).with("protocol", mSettings.surface).with("connected", "1").with("bank", (long long)mSurfaceBank).with("why", ""));
        }
        // what it shows — the model as it stands, as a diff of what it was sent
        const SurfaceState now = surfaceState();
        std::vector<uint8_t> out;
        mSurfaceCodec->encode(mSurfaceShown, now, mSurfaceAll, mSurfaceTouched, out);
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (t - mSurfacePing >= 1.0)
        {
            mSurfaceCodec->keepAlive(out); // HUI's ping, once a second
            mSurfacePing = t;
        }
        if (!out.empty()) mSurfaceOut->send(out);
        mSurfaceShown = now;
        mSurfaceAll = false;
    }
}
}
