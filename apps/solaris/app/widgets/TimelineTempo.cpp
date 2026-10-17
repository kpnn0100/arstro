// solaris_ui — Timeline: the tempo map's changes on the ruler's band (R-TEMPO-1). A flag at each change's beat saying
// its tempo; a glide into it drawn as a slope from the change before. Keyed by id: a change fades in and out, a move
// made elsewhere eases there, a tempo changed elsewhere counts there; a dragged flag is the pointer's. Every gesture is
// ONE line: dragged, `tempo move --at`; its menu — Tempo… (typed), Glide into it / Step to it, Delete.
#include "Timeline.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;

    namespace
    {
        constexpr double kPpq = 960.0;
        constexpr double kFlagPad = 6.0;
        Color fade(Color c, double a) { c.a *= a; return c; }
        std::string bpmText(double v)
        {
            char b[32];
            std::snprintf(b, sizeof b, "%g", std::round(v * 10.0) / 10.0);
            return b;
        }
    }

    const Timeline::TempoLive *Timeline::tempoLive(const std::string &tp) const
    {
        for (const auto &l : mTempos)
            if (!l.gone && l.t.id == tp) return &l;
        return nullptr;
    }

    Rect Timeline::tempoRect(const std::string &tp) const
    {
        const TempoLive *l = tempoLive(tp);
        if (!l) return Rect{};
        const auto w = mTempoW.find(tp);
        const double width = w != mTempoW.end() ? w->second : kFlagPad + 6.0 * (double)(bpmText(l->t.bpm).size() + 4) + 4.0; // before the first paint
        return Rect{beatToX(tempoBeat(tp)), 0.0, width, kMarkH};
    }
    double Timeline::tempoAlpha(const std::string &tp) const
    {
        for (const auto &l : mTempos)
            if (l.t.id == tp) return l.placed ? l.alpha.value() : 0.0;
        return 0.0;
    }
    double Timeline::tempoBeat(const std::string &tp) const
    {
        if (mTempoDragging && tp == mTempoPress) return mTempoLive;
        const TempoLive *l = tempoLive(tp);
        return l ? (l->placed ? l->at.value() : l->t.at) : -1.0;
    }
    double Timeline::tempoShown(const std::string &tp) const
    {
        const TempoLive *l = tempoLive(tp);
        return l ? (l->placed ? l->bpm.value() : l->t.bpm) : 0.0;
    }

    std::string Timeline::tempoUnder(const Point &local) const
    {
        if (local.y < 0 || local.y >= kMarkH || local.x < kHeaderW) return std::string();
        for (auto it = mTempos.rbegin(); it != mTempos.rend(); ++it)
            if (!it->gone && tempoRect(it->t.id).contains(local)) return it->t.id;
        return std::string();
    }

    void Timeline::bindTempos(const solaris::AppModel &m)
    {
        mSongBpm = m.bpm;
        for (auto &l : mTempos) l.gone = true;
        for (const auto &t : m.tempos)
        {
            TempoLive *l = nullptr;
            for (auto &x : mTempos)
                if (x.t.id == t.id) l = &x;
            if (!l) { mTempos.emplace_back(); l = &mTempos.back(); }
            l->t = t;
            l->gone = false;
        }
    }

    void Timeline::advanceTempos(double nowMs)
    {
        const bool travel = mEver && !reducedMotion();
        for (auto &l : mTempos)
        {
            if (!l.placed)
            {
                l.at.set(l.t.at);
                l.bpm.set(l.t.bpm);
                l.atLast = l.t.at;
                l.bpmLast = l.t.bpm;
                l.alpha.set(travel ? 0.0 : 1.0);
                if (travel) l.alpha.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
                l.placed = true;
            }
            if (l.t.at != l.atLast) { l.at.animateTo(l.t.at, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs); l.atLast = l.t.at; }
            if (l.t.bpm != l.bpmLast) { l.bpm.animateTo(l.t.bpm, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs); l.bpmLast = l.t.bpm; }
            if (l.gone && !l.alpha.isAnimating() && l.alpha.value() > 0.0) l.alpha.animateTo(0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            l.at.update(nowMs);
            l.bpm.update(nowMs);
            l.alpha.update(nowMs);
        }
        mTempos.erase(std::remove_if(mTempos.begin(), mTempos.end(), [](const TempoLive &l) { return l.gone && !l.alpha.isAnimating() && l.alpha.value() <= 0.001; }),
                      mTempos.end());
    }

    bool Timeline::tempoGesture(const Gesture &g, const Point &local)
    {
        auto send = [this](const std::string &line) { if (onCommand) onCommand(line); };
        switch (g.type)
        {
        case Gesture::Type::Down:
            mTempoPress = tempoUnder(local);
            mTempoDragging = false;
            if (mTempoPress.empty()) return false;
            mTempoGrab = xToBeat(local.x) - tempoBeat(mTempoPress);
            mTempoLive = tempoBeat(mTempoPress);
            return true;
        case Gesture::Type::DragStart:
        case Gesture::Type::Drag:
            if (mTempoPress.empty()) return false;
            mTempoDragging = true;
            mTempoLive = std::max(snapStep(), snap(xToBeat(local.x) - mTempoGrab)); // on the grid you see, after the start
            return true;
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
        {
            if (mTempoPress.empty() || !mTempoDragging) { mTempoPress.clear(); return false; }
            const std::string id = mTempoPress;
            mTempoDragging = false;
            mTempoPress.clear();
            for (auto &l : mTempos)
                if (l.t.id == id && std::fabs(mTempoLive - l.t.at) > 0.5 / kPpq)
                {
                    // where it was let go — the pointer was the animation — then the one line; a refusal eases it back
                    l.at.set(mTempoLive);
                    l.atLast = mTempoLive;
                    send("tempo move " + id + " --at " + beatText(mTempoLive));
                }
            return true;
        }
        case Gesture::Type::Click:
        {
            const std::string tp = tempoUnder(local);
            if (tp.empty()) return false;
            send("transport seek " + beatText(tempoBeat(tp))); // clicked, the song goes there
            return true;
        }
        case Gesture::Type::RightClick:
        {
            const std::string tp = tempoUnder(local);
            const TempoLive *l = tp.empty() ? nullptr : tempoLive(tp);
            if (!l || !onMenu) return false;
            const Point world = g.pos;
            const bool ramp = l->t.ramp;
            const std::string now = bpmText(l->t.bpm);
            std::vector<cosmo_v2::ContextMenu::Item> items;
            items.push_back({say("Tempo\xE2\x80\xA6"), [this, send, tp, now, world] {
                                 if (onRename) onRename(now, world, [send, tp](const std::string &v) { if (!v.empty()) send("tempo move " + tp + " --bpm " + v); });
                             }});
            items.push_back({ramp ? say("Step to it") : say("Glide into it"), [send, tp, ramp] { send("tempo move " + tp + " --ramp " + (ramp ? "false" : "true")); }});
            items.push_back({say("Delete tempo change"), [send, tp] { send("tempo delete " + tp); }});
            items.push_back({say("Copy ID"), [this, tp] { if (onCopy) onCopy(tp); }}); // R-UI-11
            onMenu(std::move(items), world);
            return true;
        }
        default:
            return false;
        }
    }

    void Timeline::paintTempos(IRenderTarget &t) const
    {
        if (mTempos.empty()) return;
        const double W = width.value();
        t.save();
        t.clipRect(kHeaderW, 0, W - kHeaderW, kMarkH);
        // a glide: a slope from the change before (or the start) up or down to it, faint, under the flags
        double prevBeat = 0.0, prevBpm = mSongBpm;
        for (const auto &l : mTempos)
        {
            const double a = l.placed ? l.alpha.value() : 0.0, bpm = tempoShown(l.t.id), at = tempoBeat(l.t.id);
            if (a > 0.001 && l.t.ramp && at > prevBeat)
            {
                const double up = bpm >= prevBpm ? 1.0 : 0.0;
                t.setStroke(fade(palette::foreground(), 0.35 * a), 1.0);
                t.beginPath();
                t.moveTo(beatToX(prevBeat), up > 0 ? kMarkH - 2.0 : 2.0);
                t.lineTo(beatToX(at), up > 0 ? 2.0 : kMarkH - 2.0);
                t.strokePath();
            }
            prevBeat = at;
            prevBpm = bpm;
        }
        for (const auto &l : mTempos)
        {
            const double a = l.placed ? l.alpha.value() : 0.0;
            if (a <= 0.001) continue;
            const double x = std::round(beatToX(tempoBeat(l.t.id)));
            const std::string s = bpmText(tempoShown(l.t.id)) + " bpm";
            const double w = kFlagPad + t.measureText(s, 8.0, font::mono()) + 4.0;
            mTempoW[l.t.id] = w; // measured here, where measuring is valid (R5): what a press hits
            const bool held = mTempoDragging && l.t.id == mTempoPress;
            drawRoundedRect(t, Rect{x, 2.0, w, kMarkH - 4.0}, radius::control(), Paint::filled(fade(held ? palette::popover() : palette::muted(), a)));
            drawRoundedRect(t, Rect{x, 2.0, 2.0, kMarkH - 4.0}, 0.0, Paint::filled(fade(palette::mutedForeground(), a)));
            t.setFill(fade(palette::foreground(), 0.85 * a));
            t.drawText(s, x + kFlagPad - 1.0, textfit::baseline(kMarkH * 0.5, 8.0), 8.0, font::mono());
        }
        t.restore();
    }
}
}
