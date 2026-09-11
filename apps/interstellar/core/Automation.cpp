#include "Automation.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace interstellar
{
    double Automation::mapValue(const AutoLink &l, double shapeV, double staticValue)
    {
        // Value mapping first: `from`/`to` is the form a human writes ("exposure ramps 0 to
        // +0.8 EV"), `scale`/`offset` the form a script writes. Exactly one pair is in force.
        const double mapped = l.haveFromTo ? l.from + (l.to - l.from) * shapeV
                                           : shapeV * l.scale + l.offset;
        switch (l.mode)
        {
            case AutoMode::Absolute: return mapped;
            case AutoMode::Add: return staticValue + mapped;
            case AutoMode::Multiply: return staticValue * mapped;
        }
        return mapped;
    }

    bool Automation::localTime(const AutoLink &l, const AutoClip &shape, double t, double clipAt,
                               double &localT)
    {
        // A scoped link's `at` is in the CLIP's local time, so the ramp travels with the shot
        // through a re-cut (R-AUTO-7).
        const double origin = l.scope.empty() ? l.at : clipAt + l.at;
        const double span = l.dur > 0.0 ? l.dur : shape.dur;
        if (span <= 0.0) return false;
        const double rel = t - origin;
        if (rel < 0.0 || rel >= span) return false;
        // `dur` on the link TIME-SCALES the shape; the shape is neither copied nor resampled,
        // only its argument changes.
        localT = shape.dur > 0.0 ? rel * (shape.dur / span) : 0.0;
        return true;
    }

    Automation::Sample Automation::sample(const std::string &address, double t,
                                          double staticValue) const
    {
        Sample s;
        for (const auto &l : mP.autoLinks)
        {
            if (l.target != address) continue;
            const AutoClip *shape = mP.autoClip(l.clip);
            if (!shape) continue;   // an orphaned link leaves the address static (binding.md §7)
            double clipAt = 0;
            if (!l.scope.empty())
            {
                const Clip *c = mP.clip(l.scope);
                if (!c) continue;
                clipAt = c->at;
            }
            double localT = 0;
            if (!localTime(l, *shape, t, clipAt, localT)) continue;

            double v = mapValue(l, shape->value(localT), staticValue);

            // The fades ramp from the STATIC value into the shape's first value and back out,
            // in frames. Default 0 does exactly what the numbers say — and the lint is what
            // stops that honesty being a trap (R-AUTO-5).
            const double span = l.dur > 0.0 ? l.dur : shape->dur;
            const double origin = l.scope.empty() ? l.at : clipAt + l.at;
            const double rel = t - origin;
            const double fps = mP.fps > 0 ? mP.fps : 24.0;
            if (l.fadeIn > 0)
            {
                const double fade = l.fadeIn / fps;
                if (rel < fade && fade > 0) v = staticValue + (v - staticValue) * (rel / fade);
            }
            if (l.fadeOut > 0)
            {
                const double fade = l.fadeOut / fps;
                const double left = span - rel;
                if (left < fade && fade > 0) v = staticValue + (v - staticValue) * (left / fade);
            }
            s.active = true;
            s.value = v;
            return s;   // links on one address cannot overlap, so the first hit is the answer
        }
        return s;
    }

    bool Automation::wouldOverlap(const std::string &address, double at, double dur,
                                  const NodeId &ignore) const
    {
        const double aEnd = at + dur;
        for (const auto &l : mP.autoLinks)
        {
            if (l.target != address || l.id == ignore) continue;
            const AutoClip *shape = mP.autoClip(l.clip);
            const double span = l.dur > 0.0 ? l.dur : (shape ? shape->dur : 0.0);
            double origin = l.at;
            if (!l.scope.empty())
                if (const Clip *c = mP.clip(l.scope)) origin = c->at + l.at;
            if (at < origin + span && origin < aEnd) return true;
        }
        return false;
    }

    bool Automation::addLink(const AutoLink &in, std::string &err)
    {
        const AutoClip *shape = mP.autoClip(in.clip);
        if (!shape) { err = "no such automation clip: " + in.clip; return false; }
        AutoLink l = in;
        if (l.scope.empty() ? false : mP.clip(l.scope) == nullptr)
        { err = "no such clip to scope to: " + l.scope; return false; }
        const double span = l.dur > 0.0 ? l.dur : shape->dur;
        double origin = l.at;
        if (!l.scope.empty())
            if (const Clip *c = mP.clip(l.scope)) origin = c->at + l.at;
        if (wouldOverlap(l.target, origin, span))
        {
            // Two producers for one value at one instant is the thing the model refuses rather
            // than resolves by precedence (R-AUTO-4).
            err = "a link already covers " + l.target + " at " + canonicalTime(origin) +
                  " — two authorities for one value";
            return false;
        }
        if (l.id.empty()) l.id = mP.freshId("al_");
        if (l.name.empty()) l.name = mP.freshName("al");
        mP.autoLinks.push_back(l);
        return true;
    }

    std::vector<Automation::Lane> Automation::lanes(const std::string &objectName) const
    {
        std::vector<Lane> out;
        const std::string prefix = objectName + ".";
        for (const auto &l : mP.autoLinks)
        {
            if (!objectName.empty() && l.target.rfind(prefix, 0) != 0) continue;
            auto it = std::find_if(out.begin(), out.end(),
                                   [&](const Lane &ln) { return ln.address == l.target; });
            if (it == out.end()) { out.push_back({l.target, {&l}}); continue; }
            it->links.push_back(&l);
        }
        std::sort(out.begin(), out.end(),
                  [](const Lane &a, const Lane &b) { return a.address < b.address; });
        return out;
    }
}
}
