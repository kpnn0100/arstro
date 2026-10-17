// solaris_ui — Timeline: a clip's takes under its lane (R-REC-2). A take is a row (keyed — rows below slide, eased),
// lit where the comp plays it: the model's `plays`, cross-faded from what was lit as the comp changes. A drag across a
// take is ONE `clip comp <ac> --take <k> --from --to` on the grid you see; a double-click, that take for the whole clip.
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
        constexpr double kPpq = 960.0;
        Color fade(Color c, double a) { c.a *= a; return c; }
        std::string key(const std::string &ac, int k) { return ac + "#" + std::to_string(k); }
        double litIn(const std::vector<std::pair<double, double>> &runs, double beat)
        {
            for (const auto &r : runs)
                if (beat >= r.first - 1e-9 && beat < r.second - 1e-9) return 1.0;
            return 0.0;
        }
    }

    void Timeline::showTakes(const std::string &ac, bool on)
    {
        if (on) mTakesOpen.insert(ac);
        else mTakesOpen.erase(ac);
        if (mModelRef) bind(*mModelRef); // the view's: the rows follow at once (inserted, eased)
    }

    Rect Timeline::takeRowRect(const std::string &ac, int take) const
    {
        for (int i = 0; i < (int)mRows.size(); ++i)
            if (mRows[(size_t)i].takeClip == ac && mRows[(size_t)i].take == take) return rowRect(i);
        return Rect{};
    }

    double Timeline::takeLit(const std::string &ac, int take, double beat) const
    {
        const auto it = mTakeLit.find(key(ac, take));
        if (it == mTakeLit.end()) return 0.0;
        const double t = it->second.t.value();
        return (1.0 - t) * litIn(it->second.from, beat) + t * litIn(it->second.to, beat);
    }

    void Timeline::bindTakes(const solaris::AppModel &m)
    {
        for (const auto &c : m.clips)
        {
            if (c.takes.empty()) continue;
            for (int k = 1; k <= (int)c.takes.size(); ++k)
            {
                std::vector<std::pair<double, double>> runs;
                for (const auto &p : c.plays)
                    if (p.take == k) runs.push_back({p.from, p.to});
                TakeLit &l = mTakeLit[key(c.id, k)];
                if (!l.placed) { l.from = l.to = runs; l.t.set(1.0); l.placed = true; continue; }
                if (runs != l.to)
                {
                    // cross-fade from what is SHOWN: mid-fade, the old picture is the blend frozen at its target
                    l.from = l.t.value() >= 0.5 ? l.to : l.from;
                    l.to = runs;
                    l.t.set(0.0);
                    l.t.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, mNowMs);
                }
            }
        }
    }

    void Timeline::advanceTakes(double nowMs)
    {
        for (auto &kv : mTakeLit) kv.second.t.update(nowMs);
    }

    bool Timeline::takeGesture(const Gesture &g, const Point &local)
    {
        auto rowOf = [&](const Point &p) -> const Row * {
            const int i = rowAt(p.y);
            if (p.x < kHeaderW || i < 0 || i >= (int)mRows.size() || mRows[(size_t)i].takeClip.empty()) return nullptr;
            return &mRows[(size_t)i];
        };
        auto spanOf = [&](const std::string &ac, double &a, double &b) {
            for (const auto &v : mClips)
                if (v.c.id == ac) { a = v.c.at; b = v.c.at + v.c.length; return true; }
            return false;
        };
        switch (g.type)
        {
        case Gesture::Type::Down:
        {
            mTakeDragging = false;
            const Row *r = rowOf(local);
            double a = 0, b = 0;
            if (!r || !spanOf(r->takeClip, a, b)) { mTakeClip.clear(); return false; }
            mTakeClip = r->takeClip;
            mTakeIdx = r->take;
            mTakeA = mTakeB = std::clamp(snap(xToBeat(local.x)), a, b);
            return true;
        }
        case Gesture::Type::DragStart:
        case Gesture::Type::Drag:
        {
            if (mTakeClip.empty()) return false;
            double a = 0, b = 0;
            spanOf(mTakeClip, a, b);
            mTakeDragging = true;
            mTakeB = std::clamp(snap(xToBeat(local.x)), a, b); // the pointer's, on the grid you see
            return true;
        }
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
        {
            if (mTakeClip.empty() || !mTakeDragging) return !mTakeClip.empty();
            const double a = std::min(mTakeA, mTakeB), b = std::max(mTakeA, mTakeB);
            const std::string ac = mTakeClip;
            mTakeDragging = false;
            mTakeClip.clear();
            if (b - a > 0.5 / kPpq && onCommand)
                onCommand("clip comp " + ac + " --take " + std::to_string(mTakeIdx) + " --from " + beatText(a) + " --to " + beatText(b));
            return true;
        }
        case Gesture::Type::Click:
            if (rowOf(local)) { mTakeClip.clear(); return true; } // a press with no drag chooses nothing
            return false;
        case Gesture::Type::DoubleClick:
        {
            const Row *r = rowOf(local);
            if (!r) return false;
            if (onCommand) onCommand("clip comp " + r->takeClip + " --take " + std::to_string(r->take)); // the whole clip
            return true;
        }
        default:
            return false;
        }
    }

    void Timeline::paintTakes(IRenderTarget &t) const
    {
        const double W = width.value(), H = height.value();
        t.save();
        t.clipRect(kHeaderW, top(), W - kHeaderW, H - top());
        for (const auto &row : mRowMotion.rows())
        {
            const Row &d = row.data;
            if (d.takeClip.empty() || row.liveAlpha() <= 0.001) continue;
            const ClipLive *cl = live(d.takeClip);
            if (!cl) continue;
            const double a = row.liveAlpha(), top = this->top() + row.liveY() - mScrollY.value();
            const double at = cl->at.value(), len = liveLength(*cl);
            const Rect r{beatToX(at), top + 6.0, std::max(3.0, beatToX(at + len) - beatToX(at) - 1.0), kRowH - 12.0};
            const Color hue = lerpColor(cl->hueFrom, cl->hueTo, cl->hueT.value());
            drawRoundedRect(t, r, radius::control(), Paint::filledStroked(fade(hue, 0.08 * a), fade(hue, 0.35 * a), 1.0));
            // where the comp plays it: lit — the last runs fading out, the new ones in
            const auto it = mTakeLit.find(key(d.takeClip, d.take));
            if (it != mTakeLit.end())
            {
                const double k = it->second.t.value();
                for (int side = 0; side < 2; ++side)
                {
                    const auto &runs = side ? it->second.to : it->second.from;
                    const double w = side ? k : 1.0 - k;
                    if (w <= 0.001) continue;
                    for (const auto &rn : runs)
                    {
                        const double x0 = beatToX(rn.first), x1 = beatToX(rn.second);
                        drawRoundedRect(t, Rect{x0, r.y, std::max(1.0, x1 - x0), r.h}, radius::control(), Paint::filled(fade(hue, 0.55 * w * a)));
                    }
                }
            }
            t.setFill(fade(palette::foreground(), 0.8 * a));
            t.drawText(std::to_string(d.take), std::max(r.x, kHeaderW) + 5.0, textfit::baseline(r.y + 10.0, 9.0), 9.0, font::mono());
            if (mTakeDragging && mTakeClip == d.takeClip && mTakeIdx == d.take) // the range in hand: the pointer's
            {
                const double x0 = beatToX(std::min(mTakeA, mTakeB)), x1 = beatToX(std::max(mTakeA, mTakeB));
                drawRoundedRect(t, Rect{x0, r.y, std::max(1.0, x1 - x0), r.h}, radius::control(),
                                Paint::filledStroked(palette::primaryAlpha(0.18), palette::primaryAlpha(0.8), 1.0));
            }
        }
        t.restore();
    }
}
}
