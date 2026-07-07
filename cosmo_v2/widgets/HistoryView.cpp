#include "HistoryView.h"
#include "../Theme.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        constexpr double kNodeR = 7.0;
        constexpr double kRowH = 46.0;
        constexpr double kLaneW = 30.0;
        constexpr double kPadX = 24.0, kPadY = 24.0;
        constexpr double kLabelSpace = 190.0;
        constexpr double kHeaderH = 50.0;
        constexpr double kMargin = 14.0;
        constexpr double kBtn = 22.0;
        constexpr double kCardRadius = 6.0;
        // Pan glide durations (R-G-1): drag follows the cursor tightly, wheel/jump glide.
        constexpr double kDragGlideMs = 70.0;
        constexpr double kScrollGlideMs = 170.0;
        constexpr double kJumpGlideMs = 260.0;
        double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
    }

    void HistoryView::show(std::vector<Node> nodes, int current)
    {
        mNodes = std::move(nodes);
        mCurrent = current;
        mPanX = mPanY = 0.0;
        relayout();
        scrollToCurrent();               // sets the pan TARGET on the current node
        // Open already centred on the current node — the mAppear fade covers the intro,
        // so the pan itself doesn't visibly jump; later jumps/scrolls glide instead.
        mPanXAnim.set(mPanX); mPanYAnim.set(mPanY);
        mPanIssuedX = mPanX; mPanIssuedY = mPanY;
        mOpen = true;
        raise();
    }

    void HistoryView::setCurrent(int current)
    {
        mCurrent = current;
        mPanDurMs = kJumpGlideMs;  // glide the tree to re-centre on the new current node
        scrollToCurrent();
    }

    void HistoryView::scrollBy(double wheelDelta)
    {
        mPanDurMs = kScrollGlideMs;
        mPanY -= wheelDelta * kRowH;
        clampPan();
    }

    double HistoryView::appearRise() const { return (1.0 - mAppear.value()) * 12.0; }  // rises into place
    double HistoryView::panX() const { return mPanXAnim.value(); }                     // eased/drawn pan
    double HistoryView::panY() const { return mPanYAnim.value(); }

    Rect HistoryView::cardRect() const
    {
        const double w = clampd(width.value() - 100.0, 360.0, 640.0);
        const double h = clampd(height.value() - 140.0, 300.0, 560.0);
        // +appearRise() keeps hit-testing aligned with the drawn card during the fade.
        return Rect{(width.value() - w) * 0.5, (height.value() - h) * 0.5 + appearRise(), w, h};
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

        // git-log layout: every node gets its own row (pre-order counter) so labels
        // never share a line; first child keeps the parent's lane, each further child
        // opens a fresh lane.
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
        return Point{tr.x + kPadX + mLane[i] * kLaneW - panX(),
                     tr.y + kPadY + mRow[i] * kRowH - panY()};
    }

    int HistoryView::nodeAt(const Point &local) const
    {
        if (!treeRect().contains(local)) return -1;
        for (int i = 0; i < (int)mNodes.size(); ++i)
        {
            const Point c = nodeCenter(i);
            if (std::hypot(local.x - c.x, local.y - c.y) <= kNodeR + 5.0) return i;
        }
        return -1;
    }

    void HistoryView::clampPan()
    {
        const Rect tr = treeRect();
        const double contentW = kPadX * 2 + mMaxLane * kLaneW + kLabelSpace;
        const double contentH = kPadY * 2 + mMaxRow * kRowH;
        mPanX = clampd(mPanX, 0.0, std::max(0.0, contentW - tr.w));
        mPanY = clampd(mPanY, 0.0, std::max(0.0, contentH - tr.h));
    }

    void HistoryView::scrollToCurrent()
    {
        if (mCurrent < 0 || mCurrent >= (int)mRow.size()) { clampPan(); return; }
        const Rect tr = treeRect();
        mPanY = kPadY + mRow[mCurrent] * kRowH - tr.h * 0.5;
        mPanX = kPadX + mLane[mCurrent] * kLaneW - tr.w * 0.35;
        clampPan();
    }

    void HistoryView::advance(double nowMs)
    {
        Segment::advance(nowMs);

        if (!isHovered()) { mHoverNode = -1; mCloseHover = false; }  // pointer left the modal

        if (mOpen != mWasOpen)  // ease the card in on open, out on close (like ContextMenu)
        {
            mWasOpen = mOpen;
            mAppear.animateTo(mOpen ? 1.0 : 0.0, mOpen ? 150.0 : 110.0, Easing::EaseOutCubic, nowMs);
        }
        const bool nodeHov = mOpen && mHoverNode >= 0;  // hovered-node wash fade
        if (nodeHov != mNodeHoverPrev)
        {
            mNodeHoverPrev = nodeHov;
            mHoverAmt.animateTo(nodeHov ? 1.0 : 0.0, interaction::kHoverMs, Easing::EaseOutCubic, nowMs);
        }
        const bool closeHov = mOpen && mCloseHover;  // close-X lift fade
        if (closeHov != mCloseHoverPrev)
        {
            mCloseHoverPrev = closeHov;
            mCloseAmt.animateTo(closeHov ? 1.0 : 0.0, interaction::kHoverMs, Easing::EaseOutCubic, nowMs);
        }

        // Glide the pan toward its target (drag/wheel/jump each set mPanX/mPanY + mPanDurMs).
        if (mPanX != mPanIssuedX) { mPanXAnim.animateTo(mPanX, mPanDurMs, Easing::EaseOutCubic, nowMs); mPanIssuedX = mPanX; }
        if (mPanY != mPanIssuedY) { mPanYAnim.animateTo(mPanY, mPanDurMs, Easing::EaseOutCubic, nowMs); mPanIssuedY = mPanY; }

        mAppear.update(nowMs);
        mHoverAmt.update(nowMs);
        mCloseAmt.update(nowMs);
        mPanXAnim.update(nowMs);
        mPanYAnim.update(nowMs);
    }

    bool HistoryView::handleGesture(const Gesture &g, const Point &local)
    {
        if (!mOpen) return false;
        using T = Gesture::Type;
        if (g.type == T::Move)  // track the hovered node / close button (fades in advance)
        {
            mCloseHover = closeBtnRect().contains(local);
            mHoverNode = mCloseHover ? -1 : nodeAt(local);
            return true;
        }
        if (g.type == T::DragStart || g.type == T::Drag)
        {
            if (g.type == T::DragStart) mDragLast = g.start;
            mPanDurMs = kDragGlideMs;  // tight follow while the pointer drags
            mPanX -= (g.pos.x - mDragLast.x);
            mPanY -= (g.pos.y - mDragLast.y);
            mDragLast = g.pos;
            clampPan();
            return true;
        }
        if (g.type != T::Click) return true;  // consume everything else while modal

        const Point p = local;
        if (closeBtnRect().contains(p)) { mOpen = false; return true; }
        if (!cardRect().contains(p)) { mOpen = false; return true; }  // click outside cancels
        const int i = nodeAt(p);
        if (i >= 0) { if (onSelect) onSelect(i); }
        return true;
    }

    void HistoryView::onOverlay(IRenderTarget &t) const
    {
        const double appear = mAppear.value();
        if (!mOpen && appear <= 0.001) return;  // fully closed
        auto fa = [&](Color c) { c.a *= appear; return c; };  // fade every drawn colour by mAppear (R-G-1)

        drawRoundedRect(t, Rect{0, 0, width.value(), height.value()}, 0.0, Paint::filled(Color{0, 0, 0, 0.5 * appear}));

        const Rect c = cardRect();  // already carries the appear y-rise
        drawRoundedRect(t, c, kCardRadius, Paint::filledStroked(fa(palette::popover()), fa(palette::border()), 1.0));

        // header: title + hint + close button
        t.setFill(fa(palette::foreground()));
        t.drawText("HISTORY", c.x + kMargin, c.y + 24.0, 12.0, font::sansSemiBold(), 0.12 * 12.0);
        t.setFill(fa(palette::mutedForeground()));
        t.drawText("click a node to jump  .  Ctrl+Z undo  .  Ctrl+Y redo", c.x + kMargin, c.y + 40.0, 10.0, font::sans());

        // close X: brightens + lifts on hover (mCloseAmt), eased so it never pops (R-G-1)
        const double ch = mCloseAmt.value();
        Rect close = closeBtnRect();
        close.y -= 1.5 * ch;  // subtle hover lift
        const Color closeFill = brighten(palette::secondary(), 0.18 * ch);
        const Color closeBorder = lerpColor(palette::border(), palette::primary(), 0.6 * ch);
        drawRoundedRect(t, close, radius::control(), Paint::filledStroked(fa(closeFill), fa(closeBorder), 1.0));
        t.setStroke(fa(lerpColor(palette::mutedForeground(), palette::foreground(), ch)), 1.5);
        t.beginPath();
        t.moveTo(close.x + 6, close.y + 6); t.lineTo(close.x + kBtn - 6, close.y + kBtn - 6);
        t.moveTo(close.x + kBtn - 6, close.y + 6); t.lineTo(close.x + 6, close.y + kBtn - 6);
        t.strokePath();

        const Rect tr = treeRect();
        t.save();
        t.clipRect(tr.x, tr.y, tr.w, tr.h);

        // hovered-node wash (behind the row), faded by mHoverAmt
        if (mHoverNode >= 0 && mHoverNode < (int)mNodes.size())
        {
            const double hv = mHoverAmt.value() * appear;
            if (hv > 0.001)
            {
                const Point hc = nodeCenter(mHoverNode);
                if (hc.y >= tr.y - kRowH && hc.y <= tr.y + tr.h + kRowH)
                    drawRoundedRect(t, Rect{tr.x + 2.0, hc.y - kRowH * 0.5 + 3.0, tr.w - 4.0, kRowH - 6.0},
                                    radius::control(), Paint::filled(palette::hoverWash(hv)));
            }
        }

        // edges first (parent -> child)
        t.setStroke(fa(palette::border()), 1.5);
        for (int i = 0; i < (int)mNodes.size(); ++i)
        {
            const int par = mNodes[i].parent;
            if (par < 0) continue;
            const Point a = nodeCenter(par), b = nodeCenter(i);
            t.beginPath();
            t.moveTo(a.x, a.y);
            if (std::abs(a.x - b.x) < 0.5) t.lineTo(b.x, b.y);   // same lane: straight down
            else { t.lineTo(b.x, a.y + kRowH * 0.5); t.lineTo(b.x, b.y); }
            t.strokePath();
        }

        // nodes + labels
        const Color accent = palette::primary();
        for (int i = 0; i < (int)mNodes.size(); ++i)
        {
            const Point cn = nodeCenter(i);
            if (cn.y < tr.y - kRowH || cn.y > tr.y + tr.h + kRowH) continue;
            const bool cur = (i == mCurrent);
            drawCircle(t, cn.x, cn.y, kNodeR,
                       cur ? Paint::filled(fa(accent))
                           : Paint::filledStroked(fa(palette::secondary()), fa(palette::border()), 1.5));
            if (cur) drawCircle(t, cn.x, cn.y, kNodeR + 3.0, Paint::stroked(fa(accent), 1.5));
            t.setFill(fa(cur ? palette::foreground() : palette::mutedForeground()));
            const double lx = tr.x + kPadX + mMaxLane * kLaneW + 18.0 - panX();
            t.drawText(mNodes[i].label, lx, cn.y + 4.0, 12.0, font::sans());
        }
        t.restore();

        // scrollbars (outside the clip) — track the eased pan
        const double contentH = kPadY * 2 + mMaxRow * kRowH;
        const double contentW = kPadX * 2 + mMaxLane * kLaneW + kLabelSpace;
        const double maxY = std::max(0.0, contentH - tr.h), maxX = std::max(0.0, contentW - tr.w);
        if (maxY > 0.5)
        {
            const double thumbH = std::max(28.0, tr.h * tr.h / contentH);
            const double ty = tr.y + (panY() / maxY) * (tr.h - thumbH);
            drawRoundedRect(t, Rect{tr.x + tr.w - 5.0, tr.y, 4.0, tr.h}, 2.0, Paint::filled(fa(palette::whiteAlpha(0.06))));
            drawRoundedRect(t, Rect{tr.x + tr.w - 5.0, ty, 4.0, thumbH}, 2.0, Paint::filled(fa(palette::whiteAlpha(0.22))));
        }
        if (maxX > 0.5)
        {
            const double thumbW = std::max(28.0, tr.w * tr.w / contentW);
            const double tx = tr.x + (panX() / maxX) * (tr.w - thumbW);
            drawRoundedRect(t, Rect{tr.x, tr.y + tr.h - 5.0, tr.w, 4.0}, 2.0, Paint::filled(fa(palette::whiteAlpha(0.06))));
            drawRoundedRect(t, Rect{tx, tr.y + tr.h - 5.0, thumbW, 4.0}, 2.0, Paint::filled(fa(palette::whiteAlpha(0.22))));
        }
    }
}
}
