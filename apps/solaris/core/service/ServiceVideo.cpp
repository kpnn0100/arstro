// SolarisService — the song's picture (R-VIDEO-1; docs/discussion.md Round 18): one video a song, its first frame
// `offset` seconds into the song's clock. The host reads it (the core carries no codec, R-SVC-4); the model says where
// the picture is at the heard position (`video.time`), so a front end never re-derives the tempo map (law 13).
#include "Format.h"
#include "SolarisService.h"
#include <cmath>
#include <filesystem>

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    const VideoInfo *SolarisService::videoInfo(const std::string &src)
    {
        const std::string path = resolvePath(src);
        auto it = mVideoInfo.find(path);
        if (it == mVideoInfo.end())
        {
            VideoInfo vi;
            std::string why;
            const bool ok = mHost.probeVideo && mHost.probeVideo(path, vi, why);
            it = mVideoInfo.emplace(path, std::make_pair(ok, vi)).first;
        }
        return it->second.first ? &it->second.second : nullptr;
    }

    double SolarisService::videoTime(double beat)
    {
        if (mProject.videos.empty()) return -1.0;
        const Video &v = mProject.videos[0];
        const VideoInfo *vi = videoInfo(v.src);
        const double t = sampleOfBeat(beat) / mProject.header.sampleRate - v.offset; // through the tempo map
        if (t < 0 || (vi && vi->duration > 0 && t > vi->duration)) return -1.0;
        return t;
    }

    void SolarisService::fillVideo(double beat)
    {
        VideoModel &m = mModel.video;
        m = VideoModel();
        if (mProject.videos.empty()) return;
        const Video &v = mProject.videos[0];
        m.id = v.id;
        m.src = v.src;
        m.offset = v.offset;
        const VideoInfo *vi = videoInfo(v.src);
        m.offline = !vi;
        if (vi)
        {
            m.duration = vi->duration;
            m.width = vi->width;
            m.height = vi->height;
            m.fps = vi->fps;
        }
        m.time = videoTime(beat);
        // where it lies on the song's beats — a picture begun before the song, at beat 0's tempo
        m.path = resolvePath(v.src);
        auto beatAt = [this](double sec) {
            if (sec >= 0) return beatOfSample(sec * mProject.header.sampleRate);
            double bpm = 0;
            std::string id;
            tempoAt(0, bpm, id);
            return sec * bpm / 60.0;
        };
        m.at = beatAt(v.offset);
        m.end = beatAt(v.offset + m.duration);
    }

    bool SolarisService::videoCommand(const Command &c, std::string &err)
    {
        if (!requireOpen(err)) return false;
        Project &p = mProject;
        if (c.kind == K::VideoRemove)
        {
            if (p.videos.empty()) { err = "the song has no video"; return false; }
            const std::string id = p.videos[0].id;
            p.videos.clear();
            changed("video.removed", id);
            return true;
        }
        if (!mHost.probeVideo) { err = "this build reads no video"; return false; }
        // a relative path names a file in the song's folder when one is there, else the caller's working directory
        std::string given = c.arg(0);
        if (!std::filesystem::path(given).is_absolute() && !mPath.empty() && std::filesystem::exists(resolvePath(given))) given = resolvePath(given);
        const std::string stored = relativePath(given);
        VideoInfo vi;
        std::string why;
        if (!mHost.probeVideo(resolvePath(stored), vi, why)) { err = "cannot read " + c.arg(0) + " as a video" + (why.empty() ? "" : ": " + why); return false; }
        mVideoInfo[resolvePath(stored)] = {true, vi};
        double offset = p.videos.empty() ? 0.0 : p.videos[0].offset;
        if (c.has("offset") && (!parseNumber(c.flag("offset"), offset) || !std::isfinite(offset) || std::fabs(offset) > 36000))
        {
            err = "--offset is seconds, −36000 … 36000";
            return false;
        }
        if (p.videos.empty())
        {
            Video v;
            v.id = p.nextId("vd");
            p.videos.push_back(v);
        }
        p.videos[0].src = stored;
        p.videos[0].offset = offset;
        mOutput = p.videos[0].id + "\n";
        changed("video.set", p.videos[0].id);
        return true;
    }
}
}
