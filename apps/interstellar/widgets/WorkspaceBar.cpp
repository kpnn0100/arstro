#include "WorkspaceBar.h"
#include <algorithm>
#include <cmath>

using namespace artboard;

namespace arstro
{
namespace interstellar_v1
{
    const char *WorkspaceBar::label(int i)
    {
        switch (i)
        {
            case 0: return "Grade";
            case 1: return "Cut";
            case 2: return "Mix";
            default: return "Deliver";
        }
    }

    WorkspaceBar::WorkspaceBar() { height.set(kHeight); }

    void WorkspaceBar::setWorkspace(interstellar::Workspace w)
    {
        mWorkspace = w;
        mTarget = (int)w;
    }

    Rect WorkspaceBar::segmentRect(int index) const
    {
        const double groupW = mSegW * kCount;
        const double x0 = (width.value() - groupW) * 0.5;
        return Rect{x0 + index * mSegW, (kHeight - 19.5) * 0.5, mSegW, 19.5};
    }

    void WorkspaceBar::layout() {}

    void WorkspaceBar::advance(double nowMs)
    {
        mLastMs = nowMs;
        if (std::fabs((double)mTarget - mHighlight.value()) > 1e-9 && !mHighlight.isAnimating())
            mHighlight.animateTo((double)mTarget, 220.0, Easing::EaseOutCubic, nowMs);
        mHighlight.update(nowMs);
        Segment::advance(nowMs);
    }

    void WorkspaceBar::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = kHeight;
        t.setFill(palette::card());
        t.beginPath();
        t.moveTo(0, 0); t.lineTo(w, 0); t.lineTo(w, h); t.lineTo(0, h);
        t.closePath();
        t.fillPath();
        t.setStroke(palette::border(), 1.0);
        t.beginPath();
        t.moveTo(0, h); t.lineTo(w, h);
        t.closePath();
        t.strokePath();

        // ── the wordmark (R-G-2a): tracked IN, and the dot at the MEASURED end ──
        const double wmSize = 13.0;
        const double track = -0.03 * wmSize;
        t.setFill(palette::foreground());
        t.drawText("interstellar", 9.75, h * 0.5 + wmSize * 0.35, wmSize, font::sansSemiBold(), track);
        const double wmW = t.measureText("interstellar", wmSize, font::sansSemiBold(), track);
        t.setFill(palette::primary());
        t.drawText(".", 9.75 + wmW, h * 0.5 + wmSize * 0.35, wmSize, font::sansSemiBold(), track);

        // ── the switcher. Segments size to their WIDEST label so the group is symmetric. ──
        double widest = 0;
        for (int i = 0; i < kCount; ++i)
            widest = std::max(widest, t.measureText(label(i), 10.0, font::sansMedium(), 0.0));
        mSegW = widest + 19.5;   // 6 rungs of padding

        const Rect group{segmentRect(0).x, segmentRect(0).y, mSegW * kCount, 19.5};
        drawRoundedRect(t, group, radius::control(), Paint::filled(palette::segmentedBg()));
        // The highlight travels: its x comes from the LIVE eased index, not from the target.
        const Rect hl{group.x + mHighlight.value() * mSegW, group.y, mSegW, group.h};
        drawRoundedRect(t, hl, radius::control(), Paint::filled(palette::primaryAlpha(0.22)));

        for (int i = 0; i < kCount; ++i)
        {
            const Rect r = segmentRect(i);
            // The label's emphasis is derived from the DISTANCE to the eased highlight, so the
            // text brightens continuously as the highlight arrives rather than flipping when the
            // click lands (anything derived from an eased value is recomputed from the eased
            // value, every frame).
            const double d = std::min(1.0, std::fabs(mHighlight.value() - i));
            const Color c{palette::foreground().r * (1 - d) + palette::mutedForeground().r * d,
                          palette::foreground().g * (1 - d) + palette::mutedForeground().g * d,
                          palette::foreground().b * (1 - d) + palette::mutedForeground().b * d,
                          1.0};
            const double tw = t.measureText(label(i), 10.0, font::sansMedium(), 0.0);
            t.setFill(c);
            t.drawText(label(i), r.x + (r.w - tw) * 0.5, r.y + r.h * 0.5 + 3.5, 10.0,
                       font::sansMedium(), 0.0);
        }

        // ── the project name, right-aligned, measured and ellipsized ──
        std::string name = mProject.empty() ? "untitled" : mProject;
        if (mDirty) name += " •";
        const double avail = w - (group.x + group.w) - 19.5;
        while (!name.empty() && t.measureText(name, 12.0, font::sansMedium(), 0.0) > avail)
            name.pop_back();
        const double nw = t.measureText(name, 12.0, font::sansMedium(), 0.0);
        t.setFill(palette::secondaryForeground());
        t.drawText(name, w - nw - 9.75, h * 0.5 + 4.0, 12.0, font::sansMedium(), 0.0);
    }

    bool WorkspaceBar::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type == Gesture::Type::Click || g.type == Gesture::Type::Down)
            for (int i = 0; i < kCount; ++i)
                if (segmentRect(i).contains(local))
                {
                    if (g.type == Gesture::Type::Click && onWorkspace)
                        onWorkspace((interstellar::Workspace)i);
                    return true;
                }
        return Segment::handleGesture(g, local);
    }
}
}
