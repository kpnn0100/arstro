// SolarisService — live sound (R-PLAY-1/2/3, R-TIME-4): the transport, the player thread on the
// clock device, and what an edit does while it plays. A gain, a pan, a mute, a solo, a device
// parameter or bypass, the master gain go to the player as lock-free messages and are heard at
// the next block; anything structural (a clip, a route, a device added) compiles a new engine on
// THIS thread and swaps it in at the same position (DR-PLAY-1).
#include "Compile.h"
#include "Engine.h"
#include "Format.h"
#include "MixLaws.h"
#include "Player.h"
#include "SolarisService.h"
#include "device/Device.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    SolarisService::~SolarisService() { stopPlayer(); }

    SolarisService::SolarisService(const SolarisService &o)
        : mHost(o.mHost), mProject(o.mProject), mOpen(o.mOpen), mPath(o.mPath), mModel(o.mModel), mOutput(o.mOutput),
          mSinks(), mPcm(o.mPcm), mOffline(o.mOffline), mPcmRate(o.mPcmRate), mAudit(o.mAudit),
          mLastRenderPeaks(o.mLastRenderPeaks), mSettings(o.mSettings), mRecents(o.mRecents), mRecentCards(o.mRecentCards),
          mRecentsStale(o.mRecentsStale), mDevices(o.mDevices), mBrowser(o.mBrowser), mPosition(o.mPosition),
          mLoopFrom(o.mLoopFrom), mLoopTo(o.mLoopTo)
    {
    }

    void SolarisService::stopPlayer()
    {
        if (!mPlayer) return;
        if (mPlayer->running() && mOpen)
            mPosition = (double)mPlayer->heard() / (60.0 / mProject.header.bpm * mProject.header.sampleRate);
        mPlayer.reset();
        mLiveStrips.clear();
        mLiveDevices.clear();
    }

    bool SolarisService::buildLive(std::unique_ptr<engine::Engine> &out, std::string &err)
    {
        CompileResult cr = compile(mProject, [this](const std::string &src) { return pcmFor(src); });
        auto eng = std::make_unique<engine::Engine>();
        if (!eng->build(cr.graph, err)) return false;
        mLiveStrips = cr.stripIds;
        mLiveDevices = cr.devices;
        out = std::move(eng);
        return true;
    }

    void SolarisService::pump()
    {
        if (!mPlayer) return;
        mPlayer->collect();
        TransportModel &t = mModel.transport;
        t.playing = mPlayer->running();
        const double spb = 60.0 / mProject.header.bpm * mProject.header.sampleRate;
        t.position = (double)mPlayer->heard() / spb;
        t.latencyMs = std::round(mPlayer->latency() * 10000.0) / 10.0;
        t.masterPeak[0] = mPlayer->peak(2 * Player::kMaxStrips);
        t.masterPeak[1] = mPlayer->peak(2 * Player::kMaxStrips + 1);
        for (auto &s : mModel.strips)
            for (size_t i = 0; i < mLiveStrips.size() && i < (size_t)Player::kMaxStrips; ++i)
                if (mLiveStrips[i] == s.id)
                {
                    s.peak[0] = mPlayer->peak((int)(2 * i));
                    s.peak[1] = mPlayer->peak((int)(2 * i + 1));
                }
    }

    bool SolarisService::transportCommand(const Command &c, std::string &err)
    {
        auto announce = [this]() {
            pump();
            const bool playing = mPlayer && mPlayer->running();
            emit(Event(Event::Kind::TransportChanged).with("playing", playing ? "1" : "0")
                     .with("position", canonicalBeats(playing ? mModel.transport.position : mPosition))
                     .with("loop", mLoopTo > mLoopFrom ? canonicalBeats(mLoopFrom) + "-" + canonicalBeats(mLoopTo) : std::string("off")));
        };
        const double spb = mOpen ? 60.0 / mProject.header.bpm * mProject.header.sampleRate : 1.0;
        double x = 0;
        switch (c.kind)
        {
        case K::Wait:
        {
            if (!parseNumber(c.arg(0), x) || x < 0 || x > 3600) { err = "wait takes seconds, 0 … 3600"; return false; }
            const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(x);
            while (std::chrono::steady_clock::now() < until)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                pump();
            }
            pump();
            return true;
        }
        case K::TransportPlay:
        {
            if (!requireOpen(err)) return false;
            if (!mHost.audioOut) { err = "this build cannot play audio (no output stream)"; return false; }
            if (c.has("from"))
            {
                if (!parseNumber(c.flag("from"), x) || x < 0) { err = "--from must be beats ≥ 0"; return false; }
                mPosition = toTick(x);
            }
            stopPlayer();
            std::unique_ptr<engine::Engine> eng;
            if (!buildLive(eng, err)) return false;
            // R-DEV-3: a port plays where this machine's settings put it; on the clock device here (P1).
            std::vector<Player::Route> routes;
            const auto &ports = eng->graph().ports;
            for (size_t i = 0; i < ports.size(); ++i)
            {
                const std::string dev = mSettings.portDevice(ports[i].name);
                if (!dev.empty() && dev != mSettings.output) continue; // another device: P2's followers
                int channel = 0;
                for (const auto &p : mSettings.ports)
                    if (p.first == ports[i].name) channel = std::atoi(p.second.substr(p.second.rfind(':') + 1).c_str());
                routes.push_back(Player::Route{(int)i, channel});
            }
            auto out = mHost.audioOut();
            if (!out || !out->open(mSettings.output, mProject.header.sampleRate, 2, mSettings.bufferSize, err))
            {
                if (err.empty()) err = "cannot open the output device";
                return false;
            }
            mPlayer = std::make_unique<Player>();
            mPlayer->setLoop(mLoopTo > mLoopFrom ? (long long)std::llround(mLoopFrom * spb) : 0,
                             mLoopTo > mLoopFrom ? (long long)std::llround(mLoopTo * spb) : 0);
            mModel.transport.device = mSettings.output;
            mPlayer->setTempo(spb, std::max(1, std::atoi(mProject.header.sig.c_str())));
            mPlayer->setClick(mSettings.metronome, engine::dbToLinear(mSettings.metronomeLevel));
            mPlayer->start(std::move(out), std::move(eng), routes, 2, mSettings.bufferSize, (long long)std::llround(mPosition * spb));
            announce();
            return true;
        }
        case K::TransportStop:
            stopPlayer();
            mModel.transport = TransportModel();
            mModel.transport.position = mPosition;
            announce();
            return true;
        case K::TransportSeek:
        {
            if (!requireOpen(err)) return false;
            if (!parseNumber(c.arg(0), x) || x < 0) { err = "seek takes beats ≥ 0"; return false; }
            mPosition = toTick(x);
            if (mPlayer && mPlayer->running())
            {
                Player::Live m;
                m.kind = Player::Live::Seek;
                m.sample = (long long)std::llround(mPosition * spb);
                mPlayer->send(m);
            }
            announce();
            return true;
        }
        case K::TransportLoop:
        {
            if (!requireOpen(err)) return false;
            if (c.arg(0) == "off") { mLoopFrom = mLoopTo = 0; }
            else
            {
                double a = 0, b = 0;
                if (!parseNumber(c.arg(0), a) || a < 0 || !parseNumber(c.arg(1), b) || b <= a)
                { err = "transport loop <from> <to> (beats, to > from) — or `transport loop off`"; return false; }
                mLoopFrom = toTick(a);
                mLoopTo = toTick(b);
            }
            if (mPlayer)
                mPlayer->setLoop(mLoopTo > mLoopFrom ? (long long)std::llround(mLoopFrom * spb) : 0,
                                 mLoopTo > mLoopFrom ? (long long)std::llround(mLoopTo * spb) : 0);
            mModel.transport.loopFrom = mLoopFrom;
            mModel.transport.loopTo = mLoopTo;
            announce();
            return true;
        }
        default:
            return false;
        }
    }

    void SolarisService::liveUpdate(const Command &c)
    {
        const bool bindingsTouched = mBindingsTouched;
        mBindingsTouched = false;
        if (!mPlayer || !mPlayer->running()) return;
        // the metronome follows the tempo and the bar (atomics: cheap, whatever the edit was)
        mPlayer->setTempo(60.0 / mProject.header.bpm * mProject.header.sampleRate, std::max(1, std::atoi(mProject.header.sig.c_str())));
        bool keys = false;
        for (const auto &sd : mProject.sends) keys |= sd.sidechain;
        std::vector<Player::Live> msgs;
        bool structural = c.kind != K::Set || bindingsTouched; // a formula or a curve changed: compile it
        for (const auto &f : c.fields)
            if (!structural && !readersOf(f.first).empty()) structural = true; // a formula reads it: its readers move too
        bool silences = false;
        for (const auto &f : c.fields)
        {
            if (structural) break;
            const auto dot = f.first.find('.');
            const std::string id = f.first.substr(0, dot), field = f.first.substr(dot + 1);
            Player::Live m;
            int strip = -1;
            for (size_t i = 0; i < mLiveStrips.size(); ++i)
                if (mLiveStrips[i] == id) strip = (int)i;
            if (id == "project" && field == "masterGain")
            {
                m.kind = Player::Live::MasterGain;
                m.value = engine::dbToLinear(mProject.header.masterGain);
            }
            else if (strip >= 0 && field == "gain")
            {
                m.kind = Player::Live::StripGain;
                m.strip = strip;
                m.value = engine::dbToLinear(mProject.strip(id)->gain);
            }
            else if (strip >= 0 && field == "pan")
            {
                m.kind = Player::Live::StripPan;
                m.strip = strip;
                m.value = mProject.strip(id)->pan;
            }
            else if (strip >= 0 && field == "mute" && keys) { structural = true; break; } // a muted strip stops keying: compile it (R-MIX-15)
            else if (strip >= 0 && (field == "mute" || field == "solo")) { silences = true; continue; }
            else if (mLiveDevices.count(id))
            {
                const auto where = mLiveDevices[id];
                m.strip = where.first;
                m.device = where.second;
                const DeviceNode *d = mProject.device(id);
                if (field == "bypass") { m.kind = Player::Live::DeviceBypass; m.value = d->bypass ? 1 : 0; }
                else
                {
                    const DeviceType *t = DeviceRegistry::find(d->type);
                    const int pi = t ? t->paramIndex(field) : -1;
                    if (pi < 0 || field.size() >= sizeof m.name) { structural = true; break; }
                    double v = t->params[pi].def;
                    for (const auto &kv : d->params)
                        if (kv.first == field) parseParam(t->params[pi], kv.second, v);
                    m.kind = Player::Live::DeviceParam;
                    m.value = v;
                    std::strncpy(m.name, field.c_str(), sizeof m.name - 1);
                }
            }
            else { structural = true; break; } // a clip's time, a name, a length: compile it
            msgs.push_back(m);
        }
        if (!structural && silences)
        {
            const auto silent = silentStrips(mProject);
            for (size_t i = 0; i < mLiveStrips.size(); ++i)
            {
                Player::Live m;
                m.kind = Player::Live::StripSilent;
                m.strip = (int)i;
                m.value = silent.count(mLiveStrips[i]) ? 1 : 0;
                msgs.push_back(m);
            }
        }
        if (!structural)
        {
            for (const auto &m : msgs)
                if (!mPlayer->send(m)) { structural = true; break; } // a full queue: let a swap carry it
            if (!structural) return;
        }
        std::unique_ptr<engine::Engine> eng;
        std::string err;
        if (!buildLive(eng, err))
        {
            emit(Event(Event::Kind::Error).with("why", "the edit landed but cannot be played: " + err));
            return;
        }
        Player::Live m;
        m.kind = Player::Live::Swap;
        m.engine = eng.release();
        if (!mPlayer->send(m)) delete m.engine;
    }
}
}
