#include "EditParamsIO.h"
#include <sstream>

namespace arstro
{
    namespace
    {

        // A mixer curve is a list of bezier CONTROL points (not a sampled polyline), so
        // each point serialises its handles when smooth. A corner point writes just
        // "x,y" (2 fields) — identical to the old sampled format, so pre-bezier project
        // files still load (as corner points); a smooth point writes
        // "x,y,ix,iy,ox,oy" (6 fields) and the field count alone flags it as smooth.
        std::string mixerStr(const std::vector<CurvePoint> &v) { return formatCurvePoints(v); }
        std::vector<CurvePoint> parseMixer(const std::string &s) { return parseCurvePoints(s); }
    }

    // The one codec, exported (R-SVC-5). The two names above are kept because forty call sites
    // read better as `mixerStr(p.mixer[0])`.
    std::string formatCurvePoints(const std::vector<CurvePoint> &v)
    {
        std::ostringstream o;
        o.precision(7);
        for (size_t i = 0; i < v.size(); ++i)
        {
            if (i) o << ';';
            const CurvePoint &p = v[i];
            o << p.x << ',' << p.y;
            if (p.smooth) o << ',' << p.ix << ',' << p.iy << ',' << p.ox << ',' << p.oy;
        }
        return o.str();
    }

    std::vector<CurvePoint> parseCurvePoints(const std::string &s)
    {
        std::vector<CurvePoint> v;
        std::stringstream ss(s);
        std::string seg;
        while (std::getline(ss, seg, ';'))
        {
            std::vector<float> n;
            std::stringstream fs(seg);
            std::string tok;
            while (std::getline(fs, tok, ',')) { try { n.push_back(std::stof(tok)); } catch (...) {} }
            if (n.size() < 2) continue;
            CurvePoint p;
            p.x = n[0]; p.y = n[1];
            if (n.size() >= 6) { p.smooth = true; p.ix = n[2]; p.iy = n[3]; p.ox = n[4]; p.oy = n[5]; }
            v.push_back(p);
        }
        return v;
    }

    namespace
    {
        float f(const std::string &s) { try { return std::stof(s); } catch (...) { return 0.f; } }

        std::vector<float> floats(const std::string &s, char sep)
        {
            std::vector<float> v; std::stringstream ss(s); std::string t;
            while (std::getline(ss, t, sep)) v.push_back(f(t));
            return v;
        }

        // One mask packed as: geometry(11) | localAdjust(12) | dabs(x:y:r:f;...) | path(pts)
        //
        // The fourth group is APPENDED rather than folded into the first: a project written by
        // this build must still load in one that predates path masks (the old parser stops after
        // the third group), and a project written by the old build must still load here (a
        // missing group leaves the path empty). Same reason the third group was separate.
        //
        // The path is written by `mixerStr`, the same codec the curves use, because a path point
        // IS a CurvePoint — one format, one parser, and no second place for a handle to be
        // dropped.
        std::string maskStr(const MaskParams &m)
        {
            std::ostringstream o; o.precision(7);
            o << m.type << ',' << (m.inverted ? 1 : 0) << ',' << m.feather << ',' << m.cx << ',' << m.cy
              << ',' << m.rx << ',' << m.ry << ',' << m.x0 << ',' << m.y0 << ',' << m.x1 << ',' << m.y1;
            const LocalAdjust &a = m.adjust;
            o << '|' << a.exposure << ',' << a.contrast << ',' << a.highlights << ',' << a.shadows
              << ',' << a.whites << ',' << a.blacks << ',' << a.temp << ',' << a.tint << ',' << a.saturation
              << ',' << a.texture << ',' << a.clarity << ',' << a.dehaze << '|';
            for (size_t i = 0; i < m.dabs.size(); ++i)
            {
                const auto &d = m.dabs[i];
                if (i) o << ';';
                o << d.x << ':' << d.y << ':' << d.radius << ':' << d.flow;
            }
            // Only when there IS one: an empty trailing group on every radial mask ever written
            // is noise in a file people read and diff.
            if (!m.path.empty()) o << '|' << mixerStr(m.path);
            return o.str();
        }

        MaskParams parseMask(const std::string &s)
        {
            MaskParams m;
            std::vector<std::string> parts; std::stringstream ss(s); std::string part;
            while (std::getline(ss, part, '|')) parts.push_back(part);
            if (!parts.empty())
            {
                auto g = floats(parts[0], ',');
                if (g.size() >= 11)
                {
                    m.type = (int)g[0]; m.inverted = g[1] != 0; m.feather = g[2];
                    m.cx = g[3]; m.cy = g[4]; m.rx = g[5]; m.ry = g[6];
                    m.x0 = g[7]; m.y0 = g[8]; m.x1 = g[9]; m.y1 = g[10];
                }
            }
            if (parts.size() >= 2)
            {
                auto a = floats(parts[1], ',');
                if (a.size() >= 12)
                {
                    LocalAdjust &la = m.adjust;
                    la.exposure = a[0]; la.contrast = a[1]; la.highlights = a[2]; la.shadows = a[3];
                    la.whites = a[4]; la.blacks = a[5]; la.temp = a[6]; la.tint = a[7];
                    la.saturation = a[8]; la.texture = a[9]; la.clarity = a[10]; la.dehaze = a[11];
                }
            }
            if (parts.size() >= 3 && !parts[2].empty())
            {
                std::stringstream ds(parts[2]); std::string dab;
                while (std::getline(ds, dab, ';'))
                {
                    auto df = floats(dab, ':');
                    if (df.size() >= 4) m.dabs.push_back({df[0], df[1], df[2], df[3]});
                }
            }
            if (parts.size() >= 4 && !parts[3].empty()) m.path = parseMixer(parts[3]);
            return m;
        }
    }

    std::string serializeParams(const EditParams &p)
    {
        std::ostringstream o;
        o.precision(7);
        o << "exposure=" << p.exposure << "\ncontrast=" << p.contrast
          << "\nhighlights=" << p.highlights << "\nshadows=" << p.shadows
          << "\nwhites=" << p.whites << "\nblacks=" << p.blacks
          << "\ntemp=" << p.temp << "\ntint=" << p.tint
          << "\nvibrance=" << p.vibrance << "\nsaturation=" << p.saturation
          << "\ntexture=" << p.texture << "\nclarity=" << p.clarity
          << "\ndehaze=" << p.dehaze << "\ngrainAmount=" << p.grainAmount
          << "\ngrainSize=" << p.grainSize
          << "\nsharpenAmount=" << p.sharpenAmount << "\nsharpenRadius=" << p.sharpenRadius
          << "\nsharpenMasking=" << p.sharpenMasking
          << "\nnrLuminance=" << p.nrLuminance << "\nnrColor=" << p.nrColor
          << "\nlensDistortion=" << p.lensDistortion << "\nlensCA=" << p.lensCA
          << "\nlensVignette=" << p.lensVignette
          << "\ncurve=" << mixerStr(p.curve) << "\ncurveLog=" << (p.curveLog ? 1 : 0)
          << "\ncurveR=" << mixerStr(p.curveChannel[0])
          << "\ncurveG=" << mixerStr(p.curveChannel[1])
          << "\ncurveB=" << mixerStr(p.curveChannel[2])
          << "\nmixer0=" << mixerStr(p.mixer[0]) << "\nmixer1=" << mixerStr(p.mixer[1])
          << "\nmixer2=" << mixerStr(p.mixer[2]) << "\nmixerSpread=" << p.mixerSpread;
        for (int r = 0; r < 3; ++r)
            o << "\ngrade" << r << '=' << p.grade[r].hue << ',' << p.grade[r].sat << ',' << p.grade[r].lum;
        o << "\nbalance=" << p.balance
          << "\nremapEnable=" << (p.remapEnable ? 1 : 0)
          << "\nremapSrc=" << p.remapSrc << "\nremapRange=" << p.remapRange
          << "\nremapDst=" << p.remapDst << "\nremapStrength=" << p.remapStrength
          << "\ncrop=" << p.cropX << ',' << p.cropY << ',' << p.cropW << ',' << p.cropH
          << "\nrotation=" << p.rotation << "\nquarterTurns=" << p.quarterTurns << "\n";
        for (const auto &m : p.masks) o << "mask=" << maskStr(m) << "\n";
        return o.str();
    }

    bool deserializeParams(const std::string &text, EditParams &out)
    {
        std::stringstream ss(text);
        std::string line;
        bool any = false;
        while (std::getline(ss, line))
        {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            any = true;
            if (k == "exposure") out.exposure = f(v);
            else if (k == "contrast") out.contrast = f(v);
            else if (k == "highlights") out.highlights = f(v);
            else if (k == "shadows") out.shadows = f(v);
            else if (k == "whites") out.whites = f(v);
            else if (k == "blacks") out.blacks = f(v);
            else if (k == "temp") out.temp = f(v);
            else if (k == "tint") out.tint = f(v);
            else if (k == "vibrance") out.vibrance = f(v);
            else if (k == "saturation") out.saturation = f(v);
            else if (k == "texture") out.texture = f(v);
            else if (k == "clarity") out.clarity = f(v);
            else if (k == "sharpenAmount") out.sharpenAmount = f(v);
            else if (k == "sharpenRadius") out.sharpenRadius = f(v);
            else if (k == "sharpenMasking") out.sharpenMasking = f(v);
            else if (k == "nrLuminance") out.nrLuminance = f(v);
            else if (k == "nrColor") out.nrColor = f(v);
            else if (k == "lensDistortion") out.lensDistortion = f(v);
            else if (k == "lensCA") out.lensCA = f(v);
            else if (k == "lensVignette") out.lensVignette = f(v);
            else if (k == "dehaze") out.dehaze = f(v);
            else if (k == "grainAmount") out.grainAmount = f(v);
            else if (k == "grainSize") out.grainSize = f(v);
            else if (k == "curve") out.curve = parseMixer(v);
            else if (k == "curveLog") out.curveLog = (v != "0");
            else if (k == "curveR") out.curveChannel[0] = parseMixer(v);
            else if (k == "curveG") out.curveChannel[1] = parseMixer(v);
            else if (k == "curveB") out.curveChannel[2] = parseMixer(v);
            else if (k == "mixer0") out.mixer[0] = parseMixer(v);
            else if (k == "mixer1") out.mixer[1] = parseMixer(v);
            else if (k == "mixer2") out.mixer[2] = parseMixer(v);
            // Absent in every project written before R-MIXER-5, which leaves the field at its
            // constructed default — that is the intended migration, not an oversight: the
            // spread only adds reach (R-MIXER-6), so an old project renders more of what it
            // already asked for. `set mixerSpread=0` is how a photographer opts out.
            else if (k == "mixerSpread") out.mixerSpread = f(v);
            else if (k == "balance") out.balance = f(v);
            else if (k == "remapEnable") out.remapEnable = (v != "0");
            else if (k == "remapSrc") out.remapSrc = f(v);
            else if (k == "remapRange") out.remapRange = f(v);
            else if (k == "remapDst") out.remapDst = f(v);
            else if (k == "remapStrength") out.remapStrength = f(v);
            else if (k == "rotation") out.rotation = f(v);
            else if (k == "quarterTurns") out.quarterTurns = (int)f(v);
            else if (k == "mask") out.masks.push_back(parseMask(v));
            else if (k.rfind("grade", 0) == 0 && k.size() == 6)
            {
                const int r = k[5] - '0';
                if (r >= 0 && r < 3)
                {
                    std::stringstream ts(v); std::string t; float vals[3] = {0, 0, 0}; int i = 0;
                    while (i < 3 && std::getline(ts, t, ',')) vals[i++] = f(t);
                    out.grade[r] = {vals[0], vals[1], vals[2]};
                }
            }
            else if (k == "crop")
            {
                std::stringstream ts(v); std::string t; float vals[4] = {0, 0, 1, 1}; int i = 0;
                while (i < 4 && std::getline(ts, t, ',')) vals[i++] = f(t);
                out.cropX = vals[0]; out.cropY = vals[1]; out.cropW = vals[2]; out.cropH = vals[3];
            }
        }
        return any;
    }
}

// ── Non-finite parameter guards (D-36) ───────────────────────────────────────────────
namespace arstro
{
    namespace
    {
        /** Every scalar in an EditParams, walked once, **by reference**, so one list serves
         *  the check and the repair. A field list rather than a loop over memory:
         *  `EditParams` also holds vectors, arrays and a bool, so reinterpreting the struct
         *  as an array of floats would break the first time somebody reorders it — silently,
         *  which is the failure mode this whole family keeps producing.
         *
         *  Adding a scalar to EditParams means adding it here. `guardedParamScalarCount()`
         *  exists so a test fails when that is forgotten. */
        template <class Fn> void forEachScalar(EditParams &p, Fn fn)
        {
            fn("exposure", p.exposure);           fn("contrast", p.contrast);
            fn("highlights", p.highlights);       fn("shadows", p.shadows);
            fn("whites", p.whites);               fn("blacks", p.blacks);
            fn("temp", p.temp);                   fn("tint", p.tint);
            fn("vibrance", p.vibrance);           fn("saturation", p.saturation);
            fn("texture", p.texture);             fn("clarity", p.clarity);
            fn("dehaze", p.dehaze);               fn("grainAmount", p.grainAmount);
            fn("grainSize", p.grainSize);         fn("sharpenAmount", p.sharpenAmount);
            fn("sharpenRadius", p.sharpenRadius); fn("sharpenMasking", p.sharpenMasking);
            fn("nrLuminance", p.nrLuminance);     fn("nrColor", p.nrColor);
            fn("lensDistortion", p.lensDistortion);
            fn("lensCA", p.lensCA);               fn("lensVignette", p.lensVignette);
            fn("mixerSpread", p.mixerSpread);
            fn("balance", p.balance);             fn("remapSrc", p.remapSrc);
            fn("remapRange", p.remapRange);       fn("remapDst", p.remapDst);
            fn("remapStrength", p.remapStrength);
            fn("cropX", p.cropX);                 fn("cropY", p.cropY);
            fn("cropW", p.cropW);                 fn("cropH", p.cropH);
            fn("rotation", p.rotation);
            for (int r = 0; r < 3; ++r)
            {
                fn("gradeHue", p.grade[r].hue);
                fn("gradeSat", p.grade[r].sat);
                fn("gradeLum", p.grade[r].lum);
            }
            // Masks too: geometry AND the local adjustments. A mask with a NaN radius is
            // exactly as fatal as a NaN exposure, and a hand-edited project can carry one.
            for (MaskParams &m : p.masks)
            {
                fn("mask.feather", m.feather);
                fn("mask.cx", m.cx); fn("mask.cy", m.cy);
                fn("mask.rx", m.rx); fn("mask.ry", m.ry);
                fn("mask.x0", m.x0); fn("mask.y0", m.y0);
                fn("mask.x1", m.x1); fn("mask.y1", m.y1);
                fn("mask.exposure", m.adjust.exposure);
                fn("mask.contrast", m.adjust.contrast);
                fn("mask.highlights", m.adjust.highlights);
                fn("mask.shadows", m.adjust.shadows);
                fn("mask.whites", m.adjust.whites);
                fn("mask.blacks", m.adjust.blacks);
                fn("mask.temp", m.adjust.temp);
                fn("mask.tint", m.adjust.tint);
                fn("mask.saturation", m.adjust.saturation);
                fn("mask.texture", m.adjust.texture);
                fn("mask.clarity", m.adjust.clarity);
                fn("mask.dehaze", m.adjust.dehaze);
                for (BrushDab &d : m.dabs)
                {
                    fn("dab.x", d.x); fn("dab.y", d.y);
                    fn("dab.radius", d.radius); fn("dab.flow", d.flow);
                }
                // A NaN in a path point is as fatal as a NaN radius: it poisons the polygon's
                // crossing test, which decides whether every pixel of the row is inside.
                for (CurvePoint &cp : m.path)
                {
                    fn("path.x", cp.x); fn("path.y", cp.y);
                    fn("path.ix", cp.ix); fn("path.iy", cp.iy);
                    fn("path.ox", cp.ox); fn("path.oy", cp.oy);
                }
            }
            // Curve and mixer control points: a NaN x makes the LUT builder's sort
            // ill-defined and a NaN y poisons every sample taken from it.
            auto points = [&fn](std::vector<CurvePoint> &pts, const char *name) {
                for (CurvePoint &cp : pts) { fn(name, cp.x); fn(name, cp.y); }
            };
            points(p.curve, "curve");
            for (int c = 0; c < 3; ++c) points(p.curveChannel[c], "curveChannel");
            for (int c = 0; c < 3; ++c) points(p.mixer[c], "mixer");
        }

        /** Finite AND inside float's range. An `inf` fails `v == v` no more than a huge
         *  finite value does, and `2^1e30` is as fatal downstream as a NaN. */
        inline bool finiteValue(float v) { return v == v && v > -3.4e38f && v < 3.4e38f; }
    }

    const char *firstNonFiniteParam(const EditParams &p)
    {
        EditParams copy = p;   // forEachScalar needs mutable references; the caller's is const
        const char *bad = nullptr;
        forEachScalar(copy, [&bad](const char *name, float &v) {
            if (!bad && !finiteValue(v)) bad = name;
        });
        return bad;
    }

    int sanitizeParams(EditParams &p)
    {
        // The neutral value for a field is that field's value in a default-constructed
        // EditParams, read positionally through the SAME walk — so there is no second table
        // of defaults to drift out of step with the first.
        EditParams neutral;
        // A default EditParams has no masks and a 2-point curve, so the neutral walk is
        // shorter than the real one. Pad with 0, which is the neutral value for every mask
        // adjustment and every curve deviation anyway.
        std::vector<float> neutralValues;
        forEachScalar(neutral, [&neutralValues](const char *, float &v) { neutralValues.push_back(v); });
        std::size_t i = 0;
        int fixed = 0;
        forEachScalar(p, [&](const char *, float &v) {
            const float n = i < neutralValues.size() ? neutralValues[i] : 0.f;
            ++i;
            if (!finiteValue(v)) { v = n; ++fixed; }
        });
        return fixed;
    }

    int guardedParamScalarCount()
    {
        EditParams p;
        int n = 0;
        forEachScalar(p, [&n](const char *, float &) { ++n; });
        return n;
    }
}
