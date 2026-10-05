/*
 *  interstellar_core — Interchange: a cut, in and out of the formats other editors read (R-XCH).
 *
 *  One neutral shape, `XTimeline` — clips on numbered video and audio tracks, each a span of a
 *  media file placed on the record clock, dissolves between neighbours — and a writer and reader
 *  per format:
 *
 *    EDL       CMX 3600, one video track per file: reel names (≤ 32 characters, as modern EDLs
 *              allow), source and record timecode, `D` dissolves, `M2` speed changes, and the
 *              `* FROM CLIP NAME:` / `* SOURCE FILE:` comments Resolve and Premiere write and read.
 *    FCPXML    1.9 — assets with `media-rep`, a primary storyline (the lowest video track, gaps
 *              between), every other track as connected clips on lanes (audio below), cross
 *              dissolves in the spine, speed as a linear `timeMap`.
 *    OTIO      OpenTimelineIO JSON — a Stack of Tracks of Clip.1 / Gap.1 / Transition.1, external
 *              references with their available range, speed as a LinearTimeWarp.
 *
 *  Time: a clip's source span is in the MEDIA's own seconds (0 = its first frame); each format
 *  speaks source timecode, so a file's timecode start (`XMedia::tcStart`, R-XCH-5) is added on the
 *  way out and taken off on the way in. The record clock starts at `recordStart` (01:00:00:00 by
 *  default, a broadcast habit every NLE expects). 29.97 and 59.94 are drop-frame where a format
 *  says timecode; 23.976 never is. Start times are REAL seconds of the frames they name (at 23.976,
 *  01:00:00:00 is 86400 frames = 3603.6 s), so frames ↔ seconds is one multiply at the rate.
 *
 *  Pure text in, text out: no file is opened here, no codec is named. Readers say why they refuse,
 *  with the line (EDL) or element (XML) that made them.
 */
#pragma once
#include "Timecode.h"
#include <map>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
namespace xch
{
    struct XMedia
    {
        std::string path;                       // absolute when known; "" when only a reel/name is
        std::string name;                       // the clip name editors see (a file stem)
        std::string reel;                       // R-XCH-5: from the file's tags, else the name
        double tcStart = 0.0;                   // the file's first frame as source timecode: frames / fps, seconds
        double duration = 0.0;                  // seconds, 0 = unknown
        bool video = true, audio = false;
    };

    struct XClip
    {
        int media = -1;                         // index into XTimeline::media
        std::string name;
        int track = 1;                          // 1-based; video V1.., audio A1..
        bool audio = false;
        double at = 0.0;                        // record seconds from the timeline's start
        double in = 0.0, out = 0.0;             // source seconds, media-relative
        double speed = 1.0;
        double gainDb = 0.0;
        double duration() const { return (out - in) / (speed > 0 ? speed : 1.0); }
    };

    struct XTransition
    {
        int from = -1, to = -1;                 // clip indices: outgoing, incoming (adjacent on a track)
        double dur = 0.0;                       // seconds, starting at the cut (the outgoing is held)
    };

    struct XTimeline
    {
        std::string name;
        double fps = 24.0;
        int width = 1920, height = 1080;
        double recordStart = -1.0;              // record timecode of the first frame, frames / fps; -1 = 01:00:00:00
        double recordStartSeconds() const;      // the value, with -1 resolved at this rate
        std::vector<XMedia> media;
        std::vector<XClip> clips;
        std::vector<XTransition> transitions;
        int videoTracks() const;
        int audioTracks() const;
    };

    // ── writers ──
    /** `track` is the video track the EDL carries (1 = V1). */
    std::string writeEdl(const XTimeline &t, int track = 1);
    std::string writeFcpxml(const XTimeline &t);
    std::string writeOtio(const XTimeline &t);

    // ── readers ── `fps` is the EDL's rate (an EDL does not say it); XML and OTIO carry their own.
    bool readEdl(const std::string &text, double fps, XTimeline &out, std::string &err);
    bool readFcpxml(const std::string &text, XTimeline &out, std::string &err);
    bool readOtio(const std::string &text, XTimeline &out, std::string &err);

    /** "file:///a%20b.mov" → "/a b.mov"; a plain path is returned as it is. */
    std::string pathFromUrl(const std::string &url);
    std::string urlFromPath(const std::string &path);
}
}
}
