/*
 *  interstellar_core — captions (R-DLV-1).
 *
 *  A caption is a `#caption` node: words on screen for a span of the TIMELINE (at, dur, text). It is an
 *  arrangement node like a marker, so a version inherits the base's captions and overrides one by delta
 *  (law 2). `caption import` reads an SRT's cues onto the open timeline; `caption export` writes them
 *  back. The monitor shows the caption under the playhead (the app draws it; `view captions`). A render
 *  carries them only when asked (`render --captions burn,track,sidecar`): burned into the picture (the
 *  host draws them, as it draws burn-ins), as a subtitle stream the writer muxes (mov_text in MP4/MOV,
 *  SubRip in MKV), or as an .srt beside the file — in each case the cues in the render's own seconds,
 *  cut to its range, never overlapping.
 */
#include "ServiceInternal.h"
#include "Project.h"
#include "Versions.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
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
        bool parseDouble(const std::string &s, double &out)
        {
            char *end = nullptr;
            out = std::strtod(s.c_str(), &end);
            return end && end != s.c_str() && !*end && std::isfinite(out);
        }

        // "01:02:03,456" (or "01:02:03.456") → seconds
        bool srtTime(const std::string &s, double &out)
        {
            int h = 0, m = 0, sec = 0, ms = 0;
            char sep = 0;
            if (std::sscanf(s.c_str(), "%d:%d:%d%c%d", &h, &m, &sec, &sep, &ms) != 5 || (sep != ',' && sep != '.')) return false;
            if (h < 0 || m < 0 || m > 59 || sec < 0 || sec > 59 || ms < 0 || ms > 999) return false;
            out = h * 3600.0 + m * 60.0 + sec + ms / 1000.0;
            return true;
        }

        std::string srtStamp(double t)
        {
            const long long ms = std::max(0LL, (long long)std::llround(t * 1000.0));
            char b[32];
            std::snprintf(b, sizeof b, "%02lld:%02lld:%02lld,%03lld", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, ms % 1000);
            return b;
        }

        // the styling SRT borrows from HTML and ASS (<i>, <font …>, {\an8}) is not kept: plain words
        std::string plain(const std::string &s)
        {
            std::string out;
            for (size_t i = 0; i < s.size(); ++i)
            {
                const char close = s[i] == '<' ? '>' : (s[i] == '{' && i + 1 < s.size() && s[i + 1] == '\\') ? '}' : 0;
                const size_t end = close ? s.find(close, i) : std::string::npos;
                if (end != std::string::npos) { i = end; continue; }
                out += s[i];
            }
            return out;
        }

        bool parseSrt(std::string text, std::vector<EncodeSpec::Cue> &out, std::string &err)
        {
            if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);   // a UTF-8 BOM
            text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
            std::istringstream in(text);
            std::string line;
            int lineNo = 0;
            EncodeSpec::Cue cue;
            bool inCue = false;
            auto finish = [&] {
                if (inCue) out.push_back(cue);
                inCue = false;
            };
            while (std::getline(in, line))
            {
                ++lineNo;
                const auto arrow = line.find("-->");
                if (arrow != std::string::npos)
                {
                    finish();
                    std::string a = line.substr(0, arrow), b = line.substr(arrow + 3);
                    a.erase(0, a.find_first_not_of(" \t"));
                    b.erase(0, b.find_first_not_of(" \t"));
                    b = b.substr(0, b.find_first_of(" \t"));   // a position after the end time is not kept
                    if (!srtTime(a, cue.start) || !srtTime(b, cue.end))
                    { err = "line " + std::to_string(lineNo) + ": a cue's times are HH:MM:SS,mmm --> HH:MM:SS,mmm"; return false; }
                    if (!(cue.end > cue.start)) { err = "line " + std::to_string(lineNo) + ": a cue ends after it starts"; return false; }
                    cue.text.clear();
                    inCue = true;
                    continue;
                }
                if (line.find_first_not_of(" \t") == std::string::npos) { finish(); continue; }
                if (!inCue) continue;   // the cue's number, before its times
                const std::string words = plain(line);
                if (!words.empty()) cue.text += (cue.text.empty() ? "" : "\n") + words;
            }
            finish();
            out.erase(std::remove_if(out.begin(), out.end(), [](const EncodeSpec::Cue &c) { return c.text.empty(); }), out.end());
            if (out.empty()) { err = "no cues in it"; return false; }
            return true;
        }

        std::string writeSrt(const std::vector<EncodeSpec::Cue> &cues)
        {
            std::ostringstream o;
            int n = 0;
            for (const auto &c : cues)
                o << ++n << "\n" << srtStamp(c.start) << " --> " << srtStamp(c.end) << "\n" << c.text << "\n\n";
            return o.str();
        }
    }

    bool InterstellarService::captionCues(const NodeId &tl, double a, double b, std::vector<EncodeSpec::Cue> &out, std::string &err)
    {
        out.clear();
        ResolvedTimeline R;
        if (!resolved(tl, R, err)) return false;
        for (const auto &k : R.captions)
        {
            if (R.provenance.count(k.id) && R.provenance.at(k.id) == Provenance::Dangling) continue;
            const double s = std::max(k.at, a), e = std::min(k.at + k.dur, b);
            if (e > s + 1e-6) out.push_back({s - a, e - a, k.text});
        }
        std::sort(out.begin(), out.end(), [](const EncodeSpec::Cue &x, const EncodeSpec::Cue &y) { return x.start < y.start; });
        // a stream shows one cue at a time: two that start together are one, an overlap ends the earlier
        std::vector<EncodeSpec::Cue> flat;
        for (const auto &c : out)
        {
            if (!flat.empty() && std::fabs(flat.back().start - c.start) < 1e-6)
            {
                flat.back().text += "\n" + c.text;
                flat.back().end = std::max(flat.back().end, c.end);
                continue;
            }
            if (!flat.empty() && flat.back().end > c.start) flat.back().end = c.start;
            flat.push_back(c);
        }
        out = std::move(flat);
        return true;
    }

    bool InterstellarService::captionCommand(const Command &c)
    {
        Project &P = *mProject;
        const std::string verb = specFor(c.kind)->verb;
        std::string err;
        if (c.kind == CK::ViewCaptions)
        {
            const std::string v = c.arg(0);
            if (v != "on" && v != "off") return fail("view captions: on or off, got " + v);
            mShowCaptions = v == "on";
            refreshModel();
            return true;
        }
        if (c.kind == CK::CaptionExport)
        {
            const NodeId tl = c.has("timeline") ? P.idForRef(c.flag("timeline")) : currentTimeline();
            if (!P.timeline(tl)) return fail(verb + ": no timeline named " + c.flag("timeline"));
            std::vector<EncodeSpec::Cue> cues;
            if (!captionCues(tl, 0.0, 1e12, cues, err)) return fail(verb + ": " + err);
            if (cues.empty()) return fail(verb + ": " + P.timeline(tl)->name + " has no captions");
            std::ofstream f(c.arg(0), std::ios::binary | std::ios::trunc);
            if (!f || !(f << writeSrt(cues))) return fail(verb + ": cannot write " + c.arg(0));
            mOutput = std::to_string(cues.size()) + " captions to " + c.arg(0) + "\n";
            return true;
        }

        const NodeId tl = currentTimeline();
        if (tl.empty()) return fail("the project has no timeline — `timeline new main`");
        auto added = [&](const std::string &what, const NodeId &node) {
            markDirty();
            bumpFrame();
            emit(Event(EK::ArrangeChanged).with("timeline", tl).with("what", what).with("node", node));
            return true;
        };
        auto add = [&](double at, double dur, const std::string &text, std::string name) {
            Caption k;
            k.id = P.freshId("cap_");
            k.name = name.empty() ? P.freshName("cue") : name;
            k.timeline = tl;
            k.at = std::round(at * 1000.0) / 1000.0;     // SRT's own precision
            k.dur = std::round(dur * 1000.0) / 1000.0;
            k.text = text;
            P.captions.push_back(k);
            return k.id;
        };
        if (c.kind == CK::CaptionRemove)
        {
            const NodeId id = P.idForRef(c.arg(0));
            if (!P.caption(id)) return fail(verb + ": no caption named " + c.arg(0));
            if (!dropNode(P, tl, id, err)) return fail(verb + ": " + err);
            return added("caption removed", id);
        }
        if (c.kind == CK::CaptionAdd)
        {
            double at = 0, dur = 0;
            if (!c.has("at") || !parseDouble(c.flag("at"), at) || at < 0) return fail(verb + ": --at is the timeline time, in seconds");
            if (!c.has("dur") || !parseDouble(c.flag("dur"), dur) || !(dur > 0)) return fail(verb + ": --dur is more than zero seconds");
            std::string text = c.flag("text");
            for (size_t p = text.find("\\n"); p != std::string::npos; p = text.find("\\n", p + 1)) text.replace(p, 2, "\n");
            if (text.empty()) return fail(verb + ": --text is the words");
            const std::string name = c.flag("name");
            if (!name.empty() && P.nameIsTaken(name)) return fail(verb + ": the name " + name + " is taken");
            if (!name.empty() && !Project::nameIsLegal(name, err)) return fail(verb + ": " + err);
            const NodeId id = add(at, dur, text, name);
            mOutput = id + " " + P.caption(id)->name + "\n";
            return added("caption added", id);
        }

        // caption import <file.srt> [--offset s] [--replace]
        std::ifstream f(c.arg(0), std::ios::binary);
        if (!f) return fail(verb + ": cannot read " + c.arg(0));
        const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::vector<EncodeSpec::Cue> cues;
        if (!parseSrt(text, cues, err)) return fail(verb + ": " + c.arg(0) + ": " + err);
        double offset = 0;
        if (c.has("offset") && !parseDouble(c.flag("offset"), offset)) return fail(verb + ": --offset is seconds, got " + c.flag("offset"));
        for (const auto &q : cues)
            if (q.start + offset < 0) return fail(verb + ": --offset " + c.flag("offset") + " puts a cue before the timeline's start");
        if (c.has("replace"))
        {
            ResolvedTimeline R;
            if (!resolved(tl, R, err)) return fail(verb + ": " + err);
            for (const auto &k : R.captions)
                if (!dropNode(P, tl, k.id, err)) return fail(verb + ": " + err);
        }
        NodeId last;
        for (const auto &q : cues) last = add(q.start + offset, q.end - q.start, q.text, std::string());
        mOutput = std::to_string(cues.size()) + " captions from " + c.arg(0) + "\n";
        emit(Event(EK::Info).with("text", std::to_string(cues.size()) + " captions imported onto " + P.timeline(tl)->name));
        return added("captions imported", last);
    }
}
}
