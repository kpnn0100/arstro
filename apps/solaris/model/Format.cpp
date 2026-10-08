#include "Format.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace arstro
{
namespace solaris
{
    namespace
    {
        // The fewest digits after the point (at least one) for which `ok(text)` holds.
        template <class Ok> std::string fewestDecimals(double v, int maxDecimals, Ok ok)
        {
            char buf[64];
            for (int d = 1; d <= maxDecimals; ++d)
            {
                std::snprintf(buf, sizeof buf, "%.*f", d, v);
                if (ok(std::string(buf))) return buf;
            }
            std::snprintf(buf, sizeof buf, "%.*f", maxDecimals, v);
            return buf;
        }
        std::string trimmed(const std::string &s)
        {
            const auto a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) return std::string();
            const auto b = s.find_last_not_of(" \t\r\n");
            return s.substr(a, b - a + 1);
        }
    }

    std::string canonicalNumber(double v)
    {
        if (std::isnan(v)) return "nan";
        if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
        if (v == 0.0) return "0.0"; // also −0
        // Fixed notation for anything a person would type; shortest round-trip, at least one decimal.
        if (std::fabs(v) >= 1e-6 && std::fabs(v) < 1e15)
            return fewestDecimals(v, 17, [v](const std::string &t) { return std::strtod(t.c_str(), nullptr) == v; });
        char buf[64];
        for (int p = 1; p <= 17; ++p)
        {
            std::snprintf(buf, sizeof buf, "%.*g", p, v);
            if (std::strtod(buf, nullptr) == v) break;
        }
        return buf;
    }

    double toTick(double beats) { return std::round(beats * kPpq) / kPpq; }

    std::string canonicalBeats(double beats)
    {
        if (!std::isfinite(beats)) return canonicalNumber(beats);
        const double tick = std::round(beats * kPpq);
        if (tick == 0.0) return "0.0";
        // The fewest decimals that read back to the same tick (six always suffice: 1e-6·960 < ½).
        return fewestDecimals(tick / kPpq, 6, [tick](const std::string &t) {
            return std::round(std::strtod(t.c_str(), nullptr) * kPpq) == tick;
        });
    }

    std::string canonicalSeconds(double seconds)
    {
        if (!std::isfinite(seconds)) return canonicalNumber(seconds);
        return canonicalNumber(std::round(seconds * 1e6) / 1e6);
    }

    std::string quoteIfNeeded(const std::string &s)
    {
        if (!s.empty() && s.find_first_of(" \t\";=#") == std::string::npos) return s;
        std::string q = "\"";
        for (char c : s) q += (c == '"') ? '\'' : c; // the format has no escape: a quote becomes an apostrophe
        return q + "\"";
    }

    std::string boolText(bool b) { return b ? "true" : "false"; }

    bool parseNumber(const std::string &s, double &out)
    {
        if (s.empty()) return false;
        char *end = nullptr;
        const double v = std::strtod(s.c_str(), &end);
        if (end == s.c_str() || *end != '\0') return false;
        out = v;
        return true;
    }

    bool parseBool(const std::string &s, bool &out)
    {
        if (s == "true" || s == "1") { out = true; return true; }
        if (s == "false" || s == "0") { out = false; return true; }
        return false;
    }

    LineTokens tokenizeLine(const std::string &line)
    {
        LineTokens t;
        std::vector<std::string> tokens;
        std::string cur;
        bool inQuote = false, have = false;
        size_t i = 0;
        for (; i < line.size(); ++i)
        {
            const char c = line[i];
            if (c == '"') { inQuote = !inQuote; have = true; cur += c; continue; }
            if (!inQuote && c == ';') break; // the inline comment
            if (!inQuote && (c == ' ' || c == '\t' || c == '\r' || c == '\n'))
            {
                if (have) { tokens.push_back(cur); cur.clear(); have = false; }
                continue;
            }
            cur += c;
            have = true;
        }
        if (have) tokens.push_back(cur);
        if (i < line.size()) t.comment = trimmed(line.substr(i + 1));

        auto unquote = [](const std::string &v) {
            return (v.size() >= 2 && v.front() == '"' && v.back() == '"') ? v.substr(1, v.size() - 2) : v;
        };
        for (size_t k = 0; k < tokens.size(); ++k)
        {
            const std::string &tok = tokens[k];
            if (k == 0 && !tok.empty() && tok[0] == '#') { t.type = tok.substr(1); continue; }
            const auto eq = tok.find('=');
            if (eq == std::string::npos || eq == 0) { t.bare.push_back(tok); continue; }
            t.fields.emplace_back(tok.substr(0, eq), unquote(tok.substr(eq + 1)));
        }
        return t;
    }
}
}
