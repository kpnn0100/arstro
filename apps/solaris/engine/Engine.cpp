#include "Engine.h"
#include "MixLaws.h"
#include <algorithm>
#include <cmath>
#include "base/AudioConfig.h"
#include "device/Device.h"

namespace arstro
{
namespace solaris
{
namespace engine
{
    struct Engine::StripState
    {
        std::vector<std::unique_ptr<Device>> rack; // aligned with the graph's rack
        std::vector<double> inL, inR, outL, outR;
        std::vector<double> keyL, keyR;           // R-MIX-15: what earlier strips keyed it with, this piece
        size_t nextEvent = 0;                     // the first note event at or after the position
        double sumSq[2] = {0, 0};
        long long counted = 0;
        // bound gain / pan / sends ramp from A to B across one control period starting at `ramp`
        bool gainBound = false, panBound = false;
        double gainA = 1, gainB = 1, panA = 0, panB = 0;
        long long ramp = 0;
        std::vector<char> sendBound;
        std::vector<double> sendA, sendB, tmpL, tmpR;
    };

    Engine::Engine() = default;
    Engine::~Engine() = default;

    namespace
    {
        std::unique_ptr<Device> makeDevice(const DeviceDesc &d, std::string &err)
        {
            auto dev = DeviceRegistry::create(d.type);
            if (!dev)
            {
                err = "unknown device type `" + d.type + "` (" + d.id + ")";
                return nullptr;
            }
            for (const auto &p : d.params)
                if (!dev->setParam(p.first, p.second))
                {
                    err = "device " + d.id + " (" + d.type + ") has no parameter `" + p.first + "`";
                    return nullptr;
                }
            return dev;
        }

        // One block of silence through a device, so every parameter ramp the writes above started
        // finishes before time zero (the DSP library smooths each write over a block).
        void warm(Device &d)
        {
            std::vector<Sample> L(Engine::kBlock, 0.0), R(Engine::kBlock, 0.0);
            Sample *io[2] = {L.data(), R.data()};
            d.process(io, 2, Engine::kBlock);
            d.reset();
        }

        bool checkTarget(const Target &t, int from, const MixGraph &g, std::string &err, const std::string &what)
        {
            if (t.kind == Target::Strip && (t.index <= from || t.index >= (int)g.strips.size()))
            {
                err = g.strips[from].id + "'s " + what + " points to strip index " + std::to_string(t.index) +
                      ", which is not LATER in processing order (R-MIX-4)";
                return false;
            }
            if (t.kind == Target::Port && (t.index < 0 || t.index >= (int)g.ports.size()))
            {
                err = g.strips[from].id + "'s " + what + " points to port index " + std::to_string(t.index) + ", which does not exist";
                return false;
            }
            return true;
        }
    }

    bool Engine::build(const MixGraph &graph, std::string &err)
    {
        err.clear();
        for (int i = 0; i < (int)graph.strips.size(); ++i)
        {
            if (!checkTarget(graph.strips[i].out, i, graph, err, "output")) return false;
            for (const auto &s : graph.strips[i].sends)
            {
                if (!checkTarget(s.to, i, graph, err, s.key ? "sidechain key" : "send")) return false;
                if (s.key && s.to.kind != Target::Strip)
                {
                    err = graph.strips[i].id + "'s sidechain key goes to the master or a port — a key goes to a strip";
                    return false;
                }
            }
        }
        for (int p : graph.masterPorts)
            if (p < 0 || p >= (int)graph.ports.size())
            {
                err = "the master feeds port index " + std::to_string(p) + ", which does not exist";
                return false;
            }

        // The DSP library's config is a process singleton (R-NFR-7): set it before any device exists.
        AudioConfig &cfg = AudioConfig::instance();
        if (cfg.sampleRate() != graph.sampleRate) cfg.setSampleRate(graph.sampleRate);
        if (cfg.channelCount() != 2) cfg.setChannelCount(2);
        if (cfg.bufferSize() != kBlock) cfg.setBufferSize(kBlock);

        std::vector<std::unique_ptr<StripState>> strips;
        for (const auto &s : graph.strips)
        {
            auto st = std::make_unique<StripState>();
            for (const auto &d : s.rack)
            {
                auto dev = makeDevice(d, err);
                if (!dev) return false;
                st->rack.push_back(std::move(dev));   // warmed below, once its bindings are applied
            }
            if (s.kind == Strip::Instrument && (st->rack.empty() || !st->rack[0]->isInstrument()))
            {
                err = "instrument strip " + s.id + " has no instrument first in its rack";
                return false;
            }
            st->inL.assign(kBlock, 0.0); st->inR.assign(kBlock, 0.0);
            st->outL.assign(kBlock, 0.0); st->outR.assign(kBlock, 0.0);
            st->tmpL.assign(kBlock, 0.0); st->tmpR.assign(kBlock, 0.0);
            st->keyL.assign(kBlock, 0.0); st->keyR.assign(kBlock, 0.0);
            st->gainA = st->gainB = s.gain;
            st->panA = st->panB = s.pan;
            st->sendBound.assign(s.sends.size(), 0);
            st->sendA.assign(s.sends.size(), 1.0);
            st->sendB.assign(s.sends.size(), 1.0);
            for (size_t k = 0; k < s.sends.size(); ++k) st->sendA[k] = st->sendB[k] = s.sends[k].gain;
            strips.push_back(std::move(st));
        }
        std::vector<std::unique_ptr<Device>> master;
        for (const auto &d : graph.masterRack)
        {
            auto dev = makeDevice(d, err);
            if (!dev) return false;
            master.push_back(std::move(dev));
        }
        for (const auto &b : graph.binds)
        {
            const bool bad = (b.kind == Bind::StripGain || b.kind == Bind::StripPan || b.kind == Bind::SendGain || b.kind == Bind::DeviceParam)
                                 ? (b.strip < 0 || b.strip >= (int)graph.strips.size())
                                 : false;
            if (bad || (b.kind == Bind::SendGain && (b.index < 0 || b.index >= (int)graph.strips[b.strip].sends.size())) ||
                (b.kind == Bind::DeviceParam && (b.index < 0 || b.index >= (int)strips[b.strip]->rack.size())) ||
                (b.kind == Bind::MasterDeviceParam && (b.index < 0 || b.index >= (int)master.size())))
            {
                err = "a binding points at a strip, send or device the graph does not have";
                return false;
            }
        }

        mGraph = graph;
        mStrips = std::move(strips);
        mMasterRack = std::move(master);
        mMasterL.assign(kBlock, 0.0);
        mMasterR.assign(kBlock, 0.0);
        mStripMeters.assign(mGraph.strips.size(), Meter{});
        mMasterMeter = Meter{};
        mCaptured.clear();
        mPos = 0;
        mVars.assign(kClockSlots + mGraph.curves.size() + mGraph.binds.size(), 0.0);
        mBindValues.assign(mGraph.binds.size(), 0.0);
        mApplied.assign(mGraph.binds.size(), std::nan(""));
        mMasterBound = false;
        mMasterA = mMasterB = mGraph.masterGain;
        for (size_t b = 0; b < mGraph.binds.size(); ++b)
        {
            const Bind &bd = mGraph.binds[b];
            mBindValues[b] = bd.own;
            if (bd.kind == Bind::StripGain) mStrips[bd.strip]->gainBound = true;
            if (bd.kind == Bind::StripPan) mStrips[bd.strip]->panBound = true;
            if (bd.kind == Bind::SendGain) mStrips[bd.strip]->sendBound[bd.index] = 1;
            if (bd.kind == Bind::MasterGain) mMasterBound = true;
        }
        // the values at time zero go in BEFORE the warm-up, so its block of silence finishes their ramps too
        evalBinds(0, true);
        for (auto &st : mStrips)
            for (auto &dev : st->rack) warm(*dev);
        for (auto &dev : mMasterRack) warm(*dev);
        return true;
    }

    void Engine::evalBinds(long long sample, bool immediate)
    {
        if (mGraph.binds.empty()) return;
        evaluateBinds(mGraph.clock, mGraph.curves, mGraph.binds, sample, mVars.data(), mBindValues.data());
        for (size_t b = 0; b < mGraph.binds.size(); ++b)
        {
            const Bind &bd = mGraph.binds[b];
            const double v = mBindValues[b];
            switch (bd.kind)
            {
            case Bind::StripGain:
            {
                StripState &st = *mStrips[bd.strip];
                const double lin = v <= -119.95 ? 0.0 : dbToLinear(v);
                st.gainA = immediate ? lin : st.gainB;
                st.gainB = lin;
                st.ramp = sample;
                mGraph.strips[bd.strip].gain = lin;
                break;
            }
            case Bind::StripPan:
            {
                StripState &st = *mStrips[bd.strip];
                st.panA = immediate ? v : st.panB;
                st.panB = v;
                st.ramp = sample;
                mGraph.strips[bd.strip].pan = v;
                break;
            }
            case Bind::SendGain:
            {
                StripState &st = *mStrips[bd.strip];
                const double lin = v <= -119.95 ? 0.0 : dbToLinear(v);
                st.sendA[bd.index] = immediate ? lin : st.sendB[bd.index];
                st.sendB[bd.index] = lin;
                st.ramp = sample;
                mGraph.strips[bd.strip].sends[bd.index].gain = lin;
                break;
            }
            case Bind::MasterGain:
            {
                const double lin = v <= -119.95 ? 0.0 : dbToLinear(v);
                mMasterA = immediate ? lin : mMasterB;
                mMasterB = lin;
                mMasterRamp = sample;
                mGraph.masterGain = lin;
                break;
            }
            case Bind::DeviceParam:
            case Bind::MasterDeviceParam:
                if (v != mApplied[b]) // a device smooths its own writes; only a change is written
                {
                    Device &dev = bd.kind == Bind::DeviceParam ? *mStrips[bd.strip]->rack[bd.index] : *mMasterRack[bd.index];
                    dev.setParam(bd.param, v);
                    mApplied[b] = v;
                }
                break;
            }
        }
    }

    void Engine::seek(long long sample)
    {
        mPos = std::max(0LL, sample);
        for (size_t i = 0; i < mStrips.size(); ++i)
        {
            auto &st = *mStrips[i];
            const auto &notes = mGraph.strips[i].notes;
            st.nextEvent = (size_t)(std::lower_bound(notes.begin(), notes.end(), mPos,
                                                     [](const NoteEvent &e, long long p) { return e.at < p; }) - notes.begin());
            if (mGraph.strips[i].kind == Strip::Instrument && !st.rack.empty()) st.rack[0]->reset();
        }
        evalBinds(mPos, true); // a jump has no value to ramp from
    }

    void Engine::clearPeaks()
    {
        for (auto &m : mStripMeters) m.maxPeak[0] = m.maxPeak[1] = 0;
        mMasterMeter.maxPeak[0] = mMasterMeter.maxPeak[1] = 0;
    }

    bool Engine::setDeviceParam(int strip, int device, const std::string &name, double value)
    {
        if (strip < 0 || strip >= (int)mStrips.size()) return false;
        auto &rack = mStrips[strip]->rack;
        if (device < 0 || device >= (int)rack.size()) return false;
        return rack[device]->setParam(name, value);
    }

    bool Engine::setMasterDeviceParam(int device, const std::string &name, double value)
    {
        if (device < 0 || device >= (int)mMasterRack.size()) return false;
        return mMasterRack[device]->setParam(name, value);
    }

    void Engine::meter(Meter &m, const double *L, const double *R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            m.peak[0] = std::max(m.peak[0], (float)std::fabs(L[i]));
            m.peak[1] = std::max(m.peak[1], (float)std::fabs(R[i]));
        }
        m.maxPeak[0] = std::max(m.maxPeak[0], m.peak[0]);
        m.maxPeak[1] = std::max(m.maxPeak[1], m.peak[1]);
    }

    void Engine::routeKey(const Target &t, const double *L, const double *R, int n, double gain)
    {
        if (t.kind != Target::Strip) return; // refused at build; a key goes to a strip
        auto &st = *mStrips[t.index];
        for (int i = 0; i < n; ++i) { st.keyL[i] += L[i] * gain; st.keyR[i] += R[i] * gain; }
    }

    void Engine::route(const Target &t, const double *L, const double *R, int n, double gain, PortBuffers &out, int off)
    {
        switch (t.kind)
        {
        case Target::Master:
            for (int i = 0; i < n; ++i) { mMasterL[i] += L[i] * gain; mMasterR[i] += R[i] * gain; }
            break;
        case Target::Strip:
        {
            auto &st = *mStrips[t.index];
            for (int i = 0; i < n; ++i) { st.inL[i] += L[i] * gain; st.inR[i] += R[i] * gain; }
            break;
        }
        case Target::Port:
        {
            auto &chans = out.ports[t.index];
            if (chans.size() >= 2)
                for (int i = 0; i < n; ++i) { chans[0][off + i] += (float)(L[i] * gain); chans[1][off + i] += (float)(R[i] * gain); }
            else if (chans.size() == 1)
                for (int i = 0; i < n; ++i) chans[0][off + i] += (float)(0.5 * (L[i] + R[i]) * gain); // (M4) a mono port gets the average
            break;
        }
        case Target::None:
            break;
        }
    }

    void Engine::renderPiece(long long p0, int n, PortBuffers &out, int off)
    {
        for (auto &st : mStrips)
        {
            std::fill(st->inL.begin(), st->inL.begin() + n, 0.0);
            std::fill(st->inR.begin(), st->inR.begin() + n, 0.0);
            std::fill(st->keyL.begin(), st->keyL.begin() + n, 0.0);
            std::fill(st->keyR.begin(), st->keyR.begin() + n, 0.0);
        }
        std::fill(mMasterL.begin(), mMasterL.begin() + n, 0.0);
        std::fill(mMasterR.begin(), mMasterR.begin() + n, 0.0);

        for (size_t si = 0; si < mStrips.size(); ++si)
        {
            const Strip &d = mGraph.strips[si];
            StripState &st = *mStrips[si];
            double *L = st.inL.data(), *R = st.inR.data();

            if (d.kind == Strip::Audio)
            {
                for (const Region &rg : d.regions)
                {
                    if (!rg.pcm || rg.frames <= 0) continue;
                    const long long a = std::max(p0, rg.start), b = std::min(p0 + n, rg.start + rg.frames);
                    for (long long t = a; t < b; ++t)
                    {
                        const long long local = t - rg.start;
                        long long src = local;
                        if (rg.loop && rg.srcFrames > 0) src %= rg.srcFrames;
                        else if (src >= rg.srcFrames) continue;
                        src += rg.srcOffset;
                        if (src < 0 || src >= rg.pcm->frames) continue; // outside the file is silence
                        const double g = rg.gain * fadeGain(local, rg.frames, rg.fadeIn, rg.fadeOut);
                        L[t - p0] += g * rg.pcm->at(src, 0);
                        R[t - p0] += g * rg.pcm->at(src, 1);
                    }
                }
            }
            size_t firstFx = 0;
            if (d.kind == Strip::Instrument && !st.rack.empty())
            {
                firstFx = 1;
                Device &inst = *st.rack[0];
                const bool playing = !d.rack[0].bypass;
                long long cur = p0;
                // (M5) split the block at every note event so it starts on its exact sample
                while (st.nextEvent < d.notes.size() && d.notes[st.nextEvent].at < p0 + n)
                {
                    const NoteEvent &e = d.notes[st.nextEvent];
                    if (e.at > cur && playing)
                    {
                        Sample *io[2] = {L + (cur - p0), R + (cur - p0)};
                        inst.process(io, 2, (int)(e.at - cur));
                    }
                    cur = std::max(cur, e.at);
                    if (e.on) inst.noteOn(e.note, e.velocity);
                    else inst.noteOff(e.note);
                    ++st.nextEvent;
                }
                if (cur < p0 + n && playing)
                {
                    Sample *io[2] = {L + (cur - p0), R + (cur - p0)};
                    inst.process(io, 2, (int)(p0 + n - cur));
                }
            }
            for (size_t k = firstFx; k < st.rack.size(); ++k)
            {
                if (d.rack[k].bypass) continue;
                Sample *io[2] = {L, R};
                if (st.rack[k]->type().takesKey)
                {
                    // R-MIX-15: a compressor's detector hears the strip's key (silence when nothing keys it)
                    const Sample *key[2] = {st.keyL.data(), st.keyR.data()};
                    st.rack[k]->setKey(key, 2);
                }
                st.rack[k]->process(io, 2, n);
            }

            bool keys = false;
            for (const auto &s : d.sends) keys |= s.key;
            if (d.silent && !(keys && d.keyLive))
            {
                // A muted or solo-silenced strip sends nothing anywhere.
                std::fill(st.outL.begin(), st.outL.begin() + n, 0.0);
                std::fill(st.outR.begin(), st.outR.begin() + n, 0.0);
            }
            else
            {
                // silenced by another's solo, a strip still keys: only its key sends run (R-MIX-15)
                auto runs = [&](size_t k) { return !d.silent || d.sends[k].key; };
                // a bound value ramps from A to B across the control period that began at `ramp` (R-AUTO-7)
                auto rampAt = [&](int i) { return std::min(1.0, std::max(0.0, (double)(p0 + i - st.ramp) / kControl)); };
                auto sendTo = [&](size_t k, const double *sL, const double *sR) {
                    const Send &s = d.sends[k];
                    auto deliver = [&](const double *a, const double *b, double g) {
                        if (s.key) routeKey(s.to, a, b, n, g);
                        else route(s.to, a, b, n, g, out, off);
                    };
                    if (!st.sendBound[k]) { deliver(sL, sR, s.gain); return; }
                    for (int i = 0; i < n; ++i)
                    {
                        const double g = st.sendA[k] + (st.sendB[k] - st.sendA[k]) * rampAt(i);
                        st.tmpL[i] = sL[i] * g;
                        st.tmpR[i] = sR[i] * g;
                    }
                    deliver(st.tmpL.data(), st.tmpR.data(), 1.0);
                };
                for (size_t k = 0; k < d.sends.size(); ++k)
                    if (d.sends[k].pre && runs(k)) sendTo(k, L, R);
                if (st.gainBound || st.panBound)
                {
                    for (int i = 0; i < n; ++i)
                    {
                        const double f = rampAt(i);
                        const double g = st.gainBound ? st.gainA + (st.gainB - st.gainA) * f : d.gain;
                        const double pan = st.panBound ? st.panA + (st.panB - st.panA) * f : d.pan;
                        double gl = 1, gr = 1;
                        balancePan(pan, gl, gr);
                        st.outL[i] = L[i] * g * gl;
                        st.outR[i] = R[i] * g * gr;
                    }
                }
                else
                {
                    double gl = 1, gr = 1;
                    balancePan(d.pan, gl, gr);
                    for (int i = 0; i < n; ++i)
                    {
                        st.outL[i] = L[i] * d.gain * gl;
                        st.outR[i] = R[i] * d.gain * gr;
                    }
                }
                for (size_t k = 0; k < d.sends.size(); ++k)
                    if (!d.sends[k].pre && runs(k)) sendTo(k, st.outL.data(), st.outR.data());
                if (d.silent)
                {
                    std::fill(st.outL.begin(), st.outL.begin() + n, 0.0);
                    std::fill(st.outR.begin(), st.outR.begin() + n, 0.0);
                }
                else
                    route(d.out, st.outL.data(), st.outR.data(), n, 1.0, out, off);
            }
            meter(mStripMeters[si], st.outL.data(), st.outR.data(), n);
            for (int i = 0; i < n; ++i) { st.sumSq[0] += st.outL[i] * st.outL[i]; st.sumSq[1] += st.outR[i] * st.outR[i]; }
            st.counted += n;
            for (size_t k = 0; k < mCapture.size(); ++k)
                if (mCapture[k] == (int)si)
                    for (int i = 0; i < n; ++i) { mCaptured[k][0][off + i] = (float)st.outL[i]; mCaptured[k][1][off + i] = (float)st.outR[i]; }
        }

        for (size_t k = 0; k < mMasterRack.size(); ++k)
        {
            if (mGraph.masterRack[k].bypass) continue;
            Sample *io[2] = {mMasterL.data(), mMasterR.data()};
            mMasterRack[k]->process(io, 2, n);
        }
        if (mMasterBound)
            for (int i = 0; i < n; ++i)
            {
                const double f = std::min(1.0, std::max(0.0, (double)(p0 + i - mMasterRamp) / kControl));
                const double g = mMasterA + (mMasterB - mMasterA) * f;
                mMasterL[i] *= g;
                mMasterR[i] *= g;
            }
        else
            for (int i = 0; i < n; ++i) { mMasterL[i] *= mGraph.masterGain; mMasterR[i] *= mGraph.masterGain; }
        meter(mMasterMeter, mMasterL.data(), mMasterR.data(), n);
        if (mCaptureMaster)
            for (int i = 0; i < n; ++i) { mCapturedMaster[0][off + i] = (float)mMasterL[i]; mCapturedMaster[1][off + i] = (float)mMasterR[i]; }
        for (int p : mGraph.masterPorts) route(Target{Target::Port, p}, mMasterL.data(), mMasterR.data(), n, 1.0, out, off);
    }

    void Engine::prepare(PortBuffers &out, int maxFrames) const
    {
        out.ports.resize(mGraph.ports.size());
        for (size_t p = 0; p < mGraph.ports.size(); ++p)
        {
            out.ports[p].resize(std::max(1, mGraph.ports[p].channels));
            for (auto &ch : out.ports[p]) ch.reserve((size_t)std::max(0, maxFrames));
        }
    }

    void Engine::setStripGain(int strip, double linear)
    {
        if (strip >= 0 && strip < (int)mGraph.strips.size()) mGraph.strips[strip].gain = linear;
    }
    void Engine::setStripPan(int strip, double pan)
    {
        if (strip >= 0 && strip < (int)mGraph.strips.size()) mGraph.strips[strip].pan = pan;
    }
    void Engine::setStripSilent(int strip, bool silent)
    {
        if (strip >= 0 && strip < (int)mGraph.strips.size()) mGraph.strips[strip].silent = silent;
    }
    void Engine::setDeviceBypass(int strip, int device, bool bypass)
    {
        auto &rack = strip < 0 ? mGraph.masterRack : (strip < (int)mGraph.strips.size() ? mGraph.strips[strip].rack : mGraph.masterRack);
        if (strip >= (int)mGraph.strips.size()) return;
        if (device >= 0 && device < (int)rack.size()) rack[device].bypass = bypass;
    }

    void Engine::render(int frames, PortBuffers &out)
    {
        // No allocation once `prepare` sized the buffers (R-PLAY-2): resize/assign within capacity.
        out.ports.resize(mGraph.ports.size());
        for (size_t p = 0; p < mGraph.ports.size(); ++p)
        {
            out.ports[p].resize(std::max(1, mGraph.ports[p].channels));
            for (auto &ch : out.ports[p]) ch.assign(std::max(0, frames), 0.0f);
        }
        if (!mCapture.empty())
            mCaptured.assign(mCapture.size(), std::vector<std::vector<float>>(2, std::vector<float>(std::max(0, frames), 0.0f)));
        if (mCaptureMaster) mCapturedMaster.assign(2, std::vector<float>(std::max(0, frames), 0.0f));
        for (auto &m : mStripMeters) m = Meter{{0, 0}, {0, 0}, {m.maxPeak[0], m.maxPeak[1]}};
        mMasterMeter = Meter{{0, 0}, {0, 0}, {mMasterMeter.maxPeak[0], mMasterMeter.maxPeak[1]}};
        for (auto &st : mStrips) { st->sumSq[0] = st->sumSq[1] = 0; st->counted = 0; }

        int done = 0;
        const bool bound = !mGraph.binds.empty();
        while (done < frames)
        {
            int n = std::min(kBlock, frames - done);
            if (bound)
            {
                // pieces end on control boundaries, so every boundary is evaluated once, however time is chopped
                const int into = (int)(mPos % kControl);
                if (into == 0) evalBinds(mPos, false);
                n = std::min(n, kControl - into);
            }
            renderPiece(mPos, n, out, done);
            mPos += n;
            done += n;
        }
        for (size_t i = 0; i < mStrips.size(); ++i)
            if (mStrips[i]->counted > 0)
                for (int c = 0; c < 2; ++c)
                    mStripMeters[i].rms[c] = (float)std::sqrt(mStrips[i]->sumSq[c] / mStrips[i]->counted);
    }
}
}
}
