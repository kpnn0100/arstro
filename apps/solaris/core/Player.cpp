#include "Player.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace arstro
{
namespace solaris
{
    Player::Player() : mSlots(new LiveSlot[kLiveSlots])
    {
        mMeanSq.assign(2 * kMaxStrips + 2, 0.0);
        mHeld.assign(2 * kMaxStrips + 2, 0.0);
        mHeldFor.assign(2 * kMaxStrips + 2, 0);
        for (int i = 0; i < 2 * kMaxStrips + 2; ++i) { mRms[(size_t)i].store(0.0f); mHold[(size_t)i].store(0.0f); }
        for (int i = 0; i <= kMaxStrips; ++i) mClip[(size_t)i].store(false);
        for (auto &b : mSpectrum) b.store((float)LoudnessMeter::kSilence);
    }

    Player::~Player()
    {
        stop();
        if (mRecorder.joinable()) endTake(); // a take never ended: its thread joins, its frames go
        collect();
    }

    void Player::setInput(std::unique_ptr<IAudioIn> in, int channels)
    {
        mInput = std::move(in);
        mInChannels = std::clamp(channels, 1, 2);
    }

    void Player::startTake()
    {
        if (mRecorder.joinable()) endTake();
        mTake = Take();
        mTakeDropped.store(0);
        InChunk c;
        while (mRecQ.pop(c)) {}
        mRecRun.store(true);
        mRecorder = std::thread([this] { recorder(); });
        mTaking.store(true);
    }

    void Player::recorder()
    {
        InChunk c;
        for (;;)
        {
            const bool running = mRecRun.load();
            while (mRecQ.pop(c))
            {
                if (mTake.from < 0) mTake.from = mTakeNext = c.at;
                if (c.at != mTakeNext || mTake.marks.empty()) mTake.marks.push_back({(long long)mTake.left.size(), c.at}); // R-REC-2: a loop turned
                mTakeNext = c.at + c.n;
                mTake.left.insert(mTake.left.end(), c.l.begin(), c.l.begin() + c.n);
                mTake.right.insert(mTake.right.end(), c.r.begin(), c.r.begin() + c.n);
            }
            if (!running) return; // drained after the last push could have been made
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    Player::Take Player::endTake()
    {
        mTaking.store(false);
        mRecRun.store(false);
        if (mRecorder.joinable()) mRecorder.join();
        Take t = std::move(mTake);
        t.dropped = mTakeDropped.load();
        mTake = Take();
        return t;
    }

    void Player::input(int n, long long pos, bool song)
    {
        if (!mInput || n <= 0) return;
        mInput->read(mInBuf.data(), n);
        mInLatency.store(mInput->latency(), std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            mInL[(size_t)i] = mInBuf[(size_t)i * mInChannels];
            mInR[(size_t)i] = mInChannels > 1 ? mInBuf[(size_t)i * mInChannels + 1] : mInL[(size_t)i];
        }
        if (!song) return; // a count-in: read so the capture never backs up, neither heard nor kept
        if (mMonOn.load(std::memory_order_relaxed)) mEng->liveInput(mMonStrip.load(std::memory_order_relaxed), mInL.data(), mInR.data(), n);
        if (!mTaking.load(std::memory_order_relaxed)) return;
        for (int off = 0; off < n; off += 512)
        {
            InChunk c;
            c.at = pos + off;
            c.n = std::min(512, n - off);
            std::copy(mInL.begin() + off, mInL.begin() + off + c.n, c.l.begin());
            std::copy(mInR.begin() + off, mInR.begin() + off + c.n, c.r.begin());
            if (!mRecQ.push(c)) mTakeDropped.fetch_add(c.n, std::memory_order_relaxed); // never waits: counted, said at the stop
        }
    }

    bool Player::start(std::unique_ptr<IAudioOut> out, std::unique_ptr<engine::Engine> eng, std::vector<Route> routes,
                       int channels, int block, long long from, int generation)
    {
        auto input = std::move(mInput); // R-REC-1: set before `start` — the stop below closes what it holds
        stop();
        mInput = std::move(input);
        mGeneration = generation;
        mOutFrames = 0;
        mSlotsWritten.store(0);
        mOut = std::move(out);
        mEng = eng.release();
        mRoutes = std::move(routes);
        mChannels = std::max(1, channels);
        mBlock = std::max(32, block);
        mRate = mEng->graph().sampleRate;
        // Everything the thread will touch is sized HERE (R-PLAY-2).
        mEng->prepare(mPb, mBlock);
        mInterleaved.assign((size_t)mBlock * (size_t)mChannels, 0.0f);
        mInBuf.assign((size_t)mBlock * (size_t)mInChannels, 0.0f); // R-REC-1: the input's block, sized here
        mInL.assign((size_t)mBlock, 0.0f);
        mInR.assign((size_t)mBlock, 0.0f);
        mClick = DeviceRegistry::create("drums"); // the metronome's own kit, made here, not on the audio thread
        mClickL.assign((size_t)mBlock, 0.0);
        mClickR.assign((size_t)mBlock, 0.0);
        mClickWas = false;
        for (int i = 0; i < 2 * kMaxStrips + 2; ++i) { mPeaks[(size_t)i].store(0.0f); mRms[(size_t)i].store(0.0f); mHold[(size_t)i].store(0.0f); }
        // R-METER-1: the meters read the song's rate, which the engine's build set (AudioConfig)
        std::fill(mMeanSq.begin(), mMeanSq.end(), 0.0);
        std::fill(mHeld.begin(), mHeld.end(), 0.0);
        std::fill(mHeldFor.begin(), mHeldFor.end(), 0);
        mTapL.assign((size_t)mBlock, 0.0f);
        mTapR.assign((size_t)mBlock, 0.0f);
        mEng->tapMaster(mTapL.data(), mTapR.data(), mBlock);
        mLoud = std::make_unique<LoudnessMeter>(2);
        mTrue = std::make_unique<TruePeakMeter>(2);
        mCorr = std::make_unique<CorrelationMeter>();
        mSpec = std::make_unique<SpectrumAnalyser>();
        mLoudBlocks = -1;
        mMomentary.store(LoudnessMeter::kSilence);
        mShortTerm.store(LoudnessMeter::kSilence);
        mIntegrated.store(LoudnessMeter::kSilence);
        mTruePeak.store(LoudnessMeter::kSilence);
        mCorrelation.store(0.0);
        for (auto &b : mSpectrum) b.store((float)LoudnessMeter::kSilence);
        mClearPending.store(false);
        mClearNow = false;
        mEng->seek(from);
        mGraphLatency.store(mEng->outputLatency());
        mRendered.store(from);
        mLatency.store(mOut->latency());
        mRun.store(true);
        mThread = std::thread([this] { run(); });
        return true;
    }

    void Player::stop()
    {
        if (mThread.joinable())
        {
            mRun.store(false);
            mThread.join();
        }
        if (mOut) mOut->flush();
        mOut.reset();
        mInput.reset(); // R-REC-1: the capture closes with the stream it was read beside
        // anything still queued (a swap the thread never took) is the service's to free
        Live m;
        while (mIn.pop(m))
            if (m.kind == Live::Swap) delete m.engine;
        delete mEng;
        mEng = nullptr;
    }

    long long Player::heard() const
    {
        // during a count-in the song has not begun: what is heard is that many frames before where it will (R-MIDI-2)
        const long long r = mRendered.load() - mCountIn.load() - (long long)std::llround(mLatency.load() * mRate) - mGraphLatency.load();
        return mCountIn.load() > 0 ? r : std::max(0LL, r);
    }

    void Player::collect()
    {
        engine::Engine *e = nullptr;
        while (mRetired.pop(e)) delete e;
    }

    void Player::apply(const Live &m)
    {
        switch (m.kind)
        {
        case Live::StripGain: mEng->setStripGain(m.strip, m.value); break;
        case Live::StripPan: mEng->setStripPan(m.strip, m.value); break;
        case Live::StripSilent: mEng->setStripSilent(m.strip, m.value != 0); break;
        case Live::MasterGain: mEng->setMasterGain(m.value); break;
        case Live::DeviceBypass: mEng->setDeviceBypass(m.strip, m.device, m.value != 0); break;
        case Live::DeviceParam:
            if (m.strip < 0) mEng->setMasterDeviceParam(m.device, m.name, m.value);
            else mEng->setDeviceParam(m.strip, m.device, m.name, m.value);
            break;
        case Live::Seek: mEng->seek(m.sample); break;
        case Live::ClearMeters: mClearNow = true; break; // R-METER-2: done in `measure`, after this block renders
        case Live::Swap:
        {
            // A structural edit: the new engine continues from here; the old one goes back to be freed.
            m.engine->seek(mEng->position());
            engine::Engine *old = mEng;
            mEng = m.engine;
            mGeneration = m.generation;
            mGraphLatency.store(mEng->outputLatency(), std::memory_order_relaxed);
            mEng->prepare(mPb, mBlock); // the port count may have changed: within capacity it does not allocate
            mEng->tapMaster(mTapL.data(), mTapR.data(), mBlock);
            if (!mRetired.push(old)) delete old; // the service is not collecting: better a free here than a leak
            break;
        }
        }
    }

    void Player::keepLive(long long out)
    {
        // R-MIX-16: stores only — no allocation, no lock (R-PLAY-2)
        const auto &v = mEng->bindValues();
        const int n = (int)std::min(v.size(), (size_t)kMaxBinds);
        const long long k = mSlotsWritten.load(std::memory_order_relaxed);
        LiveSlot &s = mSlots[(size_t)(k % kLiveSlots)];
        const unsigned q = s.seq.load(std::memory_order_relaxed);
        s.seq.store(q + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        for (int i = 0; i < n; ++i) s.value[i].store(v[(size_t)i], std::memory_order_relaxed);
        s.count.store(n, std::memory_order_relaxed);
        s.generation.store(mGeneration, std::memory_order_relaxed);
        s.out.store(out, std::memory_order_relaxed);
        s.seq.store(q + 2, std::memory_order_release);
        mSlotsWritten.store(k + 1, std::memory_order_release);
    }

    bool Player::liveValues(int generation, std::vector<double> &out) const
    {
        const long long n = mSlotsWritten.load(std::memory_order_acquire);
        if (n <= 0) return false;
        const long long lag = (long long)std::llround(mLatency.load(std::memory_order_relaxed) * mRate) +
                              mGraphLatency.load(std::memory_order_relaxed); // R-MIX-17: the engine's own lag too
        // newest first: the first slot of this engine at or before what is heard; else the oldest kept of it.
        // The slot the thread may be rewriting next is never read.
        long long target = 0, pick = -1;
        bool first = true;
        for (long long k = n - 1; k >= 0 && k > n - kLiveSlots; --k)
        {
            const LiveSlot &s = mSlots[(size_t)(k % kLiveSlots)];
            const unsigned q = s.seq.load(std::memory_order_acquire);
            const long long at = s.out.load(std::memory_order_relaxed);
            const int gen = s.generation.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if ((q & 1u) || s.seq.load(std::memory_order_relaxed) != q) continue;
            if (first) { target = at - lag; first = false; }
            if (gen != generation) continue; // an engine the service has since replaced
            pick = k;
            if (at <= target) break;
        }
        if (pick < 0) return false;
        const LiveSlot &s = mSlots[(size_t)(pick % kLiveSlots)];
        const unsigned q = s.seq.load(std::memory_order_acquire);
        const int count = s.count.load(std::memory_order_relaxed), gen = s.generation.load(std::memory_order_relaxed);
        if (gen != generation) return false;
        std::vector<double> v((size_t)std::max(0, count));
        for (int i = 0; i < count; ++i) v[(size_t)i] = s.value[i].load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if ((q & 1u) || s.seq.load(std::memory_order_relaxed) != q) return false; // overwritten while read: keep the last
        out.swap(v);
        return true;
    }

    void Player::clearMeters()
    {
        mClearPending.store(true, std::memory_order_release);
        Live m;
        m.kind = Live::ClearMeters;
        if (!mIn.push(m)) mClearPending.store(false); // a full queue: nothing will take it
    }

    void Player::loudness(Loudness &out) const
    {
        out.momentary = mMomentary.load(std::memory_order_relaxed);
        out.shortTerm = mShortTerm.load(std::memory_order_relaxed);
        out.integrated = mIntegrated.load(std::memory_order_relaxed);
        out.truePeak = mTruePeak.load(std::memory_order_relaxed);
        out.correlation = mCorrelation.load(std::memory_order_relaxed);
        for (int b = 0; b < SpectrumAnalyser::kBands; ++b) out.spectrum[(size_t)b] = mSpectrum[(size_t)b].load(std::memory_order_relaxed);
    }

    void Player::ballistics(int k, float peak, double meanSquare, int n)
    {
        // R-METER-2: RMS through a 300 ms one-pole; the held peak kept 1.5 s, then falling 20 dB a second
        const double a = 1.0 - std::exp(-(double)n / (0.3 * mRate));
        mMeanSq[(size_t)k] += a * (meanSquare - mMeanSq[(size_t)k]);
        mRms[(size_t)k].store((float)std::sqrt(std::max(0.0, mMeanSq[(size_t)k])), std::memory_order_relaxed);
        double &held = mHeld[(size_t)k];
        long long &age = mHeldFor[(size_t)k];
        if (peak >= held) { held = peak; age = 0; }
        else if ((age += n) > (long long)(1.5 * mRate)) held = std::max((double)peak, held * std::pow(10.0, -20.0 * n / mRate / 20.0));
        mHold[(size_t)k].store((float)held, std::memory_order_relaxed);
    }

    void Player::measure(int n)
    {
        // stores and the meters' own processing only — no allocation, no lock (R-PLAY-2)
        if (mClearNow)
        {
            mClearNow = false;
            mLoud->reset(); // the spectrum is not reset: it is a 300 ms view, and its reset would allocate
            mTrue->reset();
            mCorr->reset();
            mLoudBlocks = -1;
            std::fill(mHeld.begin(), mHeld.end(), 0.0);
            std::fill(mHeldFor.begin(), mHeldFor.end(), 0);
            for (int i = 0; i <= kMaxStrips; ++i) mClip[(size_t)i].store(false, std::memory_order_relaxed);
            mClearPending.store(false, std::memory_order_release); // what is measured from here is after the clear
        }
        const auto &meters = mEng->stripMeters();
        for (size_t s = 0; s < meters.size() && s < (size_t)kMaxStrips; ++s)
        {
            for (int c = 0; c < 2; ++c)
                ballistics((int)(2 * s) + c, meters[s].peak[c], (double)meters[s].rms[c] * meters[s].rms[c], n);
            if (meters[s].peak[0] >= 1.0f || meters[s].peak[1] >= 1.0f) mClip[s].store(true, std::memory_order_relaxed);
        }
        const float *L = mTapL.data(), *R = mTapR.data();
        double sum[2] = {0, 0};
        for (int i = 0; i < n; ++i) { sum[0] += (double)L[i] * L[i]; sum[1] += (double)R[i] * R[i]; }
        const auto &mm = mEng->masterMeter();
        for (int c = 0; c < 2; ++c) ballistics(2 * kMaxStrips + c, mm.peak[c], n > 0 ? sum[c] / n : 0.0, n);
        if (mm.peak[0] >= 1.0f || mm.peak[1] >= 1.0f) mClip[kMaxStrips].store(true, std::memory_order_relaxed);
        // R-METER-1: the master's loudness, true peak, correlation and spectrum
        mLoud->process(L, R, n);
        mTrue->process(L, R, n);
        mCorr->process(L, R, n);
        mSpec->process(L, R, n);
        mMomentary.store(mLoud->momentary(), std::memory_order_relaxed);
        mShortTerm.store(mLoud->shortTerm(), std::memory_order_relaxed);
        if (mLoud->blocks() != mLoudBlocks) // the gated mean walks its histogram: only when a block was added
        {
            mLoudBlocks = mLoud->blocks();
            mIntegrated.store(mLoud->integrated(), std::memory_order_relaxed);
        }
        mTruePeak.store(mTrue->peakDb(), std::memory_order_relaxed);
        mCorrelation.store(mCorr->correlation(), std::memory_order_relaxed);
        for (int b = 0; b < SpectrumAnalyser::kBands; ++b) mSpectrum[(size_t)b].store((float)mSpec->bandDb(b), std::memory_order_relaxed);
    }

    void Player::midi()
    {
        // R-MIDI-2: no allocation, no lock — the engine's live ring, the pedal's bits, the record queue
        if (!mMidi) return;
        const int strip = mArmed.load(std::memory_order_relaxed);
        const bool recording = mRecording.load(std::memory_order_relaxed);
        auto play = [&](int s, int pitch, int velocity) {
            mEng->liveNote(s, pitch, velocity);
            if (velocity > 0) mSounding.set((size_t)pitch);
            else mSounding.reset((size_t)pitch);
            if (recording && s == strip) mPlayed.push(Played{heard(), pitch, velocity});
        };
        if (strip != mArmedWas)
        {
            // armed elsewhere: what was still held on the strip it left is let go there
            for (int p = 0; p < 128; ++p)
                if (mSounding.test((size_t)p) && mArmedWas >= 0) play(mArmedWas, p, 0);
            mSounding.reset();
            mPedal.reset();
            mArmedWas = strip;
        }
        MidiEvent e;
        while (mMidi->pop(e))
        {
            if (strip < 0) continue;
            const int pitch = e.d1 & 0x7F;
            if (e.noteOn())
            {
                if (mPedal.on(pitch)) play(strip, pitch, 0); // struck again under the pedal: a fresh attack
                play(strip, pitch, e.d2 & 0x7F);
            }
            else if (e.noteOff())
            {
                if (mPedal.off(pitch)) play(strip, pitch, 0);
            }
            else if (e.kind() == 0xE0 || (e.kind() == 0xB0 && e.d1 == 1))
            {
                // R-MIDI-3: the wheels — into the armed strip's instrument, kept while recording
                const bool bend = e.kind() == 0xE0;
                const double v = bend ? std::clamp(((int)((e.d2 & 0x7F) << 7 | (e.d1 & 0x7F)) - 8192) / 8192.0, -1.0, 1.0) : (e.d2 & 0x7F) / 127.0;
                mEng->liveControl(strip, bend ? 0 : 1, v);
                if (recording) mPlayed.push(Played{heard(), 0, 0, bend ? 1 : 2, v});
            }
            else if (e.kind() == 0xB0 && e.d1 == 64) mPedal.pedal(e.d2, [&](int p) { play(strip, p, 0); });
            else if (e.kind() == 0xB0 && (e.d1 == 120 || e.d1 == 123)) // all sound / all notes off
            {
                mPedal.reset();
                for (int p = 0; p < 128; ++p)
                    if (mSounding.test((size_t)p)) play(strip, p, 0);
            }
        }
    }

    void Player::click(long long pos, int n)
    {
        // every beat in [pos, pos + n) on its exact sample — the bar's first accented (R-TIME-4)
        const double spb = mSpb.load(std::memory_order_relaxed);
        const int bpb = mBpb.load(std::memory_order_relaxed);
        if (!mClick || spb <= 0.0) return;
        std::fill(mClickL.begin(), mClickL.begin() + n, 0.0);
        std::fill(mClickR.begin(), mClickR.begin() + n, 0.0);
        int cur = 0;
        auto run = [&](int to) {
            if (to <= cur) return;
            Sample *io[2] = {mClickL.data() + cur, mClickR.data() + cur};
            mClick->process(io, 2, to - cur);
            cur = to;
        };
        // R-TEMPO-1: under a tempo that changes, each beat where the playing engine's map puts it
        const engine::TempoMap *tm = mEng && !mEng->graph().clock.map.isConstant() ? &mEng->graph().clock.map : nullptr;
        for (long long k = (long long)std::ceil((tm ? tm->beatAt((double)pos) : (double)pos / spb) - 1e-9);; ++k)
        {
            const long long at = std::llround(tm ? tm->sampleAt((double)k) : (double)k * spb);
            if (at >= pos + n) break;
            if (at < pos) continue;
            run((int)(at - pos));
            const bool bar = k % bpb == 0;
            mClick->noteOn(bar ? kClickBar : kClickBeat, bar ? 127 : 96);
        }
        run(n);
        const double g = mClickGain.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            mInterleaved[(size_t)i * mChannels] += (float)(g * mClickL[(size_t)i]);
            if (mChannels > 1) mInterleaved[(size_t)i * mChannels + 1] += (float)(g * mClickR[(size_t)i]);
        }
    }

    void Player::run()
    {
        while (mRun.load())
        {
            Live m;
            while (mIn.pop(m)) apply(m);
            midi(); // R-MIDI-2: a key is heard from this block
            if (const long long left = mCountIn.load(std::memory_order_relaxed); left > 0)
            {
                // R-MIDI-2: the count-in — silence, the click on the song's beats leading into `from` (the engine waits);
                // aligned as playing is: the music of `from` comes out the engine's latency after the count-in ends
                const int n = (int)std::min<long long>(mBlock, left);
                input(n, mEng->position(), false); // R-REC-1: read, neither heard nor kept
                std::fill(mInterleaved.begin(), mInterleaved.begin() + (size_t)n * mChannels, 0.0f);
                click(mEng->position() - left - mGraphLatency.load(std::memory_order_relaxed), n);
                mOut->write(mInterleaved.data(), n);
                mLatency.store(mOut->latency(), std::memory_order_relaxed);
                mOutFrames += n;
                mCountIn.store(left - n, std::memory_order_relaxed);
                mClickWas = true;
                continue;
            }
            const long long pos = mEng->position(), a = mLoopA.load(), b = mLoopB.load();
            int n = mBlock;
            if (b > a && pos < b && pos + n > b) n = (int)(b - pos); // stop exactly at the loop's end
            input(n, pos, true); // R-REC-1: the armed strip hears it in this block; a take keeps it from here
            mEng->render(n, mPb);
            std::fill(mInterleaved.begin(), mInterleaved.begin() + (size_t)n * mChannels, 0.0f);
            for (const Route &r : mRoutes)
            {
                if (r.port < 0 || r.port >= (int)mPb.ports.size()) continue;
                const auto &ch = mPb.ports[(size_t)r.port];
                for (size_t c = 0; c < ch.size(); ++c)
                {
                    const int dst = r.channel + (int)c;
                    if (dst >= mChannels) break;
                    for (int i = 0; i < n; ++i) mInterleaved[(size_t)i * mChannels + dst] += ch[c][(size_t)i];
                }
            }
            const auto &meters = mEng->stripMeters();
            for (size_t s = 0; s < meters.size() && s < (size_t)kMaxStrips; ++s)
            {
                mPeaks[2 * s].store(meters[s].peak[0], std::memory_order_relaxed);
                mPeaks[2 * s + 1].store(meters[s].peak[1], std::memory_order_relaxed);
            }
            mPeaks[2 * kMaxStrips].store(mEng->masterMeter().peak[0], std::memory_order_relaxed);
            mPeaks[2 * kMaxStrips + 1].store(mEng->masterMeter().peak[1], std::memory_order_relaxed);
            measure(n);
            const bool clickOn = mClickOn.load(std::memory_order_relaxed);
            if (clickOn) click(pos - mGraphLatency.load(std::memory_order_relaxed), n); // on the beat the music is on (R-MIX-17)
            else if (mClickWas && mClick) mClick->reset(); // off: no tail waits to resume
            mClickWas = clickOn;
            mOut->write(mInterleaved.data(), n); // blocks: the device is the clock
            mLatency.store(mOut->latency(), std::memory_order_relaxed);
            mOutFrames += n;
            keepLive(mOutFrames);
            if (b > a && mEng->position() >= b) mEng->seek(a);
            mRendered.store(mEng->position());
        }
    }
}
}
