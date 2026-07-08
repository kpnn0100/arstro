#include "EditParamsIO.h"
#include <sstream>

namespace arstro
{
    namespace
    {
        std::string curveStr(const std::vector<std::pair<float, float>> &v)
        {
            std::ostringstream o;
            o.precision(7);
            for (size_t i = 0; i < v.size(); ++i)
            {
                if (i) o << ';';
                o << v[i].first << ',' << v[i].second;
            }
            return o.str();
        }

        std::vector<std::pair<float, float>> parseCurve(const std::string &s)
        {
            std::vector<std::pair<float, float>> v;
            std::stringstream ss(s);
            std::string seg;
            while (std::getline(ss, seg, ';'))
            {
                auto comma = seg.find(',');
                if (comma == std::string::npos) continue;
                try
                {
                    v.push_back({std::stof(seg.substr(0, comma)), std::stof(seg.substr(comma + 1))});
                }
                catch (...) {}
            }
            return v;
        }

        // A mixer curve is a list of bezier CONTROL points (not a sampled polyline), so
        // each point serialises its handles when smooth. A corner point writes just
        // "x,y" (2 fields) — identical to the old sampled format, so pre-bezier project
        // files still load (as corner points); a smooth point writes
        // "x,y,ix,iy,ox,oy" (6 fields) and the field count alone flags it as smooth.
        std::string mixerStr(const std::vector<CurvePoint> &v)
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

        std::vector<CurvePoint> parseMixer(const std::string &s)
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

        float f(const std::string &s) { try { return std::stof(s); } catch (...) { return 0.f; } }

        std::vector<float> floats(const std::string &s, char sep)
        {
            std::vector<float> v; std::stringstream ss(s); std::string t;
            while (std::getline(ss, t, sep)) v.push_back(f(t));
            return v;
        }

        // One mask packed as: geometry(11) | localAdjust(12) | dabs(x:y:r:f;...)
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
          << "\ncurve=" << curveStr(p.curve) << "\ncurveLog=" << (p.curveLog ? 1 : 0)
          << "\nmixer0=" << mixerStr(p.mixer[0]) << "\nmixer1=" << mixerStr(p.mixer[1])
          << "\nmixer2=" << mixerStr(p.mixer[2]);
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
            else if (k == "curve") out.curve = parseCurve(v);
            else if (k == "curveLog") out.curveLog = (v != "0");
            else if (k == "mixer0") out.mixer[0] = parseMixer(v);
            else if (k == "mixer1") out.mixer[1] = parseMixer(v);
            else if (k == "mixer2") out.mixer[2] = parseMixer(v);
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
