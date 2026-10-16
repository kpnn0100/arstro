// solaris_ui — Timeline: the markers on the ruler's band (R-ARR-1). A flag with its name at its beat, a section (a
// marker with a length, or a point up to the next marker — the model's `end`) a band to its end. Keyed by id: a marker
// fades in and out, a move made elsewhere eases there; a dragged flag is the pointer's. Every gesture is ONE line.
#include "Timeline.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;

    namespace
    {
        constexpr double kPpq = 960.0;     // a tick (R-TIME-1), as Timeline.cpp rounds
        constexpr double kFlagPad = 8.0;   // a flag's name inset, its edge mark included
        Color fade(Color c, double a) { c.a *= a; return c; }
        std::string quoted(const std::string &name)
        {
            std::string s;
            for (char c : name)
                if (c != '"') s += c; // a name is one token: no quote inside it
            return "\"" + s + "\"";
        }
    }

    const Timeline::MarkerLive *Timeline::markerLive(const std::string &mk) const
    {
        for (const auto &l : mMarkers)
            if (!l.gone && l.m.id == mk) return &l;
        return nullptr;
    }

    Rect Timeline::markerRect(const std::string &mk) const
    {
        const MarkerLive *l = markerLive(mk);
        if (!l) return Rect{};
        const auto w = mMarkW.find(mk);
        const double width = w != mMarkW.end() ? w->second : kFlagPad + 6.0 * (double)l->m.name.size() + 4.0; // before the first paint
        return Rect{beatToX(markerBeat(mk)), 0.0, width, kMarkH};
    }
    double Timeline::markerAlpha(const std::string &mk) const
    {
        for (const auto &l : mMarkers)
            if (l.m.id == mk) return l.placed ? l.alpha.value() : 0.0;
        return 0.0;
    }
    double Timeline::markerBeat(const std::string &mk) const
    {
        if (mMarkDragging && mk == mMarkPress) return mMarkLive;
        const MarkerLive *l = markerLive(mk);
        return l ? (l->placed ? l->at.value() : l->m.at) : -1.0;
    }
    double Timeline::markerEnd(const std::string &mk) const
    {
        const MarkerLive *l = markerLive(mk);
        if (!l) return -1.0;
        const double e = l->placed ? l->end.value() : l->m.end;
        return mMarkDragging && mk == mMarkPress ? e + (mMarkLive - l->at.value()) : e; // a section travels with its flag
    }

    std::string Timeline::markerUnder(const Point &local) const
    {
        if (local.y < 0 || local.y >= kMarkH || local.x < kHeaderW) return std::string();
        for (auto it = mMarkers.rbegin(); it != mMarkers.rend(); ++it) // the last drawn is on top
            if (!it->gone && markerRect(it->m.id).contains(local)) return it->m.id;
        return std::string();
    }

    void Timeline::bindMarkers(const solaris::AppModel &m)
    {
        for (auto &l : mMarkers) l.gone = true;
        for (const auto &mk : m.markers)
        {
            MarkerLive *l = nullptr;
            for (auto &x : mMarkers)
                if (x.m.id == mk.id) l = &x;
            if (!l) { mMarkers.emplace_back(); l = &mMarkers.back(); }
            l->m = mk;
            l->gone = false;
        }
    }

    void Timeline::advanceMarkers(double nowMs)
    {
        const bool travel = mEver && !reducedMotion(); // a song opened places them where they are
        for (auto &l : mMarkers)
        {
            if (!l.placed)
            {
                l.at.set(l.m.at);
                l.end.set(l.m.end);
                l.atLast = l.m.at;
                l.endLast = l.m.end;
                l.alpha.set(travel ? 0.0 : 1.0);
                if (travel) l.alpha.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
                l.placed = true;
            }
            if (l.m.at != l.atLast)
            {
                l.at.animateTo(l.m.at, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
                l.atLast = l.m.at;
            }
            if (l.m.end != l.endLast)
            {
                l.end.animateTo(l.m.end, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
                l.endLast = l.m.end;
            }
            if (l.gone && !l.alpha.isAnimating() && l.alpha.value() > 0.0) l.alpha.animateTo(0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            l.at.update(nowMs);
            l.end.update(nowMs);
            l.alpha.update(nowMs);
        }
        mMarkers.erase(std::remove_if(mMarkers.begin(), mMarkers.end(), [](const MarkerLive &l) { return l.gone && !l.alpha.isAnimating() && l.alpha.value() <= 0.001; }),
                       mMarkers.end());
    }

    bool Timeline::markerGesture(const Gesture &g, const Point &local)
    {
        auto send = [this](const std::string &line) { if (onCommand) onCommand(line); };
        switch (g.type)
        {
        case Gesture::Type::Down:
        {
            mMarkPress = markerUnder(local);
            mMarkDragging = false;
            if (mMarkPress.empty()) return false; // the ruler's own: a seek, a scrub, the brace
            mMarkGrab = xToBeat(local.x) - markerBeat(mMarkPress);
            mMarkLive = markerBeat(mMarkPress);
            return true;
        }
        case Gesture::Type::DragStart:
        case Gesture::Type::Drag:
            if (mMarkPress.empty()) return false;
            mMarkDragging = true;
            mMarkLive = std::max(0.0, snap(xToBeat(local.x) - mMarkGrab)); // on the grid you see — the pointer's
            return true;
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
        {
            if (mMarkPress.empty() || !mMarkDragging) return false; // a press not moved: its Click follows
            const std::string id = mMarkPress;
            mMarkDragging = false;
            mMarkPress.clear();
            for (auto &l : mMarkers)
                if (l.m.id == id && std::fabs(mMarkLive - l.m.at) > 0.5 / kPpq)
                {
                    // where it was let go — the pointer was the animation — then the one line
                    const double d = mMarkLive - l.at.value();
                    l.at.set(mMarkLive);
                    l.atLast = mMarkLive;
                    l.end.set(l.end.value() + d);
                    send("marker move " + id + " --at " + beatText(mMarkLive));
                }
            return true;
        }
        case Gesture::Type::Click:
        {
            const std::string mk = markerUnder(local);
            mMarkPress.clear();
            if (mk.empty()) return false;
            send("transport seek " + mk); // R-ARR-1: clicked, the song goes there
            return true;
        }
        case Gesture::Type::DoubleClick:
        {
            const std::string mk = markerUnder(local);
            if (mk.empty()) return false;
            send("transport loop " + mk); // … double-clicked, its section loops
            return true;
        }
        case Gesture::Type::RightClick:
        {
            if (!rulerRect().contains(local) || !onMenu) return false;
            const std::string mk = markerUnder(local);
            const Point world = g.pos;
            std::vector<cosmo_v2::ContextMenu::Item> items;
            if (const MarkerLive *l = mk.empty() ? nullptr : markerLive(mk))
            {
                const std::string name = l->m.name;
                items.push_back({"Loop its section", [send, mk] { send("transport loop " + mk); }});
                items.push_back({"Rename\xE2\x80\xA6", [this, send, mk, name, world] {
                                     if (onRename) onRename(name, world, [send, mk](const std::string &to) { if (!to.empty()) send("marker rename " + mk + " " + quoted(to)); });
                                 }});
                items.push_back({"Delete marker", [send, mk] { send("marker delete " + mk); }});
                items.push_back({"Copy ID", [this, mk] { if (onCopy) onCopy(mk); }}); // R-UI-11
            }
            else
            {
                // a marker here, on the grid you see, named by its place in the song
                const double at = snap(xToBeat(local.x));
                const std::string name = "Marker " + std::to_string(mMarkers.size() + 1);
                items.push_back({"Add marker here", [send, at, name] { send("marker add " + quoted(name) + " --at " + beatText(at)); }});
                // R-ARR-2: time across the whole song — the bar under the pointer, or the loop's span
                const double bar = std::floor(std::max(0.0, xToBeat(local.x)) / mBeatsPerBar) * mBeatsPerBar, len = (double)mBeatsPerBar;
                items.push_back({"Insert a bar here", [send, bar, len] { send("time insert --at " + beatText(bar) + " --length " + beatText(len)); }});
                items.push_back({"Delete this bar", [send, bar, len] { send("time delete --from " + beatText(bar) + " --to " + beatText(bar + len)); }});
                if (mLoopTo > mLoopFrom)
                {
                    const double f = mLoopFrom, t = mLoopTo;
                    items.push_back({"Delete the loop's time", [send, f, t] { send("time delete --from " + beatText(f) + " --to " + beatText(t)); }});
                }
            }
            onMenu(std::move(items), world);
            return true;
        }
        default:
            return false;
        }
    }

    void Timeline::paintMarkers(IRenderTarget &t) const
    {
        const double W = width.value();
        t.save();
        t.clipRect(0, 0, W, kMarkH);
        t.setFill(palette::mutedForeground());
        t.drawText("MARKERS", 12.0, textfit::baseline(kMarkH * 0.5, 8.0), 8.0, font::sansSemiBold(), 0.13 * 8.0);
        t.clipRect(kHeaderW, 0, W - kHeaderW, kMarkH);
        // sections first (under every flag), then the flags in time order — a later one over an earlier
        for (const auto &l : mMarkers)
        {
            const double a = l.placed ? l.alpha.value() : 0.0;
            const double x0 = beatToX(markerBeat(l.m.id)), x1 = beatToX(markerEnd(l.m.id));
            if (a <= 0.001 || x1 <= x0 + 0.5) continue;
            drawRoundedRect(t, Rect{x0, 1.5, x1 - x0, kMarkH - 3.0}, radius::control(),
                            Paint::filled(palette::whiteAlpha((l.m.length > 0 ? 0.075 : 0.035) * a)));
        }
        for (const auto &l : mMarkers)
        {
            const double a = l.placed ? l.alpha.value() : 0.0;
            if (a <= 0.001) continue;
            const double x = std::round(beatToX(markerBeat(l.m.id)));
            const double w = kFlagPad + t.measureText(l.m.name, 9.0, font::sans()) + 4.0;
            mMarkW[l.m.id] = w; // measured here, where measuring is valid (R5): what a press hits
            const bool held = mMarkDragging && l.m.id == mMarkPress;
            drawRoundedRect(t, Rect{x, 1.0, w, kMarkH - 2.0}, radius::control(), Paint::filled(fade(held ? palette::popover() : palette::secondary(), a)));
            drawRoundedRect(t, Rect{x, 1.0, 2.0, kMarkH - 2.0}, 0.0, Paint::filled(fade(palette::foreground(), a)));
            t.setFill(fade(palette::foreground(), a));
            t.drawText(l.m.name, x + kFlagPad - 2.0, textfit::baseline(kMarkH * 0.5, 9.0), 9.0, font::sans());
        }
        t.restore();
        // where each marker is, down through the bars' band: a hairline
        t.save();
        t.clipRect(kHeaderW, kMarkH, W - kHeaderW, kRulerH - kMarkH);
        for (const auto &l : mMarkers)
        {
            const double a = l.placed ? l.alpha.value() : 0.0;
            if (a <= 0.001) continue;
            const double x = std::round(beatToX(markerBeat(l.m.id))) + 0.5;
            t.setStroke(palette::whiteAlpha(0.28 * a), 1.0);
            t.beginPath(); t.moveTo(x, kMarkH); t.lineTo(x, kRulerH); t.strokePath();
        }
        t.restore();
    }
}
}
