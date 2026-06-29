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

        float f(const std::string &s) { try { return std::stof(s); } catch (...) { return 0.f; } }
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
          << "\ndehaze=" << p.dehaze << "\ngrainAmount=" << p.grainAmount
          << "\ngrainSize=" << p.grainSize
          << "\ncurve=" << curveStr(p.curve) << "\ncurveLog=" << (p.curveLog ? 1 : 0)
          << "\nmixer0=" << curveStr(p.mixer[0]) << "\nmixer1=" << curveStr(p.mixer[1])
          << "\nmixer2=" << curveStr(p.mixer[2]);
        for (int r = 0; r < 3; ++r)
            o << "\ngrade" << r << '=' << p.grade[r].hue << ',' << p.grade[r].sat << ',' << p.grade[r].lum;
        o << "\nbalance=" << p.balance
          << "\nremapEnable=" << (p.remapEnable ? 1 : 0)
          << "\nremapSrc=" << p.remapSrc << "\nremapRange=" << p.remapRange
          << "\nremapDst=" << p.remapDst << "\nremapStrength=" << p.remapStrength
          << "\ncrop=" << p.cropX << ',' << p.cropY << ',' << p.cropW << ',' << p.cropH
          << "\nrotation=" << p.rotation << "\nquarterTurns=" << p.quarterTurns << "\n";
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
            else if (k == "dehaze") out.dehaze = f(v);
            else if (k == "grainAmount") out.grainAmount = f(v);
            else if (k == "grainSize") out.grainSize = f(v);
            else if (k == "curve") out.curve = parseCurve(v);
            else if (k == "curveLog") out.curveLog = (v != "0");
            else if (k == "mixer0") out.mixer[0] = parseCurve(v);
            else if (k == "mixer1") out.mixer[1] = parseCurve(v);
            else if (k == "mixer2") out.mixer[2] = parseCurve(v);
            else if (k == "balance") out.balance = f(v);
            else if (k == "remapEnable") out.remapEnable = (v != "0");
            else if (k == "remapSrc") out.remapSrc = f(v);
            else if (k == "remapRange") out.remapRange = f(v);
            else if (k == "remapDst") out.remapDst = f(v);
            else if (k == "remapStrength") out.remapStrength = f(v);
            else if (k == "rotation") out.rotation = f(v);
            else if (k == "quarterTurns") out.quarterTurns = (int)f(v);
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
