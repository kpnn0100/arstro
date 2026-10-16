// SolarisService — live sound (R-PLAY-1/2/3, R-TIME-4): the transport, the player thread on the
// clock device, and what an edit does while it plays. A gain, a pan, a mute, a solo, a device
// parameter or bypass, the master gain go to the player as lock-free messages and are heard at
// the next block; anything structural (a clip, a route, a device added) compiles a new engine on
// THIS thread and swaps it in at the same position (DR-PLAY-1).
#include "Compile.h"
#include "Engine.h"
#include "Format.h"
#include "MixLaws.h"
#include "Notation.h"
#include "Player.h"
#include "SolarisService.h"
#include "device/Device.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <thread>

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;
    namespace fs = std::filesystem;

    SolarisService::~SolarisService()
    {
        if (mMidiIn) mMidiIn->close(); // its thread first: it pushes into the queue the players drain
        mMonitor.reset();
        stopPlayer();
    }

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
        mLiveBinds.clear();
    }

    bool SolarisService::buildLive(std::unique_ptr<engine::Engine> &out, std::string &err)
    {
        CompileResult cr = compile(mProject, [this](const std::string &src) { return pcmFor(src); });
        auto eng = std::make_unique<engine::Engine>();
        if (!eng->build(cr.graph, err)) return false;
        mLiveStrips = cr.stripIds;
        mLiveDevices = cr.devices;
        mLiveBinds = cr.bindAddresses;
        mLiveLatency.clear();
        for (const auto &d : cr.devices) mLiveLatency[d.first] = eng->deviceLatency(d.second.first, d.second.second);
        ++mLiveGen; // its live values are told apart from the engine it replaces
        out = std::move(eng);
        return true;
    }

    int SolarisService::latencyOf(const DeviceNode &d)
    {
        // R-MIX-17: a probe of the type (made once, on this thread) set to the node's stored values
        const DeviceType *t = DeviceRegistry::find(d.type);
        if (!t) return 0;
        auto &probe = mLatencyProbes[d.type];
        if (!probe) probe = DeviceRegistry::create(d.type);
        for (size_t i = 0; i < t->params.size(); ++i)
        {
            double v = t->params[i].def;
            for (const auto &kv : d.params)
                if (kv.first == t->params[i].name) parseParam(t->params[i], kv.second, v);
            probe->setParam((int)i, v);
        }
        return probe->latency();
    }

    std::unique_ptr<IAudioOut> SolarisService::openOutput(int rate, int block, std::string &driver, std::string &err)
    {
        // R-DEV-9: the setting's driver — PipeWire not there, or linking nowhere, plays through PulseAudio (the fallback),
        // said ONCE so the user knows why the buffer is not what they asked; `settings set driver=pipewire` tries again
        driver.clear();
        if (mSettings.driver == "pipewire" && !mPipewireDown)
        {
            std::string why = "this build has no PipeWire driver";
            if (mHost.audioOutFor)
                if (auto out = mHost.audioOutFor("pipewire"))
                {
                    why.clear();
                    if (out->open(mSettings.output, rate, 2, block, why)) { driver = "pipewire"; return out; }
                }
            emit(Event(Event::Kind::Info).with("text", (why.empty() ? std::string("PipeWire did not open") : why) + " — playing through PulseAudio"));
            mPipewireDown = true; // not tried again (a preview would wait 2 s each time) until the driver is set again
        }
        auto out = mHost.audioOut ? mHost.audioOut() : nullptr;
        if (!out || !out->open(mSettings.output, rate, 2, block, err))
        {
            if (err.empty()) err = "cannot open the output device";
            return nullptr;
        }
        driver = "pulse";
        return out;
    }

    void SolarisService::pump()
    {
        // R-SAFE-1: an unsaved song is written beside itself on the clock, whatever else is happening
        if (mOpen && mModel.dirty && mSettings.autosave > 0 && steadySeconds() - mLastAutosave >= mSettings.autosave) autosave();
        if (mPreview) mPreview->collect(); // R-ROLL-7: what its thread is done with, freed here
        mModel.midiMessages = mMidiCount->load(std::memory_order_relaxed); // R-MIDI-2: the keyboard's activity
        if (mMonitor) mMonitor->collect();
        if (mAudition)
        {
            // the preview runs on its own: its progress, and its end when it reaches it
            mModel.audition.progress = mAudition->progress();
            if (mModel.audition.playing && !mAudition->playing())
            {
                mModel.audition.playing = false;
                emit(Event(Event::Kind::AuditionChanged).with("file", mModel.audition.file).with("playing", "0"));
            }
        }
        if (!mPlayer) return;
        mPlayer->collect();
        TransportModel &t = mModel.transport;
        t.playing = mPlayer->running();
        const double spb = 60.0 / mProject.header.bpm * mProject.header.sampleRate;
        t.position = (double)mPlayer->heard() / spb;
        t.latencyMs = std::round(mPlayer->latency() * 10000.0) / 10.0;
        // R-METER-2: while a `meters clear` is on its way to the audio thread, its holds, latches and loudness are
        // from before it — the model keeps what the clear set
        const bool fresh = !mPlayer->clearing();
        for (int c = 0; c < 2; ++c)
        {
            t.masterPeak[c] = mPlayer->peak(2 * Player::kMaxStrips + c);
            t.masterRms[c] = mPlayer->rms(2 * Player::kMaxStrips + c);
            if (fresh) t.masterHold[c] = mPlayer->hold(2 * Player::kMaxStrips + c);
        }
        t.xruns = mPlayer->xruns(); // R-DEV-5: what the device says (−1: it cannot count them)
        if (fresh && mPlayer->takeClip(Player::kMaxStrips)) mMasterClip = true;
        if (mRecording)
        {
            // R-MIDI-2: what the keyboard played, kept as it comes
            Player::Played pl;
            while (mPlayer->takePlayed(pl))
            {
                mTaken.push_back(pl);
                if (pl.velocity > 0 && (double)pl.at / spb >= mRecordFrom - 0.5) ++t.recorded; // what will land (not a count-in's)
            }
            t.recording = true;
            t.countingIn = mPlayer->countingIn();
        } // a latch: only `meters clear` puts it out
        t.masterClip = mMasterClip;
        for (size_t i = 0; fresh && i < mLiveStrips.size() && i < (size_t)Player::kMaxStrips; ++i)
            if (mPlayer->takeClip((int)i)) mClipLatch.insert(mLiveStrips[i]);
        for (auto &s : mModel.strips)
        {
            for (size_t i = 0; i < mLiveStrips.size() && i < (size_t)Player::kMaxStrips; ++i)
                if (mLiveStrips[i] == s.id)
                    for (int c = 0; c < 2; ++c)
                    {
                        s.peak[c] = mPlayer->peak((int)(2 * i) + c);
                        s.rms[c] = mPlayer->rms((int)(2 * i) + c);
                        if (fresh) s.hold[c] = mPlayer->hold((int)(2 * i) + c);
                    }
            s.clip = mClipLatch.count(s.id) > 0;
        }
        // R-METER-1: the master's loudness, as the player measured it
        if (t.playing && fresh)
        {
            Player::Loudness l;
            mPlayer->loudness(l);
            LoudnessModel &lm = mModel.loudness;
            lm.momentary = l.momentary;
            lm.shortTerm = l.shortTerm;
            lm.integrated = l.integrated;
            lm.truePeak = l.truePeak;
            lm.correlation = l.correlation;
            lm.spectrum.assign(l.spectrum.begin(), l.spectrum.end());
        }
        // R-MIX-16: what each formula evaluated to for the block heard now, by the engine that played it
        if (!mLiveBinds.empty() && mPlayer->liveValues(mLiveGen, mLiveValues))
            for (auto &bm : mModel.bindings)
                for (size_t i = 0; i < mLiveBinds.size() && i < mLiveValues.size(); ++i)
                    if (mLiveBinds[i] == bm.address) { bm.live = mLiveValues[i]; break; }
        refreshNow(); // the automations' values follow what is heard (R-AUTO-11)
    }

    bool SolarisService::auditionCommand(const Command &c, std::string &err)
    {
        // R-EDM-9: a sample heard now, outside the song — not an edit, never in a render
        const std::string a = c.arg(0);
        if (a == "stop")
        {
            if (mAudition) mAudition->stop();
            const bool was = mModel.audition.playing;
            mModel.audition.playing = false;
            if (was) emit(Event(Event::Kind::AuditionChanged).with("file", mModel.audition.file).with("playing", "0"));
            return true;
        }
        std::string path = a;
        if (!fs::path(path).is_absolute() && !mPath.empty() && fs::exists(resolvePath(path))) path = resolvePath(path);
        auto pcm = pcmFor(path);
        if (!pcm) { err = mHost.decodeAudio ? "cannot read " + a + " as audio" : "this build has no audio decoder"; return false; }
        std::string driver;
        auto out = openOutput(mPcmRate, mSettings.bufferSize, driver, err);
        if (!out) return false;
        if (!mAudition) mAudition = std::make_unique<Auditioner>();
        mAudition->start(std::move(out), pcm, path, engine::dbToLinear(mSettings.auditionLevel), mSettings.bufferSize);
        mModel.audition = AuditionModel{path, true, 0.0};
        emit(Event(Event::Kind::AuditionChanged).with("file", path).with("playing", "1"));
        return true;
    }

    bool SolarisService::previewCommand(const Command &c, std::string &err)
    {
        // R-ROLL-7: one note through one strip's chain, outside the song — not an edit, never in a render.
        // The strip, its rack and a one-note pattern compiled into a graph of their own: the preview IS the
        // strip's sound, by the same compiler and engine as the song's, and the song's engine never knows.
        if (!requireOpen(err)) return false;
        const Strip *s = nullptr;
        for (const auto &x : mProject.strips)
            if (x.id == c.arg(0)) s = &x;
        if (!s) { err = "no strip '" + c.arg(0) + "'"; return false; }
        if (s->kind != "instrument") { err = "strip " + s->id + " (" + s->name + ") plays no notes — an instrument strip does"; return false; }
        const Rack *rk = mProject.rack(s->id);
        int pitch = 60;
        double v = 0;
        std::string nameErr;
        if (parseNumber(c.arg(1), v) && std::isfinite(v))
        {
            if (v != std::floor(v) || v < 0 || v > 127) { err = "a pitch number is a whole number 0–127, got " + c.arg(1); return false; }
            pitch = (int)v;
        }
        else if (!parsePitchName(c.arg(1), pitch, nameErr))
        {
            // the strip's kit's pad, by name
            const DeviceType *t = rk && !rk->devices.empty() ? DeviceRegistry::find(rk->devices.front().type) : nullptr;
            bool found = false;
            if (t)
                for (const auto &nn : t->noteNames)
                    if (padKey(nn.second) == padKey(c.arg(1))) { pitch = nn.first; found = true; }
            if (!found) { err = nameErr; return false; }
        }
        int vel = 100;
        if (c.has("vel"))
        {
            if (!parseNumber(c.flag("vel"), v) || v != std::floor(v) || v < 1 || v > 127) { err = "--vel is a whole number 1–127"; return false; }
            vel = (int)v;
        }
        double length = 0.5;
        if (c.has("length") && (!parseNumber(c.flag("length"), length) || !(length > 0) || length > 64)) { err = "--length is beats, > 0 and ≤ 64"; return false; }
        if (!mHost.audioOut) { err = "this build cannot play audio (no output stream)"; return false; }

        Project p;
        p.header = mProject.header;
        p.header.masterGain = 0.0;               // the strip as it reaches the master, not the master's level
        p.ports = mProject.ports;
        p.mixers = mProject.mixers;
        Strip st = *s;
        st.out.clear();                          // straight to the master: no bus, no send, no key
        st.mute = st.solo = false;
        p.strips.push_back(st);
        if (rk) p.racks.push_back(*rk);          // its instrument and its rack — the master's rack is the song's
        Pattern pt;
        pt.id = "pt_preview";
        pt.length = length;
        Note n;
        n.pitch = pitch;
        n.vel = vel;
        n.length = length;
        pt.notes.push_back(n);
        p.patterns.push_back(pt);
        Clip cl;
        cl.id = "ac_preview";
        cl.track = st.id;
        cl.pattern = pt.id;
        p.clips.push_back(cl);
        // bindings are left out: a formula may read the song's other addresses — the preview is the strip's
        // values as they stand
        CompileResult cr = compile(p, [this](const std::string &src) { return pcmFor(src); });
        auto voice = std::make_unique<NotePreview::Voice>();
        voice->engine = std::make_unique<engine::Engine>();
        if (!voice->engine->build(cr.graph, err)) return false;
        if (!mPreview) mPreview = std::make_unique<NotePreview>();
        const int block = mPreview->running() ? mPreview->block() : std::max(32, mSettings.bufferSize);
        voice->engine->prepare(voice->out, block);
        const double spb = 60.0 / mProject.header.bpm * mProject.header.sampleRate;
        voice->noteFrames = (long long)std::llround(length * spb) + voice->engine->outputLatency();
        voice->maxFrames = voice->noteFrames + 4LL * mProject.header.sampleRate; // a tail of four seconds at most
        mPreview->hear(std::move(voice));
        if (!mPreview->running())
        {
            std::string driver;
            auto out = openOutput(mProject.header.sampleRate, block, driver, err);
            if (!out) return false;
            mPreview->start(std::move(out), block);
        }
        emit(Event(Event::Kind::NotePreviewed).with("strip", s->id).with("pitch", std::to_string(pitch)));
        return true;
    }

    bool SolarisService::transportCommand(const Command &c, std::string &err)
    {
        if (c.kind == K::Audition) return auditionCommand(c, err);
        if (c.kind == K::NotePreview) return previewCommand(c, err);
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
        case K::TransportRecord:
        {
            // R-MIDI-2: a play that keeps the armed strip's keyboard — landed at `transport stop`
            if (!requireOpen(err)) return false;
            if (mRecording) { err = "already recording — `transport stop` lands the take"; return false; }
            if (mArmed.empty()) { err = "arm a strip first: `strip arm <ch>`"; return false; }
            double bars = mSettings.countIn;
            if (c.has("count-in") && (!parseNumber(c.flag("count-in"), bars) || bars < 0 || bars > 4 || bars != std::floor(bars)))
            {
                err = "--count-in is bars, 0 … 4";
                return false;
            }
            Command play;
            play.kind = K::TransportPlay;
            if (c.has("from")) play.flags = {{"from", c.flag("from")}};
            mCountInFrames = (long long)std::llround(bars * std::max(1, std::atoi(mProject.header.sig.c_str())) * spb);
            if (!transportCommand(play, err)) { mCountInFrames = 0; return false; }
            mPlayer->setRecording(true);
            mRecording = true;
            mOverdub = c.has("overdub");
            mRecordStrip = mArmed;
            mRecordFrom = mPosition;
            mTaken.clear();
            mModel.transport.recording = true;
            mModel.transport.recorded = 0;
            return true;
        }
        case K::TransportPlay:
        {
            if (!requireOpen(err)) return false;
            if (mRecording) { err = "recording — `transport stop` lands the take first"; return false; }
            if (!mHost.audioOut) { err = "this build cannot play audio (no output stream)"; return false; }
            if (c.has("from"))
            {
                if (!parseNumber(c.flag("from"), x) || x < 0) { err = "--from must be beats ≥ 0"; return false; }
                mPosition = toTick(x);
            }
            if (c.has("clip"))
            {
                // R-ROLL-6: the roll's play — the clip's span, looped, the song sounding there
                const Clip *cl = nullptr;
                for (const auto &k : mProject.clips)
                    if (k.id == c.flag("clip")) cl = &k;
                if (!cl) { err = "no clip '" + c.flag("clip") + "'"; return false; }
                const double len = clipLengthBeats(mProject, *cl);
                if (len <= 0) { err = "clip '" + cl->id + "' has no length to loop"; return false; }
                if (mClipLoop.empty()) { mSongLoopFrom = mLoopFrom; mSongLoopTo = mLoopTo; }
                mClipLoop = cl->id;
                mLoopFrom = cl->at;
                mLoopTo = cl->at + len;
                if (!c.has("from") || mPosition < mLoopFrom || mPosition >= mLoopTo) mPosition = mLoopFrom;
            }
            else endClipLoop(); // the song's play: the song's loop
            // R-TIME-7 (amended): a play that SAYS where it begins puts the time bar there; a plain play starts where it
            // stands — after a stop-here that place is temporary, and the cue stays where the user put the time bar
            if (c.has("from") || c.has("clip")) mCue = mPosition;
            mModel.transport.loopFrom = mLoopFrom;
            mModel.transport.loopTo = mLoopTo;
            mModel.transport.loopClip = mClipLoop;
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
            std::string driver;
            auto out = openOutput(mProject.header.sampleRate, mSettings.bufferSize, driver, err);
            if (!out) return false;
            mModel.transport.driver = driver; // R-DEV-9: what it plays through
            stopMonitor();                    // R-MIDI-2: one consumer of the keyboard at a time — the song's player now
            mPlayer = std::make_unique<Player>();
            if (mMidiQ) { mPlayer->setMidi(mMidiQ.get()); mPlayer->setArmed(armedIn(mLiveStrips)); }
            mPlayer->setCountIn(mCountInFrames); // R-MIDI-2: a recording's count-in, else none
            mCountInFrames = 0;
            mPlayer->setLoop(mLoopTo > mLoopFrom ? (long long)std::llround(mLoopFrom * spb) : 0,
                             mLoopTo > mLoopFrom ? (long long)std::llround(mLoopTo * spb) : 0);
            mModel.transport.device = mSettings.output;
            mPlayer->setTempo(spb, std::max(1, std::atoi(mProject.header.sig.c_str())));
            mPlayer->setClick(mSettings.metronome, engine::dbToLinear(mSettings.metronomeLevel));
            mPlayer->start(std::move(out), std::move(eng), routes, 2, mSettings.bufferSize, (long long)std::llround(mPosition * spb), mLiveGen);
            announce();
            return true;
        }
        case K::MetersClear:
        {
            // R-METER-2: the latches and holds out, the master's true peak and integrated loudness started again
            mClipLatch.clear();
            mMasterClip = false;
            if (mPlayer) mPlayer->clearMeters();
            for (auto &s : mModel.strips) { s.clip = false; s.hold[0] = s.hold[1] = 0; }
            mModel.transport.masterClip = false;
            mModel.transport.masterHold[0] = mModel.transport.masterHold[1] = 0;
            const std::vector<double> bands = mModel.loudness.spectrum;
            mModel.loudness = LoudnessModel();
            mModel.loudness.spectrum = bands; // a 300 ms view: it follows the sound, not the clear
            emit(Event(Event::Kind::MetersCleared));
            return true;
        }
        case K::TransportStop:
        {
            // R-MIDI-2: a recording's last notes, and where it stopped, before the player goes
            long long stoppedAt = 0;
            if (mRecording && mPlayer)
            {
                Player::Played pl;
                while (mPlayer->takePlayed(pl)) mTaken.push_back(pl);
                stoppedAt = mPlayer->heard();
            }
            stopPlayer();
            endClipLoop();
            if (c.has("return")) mPosition = mCue; // R-TIME-7: back to where the time bar was put
            for (auto &s : mModel.strips) // R-METER-2: a stopped strip reads nothing — but its clip light stays lit
                for (int ch = 0; ch < 2; ++ch) s.peak[ch] = s.rms[ch] = s.hold[ch] = 0;
            mModel.transport = TransportModel();
            mModel.transport.masterClip = mMasterClip;
            mModel.transport.position = mPosition;
            mModel.transport.cue = mCue;
            mModel.transport.loopFrom = mLoopFrom;
            mModel.transport.loopTo = mLoopTo;
            if (mRecording && !commitRecording(stoppedAt, err)) return false; // the take: an edit (dispatch restores on failure)
            startMonitor(); // R-MIDI-2: armed — the keyboard is heard again, through the monitor
            announce();
            return true;
        }
        case K::TransportSeek:
        {
            if (!requireOpen(err)) return false;
            if (mRecording) { err = "recording — `transport stop` lands the take first"; return false; }
            if (!parseNumber(c.arg(0), x) || x < 0) { err = "seek takes beats ≥ 0"; return false; }
            mPosition = toTick(x);
            mCue = mPosition; // R-TIME-7: the time bar, put here
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
            mClipLoop.clear(); // a loop set by hand is the song's: the roll's play gives way to it
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
            mModel.transport.loopClip.clear();
            announce();
            return true;
        }
        default:
            return false;
        }
    }

    void SolarisService::endClipLoop()
    {
        if (mClipLoop.empty()) return;
        mClipLoop.clear();
        mLoopFrom = mSongLoopFrom;
        mLoopTo = mSongLoopTo;
        if (mPlayer)
        {
            const double spb = 60.0 / std::max(1.0, mProject.header.bpm) * mProject.header.sampleRate;
            mPlayer->setLoop(mLoopTo > mLoopFrom ? (long long)std::llround(mLoopFrom * spb) : 0,
                             mLoopTo > mLoopFrom ? (long long)std::llround(mLoopTo * spb) : 0);
        }
    }

    void SolarisService::followClipLoop()
    {
        if (mClipLoop.empty()) return;
        const Clip *cl = nullptr;
        for (const auto &k : mProject.clips)
            if (k.id == mClipLoop) cl = &k;
        const double len = cl ? clipLengthBeats(mProject, *cl) : 0.0;
        if (!cl || len <= 0) { endClipLoop(); return; } // the clip is gone: the song's loop again
        if (cl->at == mLoopFrom && cl->at + len == mLoopTo) return;
        mLoopFrom = cl->at;
        mLoopTo = cl->at + len;
        if (mPlayer)
        {
            const double spb = 60.0 / std::max(1.0, mProject.header.bpm) * mProject.header.sampleRate;
            mPlayer->setLoop((long long)std::llround(mLoopFrom * spb), (long long)std::llround(mLoopTo * spb));
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
                    // R-MIX-17: a value that moves the device's latency (a limiter's lookahead) re-plans the compensation
                    const auto was = mLiveLatency.find(id);
                    if (was != mLiveLatency.end() && latencyOf(*d) != was->second) { structural = true; break; }
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
        m.generation = mLiveGen;
        if (!mPlayer->send(m)) delete m.engine;
    }
}
}
