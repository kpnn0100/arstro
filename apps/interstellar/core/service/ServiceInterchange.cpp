/*
 *  interstellar_core — the service's interchange (R-XCH): a resolved timeline out to EDL / FCPXML /
 *  OTIO, and any of them in as a new root timeline. The formats themselves are `xch::` (pure text);
 *  this file is the mapping to and from the project — the rack, the tracks, the clips — and the
 *  media's own timecode and reel (R-XCH-5), read from the files through the frame source.
 */
#include "ServiceInternal.h"
#include "Arrange.h"
#include "Interchange.h"
#include "Versions.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace arstro
{
namespace interstellar
{
    using CK = Command::Kind;
    using EK = Event::Kind;

    namespace
    {
        std::string lower(std::string s)
        {
            for (char &c : s) c = (char)std::tolower((unsigned char)c);
            return s;
        }
        std::string formatOf(const Command &c, const std::string &path)
        {
            if (c.has("format")) return lower(c.flag("format"));
            const std::string ext = lower(fs::path(path).extension().string());
            if (ext == ".edl") return "edl";
            if (ext == ".fcpxml" || ext == ".xml") return "fcpxml";
            if (ext == ".otio") return "otio";
            if (ext == ".aaf") return "aaf";
            return std::string();
        }
        /** A timeline name an address can spell (`social30`, not `Social 30s!`). */
        std::string bindable(std::string s)
        {
            for (char &c : s) if (!std::isalnum((unsigned char)c) && c != '_') c = '_';
            if (s.empty() || std::isdigit((unsigned char)s[0])) s = "tl_" + s;
            return s;
        }
    }

    bool InterstellarService::interchangeCommand(const Command &c)
    {
        Project &P = *mProject;
        std::string err;

        if (c.kind == CK::InterchangeExport)
        {
            const NodeId tl = timelineRef(c.arg(0));
            if (tl.empty()) return fail("interchange export: no timeline named " + c.arg(0));
            const std::string out = c.flag("out");
            if (out.empty()) return fail("interchange export: --out <path> is required");
            const std::string format = formatOf(c, out);
            if (format == "aaf")
                return fail("interchange export: AAF has no library in this build (R-XCH-4) — export OTIO and convert it with "
                            "OpenTimelineIO's `otioconvert -i cut.otio -o cut.aaf` (the otio-aaf-adapter plugin)");
            if (format != "edl" && format != "fcpxml" && format != "otio")
                return fail("interchange export: --format is edl, fcpxml or otio (or .edl/.fcpxml/.otio on --out)");
            ResolvedTimeline R;
            if (!resolved(tl, R, err)) return fail("interchange export: " + err);

            xch::XTimeline t;
            t.name = P.timeline(tl)->name;
            t.fps = P.fps;
            t.width = P.width;
            t.height = P.height;
            if (c.has("start"))
            {
                long long f = 0;
                const xch::TcRate rate = xch::TcRate::of(P.fps);
                if (!xch::framesFromTc(c.flag("start"), rate, f)) return fail("interchange export: --start is HH:MM:SS:FF, got " + c.flag("start"));
                t.recordStart = f / rate.fps;
            }
            // tracks: video V1 = the lowest; audio lanes (#track kind=audio and #atrack) A1 = the lowest
            std::vector<const Track *> video;
            std::vector<std::pair<int, NodeId>> lanes;
            for (const auto &tr : R.tracks)
            {
                if (tr.audio()) lanes.push_back({tr.order, tr.id});
                else video.push_back(&tr);
            }
            for (const auto &a : R.audioTracks) lanes.push_back({a.order, a.id});
            std::stable_sort(video.begin(), video.end(), [](const Track *a, const Track *b) { return a->order < b->order; });
            std::stable_sort(lanes.begin(), lanes.end());
            std::map<NodeId, int> vNum, aNum;
            for (size_t i = 0; i < video.size(); ++i) vNum[video[i]->id] = (int)i + 1;
            for (size_t i = 0; i < lanes.size(); ++i) aNum[lanes[i].second] = (int)i + 1;
            auto dangling = [&](const NodeId &id) {
                const auto p = R.provenance.find(id);
                return p != R.provenance.end() && p->second == Provenance::Dangling;
            };
            // media: each file once, with what the file says about itself (R-XCH-5)
            std::map<std::string, int> mediaOf;
            auto mediaFor = [&](const std::string &path, bool isVideo) {
                const auto f = mediaOf.find(path);
                if (f != mediaOf.end()) return f->second;
                xch::XMedia m;
                m.path = path;
                m.name = fs::path(path).stem().string();
                m.video = isVideo;
                m.audio = !isVideo;
                if (isVideo)
                    if (Source *s = source(*mSync, path); s && s->ok)
                    {
                        const double sfps = s->info.fps > 0 ? s->info.fps : P.fps;
                        m.duration = s->info.frames > 1 ? s->info.frames / sfps : 0.0;
                        m.reel = s->info.reel;
                        m.audio = s->info.hasAudio;
                        long long f = 0;
                        if (!s->info.timecode.empty() && xch::framesFromTc(s->info.timecode, xch::TcRate::of(sfps), f)) m.tcStart = f / sfps;
                    }
                if (!isVideo && mHost.audioSource)
                {
                    // the file's length — every format names the media's available range
                    std::unique_ptr<IAudioSource> probe = mHost.audioSource();
                    IAudioSource::Info ai;
                    if (probe && probe->open(path, P.sampleRate > 0 ? P.sampleRate : 48000, ai)) m.duration = ai.duration;
                }
                const int i = (int)t.media.size();
                t.media.push_back(m);
                mediaOf[path] = i;
                return i;
            };
            std::map<NodeId, int> clipIndex;
            for (const auto &cl : R.clips)
            {
                if (dangling(cl.id) || !vNum.count(cl.track)) continue;
                const RackObj *ro = P.rackObj(cl.src);
                if (!ro || ro->media.empty()) continue;
                xch::XClip x;
                x.media = mediaFor(resolvePath(ro->media), true);
                x.name = cl.name;
                x.track = vNum[cl.track];
                x.at = cl.at;
                x.in = cl.in;
                x.out = cl.out;
                x.speed = cl.speed;
                clipIndex[cl.id] = (int)t.clips.size();
                t.clips.push_back(x);
            }
            for (const auto &a : R.audioClips)
            {
                if (dangling(a.id) || !aNum.count(a.track) || a.src.empty()) continue;
                xch::XClip x;
                x.media = mediaFor(resolvePath(a.src), false);
                x.name = a.name;
                x.track = aNum[a.track];
                x.audio = true;
                x.at = a.at;
                x.in = a.in;
                x.out = a.out;
                x.gainDb = a.gain;
                t.clips.push_back(x);
            }
            int dips = 0;
            for (const auto &x : R.transitions)
            {
                if (!clipIndex.count(x.clipA) || !clipIndex.count(x.clipB)) continue;
                if (x.kind != "dissolve") ++dips;
                t.transitions.push_back({clipIndex[x.clipA], clipIndex[x.clipB], x.dur});
            }
            std::string text;
            std::string note;
            if (format == "edl")
            {
                int track = 1;
                if (c.has("track"))
                {
                    track = std::atoi(c.flag("track").c_str());
                    if (track < 1 || track > (int)video.size()) return fail("interchange export: --track is 1 … " + std::to_string(video.size()));
                }
                text = xch::writeEdl(t, track);
                if (video.size() > 1 || t.audioTracks() > 0) note = " (an EDL carries one video track: V" + std::to_string(track) + ")";
            }
            else text = format == "fcpxml" ? xch::writeFcpxml(t) : xch::writeOtio(t);
            if (dips) note += " (" + std::to_string(dips) + " dip(s) written as dissolves)";
            std::ofstream f(out, std::ios::binary);
            if (!f || !(f << text)) return fail("interchange export: cannot write " + out);
            mOutput = "exported " + std::to_string(t.clips.size()) + " clips of " + t.name + " to " + out + note + "\n";
            emit(Event(EK::Info).with("text", "interchange export: " + out));
            return true;
        }

        // ── import ──
        const std::string in = c.arg(0);
        std::ifstream f(in, std::ios::binary);
        if (!f) return fail("interchange import: cannot read " + in);
        const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const std::string format = formatOf(c, in);
        if (format == "aaf") return fail("interchange import: AAF has no library in this build (R-XCH-4) — convert it to OTIO with `otioconvert` first");
        double fps = P.fps;
        if (c.has("fps"))
        {
            const std::string fs_ = c.flag("fps");
            const auto slash = fs_.find('/');
            fps = std::atof(fs_.c_str());
            if (slash != std::string::npos) { const double den = std::atof(fs_.c_str() + slash + 1); if (den > 0) fps /= den; }
            if (!(fps > 0)) return fail("interchange import: --fps is a rate or num/den, got " + fs_);
        }
        xch::XTimeline t;
        bool ok = false;
        if (format == "edl") ok = xch::readEdl(text, fps, t, err);
        else if (format == "fcpxml") ok = xch::readFcpxml(text, t, err);
        else if (format == "otio") ok = xch::readOtio(text, t, err);
        else return fail("interchange import: the format is edl, fcpxml or otio (by --format or the extension)");
        if (!ok) return fail("interchange import: " + err);
        if (mPending) return fail("interchange import: the rack is still loading — `wait rack.loaded` first");

        // find each medium: as written, else by clip name or reel under --media
        const std::string mediaDir = c.flag("media");
        std::vector<std::string> pool;
        if (!mediaDir.empty())
        {
            std::error_code ec;
            for (auto it = fs::recursive_directory_iterator(mediaDir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
                if (it->is_regular_file(ec) && it.depth() <= 3) pool.push_back(it->path().string());
        }
        std::vector<std::string> found(t.media.size()), missing;
        for (size_t i = 0; i < t.media.size(); ++i)
        {
            const xch::XMedia &m = t.media[i];
            std::error_code ec;
            if (!m.path.empty() && fs::exists(m.path, ec)) { found[i] = fs::absolute(m.path).lexically_normal().string(); continue; }
            for (const auto &p : pool)
            {
                const std::string stem = lower(fs::path(p).stem().string());
                if ((!m.name.empty() && stem == lower(m.name)) || (!m.reel.empty() && stem == lower(m.reel)) ||
                    (!m.path.empty() && lower(fs::path(m.path).filename().string()) == lower(fs::path(p).filename().string())))
                {
                    found[i] = fs::absolute(p).lexically_normal().string();
                    break;
                }
            }
            if (found[i].empty()) missing.push_back(!m.name.empty() ? m.name : m.reel);
        }
        // the picture media into the rack (what is not there already), in one add
        std::vector<std::string> toAdd;
        std::map<std::string, NodeId> rackFor;
        for (const auto &ro : P.rackObjs)
            if (ro.kind != "group" && !ro.media.empty()) rackFor.emplace(fs::absolute(resolvePath(ro.media)).lexically_normal().string(), ro.id);
        for (size_t i = 0; i < t.media.size(); ++i)
        {
            bool picture = false;
            for (const auto &x : t.clips) picture = picture || (x.media == (int)i && !x.audio);
            if (picture && !found[i].empty() && !rackFor.count(found[i]) && std::find(toAdd.begin(), toAdd.end(), found[i]) == toAdd.end()) toAdd.push_back(found[i]);
        }
        if (!toAdd.empty())
        {
            Command add;
            add.kind = CK::RackAdd;
            add.args = toAdd;
            if (!rackCommand(add)) return false;
            for (const auto &ro : P.rackObjs)
                if (ro.kind != "group" && !ro.media.empty()) rackFor.emplace(fs::absolute(resolvePath(ro.media)).lexically_normal().string(), ro.id);
        }
        // an EDL's source times are timecode: take each file's own start off (R-XCH-5)
        std::vector<double> tcOff(t.media.size(), 0.0);
        for (size_t i = 0; i < t.media.size(); ++i)
            if (t.media[i].tcStart < 0 && !found[i].empty())
                if (Source *s = source(*mSync, found[i]); s && s->ok && !s->info.timecode.empty())
                {
                    long long fr = 0;
                    const double sfps = s->info.fps > 0 ? s->info.fps : t.fps;
                    if (xch::framesFromTc(s->info.timecode, xch::TcRate::of(sfps), fr)) tcOff[i] = fr / sfps;
                }

        // the timeline: a new root, its tracks, its clips
        std::string name = bindable(c.has("name") ? c.flag("name") : (!t.name.empty() ? t.name : fs::path(in).stem().string()));
        for (int k = 2; P.nameIsTaken(name); ++k) name = bindable((c.has("name") ? c.flag("name") : t.name.empty() ? fs::path(in).stem().string() : t.name)) + "_" + std::to_string(k);
        NodeId tl;
        if (!newTimeline(P, name, NodeId(), tl, err)) return fail("interchange import: " + err);
        std::map<int, NodeId> vTrack, aTrack;
        for (int v = 1; v <= std::max(1, t.videoTracks()); ++v)
        {
            NodeId id;
            // track names are bind names, project-wide: the timeline's own (`from_edl_v1`)
            if (!arrange::addTrack(P, tl, "video", P.freshName(name + "_v" + std::to_string(v)), id, err)) return fail("interchange import: " + err);
            vTrack[v] = id;
        }
        for (int a = 1; a <= t.audioTracks(); ++a)
        {
            NodeId id;
            if (!arrange::addTrack(P, tl, "audio", P.freshName(name + "_a" + std::to_string(a)), id, err)) return fail("interchange import: " + err);
            aTrack[a] = id;
        }
        std::map<int, NodeId> placed;
        int skipped = 0;
        for (int i = 0; i < (int)t.clips.size(); ++i)
        {
            const xch::XClip &x = t.clips[(size_t)i];
            const std::string path = x.media >= 0 ? found[(size_t)x.media] : std::string();
            if (path.empty()) { ++skipped; continue; }
            const double off = x.media >= 0 ? tcOff[(size_t)x.media] : 0.0;
            const double sIn = std::max(0.0, x.in - off), sOut = std::max(sIn, x.out - off);
            if (x.audio)
            {
                AClip a;
                a.id = P.freshId("aclp_");
                a.name = P.freshName(bindable(fs::path(path).stem().string()));
                a.track = aTrack[x.track];
                a.timeline = tl;
                a.src = relativePath(path);
                a.at = x.at;
                a.in = sIn;
                a.out = sOut;
                a.gain = x.gainDb;
                P.audioClips.push_back(a);
                continue;
            }
            const auto ro = rackFor.find(path);
            if (ro == rackFor.end()) { ++skipped; continue; }
            NodeId id;
            if (!arrange::addClip(P, tl, vTrack[x.track], ro->second, sIn, sOut, x.at, std::string(), id, err)) { ++skipped; continue; }
            if (std::fabs(x.speed - 1.0) > 1e-9)
            {
                std::string e2;
                setField(P, tl, id, "speed", canonicalNumber(x.speed), e2);
            }
            placed[i] = id;
        }
        int dissolves = 0;
        for (const auto &x : t.transitions)
            if (placed.count(x.from) && placed.count(x.to))
            {
                NodeId id;
                std::string e2;
                if (arrange::addTransition(P, tl, placed[x.from], placed[x.to], "dissolve", x.dur, id, e2)) ++dissolves;
            }
        P.current = tl;
        markDirty();
        bumpFrame();
        std::ostringstream o;
        o << "imported " << placed.size() << " clips, " << dissolves << " dissolves into timeline " << name;
        if (skipped) o << " — " << skipped << " not placed, media not found: " << [&] {
            std::string s;
            for (size_t k = 0; k < missing.size() && k < 8; ++k) s += (k ? ", " : "") + missing[k];
            return s + (missing.size() > 8 ? ", …" : "") + (mediaDir.empty() ? " (give --media <dir> to search)" : "");
        }();
        mOutput = o.str() + "\n";
        emit(Event(EK::Info).with("text", "interchange import: " + o.str()));
        return true;
    }
}
}
