#include "HistoryView.h"
#include "../CosmoTheme.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace
    {
        constexpr double kNodeR = 7.0;
        constexpr double kRowH = 46.0;
        constexpr double kLaneW = 30.0;
        constexpr double kPadX = 24.0, kPadY = 24.0;   // content inset within the tree area
        constexpr double kLabelSpace = 190.0;          // room for the widest label
        constexpr double kHeaderH = 50.0;
        constexpr double kMargin = 14.0;
        constexpr double kBtn = 22.0;                   // close button side
        double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
    }

    HistoryView::HistoryView(const Color &accent) : mAccent(accent) {}

    void HistoryView::show(std::vector<Node> nodes, int current)
    {
        mNodes = std::move(nodes);
        mCurrent = current;
        mPanX = mPanY = 0.0;
        relayout();
        scrollToCurrent();
        mOpen = true;
        raise();
    }

    void HistoryView::setCurrent(int current)
    {
        mCurrent = current;
        scrollToCurrent();
    }

    Rect HistoryView::cardRect() const
    {
        const double w = clampd(width.value() - 100.0, 360.0, 640.0);
        const double h = clampd(height.value() - 140.0, 300.0, 560.0);
        return Rect{(width.value() - w) * 0.5, (height.value() - h) * 0.5, w, h};
    }

    Rect HistoryView::treeRect() const
    {
        const Rect c = cardRect();
        return Rect{c.x + kMargin, c.y + kHeaderH, c.w - 2 * kMargin, c.h - kHeaderH - kMargin};
    }

    Rect HistoryView::closeBtnRect() const
    {
        const Rect c = cardRect();
        return Rect{c.x + c.w - kMargin - kBtn, c.y + (kHeaderH - kBtn) * 0.5 - 6.0, kBtn, kBtn};
    }

    void HistoryView::relayout()
    {
        const int n = (int)mNodes.size();
        mRow.assign(n, 0);
        mLane.assign(n, 0);
        mMaxRow = mMaxLane = 0;
        if (n == 0) return;

        std::vector<std::vector<int>> kids(n);
        std::vector<int> roots;
        for (int i = 0; i < n; ++i)
        {
            const int p = mNodes[i].parent;
            if (p >= 0 && p < n) kids[p].push_back(i);
            else roots.push_back(i);
        }

        // git-log layout: every node gets its OWN row (pre-order visitation counter) so
        // labels never share a line; lanes are only the graph columns. First child stays
        // in the parent's lane; each further child opens a fresh lane.
        int nextRow = 0, nextLane = 0;
        std::function<void(int, int)> dfs = [&](int node, int lane) {
            mLane[node] = lane;
            mRow[node] = nextRow++;
            mMaxLane = std::max(mMaxLane, lane);
            mMaxRow = std::max(mMaxRow, mRow[node]);
            for (size_t i = 0; i < kids[node].size(); ++i)
                dfs(kids[node][i], i == 0 ? lane : ++nextLane);
        };
        for (size_t i = 0; i < roots.size(); ++i)
            dfs(roots[i], i == 0 ? 0 : ++nextLane);
    }

    Point HistoryView::nodeCenter(int i) const
    {
        const Rect tr = treeRect();
        return Point{tr.x + kPadX + mLane[i] * kLaneW - mPanX,
                     tr.y + kPadY + mRow[i] * kRowH - mPanY};
    }

    void HistoryView::clampPan()
    {
        const Rect tr = treeRect();
        const double contentW = kPadX * 2 + mMaxLane * kLaneW + kLabelSpace;
        const double contentH = kPadY * 2 + mMaxRow * kRowH;
        const double maxX = std::max(0.0, contentW - tr.w);
        const double maxY = std::max(0.0, contentH - tr.h);
        mPanX = clampd(mPanX, 0.0, maxX);
        mPanY = clampd(mPanY, 0.0, maxY);
    }

    void HistoryView::scrollToCurrent()
    {
        if (mCurrent < 0 || mCurrent >= (int)mRow.size()) { clampPan(); return; }
        const Rect tr = treeRect();
        mPanY = kPadY + mRow[mCurrent] * kRowH - tr.h * 0.5;     // centre the current row
        mPanX = kPadX + mLane[mCurrent] * kLaneW - tr.w * 0.35;
        clampPan();
    }

    bool HistoryView::handleGesture(const Gesture &g, const Point &local)
    {
        if (!mOpen) return false;
        using T = Gesture::Type;
        if (g.type == T::DragStart || g.type == T::Drag)
        {
            // pan: move content with the cursor
            if (g.type == T::DragStart) mDragLast = g.start;
            mPanX -= (g.pos.x - mDragLast.x);
            mPanY -= (g.pos.y - mDragLast.y);
            mDragLast = g.pos;
            clampPan();
            return true;
        }
        if (g.type != T::Click) return true;  // consume everything else while modal

        const Point p = local;  // root child at origin -> local == world
        if (closeBtnRect().contains(p)) { mOpen = false; return true; }
        if (!cardRect().contains(p)) { mOpen = false; return true; }  // click outside cancels
        if (treeRect().contains(p))
        {
            for (int i = 0; i < (int)mNodes.size(); ++i)
            {
                const Point c = nodeCenter(i);
                if (std::hypot(p.x - c.x, p.y - c.y) <= kNodeR + 5.0)
                {
                    if (onSelect) onSelect(i);
                    return true;
                }
            }
        }
        return true;
    }

    void HistoryView::onOverlay(IRenderTarget &t) const
    {
        if (!mOpen) return;
        drawRoundedRect(t, Rect{0, 0, width.value(), height.value()}, 0.0, Paint::filled(Color{0, 0, 0, 0.45}));

        const Rect c = cardRect();
        drawRoundedRect(t, c, radius::panel(), Paint::filledStroked(palette::panel(), palette::line(), 1.0));

        // header: title + hint + close button
        t.setFill(palette::ink());
        for (double ox : {0.0, 0.4}) t.drawText("HISTORY", c.x + kMargin + ox, c.y + 24.0, 14.0);
        t.setFill(palette::faint());
        t.drawText("click a node to jump  .  Ctrl+Z undo  .  Ctrl+Y redo", c.x + kMargin, c.y + 40.0, 10.0);

        const Rect close = closeBtnRect();
        drawRoundedRect(t, close, radius::control(), Paint::filledStroked(palette::surface(), palette::line(), 1.0));
        t.setStroke(palette::muted(), 1.5);
        t.beginPath();
        t.moveTo(close.x + 6, close.y + 6); t.lineTo(close.x + kBtn - 6, close.y + kBtn - 6);
        t.moveTo(close.x + kBtn - 6, close.y + 6); t.lineTo(close.x + 6, close.y + kBtn - 6);
        t.strokePath();

        // clip the tree area so panned content doesn't spill out of the card
        const Rect tr = treeRect();
        t.save();
        t.clipRect(tr.x, tr.y, tr.w, tr.h);

        // edges first (parent -> child)
        t.setStroke(palette::line(), 1.5);
        for (int i = 0; i < (int)mNodes.size(); ++i)
        {
            const int par = mNodes[i].parent;
            if (par < 0) continue;
            const Point a = nodeCenter(par), b = nodeCenter(i);
            t.beginPath();
            t.moveTo(a.x, a.y);
            if (std::abs(a.x - b.x) < 0.5) t.lineTo(b.x, b.y);   // same lane: straight down
            else { t.lineTo(b.x, a.y + kRowH * 0.5); t.lineTo(b.x, b.y); }  // fork into child's lane, then down it
            t.strokePath();
        }

        // nodes + labels
        for (int i = 0; i < (int)mNodes.size(); ++i)
        {
            const Point cn = nodeCenter(i);
            if (cn.y < tr.y - kRowH || cn.y > tr.y + tr.h + kRowH) continue;  // cheap vertical cull
            const bool cur = (i == mCurrent);
            drawCircle(t, cn.x, cn.y, kNodeR,
                       cur ? Paint::filled(mAccent)
                           : Paint::filledStroked(palette::surface(), palette::line(), 1.5));
            if (cur)  // accent ring around the live state
                drawCircle(t, cn.x, cn.y, kNodeR + 3.0, Paint::stroked(mAccent, 1.5));
            t.setFill(cur ? palette::ink() : palette::muted());
            const double lx = tr.x + kPadX + mMaxLane * kLaneW + 18.0 - mPanX;
            t.drawText(mNodes[i].label, lx, cn.y + 4.0, 12.0);
        }
        t.restore();
    }
}
}
