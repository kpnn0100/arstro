#include "EditParamsApf.h"
#include "EditParamsIO.h"
#include <algorithm>
#include <sstream>

namespace arstro
{
    namespace
    {
        std::string fs(float v) { std::ostringstream o; o.precision(7); o << v; return o.str(); }
        float f(const std::string &s) { try { return std::stof(s); } catch (...) { return 0.f; } }
        bool has(const std::vector<std::string> &v, const std::string &s)
        { return std::find(v.begin(), v.end(), s) != v.end(); }

        std::vector<float> floats(const std::string &s, char sep)
        {
            std::vector<float> v; std::stringstream ss(s); std::string t;
            while (std::getline(ss, t, sep)) v.push_back(f(t));
            return v;
        }
        // Mixer curves persist bezier CONTROL points: "x,y" for a corner (2 fields,
        // matching pre-bezier presets) or "x,y,ix,iy,ox,oy" for a smooth point.
        std::string mixerStr(const std::vector<CurvePoint> &v)
        {
            std::ostringstream o; o.precision(7);
            for (size_t i = 0; i < v.size(); ++i)
            { if (i) o << ';'; const CurvePoint &p = v[i]; o << p.x << ',' << p.y;
              if (p.smooth) o << ',' << p.ix << ',' << p.iy << ',' << p.ox << ',' << p.oy; }
            return o.str();
        }
        std::vector<CurvePoint> parseMixer(const std::string &s)
        {
            std::vector<CurvePoint> v; std::stringstream ss(s); std::string seg;
            while (std::getline(ss, seg, ';'))
            { auto n = floats(seg, ','); if (n.size() < 2) continue;
              CurvePoint p; p.x = n[0]; p.y = n[1];
              if (n.size() >= 6) { p.smooth = true; p.ix = n[2]; p.iy = n[3]; p.ox = n[4]; p.oy = n[5]; }
              v.push_back(p); }
            return v;
        }
    }

    const char *apfImageEngine() { return "image"; }

    std::vector<std::string> apfImageCategories()
    {
        return {"basic", "color", "presence", "effects", "detail", "lens", "curve", "mixer", "grade", "transform", "masks"};
    }

    apf::Document editParamsToApf(const EditParams &p, const std::vector<std::string> &cats,
                                  const std::string &name, const std::string &app)
    {
        apf::Document d;
        d.engine = apfImageEngine();
        d.app = app;
        d.name = name;
        if (has(cats, "basic")) { auto &c = d.category("basic");
            c.set("exposure", fs(p.exposure)); c.set("contrast", fs(p.contrast));
            c.set("highlights", fs(p.highlights)); c.set("shadows", fs(p.shadows));
            c.set("whites", fs(p.whites)); c.set("blacks", fs(p.blacks)); }
        if (has(cats, "color")) { auto &c = d.category("color");
            c.set("temp", fs(p.temp)); c.set("tint", fs(p.tint));
            c.set("vibrance", fs(p.vibrance)); c.set("saturation", fs(p.saturation)); }
        if (has(cats, "presence")) { auto &c = d.category("presence");
            c.set("texture", fs(p.texture)); c.set("clarity", fs(p.clarity)); c.set("dehaze", fs(p.dehaze)); }
        if (has(cats, "effects")) { auto &c = d.category("effects");
            c.set("grainAmount", fs(p.grainAmount)); c.set("grainSize", fs(p.grainSize)); }
        if (has(cats, "detail")) { auto &c = d.category("detail");
            c.set("sharpenAmount", fs(p.sharpenAmount)); c.set("sharpenRadius", fs(p.sharpenRadius));
            c.set("sharpenMasking", fs(p.sharpenMasking)); c.set("nrLuminance", fs(p.nrLuminance));
            c.set("nrColor", fs(p.nrColor)); }
        if (has(cats, "lens")) { auto &c = d.category("lens");
            c.set("lensDistortion", fs(p.lensDistortion)); c.set("lensCA", fs(p.lensCA));
            c.set("lensVignette", fs(p.lensVignette)); }
        if (has(cats, "curve")) { auto &c = d.category("curve");
            c.set("curve", mixerStr(p.curve)); c.set("curveLog", p.curveLog ? "1" : "0");
            c.set("curveR", mixerStr(p.curveChannel[0])); c.set("curveG", mixerStr(p.curveChannel[1]));
            c.set("curveB", mixerStr(p.curveChannel[2])); }
        if (has(cats, "mixer")) { auto &c = d.category("mixer");
            c.set("mixer0", mixerStr(p.mixer[0])); c.set("mixer1", mixerStr(p.mixer[1])); c.set("mixer2", mixerStr(p.mixer[2]));
            c.set("mixerSpread", fs(p.mixerSpread)); }
        if (has(cats, "grade")) { auto &c = d.category("grade");
            for (int r = 0; r < 3; ++r) { std::ostringstream g; g.precision(7);
                g << p.grade[r].hue << ',' << p.grade[r].sat << ',' << p.grade[r].lum;
                c.set("grade" + std::to_string(r), g.str()); }
            c.set("balance", fs(p.balance)); c.set("remapEnable", p.remapEnable ? "1" : "0");
            c.set("remapSrc", fs(p.remapSrc)); c.set("remapRange", fs(p.remapRange));
            c.set("remapDst", fs(p.remapDst)); c.set("remapStrength", fs(p.remapStrength)); }
        if (has(cats, "transform")) { auto &c = d.category("transform");
            std::ostringstream cr; cr.precision(7); cr << p.cropX << ',' << p.cropY << ',' << p.cropW << ',' << p.cropH;
            c.set("crop", cr.str()); c.set("rotation", fs(p.rotation)); c.set("quarterTurns", std::to_string(p.quarterTurns)); }
        if (has(cats, "masks")) { auto &c = d.category("masks");
            // ONE codec, shared with the project format (D-57). This was a second
            // hand-written copy and it was a group short, so a drawn path mask was silently
            // dropped by every preset — and a semantic mask's subject would have been next.
            for (const auto &m : p.masks) c.set("mask", formatMaskBlob(m)); }
        return d;
    }

    std::vector<std::string> apfPresentImageCategories(const apf::Document &doc)
    {
        std::vector<std::string> out;
        for (const auto &c : apfImageCategories()) if (doc.has(c)) out.push_back(c);
        return out;
    }

    bool applyApfToEditParams(const apf::Document &doc, const std::vector<std::string> &cats, EditParams &io)
    {
        if (!doc.engine.empty() && doc.engine != apfImageEngine()) return false;  // reject cross-engine
        auto val = [](const apf::Category *c, const std::string &k, const std::string &dflt = "") -> std::string
        { if (!c) return dflt; const std::string *v = c->get(k); return v ? *v : dflt; };

        if (const apf::Category *c = has(cats, "basic") ? doc.find("basic") : nullptr) {
            io.exposure = f(val(c, "exposure")); io.contrast = f(val(c, "contrast"));
            io.highlights = f(val(c, "highlights")); io.shadows = f(val(c, "shadows"));
            io.whites = f(val(c, "whites")); io.blacks = f(val(c, "blacks")); }
        if (const apf::Category *c = has(cats, "color") ? doc.find("color") : nullptr) {
            io.temp = f(val(c, "temp")); io.tint = f(val(c, "tint"));
            io.vibrance = f(val(c, "vibrance")); io.saturation = f(val(c, "saturation")); }
        if (const apf::Category *c = has(cats, "presence") ? doc.find("presence") : nullptr) {
            io.texture = f(val(c, "texture")); io.clarity = f(val(c, "clarity")); io.dehaze = f(val(c, "dehaze")); }
        if (const apf::Category *c = has(cats, "effects") ? doc.find("effects") : nullptr) {
            io.grainAmount = f(val(c, "grainAmount")); io.grainSize = f(val(c, "grainSize")); }
        if (const apf::Category *c = has(cats, "detail") ? doc.find("detail") : nullptr) {
            io.sharpenAmount = f(val(c, "sharpenAmount")); io.sharpenRadius = f(val(c, "sharpenRadius"));
            io.sharpenMasking = f(val(c, "sharpenMasking")); io.nrLuminance = f(val(c, "nrLuminance"));
            io.nrColor = f(val(c, "nrColor")); }
        if (const apf::Category *c = has(cats, "lens") ? doc.find("lens") : nullptr) {
            io.lensDistortion = f(val(c, "lensDistortion")); io.lensCA = f(val(c, "lensCA"));
            io.lensVignette = f(val(c, "lensVignette")); }
        if (const apf::Category *c = has(cats, "curve") ? doc.find("curve") : nullptr) {
            io.curve = parseMixer(val(c, "curve")); io.curveLog = val(c, "curveLog", "1") != "0";
            io.curveChannel[0] = parseMixer(val(c, "curveR", "0,0;1,1"));
            io.curveChannel[1] = parseMixer(val(c, "curveG", "0,0;1,1"));
            io.curveChannel[2] = parseMixer(val(c, "curveB", "0,0;1,1")); }
        if (const apf::Category *c = has(cats, "mixer") ? doc.find("mixer") : nullptr) {
            io.mixer[0] = parseMixer(val(c, "mixer0")); io.mixer[1] = parseMixer(val(c, "mixer1"));
            io.mixer[2] = parseMixer(val(c, "mixer2"));
            io.mixerSpread = f(val(c, "mixerSpread", "25")); }
        if (const apf::Category *c = has(cats, "grade") ? doc.find("grade") : nullptr) {
            for (int r = 0; r < 3; ++r) { auto g = floats(val(c, "grade" + std::to_string(r)), ',');
                if (g.size() >= 3) { io.grade[r].hue = g[0]; io.grade[r].sat = g[1]; io.grade[r].lum = g[2]; } }
            io.balance = f(val(c, "balance")); io.remapEnable = val(c, "remapEnable", "0") != "0";
            io.remapSrc = f(val(c, "remapSrc")); io.remapRange = f(val(c, "remapRange"));
            io.remapDst = f(val(c, "remapDst")); io.remapStrength = f(val(c, "remapStrength")); }
        if (const apf::Category *c = has(cats, "transform") ? doc.find("transform") : nullptr) {
            auto cr = floats(val(c, "crop"), ','); if (cr.size() >= 4) { io.cropX = cr[0]; io.cropY = cr[1]; io.cropW = cr[2]; io.cropH = cr[3]; }
            io.rotation = f(val(c, "rotation")); io.quarterTurns = (int)f(val(c, "quarterTurns")); }
        if (const apf::Category *c = has(cats, "masks") ? doc.find("masks") : nullptr) {
            io.masks.clear();
            for (const auto &kv : c->values) if (kv.first == "mask") io.masks.push_back(parseMaskBlob(kv.second)); }
        return true;
    }
}
