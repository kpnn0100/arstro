#include "CommandLine.h"
#include <cctype>
#include "engine/EditParamsIO.h"
#include <cmath>
#include <cstdio>
#include <sstream>

namespace arstro
{
namespace interstellar_v1
{
namespace cmd
{
    std::string num(double v)
    {
        if (std::fabs(v) < 1e-9) v = 0.0;
        std::ostringstream o;
        o.precision(7);
        o << v;
        return o.str();
    }

    std::string seconds(double t, double fps)
    {
        if (t < 0.0) t = 0.0;
        if (fps > 0.0) t = std::round(t * fps) / fps;
        return num(t);
    }

    std::string bindName(const std::string &typed)
    {
        std::string b;
        for (unsigned char c : typed)
        {
            if (std::isalnum(c) || c == '_') b += (char)c;
            else if ((c == ' ' || c == '-' || c == '.') && !b.empty() && b.back() != '_') b += '_';
        }
        while (!b.empty() && b.back() == '_') b.pop_back();
        if (!b.empty() && std::isdigit((unsigned char)b[0])) b = "v_" + b;
        if (b.size() > 32) b.resize(32);
        return b;
    }

    std::string quote(const std::string &arg)
    {
        bool needs = arg.empty();
        for (char c : arg)
            if (c == ' ' || c == '\t' || c == '"') { needs = true; break; }
        if (!needs) return arg;
        std::string out = "\"";
        for (char c : arg)
        {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        out += '"';
        return out;
    }

    std::string points(const std::vector<arstro::CurvePoint> &pts) { return arstro::formatCurvePoints(pts); }

    std::string gradeSet(const std::string &bindName, const std::string &filter, const Fields &fields)
    {
        std::string line = "set";
        for (const auto &kv : fields)
            line += " " + quote(bindName + "." + filter + "." + kv.first + "=" + kv.second);
        return line;
    }

    std::string timecode(double t, double fps)
    {
        if (t < 0.0) t = 0.0;
        if (fps <= 0.0) fps = 24.0;
        const long long frames = (long long)std::floor(t * fps + 1e-6);
        const long long ifps = (long long)std::llround(fps);
        const long long ff = ifps > 0 ? frames % ifps : 0;
        const long long totalS = ifps > 0 ? frames / ifps : 0;
        char buf[96];
        std::snprintf(buf, sizeof buf, "%02lld:%02lld:%02lld:%02lld", totalS / 3600, (totalS / 60) % 60, totalS % 60, ff);
        return buf;
    }
}
}
}
