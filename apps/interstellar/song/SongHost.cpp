/*
 *  interstellar_song — the hosted Solaris song (R-SONG). See SongHost.h for why this is the only
 *  file in Interstellar's core that includes Solaris.
 */
#include "SongHost.h"

#include "../core/AudioOut.h"   // Interstellar's, by path: this target's include path is Solaris's
#include "Compile.h"            // Solaris's from here on
#include "Engine.h"
#include "Event.h"
#include "Project.h"
#include "SolarisService.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace arstro
{
namespace interstellar
{
    namespace
    {
        namespace sol = arstro::solaris;

        std::string quoted(const std::string &s)
        {
            if (!s.empty() && s.find_first_of(" \t\"") == std::string::npos) return s;
            return "\"" + s + "\"";
        }

        /** Interstellar's sound output, as Solaris's player asks for one: the device and the buffer depth are
         *  Interstellar's to choose (its output knows them), the channels are always two. */
        class OutBridge : public sol::IAudioOut
        {
        public:
            explicit OutBridge(std::unique_ptr<interstellar::IAudioOut> o) : mOut(std::move(o)) {}
            bool open(const std::string &, int rate, int channels, int, std::string &err) override
            {
                if (channels != 2) { err = "Interstellar's output is stereo"; return false; }
                return mOut && mOut->open(rate, err);
            }
            bool write(const float *interleaved, int frames) override { return mOut->write(interleaved, frames); }
            double latency() override { return mOut->latency(); }
            void flush() override { mOut->flush(); }

        private:
            std::unique_ptr<interstellar::IAudioOut> mOut;   // inside the class, a bare IAudioOut is Solaris's (the base)
        };
    }

    struct SongHost::Program
    {
        sol::engine::MixGraph graph;
        long long start = 0, end = 0;    // song samples: where the span starts, where it stops sounding
        double seconds = 0;              // the span a clip places
        int rate = 48000;
    };

    struct SongHost::Impl
    {
        std::unique_ptr<sol::SolarisService> svc;
        std::vector<std::string> *events = nullptr;     // set while a line runs
        std::string path;
    };

    SongHost::SongHost(Decode decode, AudioOut audioOut) : d(new Impl)
    {
        sol::SolarisService::Host host;
        // samples the song names, decoded by Interstellar's own decoder (its host's FFmpeg)
        if (decode)
            host.decodeAudio = [decode](const std::string &path, int rate, sol::engine::Pcm &out, std::string &err) {
                std::vector<float> stereo;
                long long frames = 0;
                if (!decode(path, rate, stereo, frames, err)) return false;
                out.channels = 2;
                out.frames = frames;
                out.samples = std::move(stereo);
                return true;
            };
        // the Song page's transport plays through Interstellar's output
        if (audioOut)
            host.audioOut = [audioOut]() -> std::unique_ptr<sol::IAudioOut> {
                std::unique_ptr<IAudioOut> o = audioOut();
                if (!o) return nullptr;
                return std::unique_ptr<sol::IAudioOut>(new OutBridge(std::move(o)));
            };
        // No settings, recents, presets, library or plugin paths: hosting a song changes nothing of the
        // machine's Solaris. (A VST3 the song uses is offline here — silent or dry, said by Solaris.)
        d->svc.reset(new sol::SolarisService(host));
        d->svc->subscribe([this](const sol::Event &e) {
            if (d->events) d->events->push_back(sol::formatEvent(e));
        });
    }

    SongHost::~SongHost() = default;

    bool SongHost::open(const std::string &path, std::string &err)
    {
        std::error_code ec;
        const std::string abs = fs::absolute(path, ec).lexically_normal().string();
        std::vector<std::string> ev;
        d->events = &ev;
        const bool ok = d->svc->dispatchText("project open " + quoted(abs), err);
        d->events = nullptr;
        if (ok) d->path = abs;
        return ok;
    }

    bool SongHost::run(const std::string &line, std::string &output, std::vector<std::string> &events, std::string &err)
    {
        d->events = &events;
        const bool ok = d->svc->dispatchText(line, err);
        d->events = nullptr;
        output = d->svc->output();
        return ok;
    }

    bool SongHost::save(std::string &err)
    {
        std::vector<std::string> ev;
        d->events = &ev;
        const bool ok = d->svc->dispatchText("project save", err);
        d->events = nullptr;
        return ok;
    }

    void SongHost::pump() { d->svc->pump(); }

    namespace
    {
        SongHost::Facts factsFrom(const sol::AppModel &m, const sol::Project *p, const std::string &path)
        {
            SongHost::Facts f;
            f.path = path;
            f.name = m.projectName;
            f.bpm = m.bpm;
            f.rate = m.sampleRate > 0 ? m.sampleRate : 48000;
            f.lengthBeats = m.lengthBeats;
            f.lengthSeconds = m.lengthSeconds;
            f.dirty = m.dirty;
            f.lastError = m.lastError;
            f.versions.push_back(SongHost::Version{"main", "main", "", false, ""});
            std::string open = "main";
            for (const auto &v : m.versions)
            {
                f.versions.push_back(SongHost::Version{v.id, v.name, v.base, v.frozen, v.problem});
                if (v.id == m.version) open = v.name;
            }
            f.open = open;
            for (const auto &mk : m.markers)
                f.sections.push_back(SongHost::Section{mk.id, mk.name, mk.at, mk.end, mk.seconds, mk.endSeconds});
            // R-SONG-8: the lanes as the song shows them, each clip placed through the tempo map
            std::vector<const sol::LaneModel *> lanes;
            for (const auto &l : m.lanes)
                if (l.automation.empty()) lanes.push_back(&l);
            std::stable_sort(lanes.begin(), lanes.end(), [](const sol::LaneModel *a, const sol::LaneModel *b) { return a->order < b->order; });
            const double rate = f.rate;
            for (const auto *l : lanes)
            {
                SongHost::Lane ln;
                ln.id = l->id;
                ln.name = l->name;
                ln.colour = l->colour;
                for (const auto &c : m.clips)
                {
                    if (c.lane != l->id || c.kind == "automation" || !(c.length > 0)) continue;
                    const double a = p ? (double)sol::beatsToSamples(*p, c.at) / rate : c.at * 60.0 / std::max(1.0, m.bpm);
                    const double b = p ? (double)sol::beatsToSamples(*p, c.at + c.length) / rate : (c.at + c.length) * 60.0 / std::max(1.0, m.bpm);
                    ln.spans.emplace_back(a, b);
                }
                f.lanes.push_back(std::move(ln));
            }
            return f;
        }
    }

    SongHost::Facts SongHost::facts() const { return factsFrom(d->svc->model(), d->svc->project(), d->path); }

    bool SongHost::factsOf(const std::string &version, Facts &out, std::string &err) const
    {
        const Facts now = facts();
        const std::string want = version.empty() ? "main" : version;
        if (want == now.open) { out = now; return true; }
        // a copy opens it: the hosted song keeps the version its user had open
        sol::SolarisService probe(*d->svc);
        if (!probe.dispatchText("version open " + quoted(want), err)) return false;
        out = factsFrom(probe.model(), probe.project(), d->path);
        return true;
    }

    namespace
    {
        /** A clip's span in `f`: the whole song, or a section by id (first) or name. */
        bool spanIn(const SongHost::Facts &f, const std::string &section, double &fromBeat, double &toBeat, double &seconds, std::string &err)
        {
            fromBeat = 0;
            toBeat = -1;
            if (section.empty())
            {
                seconds = f.lengthSeconds;
                if (!(seconds > 0)) { err = f.name + " has nothing in it to hear — no clip ends after its start"; return false; }
                return true;
            }
            const SongHost::Section *s = nullptr;
            for (const auto &x : f.sections)
                if (x.id == section) s = &x;
            for (const auto &x : f.sections)
                if (!s && x.name == section) s = &x;
            if (!s)
            {
                err = "the song has no section " + section + (f.sections.empty() ? std::string(" (it has no markers)") : std::string());
                if (!f.sections.empty())
                {
                    err += " — it has:";
                    for (const auto &x : f.sections) err += " " + quoted(x.name);
                }
                return false;
            }
            if (!(s->toBeat > s->fromBeat) || !(s->toSec > s->fromSec)) { err = "the section " + section + " is empty"; return false; }
            fromBeat = s->fromBeat;
            toBeat = s->toBeat;
            seconds = s->toSec - s->fromSec;
            return true;
        }
    }

    bool SongHost::span(const std::string &version, const std::string &section, double &seconds, std::string &err) const
    {
        Facts f;
        if (!factsOf(version, f, err)) return false;
        double from = 0, to = 0;
        return spanIn(f, section, from, to, seconds, err);
    }

    unsigned long long SongHost::fingerprint() const
    {
        const sol::Project *p = d->svc->project();
        if (!p) return 0;
        const std::string text = sol::serializeProject(*p);
        unsigned long long h = 1469598103934665603ULL;
        for (unsigned char c : text) { h ^= c; h *= 1099511628211ULL; }
        return h;
    }

    long long SongHost::revision() const { return d->svc->model().revision; }

    std::shared_ptr<const SongHost::Program> SongHost::program(const std::string &version, const std::string &section, std::string &err) const
    {
        Facts f;
        if (!factsOf(version, f, err)) return nullptr;
        double from = 0, to = -1, seconds = 0;
        if (!spanIn(f, section, from, to, seconds, err)) return nullptr;
        sol::CompileResult cr;
        sol::Project song;
        if (!d->svc->compileVersion(version.empty() ? "main" : version, cr, song, err)) return nullptr;
        auto p = std::make_shared<Program>();
        p->rate = song.header.sampleRate > 0 ? song.header.sampleRate : 48000;
        p->start = sol::beatsToSamples(song, from);
        // a section stops exactly at its end, as `render --to` does; the whole song rings on past its last clip,
        // as `render` does (ten seconds at most) — the clip's own length decides what of that is heard
        p->end = to >= 0 ? sol::beatsToSamples(song, to) : std::max(p->start, cr.graph.end) + 10LL * p->rate;
        p->seconds = seconds;
        p->graph = std::move(cr.graph);
        return p;
    }

    namespace
    {
        /** One Program played. Its engine renders in the chunks `render` renders in, from wherever it was last
         *  sought, so a read of any size hears the very samples `solaris-cc render` writes (a device whose work is
         *  per block is not shifted by the reader's block size). */
        class LiveStream : public SongHost::Stream
        {
        public:
            static constexpr int kChunk = 4096;   // ServiceRender.cpp's

            explicit LiveStream(std::shared_ptr<const SongHost::Program> p) : mP(std::move(p)) {}

            bool build(std::string &err)
            {
                if (!mEng.build(mP->graph, err)) return false;
                mEng.captureMaster(true);
                mEng.prepare(mPorts, kChunk);
                mLag = mEng.outputLatency();
                return true;
            }

            int rate() const override { return mP->rate; }
            double seconds() const override { return mP->seconds; }

            void read(long long start, int frames, float *stereo) override
            {
                std::memset(stereo, 0, sizeof(float) * (size_t)frames * 2);
                const long long len = mP->end - mP->start;
                const long long lo = std::max<long long>(start, 0), hi = std::min<long long>(start + frames, len);
                if (hi <= lo) return;
                // the engine's output is `mLag` behind the song (R-MIX-17): frame i of the span is output i + lag
                if (!mSought || lo + mLag < mBufAt || lo + mLag > mBufAt + bufFrames() + kChunk)
                {
                    mEng.seek(mP->start + lo);
                    mSought = true;
                    mBuf.clear();
                    mBufAt = lo + mLag;   // the buffer's first frame is span frame `lo` (indexed by span frame + lag)
                    mRendered = 0;
                }
                while (mBufAt + bufFrames() < hi + mLag) renderChunk();
                for (long long i = lo; i < hi; ++i)
                {
                    const size_t k = (size_t)(i + mLag - mBufAt) * 2;
                    stereo[(size_t)(i - start) * 2] = mBuf[k];
                    stereo[(size_t)(i - start) * 2 + 1] = mBuf[k + 1];
                }
                // keep what a read just behind this one could still ask (a mixer's overlap), drop the rest
                const long long keepFrom = hi + mLag - kChunk;
                if (keepFrom > mBufAt)
                {
                    mBuf.erase(mBuf.begin(), mBuf.begin() + (std::ptrdiff_t)((keepFrom - mBufAt) * 2));
                    mBufAt = keepFrom;
                }
            }

        private:
            long long bufFrames() const { return (long long)mBuf.size() / 2; }

            void renderChunk()
            {
                mEng.render(kChunk, mPorts);
                const auto &m = mEng.capturedMaster();
                // the output starts `lag` after the seek: the first `lag` frames rendered are before the span frame sought
                for (int i = 0; i < kChunk; ++i)
                {
                    const long long outIdx = mRendered + i;   // engine output index since the seek
                    if (outIdx < mLag) continue;
                    mBuf.push_back(m[0][(size_t)i]);
                    mBuf.push_back(m.size() > 1 ? m[1][(size_t)i] : m[0][(size_t)i]);
                }
                mRendered += kChunk;
            }

            std::shared_ptr<const SongHost::Program> mP;
            sol::engine::Engine mEng;
            sol::engine::PortBuffers mPorts;
            long long mLag = 0, mBufAt = 0, mRendered = 0;
            bool mSought = false;
            std::vector<float> mBuf;   // interleaved; its frame j is span frame (mBufAt − lag + j)
        };
    }

    std::unique_ptr<SongHost::Stream> SongHost::stream(const std::shared_ptr<const Program> &p, std::string &err)
    {
        if (!p) { err = "no program"; return nullptr; }
        auto s = std::unique_ptr<LiveStream>(new LiveStream(p));
        if (!s->build(err)) return nullptr;
        return s;
    }

    const solaris::AppModel &SongHost::model() const { return d->svc->model(); }
    const std::string &SongHost::path() const { return d->path; }
}
}
