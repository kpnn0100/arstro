/*
 *  interstellar_core — Interchange implementation. See Interchange.h.
 */
#include "Interchange.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <set>
#include <sstream>

namespace arstro
{
namespace interstellar
{
namespace xch
{
    // ── timecode ──────────────────────────────────────────────────────────────────────────────────

    int XTimeline::videoTracks() const
    {
        int n = 0;
        for (const auto &c : clips) if (!c.audio) n = std::max(n, c.track);
        return n;
    }
    int XTimeline::audioTracks() const
    {
        int n = 0;
        for (const auto &c : clips) if (c.audio) n = std::max(n, c.track);
        return n;
    }
    double XTimeline::recordStartSeconds() const
    {
        if (recordStart >= 0) return recordStart;
        const TcRate r = TcRate::of(fps);
        long long f = 0;
        framesFromTc(r.drop ? "01:00:00;00" : "01:00:00:00", r, f);
        return f / r.fps;
    }

    std::string pathFromUrl(const std::string &url)
    {
        std::string u = url;
        if (u.rfind("file://", 0) == 0)
        {
            u = u.substr(7);
            if (u.rfind("localhost/", 0) == 0) u = u.substr(9);
        }
        std::string out;
        for (size_t i = 0; i < u.size(); ++i)
        {
            if (u[i] == '%' && i + 2 < u.size() && std::isxdigit((unsigned char)u[i + 1]) && std::isxdigit((unsigned char)u[i + 2]))
            {
                out += (char)std::strtol(u.substr(i + 1, 2).c_str(), nullptr, 16);
                i += 2;
            }
            else out += u[i];
        }
        return out;
    }

    std::string urlFromPath(const std::string &path)
    {
        std::string out = "file://";
        static const char *keep = "/-_.~";
        for (unsigned char c : path)
        {
            if (std::isalnum(c) || std::strchr(keep, c)) out += (char)c;
            else
            {
                char b[4];
                std::snprintf(b, sizeof b, "%%%02X", c);
                out += b;
            }
        }
        return out;
    }

    namespace
    {
        inline long long fr(double seconds, double fps) { return std::llround(seconds * fps); }

        /** A reel name an EDL can carry: no spaces, at most 32 characters. */
        std::string reelOf(const XMedia &m)
        {
            std::string r = m.reel.empty() ? m.name : m.reel;
            for (char &c : r) if (std::isspace((unsigned char)c)) c = '_';
            if (r.empty()) r = "AX";
            return r.substr(0, 32);
        }

        std::string xmlEscape(const std::string &s)
        {
            std::string o;
            for (char c : s)
            {
                switch (c)
                {
                    case '&': o += "&amp;"; break;
                    case '<': o += "&lt;"; break;
                    case '>': o += "&gt;"; break;
                    case '"': o += "&quot;"; break;
                    case '\'': o += "&apos;"; break;
                    default: o += c;
                }
            }
            return o;
        }

        std::string jsonEscape(const std::string &s)
        {
            std::string o;
            for (unsigned char c : s)
            {
                if (c == '"') o += "\\\"";
                else if (c == '\\') o += "\\\\";
                else if (c == '\n') o += "\\n";
                else if (c < 0x20)
                {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                }
                else o += (char)c;
            }
            return o;
        }

        std::string num(double v)
        {
            char b[32];
            std::snprintf(b, sizeof b, "%.10g", v);
            std::string s = b;
            if (s.find('.') == std::string::npos && s.find('e') == std::string::npos) s += ".0";
            return s;
        }

        std::vector<int> onTrack(const XTimeline &t, int track, bool audio)
        {
            std::vector<int> v;
            for (int i = 0; i < (int)t.clips.size(); ++i)
                if (t.clips[(size_t)i].track == track && t.clips[(size_t)i].audio == audio) v.push_back(i);
            std::stable_sort(v.begin(), v.end(), [&](int a, int b) { return t.clips[(size_t)a].at < t.clips[(size_t)b].at; });
            return v;
        }

        const XTransition *transitionInto(const XTimeline &t, int clip)
        {
            for (const auto &x : t.transitions) if (x.to == clip) return &x;
            return nullptr;
        }
    }

    // ── EDL (CMX 3600) ───────────────────────────────────────────────────────────────────────────

    std::string writeEdl(const XTimeline &t, int track)
    {
        const TcRate r = TcRate::of(t.fps);
        const long long rec0 = fr(t.recordStartSeconds(), r.fps);
        std::ostringstream o;
        o << "TITLE: " << (t.name.empty() ? std::string("Interstellar") : t.name) << "\n";
        o << "FCM: " << (r.drop ? "DROP FRAME" : "NON-DROP FRAME") << "\n\n";
        int ev = 0;
        char line[256];
        auto tcAt = [&](long long f) { return tcFromFrames(f, r); };
        const auto order = onTrack(t, track, false);
        for (size_t k = 0; k < order.size(); ++k)
        {
            const XClip &c = t.clips[(size_t)order[k]];
            const XMedia *m = c.media >= 0 && c.media < (int)t.media.size() ? &t.media[(size_t)c.media] : nullptr;
            const std::string reel = m ? reelOf(*m) : std::string("AX");
            const long long srcTc0 = m ? fr(std::max(0.0, m->tcStart), r.fps) : 0;
            const long long sIn = srcTc0 + fr(c.in, r.fps), sOut = srcTc0 + fr(c.out, r.fps);
            const long long rIn = rec0 + fr(c.at, r.fps), rOut = rIn + fr(c.duration(), r.fps);
            const XTransition *x = transitionInto(t, order[k]);
            ++ev;
            if (x && x->from >= 0)
            {
                // a dissolve: the outgoing clip held at the cut (a zero-length line), then the incoming
                const XClip &a = t.clips[(size_t)x->from];
                const XMedia *am = a.media >= 0 && a.media < (int)t.media.size() ? &t.media[(size_t)a.media] : nullptr;
                const long long aOut = (am ? fr(std::max(0.0, am->tcStart), r.fps) : 0) + fr(a.out, r.fps);
                std::snprintf(line, sizeof line, "%03d  %-8s V     C        %s %s %s %s\n", ev, (am ? reelOf(*am) : std::string("AX")).c_str(),
                              tcAt(aOut).c_str(), tcAt(aOut).c_str(), tcAt(rIn).c_str(), tcAt(rIn).c_str());
                o << line;
                std::snprintf(line, sizeof line, "%03d  %-8s V     D    %03lld %s %s %s %s\n", ev, reel.c_str(), fr(x->dur, r.fps),
                              tcAt(sIn).c_str(), tcAt(sOut).c_str(), tcAt(rIn).c_str(), tcAt(rOut).c_str());
                o << line;
            }
            else
            {
                std::snprintf(line, sizeof line, "%03d  %-8s V     C        %s %s %s %s\n", ev, reel.c_str(), tcAt(sIn).c_str(), tcAt(sOut).c_str(),
                              tcAt(rIn).c_str(), tcAt(rOut).c_str());
                o << line;
            }
            if (std::fabs(c.speed - 1.0) > 1e-6)
            {
                std::snprintf(line, sizeof line, "M2   %-8s %05.1f              %s\n", reel.c_str(), r.fps * c.speed, tcAt(sIn).c_str());
                o << line;
            }
            // the clip comments OTIO and Resolve write: FROM is the event's clip — for a dissolve the
            // outgoing one, and TO the incoming
            const XMedia *fromM = m;
            if (x && x->from >= 0)
            {
                const XClip &a = t.clips[(size_t)x->from];
                fromM = a.media >= 0 && a.media < (int)t.media.size() ? &t.media[(size_t)a.media] : nullptr;
            }
            o << "* FROM CLIP NAME: " << (fromM ? fromM->name : c.name) << "\n";
            if (fromM && !fromM->path.empty()) o << "* FROM CLIP: " << urlFromPath(fromM->path) << "\n";
            if (x && x->from >= 0)
            {
                o << "* TO CLIP NAME: " << (m ? m->name : c.name) << "\n";
                if (m && !m->path.empty()) o << "* TO CLIP: " << urlFromPath(m->path) << "\n";
            }
            o << "\n";
        }
        return o.str();
    }

    bool readEdl(const std::string &text, double fps, XTimeline &out, std::string &err)
    {
        out = XTimeline{};
        out.fps = fps > 0 ? fps : 24.0;
        TcRate r = TcRate::of(out.fps);
        std::istringstream in(text);
        std::string line;
        int lineNo = 0;
        struct Ev { std::string reel; char kind = 'C'; long long dur = 0; long long sIn = 0, sOut = 0, rIn = 0, rOut = 0; };
        struct Event { std::vector<Ev> lines; std::string clipName, file, toName, toFile; double speed = 1.0; };
        std::vector<Event> events;
        std::map<int, size_t> byNumber;
        long long firstRec = -1;
        for (Event *cur = nullptr; std::getline(in, line);)
        {
            ++lineNo;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            if (line.rfind("TITLE:", 0) == 0)
            {
                out.name = line.substr(6);
                while (!out.name.empty() && out.name.front() == ' ') out.name.erase(0, 1);
                continue;
            }
            if (line.rfind("FCM:", 0) == 0)
            {
                // the EDL says drop or not; the rate it does not say
                r.drop = line.find("NON-DROP") == std::string::npos && line.find("DROP") != std::string::npos && TcRate::of(out.fps).drop;
                continue;
            }
            if (line[0] == '*')
            {
                if (!cur) continue;
                auto after = [&](const char *key) {
                    const auto p = line.find(key);
                    std::string v = line.substr(p + std::strlen(key));
                    while (!v.empty() && v.front() == ' ') v.erase(0, 1);
                    while (!v.empty() && v.back() == ' ') v.pop_back();
                    return v;
                };
                if (line.find("FROM CLIP NAME:") != std::string::npos) cur->clipName = after("FROM CLIP NAME:");
                else if (line.find("TO CLIP NAME:") != std::string::npos) cur->toName = after("TO CLIP NAME:");
                else if (line.find("FROM CLIP:") != std::string::npos) cur->file = pathFromUrl(after("FROM CLIP:"));
                else if (line.find("TO CLIP:") != std::string::npos) cur->toFile = pathFromUrl(after("TO CLIP:"));
                else if (line.find("SOURCE FILE:") != std::string::npos) cur->file = pathFromUrl(after("SOURCE FILE:"));
                continue;
            }
            std::istringstream ls(line);
            std::string first;
            ls >> first;
            if (first == "M2")
            {
                std::string reel, speedS, tc;
                ls >> reel >> speedS;
                if (cur && !speedS.empty()) cur->speed = std::atof(speedS.c_str()) / out.fps;
                continue;
            }
            if (first.empty() || !std::isdigit((unsigned char)first[0])) continue;   // a note line an editor added
            Ev e;
            std::string track, kind;
            ls >> e.reel >> track >> kind;
            if (track.empty() || kind.empty()) { err = "line " + std::to_string(lineNo) + ": an event needs a reel, a track and a transition"; return false; }
            if (track[0] != 'V' && track[0] != 'B') continue;   // audio-only events: the EDL carries the video cut
            e.kind = kind[0] == 'D' ? 'D' : kind[0] == 'C' ? 'C' : kind[0];
            if (e.kind == 'D')
            {
                std::string d;
                ls >> d;
                e.dur = std::atoll(d.c_str());
            }
            else if (e.kind != 'C') { err = "line " + std::to_string(lineNo) + ": only cuts and dissolves are read (got `" + kind + "`)"; return false; }
            std::string t1, t2, t3, t4;
            ls >> t1 >> t2 >> t3 >> t4;
            if (!framesFromTc(t1, r, e.sIn) || !framesFromTc(t2, r, e.sOut) || !framesFromTc(t3, r, e.rIn) || !framesFromTc(t4, r, e.rOut))
            {
                err = "line " + std::to_string(lineNo) + ": four timecodes expected";
                return false;
            }
            const int n = std::atoi(first.c_str());
            auto it = byNumber.find(n);
            if (it == byNumber.end())
            {
                byNumber[n] = events.size();
                events.push_back(Event{});
                cur = &events.back();
            }
            else cur = &events[it->second];
            cur->lines.push_back(e);
            if (firstRec < 0 || e.rIn < firstRec) firstRec = e.rIn;
        }
        if (events.empty()) { err = "no events"; return false; }
        out.recordStart = firstRec / r.fps;
        // record timecode back to seconds from the start; the first event's hour is the record start
        long long rec0 = 0;
        framesFromTc(r.drop ? "01:00:00;00" : "01:00:00:00", r, rec0);
        if (firstRec < rec0) rec0 = firstRec;   // an EDL that starts before the hour starts where it starts
        out.recordStart = rec0 / r.fps;
        std::map<std::string, int> mediaOf;
        int prevClip = -1;
        for (const Event &e : events)
        {
            const Ev &last = e.lines.back();
            if (last.reel == "BL" || last.reel == "BLACK") { prevClip = -1; continue; }   // black: a gap
            // a dissolve event's clip is the incoming one: its TO comments, when the EDL wrote them
            const bool dissolve = last.kind == 'D';
            const std::string clipName = dissolve && !e.toName.empty() ? e.toName : e.clipName;
            const std::string file = dissolve && (!e.toName.empty() || !e.toFile.empty()) ? e.toFile : e.file;
            const std::string key = !file.empty() ? file : (!clipName.empty() ? clipName : last.reel);
            int mi;
            auto f = mediaOf.find(key);
            if (f == mediaOf.end())
            {
                XMedia m;
                m.path = file;
                m.name = !clipName.empty() ? clipName : last.reel;
                m.reel = last.reel;
                mi = (int)out.media.size();
                out.media.push_back(m);
                mediaOf[key] = mi;
            }
            else mi = f->second;
            XClip c;
            c.media = mi;
            c.name = out.media[(size_t)mi].name;
            c.track = 1;
            c.speed = e.speed > 0 ? e.speed : 1.0;
            c.at = (last.rIn - rec0) / r.fps;
            // the source timecode is the file's own; with no file read yet its start is unknown, so the
            // reader keeps the timecode and the importer takes the file's start off
            c.in = last.sIn / r.fps;
            c.out = c.in + (last.rOut - last.rIn) / r.fps * c.speed;
            const int ci = (int)out.clips.size();
            out.clips.push_back(c);
            if (last.kind == 'D' && prevClip >= 0) out.transitions.push_back({prevClip, ci, last.dur / r.fps});
            prevClip = ci;
        }
        // an EDL's source times are timecode: mark each medium's start as unknown (the importer reads it)
        for (auto &m : out.media) m.tcStart = -1.0;
        return true;
    }

    // ── a small XML reader (enough for FCPXML: elements, attributes, entities; no DTD) ─────────────

    namespace
    {
        struct XNode
        {
            std::string name;
            std::map<std::string, std::string> attr;
            std::vector<std::unique_ptr<XNode>> kids;
            std::string get(const std::string &k, const std::string &def = std::string()) const
            {
                const auto it = attr.find(k);
                return it == attr.end() ? def : it->second;
            }
            const XNode *child(const std::string &n) const
            {
                for (const auto &k : kids) if (k->name == n) return k.get();
                return nullptr;
            }
        };

        std::string unescape(const std::string &s)
        {
            std::string o;
            for (size_t i = 0; i < s.size(); ++i)
            {
                if (s[i] != '&') { o += s[i]; continue; }
                const auto semi = s.find(';', i);
                if (semi == std::string::npos) { o += s[i]; continue; }
                const std::string e = s.substr(i + 1, semi - i - 1);
                if (e == "amp") o += '&';
                else if (e == "lt") o += '<';
                else if (e == "gt") o += '>';
                else if (e == "quot") o += '"';
                else if (e == "apos") o += '\'';
                else if (!e.empty() && e[0] == '#') o += (char)std::strtol(e.c_str() + (e.size() > 1 && e[1] == 'x' ? 2 : 1), nullptr, e.size() > 1 && e[1] == 'x' ? 16 : 10);
                else o += "&" + e + ";";
                i = semi;
            }
            return o;
        }

        bool parseXml(const std::string &s, XNode &root, std::string &err)
        {
            std::vector<XNode *> stack{&root};
            size_t i = 0;
            while (i < s.size())
            {
                const size_t lt = s.find('<', i);
                if (lt == std::string::npos) break;
                if (s.compare(lt, 4, "<!--") == 0) { const size_t e = s.find("-->", lt); if (e == std::string::npos) break; i = e + 3; continue; }
                if (s.compare(lt, 2, "<?") == 0 || s.compare(lt, 2, "<!") == 0) { const size_t e = s.find('>', lt); if (e == std::string::npos) break; i = e + 1; continue; }
                const size_t gt = s.find('>', lt);
                if (gt == std::string::npos) { err = "an element is not closed"; return false; }
                std::string tag = s.substr(lt + 1, gt - lt - 1);
                i = gt + 1;
                if (!tag.empty() && tag[0] == '/')
                {
                    if (stack.size() <= 1) { err = "a closing </" + tag.substr(1) + "> with nothing open"; return false; }
                    stack.pop_back();
                    continue;
                }
                const bool selfClose = !tag.empty() && tag.back() == '/';
                if (selfClose) tag.pop_back();
                auto node = std::make_unique<XNode>();
                size_t p = 0;
                while (p < tag.size() && !std::isspace((unsigned char)tag[p])) ++p;
                node->name = tag.substr(0, p);
                while (p < tag.size())
                {
                    while (p < tag.size() && std::isspace((unsigned char)tag[p])) ++p;
                    const size_t eq = tag.find('=', p);
                    if (eq == std::string::npos) break;
                    std::string key = tag.substr(p, eq - p);
                    while (!key.empty() && std::isspace((unsigned char)key.back())) key.pop_back();
                    size_t q = eq + 1;
                    while (q < tag.size() && std::isspace((unsigned char)tag[q])) ++q;
                    if (q >= tag.size() || (tag[q] != '"' && tag[q] != '\'')) { err = "attribute " + key + " of <" + node->name + "> is not quoted"; return false; }
                    const char quote = tag[q];
                    const size_t close = tag.find(quote, q + 1);
                    if (close == std::string::npos) { err = "attribute " + key + " of <" + node->name + "> is not closed"; return false; }
                    node->attr[key] = unescape(tag.substr(q + 1, close - q - 1));
                    p = close + 1;
                }
                XNode *raw = node.get();
                stack.back()->kids.push_back(std::move(node));
                if (!selfClose) stack.push_back(raw);
            }
            return true;
        }

        /** "1001/24000s", "3600s", "0s" → seconds. */
        double rational(const std::string &v)
        {
            if (v.empty()) return 0.0;
            std::string s = v;
            if (s.back() == 's') s.pop_back();
            const auto slash = s.find('/');
            if (slash == std::string::npos) return std::atof(s.c_str());
            const double den = std::atof(s.c_str() + slash + 1);
            return den != 0 ? std::atof(s.substr(0, slash).c_str()) / den : 0.0;
        }

        /** The rate as FCPXML writes a frame: "1/24s", "1001/24000s", "100/2997s" … */
        void frameDuration(double fps, long long &num, long long &den)
        {
            const double ntsc = fps * 1001.0 / 1000.0;
            if (std::fabs(ntsc - std::round(ntsc)) < 1e-6 && std::fabs(fps - std::round(fps)) > 1e-6) { num = 1001; den = std::llround(ntsc) * 1000; }
            else { num = 1; den = std::llround(fps); }
        }
    }

    // ── FCPXML 1.9 ───────────────────────────────────────────────────────────────────────────────

    std::string writeFcpxml(const XTimeline &t)
    {
        long long fdN = 1, fdD = 24;
        frameDuration(t.fps, fdN, fdD);
        auto at = [&](double seconds) {
            const long long f = fr(seconds, t.fps);
            return std::to_string(f * fdN) + "/" + std::to_string(fdD) + "s";
        };
        const TcRate r = TcRate::of(t.fps);
        std::ostringstream o;
        o << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE fcpxml>\n<fcpxml version=\"1.9\">\n  <resources>\n";
        o << "    <format id=\"r1\" frameDuration=\"" << fdN << "/" << fdD << "s\" width=\"" << t.width << "\" height=\"" << t.height << "\"/>\n";
        for (size_t i = 0; i < t.media.size(); ++i)
        {
            const XMedia &m = t.media[i];
            double dur = m.duration;
            for (const auto &c : t.clips) if (c.media == (int)i) dur = std::max(dur, c.out);
            o << "    <asset id=\"a" << i + 1 << "\" name=\"" << xmlEscape(m.name) << "\" start=\"" << at(std::max(0.0, m.tcStart)) << "\" duration=\"" << at(dur)
              << "\" hasVideo=\"" << (m.video ? 1 : 0) << "\" hasAudio=\"" << (m.audio ? 1 : 0) << "\" format=\"r1\">\n";
            o << "      <media-rep kind=\"original-media\" src=\"" << xmlEscape(urlFromPath(m.path)) << "\"/>\n    </asset>\n";
        }
        o << "  </resources>\n  <library>\n    <event name=\"Interstellar\">\n      <project name=\"" << xmlEscape(t.name) << "\">\n";
        double total = 0;
        for (const auto &c : t.clips) total = std::max(total, c.at + c.duration());
        o << "        <sequence format=\"r1\" duration=\"" << at(total) << "\" tcStart=\"" << at(t.recordStartSeconds()) << "\" tcFormat=\"" << (r.drop ? "DF" : "NDF") << "\">\n";
        o << "          <spine>\n";
        // the primary storyline: V1 with gaps; every other clip connects to the element it starts over
        struct Item { bool gap; int clip; double offset, dur, start; std::vector<std::string> kids; };
        std::vector<Item> spine;
        double cursor = 0;
        for (int ci : onTrack(t, 1, false))
        {
            const XClip &c = t.clips[(size_t)ci];
            if (c.at > cursor + 1e-9) spine.push_back({true, -1, cursor, c.at - cursor, 0.0, {}});
            const double start = std::max(0.0, c.media >= 0 ? t.media[(size_t)c.media].tcStart : 0.0) + c.in;
            spine.push_back({false, ci, c.at, c.duration(), start, {}});
            cursor = c.at + c.duration();
        }
        if (cursor < total - 1e-9) spine.push_back({true, -1, cursor, total - cursor, 0.0, {}});
        const double seq0 = t.recordStartSeconds();   // the spine's clock starts at the sequence's tcStart
        auto clipXml = [&](const XClip &c, double offset, int lane, double start) {
            std::ostringstream x;
            x << "<asset-clip ref=\"a" << c.media + 1 << "\" name=\"" << xmlEscape(c.name) << "\"";
            if (lane) x << " lane=\"" << lane << "\"";
            x << " offset=\"" << at(offset) << "\" start=\"" << at(start) << "\" duration=\"" << at(c.duration()) << "\"";
            if (c.audio) x << " role=\"dialogue\"";
            std::string inner;
            if (std::fabs(c.speed - 1.0) > 1e-6)
                // speed as a linear time map: the clip's local time 0 → start, its end → start + span
                inner += "<timeMap><timept time=\"0s\" value=\"" + at(start) + "\" interp=\"linear\"/><timept time=\"" + at(c.duration()) + "\" value=\"" +
                         at(start + (c.out - c.in)) + "\" interp=\"linear\"/></timeMap>";
            if (c.audio && std::fabs(c.gainDb) > 1e-9) inner += "<adjust-volume amount=\"" + num(c.gainDb) + "dB\"/>";
            if (inner.empty()) x << "/>";
            else x << ">\n                " << inner << "\n              </asset-clip>";
            return x.str();
        };
        for (int ci = 0; ci < (int)t.clips.size(); ++ci)
        {
            const XClip &c = t.clips[(size_t)ci];
            if (!c.audio && c.track == 1) continue;
            const int lane = c.audio ? -c.track : c.track - 1;
            for (auto &it : spine)
                if (c.at >= it.offset - 1e-9 && c.at < it.offset + it.dur - 1e-9)
                {
                    // a connected clip's offset is in its parent's own time
                    const double localStart = it.gap ? 0.0 : it.start;
                    const double start = std::max(0.0, c.media >= 0 ? t.media[(size_t)c.media].tcStart : 0.0) + c.in;
                    it.kids.push_back(clipXml(c, localStart + (c.at - it.offset), lane, start));
                    break;
                }
        }
        for (size_t k = 0; k < spine.size(); ++k)
        {
            const Item &it = spine[k];
            if (!it.gap)
            {
                // a dissolve into this clip sits in the spine before it, from the cut
                if (const XTransition *x = transitionInto(t, it.clip))
                    o << "            <transition name=\"Cross Dissolve\" offset=\"" << at(seq0 + it.offset) << "\" duration=\"" << at(x->dur) << "\"/>\n";
            }
            if (it.gap)
            {
                o << "            <gap name=\"Gap\" offset=\"" << at(seq0 + it.offset) << "\" start=\"0s\" duration=\"" << at(it.dur) << "\"";
                if (it.kids.empty()) { o << "/>\n"; continue; }
                o << ">\n";
                for (const auto &kx : it.kids) o << "              " << kx << "\n";
                o << "            </gap>\n";
                continue;
            }
            std::string x = clipXml(t.clips[(size_t)it.clip], seq0 + it.offset, 0, it.start);
            if (it.kids.empty()) { o << "            " << x << "\n"; continue; }
            // open the element to hold its connected clips
            if (x.size() >= 2 && x.compare(x.size() - 2, 2, "/>") == 0)
            {
                o << "            " << x.substr(0, x.size() - 2) << ">\n";
                for (const auto &kx : it.kids) o << "              " << kx << "\n";
                o << "            </asset-clip>\n";
            }
            else
            {
                const auto close = x.rfind("</asset-clip>");
                o << "            " << x.substr(0, close);
                for (const auto &kx : it.kids) o << "  " << kx << "\n";
                o << "            </asset-clip>\n";
            }
        }
        o << "          </spine>\n        </sequence>\n      </project>\n    </event>\n  </library>\n</fcpxml>\n";
        return o.str();
    }

    bool readFcpxml(const std::string &text, XTimeline &out, std::string &err)
    {
        out = XTimeline{};
        XNode root;
        if (!parseXml(text, root, err)) return false;
        const XNode *fcp = root.child("fcpxml");
        if (!fcp) { err = "not an FCPXML document (no <fcpxml>)"; return false; }
        std::map<std::string, double> formatFps;
        std::map<std::string, std::pair<int, int>> formatSize;
        std::map<std::string, int> assetMedia;
        std::map<std::string, double> assetStart;
        if (const XNode *res = fcp->child("resources"))
            for (const auto &k : res->kids)
            {
                if (k->name == "format")
                {
                    const double fd = rational(k->get("frameDuration", "1/24s"));
                    formatFps[k->get("id")] = fd > 0 ? 1.0 / fd : 24.0;
                    formatSize[k->get("id")] = {std::atoi(k->get("width", "1920").c_str()), std::atoi(k->get("height", "1080").c_str())};
                }
                else if (k->name == "asset")
                {
                    XMedia m;
                    m.name = k->get("name");
                    std::string src = k->get("src");
                    if (const XNode *rep = k->child("media-rep")) src = rep->get("src", src);
                    m.path = pathFromUrl(src);
                    m.tcStart = rational(k->get("start", "0s"));
                    m.duration = rational(k->get("duration", "0s"));
                    m.video = k->get("hasVideo", "1") != "0";
                    m.audio = k->get("hasAudio", "0") != "0";
                    assetMedia[k->get("id")] = (int)out.media.size();
                    assetStart[k->get("id")] = m.tcStart;
                    out.media.push_back(m);
                }
            }
        // the first sequence of the first project
        const XNode *seq = nullptr;
        std::function<void(const XNode &)> find = [&](const XNode &n) {
            for (const auto &k : n.kids)
            {
                if (seq) return;
                if (k->name == "project") { out.name = k->get("name"); }
                if (k->name == "sequence") { seq = k.get(); return; }
                find(*k);
            }
        };
        find(*fcp);
        if (!seq) { err = "no <sequence> in the FCPXML"; return false; }
        const std::string fmt = seq->get("format");
        out.fps = formatFps.count(fmt) ? formatFps[fmt] : 24.0;
        if (formatSize.count(fmt)) { out.width = formatSize[fmt].first; out.height = formatSize[fmt].second; }
        out.recordStart = rational(seq->get("tcStart", "0s"));
        const XNode *spine = seq->child("spine");
        if (!spine) { err = "the sequence has no <spine>"; return false; }
        int lastSpineClip = -1;
        double pendingDissolve = -1;
        // `parentAt`: the parent's record time at its local time `parentStart` (connected clips)
        std::function<void(const XNode &, double parentAt, double parentStart, int lane)> walk;
        auto addClip = [&](const XNode &k, int ref, double recAt, int lane) -> int {
            XClip c;
            c.media = ref;
            c.name = k.get("name", ref >= 0 ? out.media[(size_t)ref].name : std::string());
            const double start = rational(k.get("start", "0s")), dur = rational(k.get("duration", "0s"));
            const double base = ref >= 0 ? out.media[(size_t)ref].tcStart : 0.0;
            c.at = recAt;
            c.in = start - base;
            c.out = c.in + dur;
            if (const XNode *tm = k.child("timeMap"))
            {
                // a linear time map: its span over the clip's span is the speed
                std::vector<std::pair<double, double>> pts;
                for (const auto &p : tm->kids) if (p->name == "timept") pts.push_back({rational(p->get("time")), rational(p->get("value"))});
                if (pts.size() >= 2 && pts.back().first > pts.front().first)
                {
                    c.speed = (pts.back().second - pts.front().second) / (pts.back().first - pts.front().first);
                    c.in = pts.front().second - base;
                    c.out = c.in + dur * c.speed;
                }
            }
            if (const XNode *av = k.child("adjust-volume")) c.gainDb = std::atof(av->get("amount", "0").c_str());
            c.audio = lane < 0 || (ref >= 0 && !out.media[(size_t)ref].video && out.media[(size_t)ref].audio);
            c.track = lane < 0 ? -lane : lane + 1;
            out.clips.push_back(c);
            return (int)out.clips.size() - 1;
        };
        walk = [&](const XNode &parent, double parentAt, double parentStart, int parentLane) {
            for (const auto &kp : parent.kids)
            {
                const XNode &k = *kp;
                const bool inSpine = &parent == spine;
                // a <clip>'s own <video>/<audio> is its media, read with it — not a connected clip
                if (parent.name == "clip" && (k.name == "video" || k.name == "audio") && !k.attr.count("lane")) continue;
                const int lane = k.attr.count("lane") ? std::atoi(k.get("lane").c_str()) : (inSpine ? 0 : parentLane);
                const double offset = rational(k.get("offset", "0s"));
                const double recAt = inSpine ? offset - out.recordStart : parentAt + (offset - parentStart);
                if (k.name == "transition" && inSpine) { pendingDissolve = rational(k.get("duration", "0s")); continue; }
                int ref = -1;
                const XNode *media = &k;
                if (k.name == "asset-clip") ref = assetMedia.count(k.get("ref")) ? assetMedia[k.get("ref")] : -1;
                else if (k.name == "clip")
                {
                    // a <clip> wraps its <video>/<audio>: the media is the inner element's
                    for (const auto &in : k.kids)
                        if ((in->name == "video" || in->name == "audio") && assetMedia.count(in->get("ref"))) { ref = assetMedia[in->get("ref")]; break; }
                }
                else if (k.name == "video" || k.name == "audio") ref = assetMedia.count(k.get("ref")) ? assetMedia[k.get("ref")] : -1;
                else if (k.name != "gap") continue;   // titles, generators, compound clips: not read in v1 (said)
                int ci = -1;
                if (k.name != "gap" && ref >= 0) ci = addClip(*media, ref, recAt, lane);
                if (inSpine)
                {
                    if (ci >= 0)
                    {
                        if (pendingDissolve > 0 && lastSpineClip >= 0) out.transitions.push_back({lastSpineClip, ci, pendingDissolve});
                        lastSpineClip = ci;
                    }
                    else lastSpineClip = -1;
                    pendingDissolve = -1;
                }
                // connected clips ride on this element, in its own time
                walk(k, recAt, rational(k.get("start", "0s")), lane);
            }
        };
        walk(*spine, 0.0, 0.0, 0);
        // record times from the sequence start
        return true;
    }

    // ── a small JSON reader (enough for OTIO) ──────────────────────────────────────────────────

    namespace
    {
        struct JVal
        {
            enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
            bool b = false;
            double n = 0;
            std::string s;
            std::vector<JVal> a;
            std::vector<std::pair<std::string, JVal>> o;
            const JVal *get(const std::string &k) const
            {
                for (const auto &kv : o) if (kv.first == k) return &kv.second;
                return nullptr;
            }
            std::string str(const std::string &k, const std::string &def = std::string()) const
            {
                const JVal *v = get(k);
                return v && v->kind == Str ? v->s : def;
            }
            double numOf(const std::string &k, double def = 0) const
            {
                const JVal *v = get(k);
                return v && v->kind == Num ? v->n : def;
            }
        };

        struct JParser
        {
            const std::string &s;
            size_t i = 0;
            std::string err;
            explicit JParser(const std::string &t) : s(t) {}
            void ws() { while (i < s.size() && std::isspace((unsigned char)s[i])) ++i; }
            bool value(JVal &v)
            {
                ws();
                if (i >= s.size()) { err = "unexpected end"; return false; }
                const char c = s[i];
                if (c == '{')
                {
                    v.kind = JVal::Obj;
                    ++i;
                    ws();
                    if (i < s.size() && s[i] == '}') { ++i; return true; }
                    for (;;)
                    {
                        JVal key;
                        ws();
                        if (!string(key.s)) return false;
                        ws();
                        if (i >= s.size() || s[i] != ':') { err = "expected ':' at " + std::to_string(i); return false; }
                        ++i;
                        JVal val;
                        if (!value(val)) return false;
                        v.o.emplace_back(key.s, std::move(val));
                        ws();
                        if (i < s.size() && s[i] == ',') { ++i; continue; }
                        if (i < s.size() && s[i] == '}') { ++i; return true; }
                        err = "expected ',' or '}' at " + std::to_string(i);
                        return false;
                    }
                }
                if (c == '[')
                {
                    v.kind = JVal::Arr;
                    ++i;
                    ws();
                    if (i < s.size() && s[i] == ']') { ++i; return true; }
                    for (;;)
                    {
                        JVal el;
                        if (!value(el)) return false;
                        v.a.push_back(std::move(el));
                        ws();
                        if (i < s.size() && s[i] == ',') { ++i; continue; }
                        if (i < s.size() && s[i] == ']') { ++i; return true; }
                        err = "expected ',' or ']' at " + std::to_string(i);
                        return false;
                    }
                }
                if (c == '"') { v.kind = JVal::Str; return string(v.s); }
                if (s.compare(i, 4, "true") == 0) { v.kind = JVal::Bool; v.b = true; i += 4; return true; }
                if (s.compare(i, 5, "false") == 0) { v.kind = JVal::Bool; i += 5; return true; }
                if (s.compare(i, 4, "null") == 0) { v.kind = JVal::Null; i += 4; return true; }
                char *end = nullptr;
                v.n = std::strtod(s.c_str() + i, &end);
                if (end == s.c_str() + i) { err = "unexpected character at " + std::to_string(i); return false; }
                v.kind = JVal::Num;
                i = (size_t)(end - s.c_str());
                return true;
            }
            bool string(std::string &out)
            {
                if (i >= s.size() || s[i] != '"') { err = "expected a string at " + std::to_string(i); return false; }
                ++i;
                while (i < s.size() && s[i] != '"')
                {
                    if (s[i] == '\\' && i + 1 < s.size())
                    {
                        const char e = s[++i];
                        if (e == 'n') out += '\n';
                        else if (e == 't') out += '\t';
                        else if (e == 'u' && i + 4 < s.size())
                        {
                            const long cp = std::strtol(s.substr(i + 1, 4).c_str(), nullptr, 16);
                            if (cp < 0x80) out += (char)cp;
                            else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                            else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                            i += 4;
                        }
                        else out += e;
                        ++i;
                    }
                    else out += s[i++];
                }
                if (i >= s.size()) { err = "a string is not closed"; return false; }
                ++i;
                return true;
            }
        };

        double rationalTime(const JVal *v)
        {
            if (!v || v->kind != JVal::Obj) return 0.0;
            const double rate = v->numOf("rate", 24.0);
            return rate > 0 ? v->numOf("value") / rate : 0.0;
        }

        std::string rt(double seconds, double fps)
        {
            return "{\"OTIO_SCHEMA\": \"RationalTime.1\", \"rate\": " + num(fps) + ", \"value\": " + num((double)fr(seconds, fps)) + "}";
        }
        std::string range(double start, double dur, double fps)
        {
            return "{\"OTIO_SCHEMA\": \"TimeRange.1\", \"duration\": " + rt(dur, fps) + ", \"start_time\": " + rt(start, fps) + "}";
        }
    }

    // ── OpenTimelineIO JSON ──────────────────────────────────────────────────────────────────────

    std::string writeOtio(const XTimeline &t)
    {
        std::ostringstream o;
        const double fps = t.fps;
        o << "{\n  \"OTIO_SCHEMA\": \"Timeline.1\",\n  \"metadata\": {\"interstellar\": {\"width\": " << t.width << ", \"height\": " << t.height << "}},\n";
        o << "  \"name\": \"" << jsonEscape(t.name) << "\",\n  \"global_start_time\": " << rt(t.recordStartSeconds(), fps) << ",\n";
        o << "  \"tracks\": {\n    \"OTIO_SCHEMA\": \"Stack.1\",\n    \"metadata\": {},\n    \"name\": \"tracks\",\n    \"source_range\": null,\n    \"effects\": [],\n    \"markers\": [],\n    \"enabled\": true,\n    \"children\": [";
        bool firstTrack = true;
        auto track = [&](int n, bool audio) {
            o << (firstTrack ? "\n" : ",\n");
            firstTrack = false;
            o << "      {\"OTIO_SCHEMA\": \"Track.1\", \"metadata\": {}, \"name\": \"" << (audio ? "A" : "V") << n << "\", \"source_range\": null, \"effects\": [], \"markers\": [], \"enabled\": true, \"kind\": \""
              << (audio ? "Audio" : "Video") << "\", \"children\": [";
            double cursor = 0;
            bool first = true;
            auto item = [&](const std::string &s) { o << (first ? "\n" : ",\n") << "        " << s; first = false; };
            for (int ci : onTrack(t, n, audio))
            {
                const XClip &c = t.clips[(size_t)ci];
                if (c.at > cursor + 1e-9)
                    item("{\"OTIO_SCHEMA\": \"Gap.1\", \"metadata\": {}, \"name\": \"\", \"source_range\": " + range(0.0, c.at - cursor, fps) + ", \"effects\": [], \"markers\": [], \"enabled\": true}");
                if (const XTransition *x = transitionInto(t, ci))
                    item("{\"OTIO_SCHEMA\": \"Transition.1\", \"metadata\": {}, \"name\": \"Cross Dissolve\", \"transition_type\": \"SMPTE_Dissolve\", \"in_offset\": " + rt(0.0, fps) +
                         ", \"out_offset\": " + rt(x->dur, fps) + "}");
                const XMedia *m = c.media >= 0 ? &t.media[(size_t)c.media] : nullptr;
                const double tc = m ? std::max(0.0, m->tcStart) : 0.0;
                std::string clip = "{\"OTIO_SCHEMA\": \"Clip.1\", \"metadata\": {\"interstellar\": {\"reel\": \"" + jsonEscape(m ? reelOf(*m) : std::string()) + "\", \"gain\": " + num(c.gainDb) +
                                   "}}, \"name\": \"" + jsonEscape(c.name) + "\", \"source_range\": " + range(tc + c.in, c.duration(), fps) + ", \"effects\": [";
                if (std::fabs(c.speed - 1.0) > 1e-6)
                    clip += "{\"OTIO_SCHEMA\": \"LinearTimeWarp.1\", \"metadata\": {}, \"name\": \"\", \"effect_name\": \"LinearTimeWarp\", \"time_scalar\": " + num(c.speed) + "}";
                clip += "], \"markers\": [], \"enabled\": true, \"media_reference\": {\"OTIO_SCHEMA\": \"ExternalReference.1\", \"metadata\": {}, \"name\": \"" +
                        jsonEscape(m ? m->name : std::string()) + "\", \"available_range\": " +
                        (m && m->duration > 0 ? range(tc, m->duration, fps) : std::string("null")) + ", \"target_url\": \"" + jsonEscape(m ? urlFromPath(m->path) : std::string()) + "\"}}";
                item(clip);
                cursor = c.at + c.duration();
            }
            o << "\n      ]}";
        };
        for (int v = 1; v <= std::max(1, t.videoTracks()); ++v) track(v, false);
        for (int a = 1; a <= t.audioTracks(); ++a) track(a, true);
        o << "\n    ]\n  }\n}\n";
        return o.str();
    }

    bool readOtio(const std::string &text, XTimeline &out, std::string &err)
    {
        out = XTimeline{};
        JParser p(text);
        JVal root;
        if (!p.value(root)) { err = "not JSON: " + p.err; return false; }
        if (root.str("OTIO_SCHEMA").rfind("Timeline.", 0) != 0) { err = "not an OpenTimelineIO timeline (OTIO_SCHEMA " + root.str("OTIO_SCHEMA") + ")"; return false; }
        out.name = root.str("name");
        if (const JVal *g = root.get("global_start_time"))
            if (g->kind == JVal::Obj) { out.fps = g->numOf("rate", 24.0); out.recordStart = rationalTime(g); }
        if (const JVal *md = root.get("metadata"))
            if (const JVal *is = md->get("interstellar")) { out.width = (int)is->numOf("width", 1920); out.height = (int)is->numOf("height", 1080); }
        const JVal *stack = root.get("tracks");
        if (!stack || !stack->get("children")) { err = "the timeline has no tracks"; return false; }
        std::map<std::string, int> mediaOf;
        int vN = 0, aN = 0;
        for (const JVal &tr : stack->get("children")->a)
        {
            if (tr.str("OTIO_SCHEMA").rfind("Track.", 0) != 0) continue;
            const bool audio = tr.str("kind") == "Audio";
            const int n = audio ? ++aN : ++vN;
            double cursor = 0;
            int prev = -1;
            double pendingDissolve = -1;
            const JVal *kids = tr.get("children");
            if (!kids) continue;
            for (const JVal &it : kids->a)
            {
                const std::string schema = it.str("OTIO_SCHEMA");
                const JVal *sr = it.get("source_range");
                const double dur = sr && sr->kind == JVal::Obj ? rationalTime(sr->get("duration")) : 0.0;
                if (!root.get("global_start_time") && sr && sr->kind == JVal::Obj && sr->get("duration")) out.fps = sr->get("duration")->numOf("rate", out.fps);
                if (schema.rfind("Gap.", 0) == 0) { cursor += dur; prev = -1; pendingDissolve = -1; continue; }
                if (schema.rfind("Transition.", 0) == 0) { pendingDissolve = rationalTime(it.get("in_offset")) + rationalTime(it.get("out_offset")); continue; }
                if (schema.rfind("Clip.", 0) != 0) { cursor += dur; prev = -1; continue; }   // stacks, generators: v1 skips (said)
                // Clip.1 media_reference, or Clip.2 media_references[active key]
                const JVal *ref = it.get("media_reference");
                if (const JVal *refs = it.get("media_references"))
                    if (refs->kind == JVal::Obj)
                    {
                        const std::string key = it.str("active_media_reference_key", "DEFAULT_MEDIA");
                        ref = refs->get(key);
                    }
                std::string url = ref ? ref->str("target_url") : std::string();
                const std::string path = pathFromUrl(url);
                const std::string key = !path.empty() ? path : it.str("name");
                int mi;
                auto f = mediaOf.find(key);
                if (f == mediaOf.end())
                {
                    XMedia m;
                    m.path = path;
                    m.name = ref && !ref->str("name").empty() ? ref->str("name") : it.str("name");
                    const JVal *ar = ref ? ref->get("available_range") : nullptr;
                    m.tcStart = ar && ar->kind == JVal::Obj ? rationalTime(ar->get("start_time")) : 0.0;
                    m.duration = ar && ar->kind == JVal::Obj ? rationalTime(ar->get("duration")) : 0.0;
                    m.video = !audio;
                    m.audio = audio;
                    if (const JVal *md = it.get("metadata"))
                        if (const JVal *is = md->get("interstellar")) m.reel = is->str("reel");
                    mi = (int)out.media.size();
                    out.media.push_back(m);
                    mediaOf[key] = mi;
                }
                else mi = f->second;
                XClip c;
                c.media = mi;
                c.name = it.str("name");
                c.track = n;
                c.audio = audio;
                c.at = cursor;
                const double start = sr && sr->kind == JVal::Obj ? rationalTime(sr->get("start_time")) : 0.0;
                c.in = start - out.media[(size_t)mi].tcStart;
                if (const JVal *fx = it.get("effects"))
                    for (const JVal &e : fx->a)
                        if (e.str("OTIO_SCHEMA").rfind("LinearTimeWarp.", 0) == 0) c.speed = e.numOf("time_scalar", 1.0);
                c.out = c.in + dur * c.speed;
                if (const JVal *md = it.get("metadata"))
                    if (const JVal *is = md->get("interstellar")) c.gainDb = is->numOf("gain", 0.0);
                const int ci = (int)out.clips.size();
                out.clips.push_back(c);
                if (pendingDissolve > 0 && prev >= 0) out.transitions.push_back({prev, ci, pendingDissolve});
                pendingDissolve = -1;
                prev = ci;
                cursor += dur;
            }
        }
        return true;
    }
}
}
}
