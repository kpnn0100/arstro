/*
 *  interstellar_core — a Solaris song as a DAW project inside Interstellar (R-SONG).
 *
 *  A song clip is an `#aclip` whose `src` is a `.slp`. The song behind it is a hosted SolarisService
 *  (`SongHost`, the one adapter that sees Solaris), opened once per file however many clips place it —
 *  the whole project, edited by Solaris command lines (`song run`, and Solaris's own window on the Song
 *  page, R-SONG-7) and saved to its own file.
 *
 *  What a clip HEARS is the song played live (R-SONG-2, amended 2026-11-18 — "it not import as a wave"):
 *
 *    want   = path | version | section          what a clip asks for
 *    media  = "song:<n>"                        a Program: that want compiled at the song's `gen`
 *
 *  `songMedia` compiles a want when its song moved since (its fingerprint, so an undo back to the same
 *  text hears the same program again only after one compile); every reader of the media — the player,
 *  a delivery render — builds its own engine from the program on its own thread and plays it block by
 *  block. An edit bumps the song's `gen` and the project's revision, so the player's next plan names
 *  the new program: the edit is heard from the next block, with no render in between.
 */
#include "ServiceInternal.h"
#include "ServiceSong.h"
#include "Project.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace arstro
{
namespace interstellar
{
    using CK = Command::Kind;
    using EK = Event::Kind;

    namespace
    {
        bool parseNum(const std::string &s, double &out)
        {
            if (s.empty()) return false;
            char *end = nullptr;
            out = std::strtod(s.c_str(), &end);
            return end && *end == '\0' && std::isfinite(out);
        }

        std::string lower(std::string s)
        {
            for (auto &c : s) c = (char)std::tolower((unsigned char)c);
            return s;
        }

        std::string wantOf(const std::string &path, const std::string &version, const std::string &section)
        {
            return path + "|" + (version == "main" ? std::string() : version) + "|" + section;
        }
    }

    bool InterstellarService::isSongSrc(const std::string &src)
    {
        return lower(fs::path(src).extension().string()) == ".slp";
    }

#if INTERSTELLAR_HAVE_SONG

    namespace
    {
        /** A program played, read as any file is: interleaved stereo frames at the mix rate. At the song's own
         *  rate (the common case) the stream's samples ARE the read's; at another, they are resampled through a
         *  cubic (Catmull-Rom) over a short history, so a sequential read never asks the stream to go back. */
        class LiveSource : public IAudioSource
        {
        public:
            explicit LiveSource(std::unique_ptr<SongHost::Stream> s) : mS(std::move(s)) {}
            bool open(const std::string &, int rate, Info &out) override
            {
                mRate = rate;
                out.rate = rate;
                out.duration = mS->seconds();
                out.fileRate = mS->rate();
                out.fileChannels = 2;
                return out.valid();
            }
            bool read(long long start, int frames, float *stereo) override
            {
                if (frames <= 0) return true;
                if (mS->rate() == mRate) { mS->read(start, frames, stereo); return true; }
                const double step = (double)mS->rate() / mRate;
                const long long first = (long long)std::floor((double)start * step) - 1;
                const long long last = (long long)std::floor((double)(start + frames - 1) * step) + 2;
                have(first, last + 1);
                for (int i = 0; i < frames; ++i)
                {
                    const double at = (double)(start + i) * step;
                    const long long k = (long long)std::floor(at);
                    const float t = (float)(at - (double)k);
                    for (int c = 0; c < 2; ++c)
                    {
                        const float y0 = sample(k - 1, c), y1 = sample(k, c), y2 = sample(k + 1, c), y3 = sample(k + 2, c);
                        const float a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
                        const float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                        const float cc = -0.5f * y0 + 0.5f * y2;
                        stereo[(size_t)i * 2 + c] = ((a * t + b) * t + cc) * t + y1;
                    }
                }
                return true;
            }

        private:
            /** The stream's frames [lo, hi) in the history: continued when they follow it, read afresh when not. */
            void have(long long lo, long long hi)
            {
                const long long end = mAt + (long long)mHist.size() / 2;
                if (mHist.empty() || lo < mAt || lo > end)
                {
                    mAt = lo;
                    mHist.assign((size_t)(hi - lo) * 2, 0.0f);
                    mS->read(lo, (int)(hi - lo), mHist.data());
                }
                else if (hi > end)
                {
                    const size_t old = mHist.size();
                    mHist.resize((size_t)(hi - mAt) * 2);
                    mS->read(end, (int)(hi - end), mHist.data() + old);
                }
                // what a read just behind could still need: a few frames before `lo`
                const long long keep = lo - 4;
                if (keep > mAt)
                {
                    mHist.erase(mHist.begin(), mHist.begin() + (std::ptrdiff_t)((keep - mAt) * 2));
                    mAt = keep;
                }
            }
            float sample(long long k, int c) const
            {
                const long long j = k - mAt;
                return j < 0 || j >= (long long)mHist.size() / 2 ? 0.0f : mHist[(size_t)j * 2 + c];
            }
            std::unique_ptr<SongHost::Stream> mS;
            int mRate = 48000;
            long long mAt = 0;
            std::vector<float> mHist;
        };

        /** The song a reference names: its file (as given, or as a clip names it), its name, or a clip of it. */
        std::string songPathFor(const InterstellarService::SongState &S, const Project *P, const std::string &ref,
                                const std::function<std::string(const std::string &)> &resolve)
        {
            std::error_code ec;
            const std::string asPath = fs::absolute(resolve(ref), ec).lexically_normal().string();
            if (S.slots.count(asPath)) return asPath;
            for (const auto &kv : S.slots)
                if (kv.second.host && kv.second.host->facts().name == ref) return kv.first;
            for (const auto &kv : S.slots)
                if (fs::path(kv.first).stem().string() == ref) return kv.first;
            if (P)
                if (const AClip *a = P->audioClip(P->idForRef(ref)); a && InterstellarService::isSongSrc(a->src))
                {
                    const std::string p = fs::absolute(resolve(a->src), ec).lexically_normal().string();
                    if (S.slots.count(p)) return p;
                }
            return std::string();
        }

        /** Interstellar's decoder for the samples a song names: whole file, interleaved stereo, at `rate`. */
        SongHost::Decode decoderFrom(const std::function<std::unique_ptr<IAudioSource>()> &make)
        {
            if (!make) return SongHost::Decode();
            return [make](const std::string &path, int rate, std::vector<float> &stereo, long long &frames, std::string &err) {
                std::unique_ptr<IAudioSource> s = make();
                IAudioSource::Info info;
                if (!s || !s->open(path, rate, info)) { err = "cannot read " + path; return false; }
                frames = (long long)std::ceil(info.duration * rate);
                stereo.assign((size_t)frames * 2, 0.0f);
                const int chunk = 65536;
                for (long long at = 0; at < frames; at += chunk)
                    s->read(at, (int)std::min<long long>(chunk, frames - at), stereo.data() + (size_t)at * 2);
                return true;
            };
        }
    }

    static InterstellarService::SongState::Slot *openSlot(InterstellarService::SongState &S, const std::string &abs,
                                                          const InterstellarService::Host &h, std::string &err)
    {
        auto it = S.slots.find(abs);
        if (it != S.slots.end()) return &it->second;
        if (!fs::exists(abs)) { err = "no such song: " + abs; return nullptr; }
        auto host = std::make_unique<SongHost>(decoderFrom(h.audioSource), h.audioOut);
        if (!host->open(abs, err)) return nullptr;
        InterstellarService::SongState::Slot s;
        s.path = abs;
        s.fp = host->fingerprint();
        s.rev = host->revision();
        s.host = std::move(host);
        return &(S.slots[abs] = std::move(s));
    }

    SongHost *InterstellarService::songHost(const std::string &song)
    {
        const std::string path = songPathFor(*mSongs, mOpen ? mProject.get() : nullptr, song, [this](const std::string &r) { return resolvePath(r); });
        if (path.empty()) return nullptr;
        return mSongs->slots[path].host.get();
    }

    std::string InterstellarService::songMedia(const std::string &src, const std::string &version, const std::string &section)
    {
        SongState &S = *mSongs;
        std::error_code ec;
        const std::string abs = fs::absolute(resolvePath(src), ec).lexically_normal().string();
        std::string err;
        SongState::Slot *sl = openSlot(S, abs, mHost, err);
        if (!sl) return std::string();   // offline: the clip says so (fillSongModel)
        const std::string want = wantOf(abs, version, section);
        SongState::Heard &h = S.heard[want];
        if (h.made && h.gen == sl->gen) return h.media;
        // the song moved since (or it is the first ask): compile what the clip hears, as the song stands NOW
        std::shared_ptr<const SongHost::Program> p = sl->host->program(version, section, err);
        std::string old = h.media;
        h.made = true;
        h.gen = sl->gen;
        h.media.clear();
        h.err.clear();
        {
            std::lock_guard<std::mutex> l(S.mu);
            if (!old.empty()) S.programs.erase(old);   // a reader that opened it keeps its own engine
            if (p)
            {
                h.media = "song:" + std::to_string(++S.nextMedia);
                S.programs[h.media] = p;
            }
        }
        if (!p)
        {
            h.err = err;
            if (S.said.insert(want + "|" + std::to_string(sl->gen)).second)
                emit(Event(EK::SongFailed).with("song", sl->host->facts().name).with("version", version.empty() ? "main" : version)
                         .with("section", section).with("why", err));
        }
        return h.media;
    }

    std::unique_ptr<IAudioSource> InterstellarService::songSource(const std::string &media, int) const
    {
        std::shared_ptr<const SongHost::Program> p;
        {
            std::lock_guard<std::mutex> l(mSongs->mu);
            const auto it = mSongs->programs.find(media);
            if (it == mSongs->programs.end()) return nullptr;
            p = it->second;
        }
        std::string err;
        std::unique_ptr<SongHost::Stream> s = SongHost::stream(p, err);   // its engine, built on this thread
        if (!s) return nullptr;
        return std::unique_ptr<IAudioSource>(new LiveSource(std::move(s)));
    }

    // ── the songs, watched ──────────────────────────────────────────────────────────────────────

    namespace
    {
        /** True when the song's text moved since it was last looked at: its gen is bumped. */
        bool looked(InterstellarService::SongState::Slot &sl, double nowMs, bool now)
        {
            const long long rev = sl.host->revision();
            if (rev == sl.rev) return false;
            // a song playing on the Song page moves its revision every pump: its text is looked at four times a second
            if (!now && nowMs - sl.fpAtMs < 250.0) return false;
            sl.rev = rev;
            sl.fpAtMs = nowMs;
            const unsigned long long fp = sl.host->fingerprint();
            if (fp == sl.fp) return false;
            sl.fp = fp;
            ++sl.gen;
            return true;
        }
    }

    void InterstellarService::pumpSongs()
    {
        SongState &S = *mSongs;
        // the project's song clips are hosted as soon as it opens, so the Song page and `song run` find them
        if (mOpen)
            for (const auto &a : mProject->audioClips)
            {
                if (!isSongSrc(a.src)) continue;
                std::error_code ec;
                std::string err;
                openSlot(S, fs::absolute(resolvePath(a.src), ec).lexically_normal().string(), mHost, err);
            }
        bool moved = false;
        for (auto &kv : S.slots)
        {
            SongState::Slot &sl = kv.second;
            sl.host->pump();   // its transport (the Song page's), its meters, the file watch
            if (!looked(sl, mNowMs, false)) continue;
            moved = true;
            // an edit made here was said where it was made; what moves now came from elsewhere (a Solaris window's save)
            emit(Event(EK::SongChanged).with("song", sl.host->facts().name).with("what", "synced").with("line", ""));
        }
        if (moved)
        {
            ++mProjectRev;   // the player's next plan names the new program (syncSoundPlan); nothing the .isp saves changed
            bumpFrame();
            refreshModel();
        }
    }

    void InterstellarService::fillSongModel(AppModel &m)
    {
        SongState &S = *mSongs;
        m.songs.clear();
        m.songsDirty = false;
        for (const auto &kv : S.slots)
        {
            const SongHost::Facts f = kv.second.host->facts();
            SongModel g;
            g.name = f.name;
            g.path = kv.first;
            g.src = mOpen ? relativePath(kv.first) : kv.first;
            g.dirty = f.dirty;
            g.bpm = f.bpm;
            g.seconds = f.lengthSeconds;
            g.open = f.open;
            for (const auto &v : f.versions) g.versions.push_back({v.id, v.name, v.base, v.frozen, v.problem});
            for (const auto &x : f.sections) g.sections.push_back({x.id, x.name, x.fromSec, x.toSec});
            for (const auto &h : S.heard)
                if (h.first.compare(0, kv.first.size() + 1, kv.first + "|") == 0 && !h.second.err.empty() && g.error.empty()) g.error = h.second.err;
            m.songsDirty = m.songsDirty || g.dirty;
            m.songs.push_back(g);
        }
        std::sort(m.songs.begin(), m.songs.end(), [](const SongModel &a, const SongModel &b) { return a.name < b.name; });
        // the song clips: what each plays, and its version's lanes in the span it plays (R-SONG-8)
        for (auto &c : m.clips)
        {
            if (!c.audio || !isSongSrc(c.src)) continue;
            c.song = true;
            std::error_code ec;
            const std::string abs = fs::absolute(resolvePath(c.src), ec).lexically_normal().string();
            auto sl = S.slots.find(abs);
            if (sl == S.slots.end()) { c.offline = true; c.media.clear(); continue; }
            c.offline = false;
            c.media = songMedia(c.src, c.version, c.section);
            c.songError = S.heard[wantOf(abs, c.version, c.section)].err;
            SongState::VersionFacts &vf = S.versions[abs + "|" + (c.version.empty() ? std::string("main") : c.version)];
            if (!vf.made || vf.gen != sl->second.gen)
            {
                std::string err;
                vf.made = true;
                vf.gen = sl->second.gen;
                vf.ok = sl->second.host->factsOf(c.version, vf.facts, err);
            }
            if (!vf.ok) continue;
            double from = 0, len = vf.facts.lengthSeconds;
            for (const auto &x : vf.facts.sections)
                if (!c.section.empty() && (x.id == c.section || x.name == c.section)) { from = x.fromSec; len = x.toSec - x.fromSec; break; }
            for (const auto &ln : vf.facts.lanes)
            {
                ClipModel::SongLane l;
                l.name = ln.name;
                l.colour = ln.colour;
                for (const auto &sp : ln.spans)
                {
                    const double a = std::max(0.0, sp.first - from), b = std::min(len, sp.second - from);
                    if (b > a) l.spans.emplace_back(a, b);
                }
                c.songLanes.push_back(std::move(l));
            }
        }
    }

    bool InterstellarService::saveSongs(std::string &err)
    {
        for (auto &kv : mSongs->slots)
        {
            if (!kv.second.host->facts().dirty) continue;
            if (!kv.second.host->save(err)) { err = kv.second.host->facts().name + ": " + err; return false; }
            emit(Event(EK::SongChanged).with("song", kv.second.host->facts().name).with("what", "saved").with("line", ""));
        }
        return true;
    }

    void InterstellarService::closeSongs()
    {
        SongState &S = *mSongs;
        {
            std::lock_guard<std::mutex> l(S.mu);
            S.programs.clear();   // a reader that opened one keeps its own engine until it lets go
        }
        S.heard.clear();
        S.versions.clear();
        S.said.clear();
        S.slots.clear();
    }

    bool InterstellarService::songSelection(const NodeId &clip, const std::string &key, const std::string &value,
                                            std::vector<std::pair<std::string, std::string>> &fields, std::string &err)
    {
        const Project &P = *mProject;
        const AClip *a = P.audioClip(clip);
        if (!a || !isSongSrc(a->src)) { err = key + " is a song clip's (its src is a Solaris song)"; return false; }
        std::error_code ec;
        const std::string abs = fs::absolute(resolvePath(a->src), ec).lexically_normal().string();
        SongState::Slot *sl = openSlot(*mSongs, abs, mHost, err);
        if (!sl) return false;
        std::string version = key == "version" ? value : a->version, section = key == "section" ? value : a->section;
        if (version == "main") version.clear();
        double seconds = 0;
        if (!sl->host->span(version, section, seconds, err)) return false;
        fields = {{"version", version}, {"section", section}, {"in", "0"}, {"out", canonicalNumber(snapToFrame(seconds, P.fps))}};
        return true;
    }

    // ── the commands ────────────────────────────────────────────────────────────────────────────

    bool InterstellarService::songCommand(const Command &c)
    {
        SongState &S = *mSongs;
        mOutput.clear();
        auto resolveRef = [this](const std::string &r) { return resolvePath(r); };
        if (c.kind == CK::SongOpen || c.kind == CK::SongImport)
        {
            const std::string file = c.arg(0);
            const std::string verb = c.kind == CK::SongOpen ? "song open" : "song import";
            if (!isSongSrc(file)) return fail(verb + ": " + file + " is not a Solaris song (.slp)");
            std::error_code ec;
            const std::string abs = fs::absolute(mOpen ? resolvePath(file) : file, ec).lexically_normal().string();
            std::string err;
            const bool had = S.slots.count(abs) != 0;
            SongState::Slot *sl = openSlot(S, abs, mHost, err);
            if (!sl) return fail(verb + ": " + err);
            const SongHost::Facts f = sl->host->facts();
            if (!had)
                emit(Event(EK::SongOpened).with("song", f.name).with("path", abs).with("versions", (long long)f.versions.size())
                         .with("sections", (long long)f.sections.size()));
            if (c.kind == CK::SongOpen) { refreshModel(); return true; }

            Project &P = *mProject;
            const NodeId tl = currentTimeline();
            if (tl.empty()) return fail("song import: the project has no timeline — `timeline new main`");
            std::string version = c.flag("version"), section = c.flag("section");
            if (version == "main") version.clear();
            double seconds = 0;
            if (!sl->host->span(version, section, seconds, err)) return fail("song import: " + err);
            double at = 0, gain = 0, fade = 0;
            if (c.has("at") && (!parseNum(c.flag("at"), at) || at < 0)) return fail("song import: --at needs seconds, got `" + c.flag("at") + "`");
            if (c.has("gain") && !parseNum(c.flag("gain"), gain)) return fail("song import: --gain needs dB");
            if (c.has("fade") && (!parseNum(c.flag("fade"), fade) || fade < 0)) return fail("song import: --fade needs seconds");
            NodeId track;
            if (c.has("track"))
            {
                track = P.idForRef(c.flag("track"));
                const Track *vt = P.track(track);
                if (!P.audioTrack(track) && !(vt && vt->audio())) return fail("song import: no audio track named " + c.flag("track"));
            }
            else
            {
                // a track of its own, named after the song: a bed is a lane, as in any editor
                ATrack t;
                t.id = P.freshId("atrk_");
                std::string base = fileStem(abs), why;
                if (!Project::nameIsLegal(base, why)) base = "song";
                t.name = P.freshName(base);
                t.timeline = tl;
                for (const auto &x : P.audioTracks)
                    if (x.timeline == tl) t.order = std::max(t.order, x.order + 1);
                P.audioTracks.push_back(t);
                track = t.id;
            }
            AClip a;
            a.id = P.freshId("aclp_");
            {
                std::string base = fileStem(abs), why;
                if (!Project::nameIsLegal(base, why)) base = "song";
                a.name = P.freshName(base);
            }
            a.track = track;
            a.timeline = tl;
            a.src = relativePath(abs);
            a.version = version;
            a.section = section;
            a.at = snapToFrame(at, P.fps);
            a.in = 0;
            a.out = snapToFrame(seconds, P.fps);
            a.gain = gain;
            a.fadeIn = a.fadeOut = fade;
            P.audioClips.push_back(a);
            markDirty();
            bumpFrame();
            emit(Event(EK::ArrangeChanged).with("timeline", tl).with("what", "song imported").with("node", a.id));
            mOutput = a.id + "\n";
            refreshModel();
            return true;
        }

        if (c.kind == CK::SongRun)
        {
            const std::string path = songPathFor(S, mOpen ? mProject.get() : nullptr, c.arg(0), resolveRef);
            if (path.empty()) return fail("song run: no hosted song " + c.arg(0) + " — `song open <file.slp>` or `song import`");
            std::string line = c.arg(1);
            // the song stays the one this project placed: the lines that would swap it, write it elsewhere or
            // leave the app are Interstellar's
            std::vector<std::string> w;
            {
                std::string cur;
                for (char ch : line + " ")
                    if (ch == ' ' || ch == '\t') { if (!cur.empty()) w.push_back(cur); cur.clear(); } else cur += ch;
            }
            SongState::Slot &sl = S.slots[path];
            if (w.size() == 2 && w[0] == "project" && w[1] == "save")
            {
                // R-SONG-5: the song's own save (Ctrl+S on the Song page) — its own file, in place
                std::string err;
                if (!sl.host->save(err)) return fail("song run: " + err);
                mOutput = path + "\n";
                emit(Event(EK::SongChanged).with("song", sl.host->facts().name).with("what", "saved").with("line", ""));
                refreshModel();
                return true;
            }
            if (!w.empty() && w[0] == "project" && w.size() > 1 && (w[1] == "open" || w[1] == "new" || w[1] == "close" || w[1] == "save"))
                return fail("song run: `project " + w[1] + "` would " +
                            (w[1] == "save" ? "write the song somewhere else — it is saved to its own file (`song save`)" : "swap the hosted song for another"));
            if (!w.empty() && (w[0] == "render" || w[0] == "export"))
                return fail("song run: `" + w[0] + "` writes a file — Interstellar plays the song live; deliver it from the Deliver tab, or render it in Solaris itself");
            if (!w.empty() && w[0] == "quit") return fail("song run: `quit` is Interstellar's to give");
            std::string out, err;
            std::vector<std::string> events;
            const bool ok = sl.host->run(line, out, events, err);
            mOutput = out;
            for (const auto &e : events) mOutput += e + "\n";
            if (!ok) return fail("song run: " + err);
            if (looked(sl, mNowMs, true))
            {
                emit(Event(EK::SongChanged).with("song", sl.host->facts().name).with("what", "edited").with("line", line));
                ++mProjectRev;   // heard from the next block the player reads (syncSoundPlan)
                bumpFrame();
            }
            refreshModel();
            return true;
        }

        if (c.kind == CK::SongSave)
        {
            std::string err;
            if (c.args.empty())
            {
                if (!saveSongs(err)) return fail("song save: " + err);
                refreshModel();
                return true;
            }
            const std::string path = songPathFor(S, mOpen ? mProject.get() : nullptr, c.arg(0), resolveRef);
            if (path.empty()) return fail("song save: no hosted song " + c.arg(0));
            SongState::Slot &sl = S.slots[path];
            if (!sl.host->save(err)) return fail("song save: " + err);
            mOutput = path + "\n";
            emit(Event(EK::SongChanged).with("song", sl.host->facts().name).with("what", "saved").with("line", ""));
            refreshModel();
            return true;
        }
        return fail("not a song command");
    }

#else   // a build without Solaris: the grammar is the same, and every verb says why it cannot

    bool InterstellarService::songCommand(const Command &) { return fail("this build hosts no Solaris song (built without apps/solaris)"); }
    bool InterstellarService::songSelection(const NodeId &, const std::string &, const std::string &,
                                            std::vector<std::pair<std::string, std::string>> &, std::string &err)
    {
        err = "this build hosts no Solaris song";
        return false;
    }
    SongHost *InterstellarService::songHost(const std::string &) { return nullptr; }
    std::string InterstellarService::songMedia(const std::string &, const std::string &, const std::string &) { return std::string(); }
    std::unique_ptr<IAudioSource> InterstellarService::songSource(const std::string &, int) const { return nullptr; }
    void InterstellarService::pumpSongs() {}
    void InterstellarService::fillSongModel(AppModel &m)
    {
        for (auto &c : m.clips)
            if (c.audio && isSongSrc(c.src)) { c.song = true; c.offline = true; c.media.clear(); }
    }
    bool InterstellarService::saveSongs(std::string &) { return true; }
    void InterstellarService::closeSongs() {}

#endif
}
}
