/*
 *  solaris_ui — a parameter's text and taper, ONCE for every face that draws one: the device list's rows
 *  and the analog face's knobs (R-UI-5, R-UI-12). Engineering units in, a readout out; the position along
 *  a control (0…1 on a log taper — frequencies move in ratios — else the value itself) and back.
 */
#pragma once
#include "AppModel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace arstro
{
namespace solaris_ui
{
namespace paramfmt
{
    inline std::string number(double v, int decimals)
    {
        char b[32];
        std::snprintf(b, sizeof b, "%.*f", decimals, v);
        std::string s = b;
        if (s == "-0" || s == "-0.0" || s == "-0.00") s = s.substr(1);
        return s;
    }
    /** The readout: engineering units, as few digits as say it. */
    inline std::string formatValue(const solaris::ParamModel &p, double v)
    {
        const double a = std::fabs(v);
        if (p.unit == "Hz") return a >= 1000.0 ? number(v / 1000.0, a >= 10000.0 ? 1 : 2) + " kHz" : number(v, a >= 100.0 ? 0 : 1) + " Hz";
        if (p.unit == "ms") return number(v, a >= 100.0 ? 0 : 1) + " ms";
        if (p.unit == "dB") return number(v, 1) + " dB";
        if (p.unit == "st" || p.unit == "ct" || p.unit == "oct") return (v > 0 ? "+" : "") + number(v, p.integer ? 0 : 1) + " " + p.unit;
        if (p.unit.empty() && !p.integer && p.min >= 0.0 && p.max <= 1.0) return number(v * 100.0, 0) + " %";
        return number(v, p.integer ? 0 : 2) + (p.unit.empty() ? "" : " " + p.unit);
    }
    /** The text a `set` stores: enough digits that a drag lands where it was let go. */
    inline std::string storeText(double v, bool integer)
    {
        if (integer) return number(std::round(v), 0);
        char b[32];
        std::snprintf(b, sizeof b, "%.6g", v);
        return b;
    }
    inline bool logTaper(const solaris::ParamModel &p) { return p.logScale && p.min > 0.0 && p.max > p.min; }
    /** Position along the control: a log taper's ratio, else the value. */
    inline double toPos(const solaris::ParamModel &p, double v)
    {
        if (!logTaper(p)) return v;
        return std::log(std::max(v, p.min) / p.min) / std::log(p.max / p.min);
    }
    inline double fromPos(const solaris::ParamModel &p, double s)
    {
        double v = logTaper(p) ? p.min * std::pow(p.max / p.min, std::clamp(s, 0.0, 1.0)) : s;
        if (p.integer) v = std::round(v);
        return std::clamp(v, p.min, p.max);
    }
    /** The same position as a fraction 0…1 of the control's travel, whatever its range (a knob's turn). */
    inline double toUnit(const solaris::ParamModel &p, double v)
    {
        if (logTaper(p)) return std::clamp(toPos(p, v), 0.0, 1.0);
        return p.max > p.min ? std::clamp((v - p.min) / (p.max - p.min), 0.0, 1.0) : 0.0;
    }
    inline double fromUnit(const solaris::ParamModel &p, double u)
    {
        u = std::clamp(u, 0.0, 1.0);
        return logTaper(p) ? fromPos(p, u) : fromPos(p, p.min + u * (p.max - p.min));
    }
}
}
}
