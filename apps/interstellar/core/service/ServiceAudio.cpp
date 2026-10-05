/*
 *  interstellar_core — the service's sound (R-AUD-5 amended, R-AUD-9): a timeline resolved into the
 *  mixer's plan, and the decoders each thread reads it through.
 *
 *  What sounds: every `#aclip` (picture is a `#clip`, sound is an `#aclip` — project-format §4) on
 *  an audio lane, either an audio-kind `#track` or a suite-schema `#atrack`: its file from `in`, its
 *  own gain and fades, its lane's gain (and an `#atrack`'s pan). A muted lane is silent; when any
 *  `#atrack` is soloed only the soloed ones sound. A camera file's dialogue is an `#aclip` naming
 *  that file (`audio clip add --src <bind>` places a rack source's sound). Buses, sends and
 *  instruments — the rest of the suite schema — parse and round-trip but sum straight to the master
 *  (said in DR-AUD-2).
 */
#include "ServiceInternal.h"
#include "Versions.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace arstro
{
namespace interstellar
{
    bool InterstellarService::planAudio(const NodeId &tl, render::AudioPlan &out)
    {
        out = render::AudioPlan{};
        if (!mOpen) return false;
        const Project &P = *mProject;
        out.rate = P.sampleRate > 0 ? P.sampleRate : 48000;
        out.masterDb = P.masterGain;
        ResolvedTimeline R;
        std::string err;
        if (!resolved(tl, R, err)) return false;
        auto dangling = [&](const NodeId &id) {
            const auto p = R.provenance.find(id);
            return p != R.provenance.end() && p->second == Provenance::Dangling;
        };
        // the lanes: audio-kind #tracks and the suite schema's #atracks
        struct Lane { double gain = 0, pan = 0; bool mute = false, solo = false; };
        std::map<NodeId, Lane> lanes;
        bool anySolo = false;
        for (const auto &t : R.tracks)
            if (t.audio()) lanes[t.id] = Lane{t.gain, 0.0, t.mute, false};
        for (const auto &t : R.audioTracks)
        {
            lanes[t.id] = Lane{t.gain, t.pan, t.mute, t.solo};
            anySolo = anySolo || t.solo;
        }
        for (const auto &a : R.audioClips)
        {
            const auto ln = lanes.find(a.track);
            if (ln == lanes.end() || dangling(a.id) || a.src.empty()) continue;
            const Lane &l = ln->second;
            if (l.mute || (anySolo && !l.solo)) continue;
            render::AudioItem it;
            it.media = resolvePath(a.src);
            it.at = a.at;
            it.in = a.in;
            it.out = a.out;
            it.gainDb = a.gain + l.gain;
            it.fadeIn = a.fadeIn;
            it.fadeOut = a.fadeOut;
            it.pan = std::clamp(l.pan, -1.0, 1.0);
            out.items.push_back(it);
        }
        return true;
    }

    IAudioSource *InterstellarService::audioSourceFor(RenderCtx &ctx, const std::string &media, int rate)
    {
        return audioSourceIn(ctx.audio, media, rate);
    }

    IAudioSource *InterstellarService::audioSourceIn(std::map<std::string, std::unique_ptr<IAudioSource>> &cache, const std::string &media, int rate)
    {
        const std::string key = media + "@" + std::to_string(rate);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second.get();
        std::unique_ptr<IAudioSource> s;
        if (mHost.audioSource)
        {
            s = mHost.audioSource();
            IAudioSource::Info info;
            if (s && !s->open(media, rate, info)) s.reset();   // no audio stream: silent, remembered as such
        }
        IAudioSource *raw = s.get();
        cache[key] = std::move(s);
        return raw;
    }

    // ── playback heard (R-AUD-6) and its meters (R-AUD-8) ─────────────────────────────────────────

    void InterstellarService::startSound()
    {
        if (!mPlayer || !mOpen) return;
        auto plan = std::make_shared<render::AudioPlan>();
        if (!planAudio(currentTimeline(), *plan) || plan->empty()) { mSoundClock = false; return; }
        AudioPlayer &p = *mPlayer;
        {
            std::lock_guard<std::mutex> l(p.mu);
            if (p.failed) { mSoundClock = false; return; }   // no sound server: picture only, as before
            p.rate = plan->rate;
            p.from = std::llround(mModel.playhead * plan->rate);
            p.written = 0;
            p.latency = 0.0;
            p.plan = plan;
            p.playing = true;
            p.blocks.clear();
            ++p.gen;
        }
        p.cv.notify_all();
        mSoundClock = true;
        mSoundSeq = mProjectRev;
    }

    void InterstellarService::stopSound()
    {
        mSoundClock = false;
        if (!mPlayer) return;
        AudioPlayer &p = *mPlayer;
        {
            std::lock_guard<std::mutex> l(p.mu);
            if (!p.playing) return;
            p.playing = false;
            p.blocks.clear();
            ++p.gen;   // the thread flushes what the device still holds: a pause is heard to stop
        }
        p.cv.notify_all();
    }

    void InterstellarService::soundGrain(double t)
    {
        if (!mPlayer || !mOpen) return;
        auto plan = std::make_shared<render::AudioPlan>();
        if (!planAudio(currentTimeline(), *plan) || plan->empty()) return;
        AudioPlayer &p = *mPlayer;
        {
            std::lock_guard<std::mutex> l(p.mu);
            if (p.playing || p.failed) return;
            p.rate = plan->rate;
            p.plan = plan;
            p.grains.push_back(std::llround(t * plan->rate));
        }
        p.cv.notify_all();
    }

    bool InterstellarService::soundTime(double &t)
    {
        if (!mSoundClock || !mPlayer) return false;
        AudioPlayer &p = *mPlayer;
        if (p.failed) { mSoundClock = false; return false; }
        const double heard = (double)p.written.load() / p.rate - p.latency.load();
        // until the device has played into what was written, the ear is still at the start
        t = ((double)p.from / p.rate) + std::max(0.0, heard);
        return true;
    }

    void InterstellarService::syncSoundPlan()
    {
        if (!mSoundClock || !mPlayer || mSoundSeq == mProjectRev) return;
        mSoundSeq = mProjectRev;
        auto plan = std::make_shared<render::AudioPlan>();
        planAudio(currentTimeline(), *plan);
        {
            std::lock_guard<std::mutex> l(mPlayer->mu);
            mPlayer->plan = plan;   // heard after what the device already holds — a few tens of ms
        }
    }

    void InterstellarService::playerLoop()
    {
        AudioPlayer &p = *mPlayer;
        unsigned seen = ~0u;
        std::vector<float> buf;
        for (;;)
        {
            std::shared_ptr<const render::AudioPlan> plan;
            bool play = false;
            long long at = 0, grain = -1;
            unsigned gen = 0;
            {
                std::unique_lock<std::mutex> l(p.mu);
                p.cv.wait(l, [&] { return p.quit || p.playing || !p.grains.empty() || p.gen != seen; });
                if (p.quit) return;
                gen = p.gen;
                plan = p.plan;
                play = p.playing;
                if (play) at = p.from + p.written.load();
                else if (!p.grains.empty())
                {
                    grain = p.grains.back();   // a scrub moves faster than grains play: the newest wins
                    p.grains.clear();
                }
            }
            if (!p.opened && !p.failed)
            {
                std::string err;
                p.opened = p.out && p.out->open(p.rate, err);
                if (!p.opened)
                {
                    std::lock_guard<std::mutex> l(p.mu);
                    p.failed = true;
                    p.playing = false;
                    continue;
                }
            }
            if (!p.opened) continue;
            if (gen != seen)
            {
                p.out->flush();
                seen = gen;
                if (!play && grain < 0) continue;
            }
            const int rate = plan ? plan->rate : p.rate;
            auto sourceFor = [&](const std::string &m) { return audioSourceIn(p.sources, m, rate); };
            if (play)
            {
                const int n = 1024;
                buf.assign((size_t)n * 2, 0.0f);
                if (plan) render::mixAudio(*plan, sourceFor, at, n, buf.data());
                {
                    std::lock_guard<std::mutex> l(p.mu);
                    if (p.gen != gen) continue;   // paused or moved while mixing: not a sample of it is heard
                }
                if (!p.out->write(buf.data(), n)) continue;
                AudioPlayer::Block b{at, n, {0, 0}, {0, 0}};
                double sq[2] = {0, 0};
                for (int i = 0; i < n; ++i)
                    for (int c = 0; c < 2; ++c)
                    {
                        const float v = std::fabs(buf[(size_t)i * 2 + c]);
                        b.peak[c] = std::max(b.peak[c], v);
                        sq[c] += (double)v * v;
                    }
                for (int c = 0; c < 2; ++c) b.rms[c] = (float)std::sqrt(sq[c] / n);
                {
                    std::lock_guard<std::mutex> l(p.mu);
                    if (p.gen != gen) continue;
                    p.written += n;
                    p.blocks.push_back(b);
                    while (p.blocks.size() > (size_t)(rate / n + 1)) p.blocks.pop_front();
                    if (b.peak[0] >= 1.0f || b.peak[1] >= 1.0f) p.clipFrame = at;
                }
                p.latency = p.out->latency();
            }
            else if (grain >= 0)
            {
                // a scrub grain: 80 ms where the playhead landed, eased in and out over 4 ms so it clicks not
                const int n = (int)(rate * 0.08), ramp = (int)(rate * 0.004);
                buf.assign((size_t)n * 2, 0.0f);
                if (plan) render::mixAudio(*plan, sourceFor, grain, n, buf.data());
                for (int i = 0; i < ramp && i < n; ++i)
                {
                    const float g = (float)i / ramp;
                    for (int c = 0; c < 2; ++c) { buf[(size_t)i * 2 + c] *= g; buf[(size_t)(n - 1 - i) * 2 + c] *= g; }
                }
                p.out->write(buf.data(), n);
            }
        }
    }

    void InterstellarService::fillSoundModel(AppModel &m)
    {
        m.soundPlaying = mSoundClock;
        m.meterPeakL = m.meterPeakR = m.meterRmsL = m.meterRmsR = 0.0;
        m.meterClip = false;
        m.peaksEpoch = mPeaks ? mPeaks->epoch.load() : 0;
        if (!mSoundClock || !mPlayer) return;
        AudioPlayer &p = *mPlayer;
        std::lock_guard<std::mutex> l(p.mu);
        // the block the listener hears now — the device holds `latency` of what was written
        const long long heard = p.from + p.written.load() - std::llround(p.latency.load() * p.rate);
        const AudioPlayer::Block *now = nullptr;
        for (const auto &b : p.blocks)
            if (b.frame <= heard) now = &b;
        if (!now && !p.blocks.empty()) now = &p.blocks.front();
        if (now)
        {
            m.meterPeakL = now->peak[0];
            m.meterPeakR = now->peak[1];
            m.meterRmsL = now->rms[0];
            m.meterRmsR = now->rms[1];
        }
        m.meterClip = heard - p.clipFrame >= 0 && heard - p.clipFrame < 3LL * p.rate;   // held three seconds
    }

    // ── waveforms (R-AUD-7) ──────────────────────────────────────────────────────────────────────

    namespace
    {
        uint64_t fnv(const std::string &s)
        {
            uint64_t h = 1469598103934665603ULL;
            for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
            return h;
        }
    }

    void InterstellarService::requestPeaks(const std::string &media)
    {
        if (!mPeaks || media.empty()) return;
        PeakStore &ps = *mPeaks;
        {
            std::lock_guard<std::mutex> l(ps.mu);
            if (!mIspPath.empty())
            {
                const fs::path isp(mIspPath);
                ps.dir = (isp.parent_path() / (isp.stem().string() + ".peaks")).string();
            }
            if (ps.asked.count(media)) return;
            ps.asked.insert(media);
            ps.queue.push_back(media);
        }
        ps.cv.notify_all();
    }

    bool InterstellarService::audioPeaks(const std::string &media, std::vector<float> &peaks, double &perSecond)
    {
        if (!mPeaks) return false;
        std::lock_guard<std::mutex> l(mPeaks->mu);
        const auto it = mPeaks->done.find(media);
        if (it == mPeaks->done.end()) return false;
        peaks = it->second;
        perSecond = PeakStore::kPerSecond;
        return true;
    }

    void InterstellarService::peaksLoop()
    {
        PeakStore &ps = *mPeaks;
        for (;;)
        {
            std::string media, dir;
            {
                std::unique_lock<std::mutex> l(ps.mu);
                ps.cv.wait(l, [&] { return ps.quit || !ps.queue.empty(); });
                if (ps.quit) return;
                media = ps.queue.front();
                ps.queue.pop_front();
                dir = ps.dir;
            }
            // the cache file is named by what the envelope depends on: the file, its size, its time
            std::error_code ec;
            const auto size = fs::file_size(media, ec);
            const auto mtime = ec ? 0 : (long long)fs::last_write_time(media, ec).time_since_epoch().count();
            char name[40];
            std::snprintf(name, sizeof name, "%016llx.pk", (unsigned long long)fnv(media + "|" + std::to_string((long long)size) + "|" + std::to_string(mtime)));
            const std::string file = dir.empty() ? std::string() : dir + "/" + name;
            std::vector<float> peaks;
            bool have = false;
            if (!file.empty())
            {
                std::ifstream in(file, std::ios::binary);
                char magic[4] = {0};
                uint64_t count = 0;
                if (in.read(magic, 4) && std::memcmp(magic, "ISPK", 4) == 0 && in.read(reinterpret_cast<char *>(&count), sizeof count) && count < (1ULL << 28))
                {
                    peaks.resize((size_t)count);
                    have = (bool)in.read(reinterpret_cast<char *>(peaks.data()), (std::streamsize)(count * sizeof(float)));
                }
            }
            if (!have)
            {
                std::unique_ptr<IAudioSource> src = mHost.audioSource ? mHost.audioSource() : nullptr;
                IAudioSource::Info info;
                const int rate = 48000, bucket = (int)(rate / PeakStore::kPerSecond);
                if (!src || !src->open(media, rate, info)) continue;   // no sound: no envelope, nothing drawn
                const long long total = (long long)std::ceil(info.duration * rate);
                std::vector<float> buf((size_t)rate * 2);
                peaks.assign((size_t)((total + bucket - 1) / bucket), 0.0f);
                for (long long at = 0; at < total; at += rate)
                {
                    const int n = (int)std::min<long long>(rate, total - at);
                    src->read(at, n, buf.data());
                    for (int i = 0; i < n; ++i)
                    {
                        float &pk = peaks[(size_t)((at + i) / bucket)];
                        pk = std::max(pk, std::max(std::fabs(buf[(size_t)i * 2]), std::fabs(buf[(size_t)i * 2 + 1])));
                    }
                    std::lock_guard<std::mutex> l(ps.mu);
                    if (ps.quit) return;
                }
                if (!file.empty())
                {
                    fs::create_directories(dir, ec);
                    std::ofstream out(file, std::ios::binary);
                    const uint64_t count = peaks.size();
                    out.write("ISPK", 4);
                    out.write(reinterpret_cast<const char *>(&count), sizeof count);
                    out.write(reinterpret_cast<const char *>(peaks.data()), (std::streamsize)(count * sizeof(float)));
                }
            }
            {
                std::lock_guard<std::mutex> l(ps.mu);
                ps.done[media] = std::move(peaks);
            }
            ps.epoch.fetch_add(1);
        }
    }
}
}
