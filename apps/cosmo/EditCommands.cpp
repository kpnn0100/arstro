#include "EditCommands.h"
#include "analysis/Segmenter.h"
#include "engine/EditParamsIO.h"
#include <sstream>

namespace arstro
{
namespace cosmo_v2
{
namespace editcmd
{
    std::string num(double v)
    {
        std::ostringstream o;
        o.precision(7);
        o << v;
        return o.str();
    }

    std::string pointsStr(const std::vector<arstro::CurvePoint> &pts)
    {
        // Delegated, not reimplemented: this WAS a second copy of the engine's control-point
        // format, which is precisely what R-SVC-5 forbids — and the copy would have had to be
        // found and changed the first time a point grew a field. The engine exports the codec
        // now (`formatCurvePoints`), so there is one.
        return arstro::formatCurvePoints(pts);
    }

    std::string maskBlob(const arstro::MaskParams &m)
    {
        std::ostringstream o;
        o.precision(7);
        o << m.type << ',' << (m.inverted ? 1 : 0) << ',' << m.feather << ',' << m.cx << ',' << m.cy
          << ',' << m.rx << ',' << m.ry << ',' << m.x0 << ',' << m.y0 << ',' << m.x1 << ',' << m.y1
          << ',' << m.subject << ',' << m.sensitivity;   // R-AISEG-9, same group, appended
        return o.str();
    }

    Fields adjustFields(const arstro::LocalAdjust &a)
    {
        return {{"adjust.exposure", num(a.exposure)},     {"adjust.contrast", num(a.contrast)},
                {"adjust.highlights", num(a.highlights)}, {"adjust.shadows", num(a.shadows)},
                {"adjust.whites", num(a.whites)},         {"adjust.blacks", num(a.blacks)},
                {"adjust.temp", num(a.temp)},             {"adjust.tint", num(a.tint)},
                {"adjust.saturation", num(a.saturation)}, {"adjust.texture", num(a.texture)},
                {"adjust.clarity", num(a.clarity)},       {"adjust.dehaze", num(a.dehaze)}};
    }

    Fields maskFields(const arstro::MaskParams &m)
    {
        Fields f = {{"type", std::to_string(m.type)}, {"inverted", m.inverted ? "1" : "0"},
                    {"feather", num(m.feather)},
                    {"cx", num(m.cx)}, {"cy", num(m.cy)}, {"rx", num(m.rx)}, {"ry", num(m.ry)},
                    {"x0", num(m.x0)}, {"y0", num(m.y0)}, {"x1", num(m.x1)}, {"y1", num(m.y1)},
                    // The NAME, not the number: this is what a script and a log line show, and
                    // `subject=sky` is readable a year later where `subject=0` is not. One codec
                    // in the engine parses both (R-SVC-5, R-AISEG-8).
                    {"subject", arstro::semanticSubjectName((arstro::SemanticSubject)m.subject)},
                    {"sensitivity", num(m.sensitivity)}};
        for (auto &kv : adjustFields(m.adjust)) f.push_back(std::move(kv));
        std::string dabs;
        for (size_t i = 0; i < m.dabs.size(); ++i)
        {
            const auto &d = m.dabs[i];
            if (i) dabs += ';';
            dabs += num(d.x) + ':' + num(d.y) + ':' + num(d.radius) + ':' + num(d.flow);
        }
        f.emplace_back("dabs", dabs);   // empty clears them, which is the correct erase
        // R-MASK-6, same rule as dabs: a variable-length list no per-scalar field can carry, and
        // an empty value is the erase. `pointsStr` is the control-point codec the curves use, so
        // a path point is written exactly once, in one format.
        f.emplace_back("path", pointsStr(m.path));
        return f;
    }

    namespace
    {
        cosmo::Command of(cosmo::Command::Kind k)
        {
            cosmo::Command c;
            c.kind = k;
            return c;
        }
    }

    cosmo::Command set(Fields fields)
    {
        cosmo::Command c = of(cosmo::Command::Kind::Set);
        c.fields = std::move(fields);
        return c;
    }

    cosmo::Command setNumber(const std::string &key, double value) { return set({{key, num(value)}}); }

    cosmo::Command setPoints(const std::string &key, const std::vector<arstro::CurvePoint> &pts)
    {
        return set({{key, pointsStr(pts)}});
    }

    cosmo::Command addMask(const arstro::MaskParams &m) { return set({{"mask", maskBlob(m)}}); }

    cosmo::Command maskSet(int index, Fields fields)
    {
        cosmo::Command c = of(cosmo::Command::Kind::MaskSet);
        c.index = index;
        c.fields = std::move(fields);
        return c;
    }

    cosmo::Command maskDelete(int index)
    {
        cosmo::Command c = of(cosmo::Command::Kind::MaskDelete);
        c.index = index;
        return c;
    }

    cosmo::Command diff(const arstro::EditParams &from, const arstro::EditParams &to)
    {
        auto lines = [](const std::string &text) {
            std::vector<std::string> out;
            std::istringstream in(text);
            std::string line;
            while (std::getline(in, line))
                if (!line.empty()) out.push_back(line);
            return out;
        };
        const std::vector<std::string> a = lines(arstro::serializeParams(from));
        const std::vector<std::string> b = lines(arstro::serializeParams(to));

        // Same key order out of the same writer, so a positional walk pairs the keys — except
        // for `mask=`, of which there may be a different NUMBER on each side. Those are skipped
        // (see the header): a mask edit is a mask command.
        Fields changed;
        auto keyOf = [](const std::string &l) { return l.substr(0, l.find('=')); };
        size_t i = 0, j = 0;
        while (i < a.size() && j < b.size())
        {
            const std::string ka = keyOf(a[i]), kb = keyOf(b[j]);
            if (ka == "mask") { ++i; continue; }
            if (kb == "mask") { ++j; continue; }
            if (ka != kb) { ++i; ++j; continue; }        // writer drift: neither side is trusted
            if (a[i] != b[j])
                changed.emplace_back(kb, b[j].substr(b[j].find('=') + 1));
            ++i; ++j;
        }
        for (; j < b.size(); ++j)                        // keys only the new side has
            if (keyOf(b[j]) != "mask")
                changed.emplace_back(keyOf(b[j]), b[j].substr(b[j].find('=') + 1));

        if (changed.empty()) return of(cosmo::Command::Kind::None);
        return set(std::move(changed));
    }

    cosmo::Command settings(Fields fields)
    {
        cosmo::Command c = of(cosmo::Command::Kind::SettingsSet);
        c.fields = std::move(fields);
        return c;
    }

    cosmo::Command select(int node)
    {
        cosmo::Command c = of(cosmo::Command::Kind::Select);
        c.index = node;
        return c;
    }

    cosmo::Command undo() { return of(cosmo::Command::Kind::Undo); }
    cosmo::Command redo() { return of(cosmo::Command::Kind::Redo); }

    cosmo::Command screen(const char *name)
    {
        cosmo::Command c = of(cosmo::Command::Kind::Screen);
        c.name = name ? name : "";
        return c;
    }
}
}
}
