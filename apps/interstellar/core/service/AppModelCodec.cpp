#include "AppModelCodec.h"
#include <cstdio>
#include <sstream>

namespace arstro
{
namespace interstellar
{
    const char *workspaceName(Workspace w)
    {
        switch (w)
        {
            case Workspace::Grade: return "grade";
            case Workspace::Cut: return "cut";
            case Workspace::Mix: return "mix";
            case Workspace::Deliver: return "deliver";
        }
        return "cut";
    }

    namespace
    {
        std::string n3(double v)
        {
            char b[40];
            std::snprintf(b, sizeof b, "%.3f", v);
            return b;
        }
        std::string jstr(const std::string &s)
        {
            std::string o = "\"";
            for (char c : s)
            {
                if (c == '"' || c == '\\') o += '\\';
                o += c;
            }
            return o + "\"";
        }
    }

    std::string formatModel(const AppModel &m, const DumpOptions &o)
    {
        std::ostringstream s;
        if (o.json)
        {
            s << "{\n";
            if (!o.stable) s << "  \"revision\": " << m.revision << ",\n";
            s << "  \"project\": " << jstr(m.projectName) << ",\n"
              << "  \"workspace\": " << jstr(workspaceName(m.workspace)) << ",\n"
              << "  \"fps\": " << n3(m.fps) << ",\n"
              << "  \"width\": " << m.width << ", \"height\": " << m.height << ",\n"
              << "  \"duration\": " << n3(m.duration) << ",\n"
              << "  \"playhead\": " << n3(m.playhead) << ",\n"
              << "  \"rackPinned\": " << (m.rackPinned ? "true" : "false") << ",\n";
            s << "  \"rack\": [";
            for (size_t i = 0; i < m.rack.size(); ++i)
            {
                const auto &r = m.rack[i];
                s << (i ? ", " : "") << "{\"name\": " << jstr(r.bindName) << ", \"node\": "
                  << jstr(r.node) << ", \"group\": " << (r.group ? "true" : "false")
                  << ", \"weight\": " << n3(r.gradeWeight) << "}";
            }
            s << "],\n  \"tracks\": [";
            for (size_t i = 0; i < m.tracks.size(); ++i)
                s << (i ? ", " : "") << "{\"name\": " << jstr(m.tracks[i].name) << ", \"order\": "
                  << m.tracks[i].order << "}";
            s << "],\n  \"clips\": [";
            for (size_t i = 0; i < m.clips.size(); ++i)
            {
                const auto &c = m.clips[i];
                s << (i ? ", " : "") << "{\"name\": " << jstr(c.name) << ", \"at\": " << n3(c.at)
                  << ", \"in\": " << n3(c.in) << ", \"out\": " << n3(c.out) << "}";
            }
            s << "],\n  \"bindings\": [";
            for (size_t i = 0; i < m.bindings.size(); ++i)
                s << (i ? ", " : "") << "{\"target\": " << jstr(m.bindings[i].target)
                  << ", \"expr\": " << jstr(m.bindings[i].expr) << "}";
            s << "]\n}\n";
            return s.str();
        }

        if (!o.stable) s << "revision " << m.revision << '\n';
        s << "project " << (m.projectName.empty() ? "-" : m.projectName) << '\n'
          << "workspace " << workspaceName(m.workspace) << '\n'
          << "dirty " << (m.dirty ? 1 : 0) << '\n'
          << "raster " << m.width << 'x' << m.height << " fps " << n3(m.fps) << '\n'
          << "duration " << n3(m.duration) << '\n'
          << "playhead " << n3(m.playhead) << " frame " << m.playheadFrame << '\n'
          << "rack " << m.rack.size() << (m.rackPinned ? " pinned" : "") << '\n';
        for (const auto &r : m.rack)
            s << "  node " << r.bindName << " id=" << r.node
              << (r.group ? " group" : " source") << " depth=" << r.depth
              << " weight=" << n3(r.gradeWeight) << (r.bypass ? " bypass" : "")
              << (r.referenced ? " used" : " unused") << '\n';
        s << "tracks " << m.tracks.size() << '\n';
        for (const auto &t : m.tracks)
            s << "  track " << t.name << (t.audio ? " audio" : " video") << " order=" << t.order
              << " opacity=" << n3(t.opacity) << (t.mute ? " mute" : "") << '\n';
        s << "clips " << m.clips.size() << '\n';
        for (const auto &c : m.clips)
            s << "  clip " << c.name << " track=" << c.track << " src=" << c.src
              << " at=" << n3(c.at) << " in=" << n3(c.in) << " out=" << n3(c.out)
              << " speed=" << n3(c.speed) << " opacity=" << n3(c.opacity)
              << (c.srcOffline ? " offline" : "") << '\n';
        s << "lanes " << m.lanes.size() << '\n';
        for (const auto &l : m.lanes)
            s << "  lane " << l.address << " links=" << l.links.size() << '\n';
        s << "bindings " << m.bindings.size() << '\n';
        for (const auto &b : m.bindings)
        {
            s << "  bind " << b.target << " = " << b.expr;
            if (b.broken) s << "  BROKEN: " << b.brokenWhy;
            s << '\n';
            for (const auto &d : b.deps) s << "    dep " << d << '\n';
        }
        s << "lint " << m.lint.size() << '\n';
        for (const auto &l : m.lint) s << "  " << l << '\n';
        if (o.resolved)
        {
            s << "resolved " << m.resolved.size() << '\n';
            for (const auto &kv : m.resolved) s << "  " << kv.first << " = " << n3(kv.second) << '\n';
        }
        // Frame metadata is MACHINE-DEPENDENT: two services given the same commands on a fast
        // and a slow box are in the same STATE while having rendered a different number of
        // frames. Excluded from the stable dump for exactly that reason (R-SVC-9).
        if (!o.stable)
            s << "frame " << m.frameWidth << 'x' << m.frameHeight << " layers " << m.frameLayers
              << " seq " << m.frameSeq << " ms " << n3(m.frameMs) << '\n';
        s << "settings proxyEdge=" << m.settings.proxyEdge << " cpuPercent=" << m.settings.cpuPercent
          << " lintOnRender=" << (m.settings.lintOnRender ? 1 : 0) << '\n';
        if (!m.lastError.empty()) s << "lastError " << m.lastError << '\n';
        return s.str();
    }
}
}
