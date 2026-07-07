#include "Filmstrip.h"
#include "../Theme.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    Filmstrip::Filmstrip() { clipToBounds = true; height.set(kHeight); }

    void Filmstrip::addThumb(const uint8_t *rgba, int w, int h)
    {
        auto iv = std::make_shared<ImageView>();
        iv->setFit(ImageView::Fit::Cover);
        iv->setImage(rgba, w, h);
        iv->visible = false;
        addChild(iv);
        mThumbs.push_back(iv);
    }

    double Filmstrip::cellX(int i) const
    {
        double x = kPadX;
        for (int k = 0; k < i; ++k) x += cellW(k) + kGap;
        return x - mScrollX.value();
    }

    void Filmstrip::positionThumbs()
    {
        for (int i = 0; i < (int)mCells.size(); ++i)
        {
            const Cell &c = mCells[i];
            if (c.group || c.thumbSlot < 0 || c.thumbSlot >= (int)mThumbs.size()) continue;
            mThumbs[c.thumbSlot]->x.set(cellX(i));
        }
    }

    int Filmstrip::cellAt(double localX) const
    {
        for (int i = 0; i < (int)mCells.size(); ++i)
        {
            const double x = cellX(i);
            if (localX >= x && localX <= x + cellW(i)) return i;
        }
        return -1;
    }

    void Filmstrip::setCells(std::vector<Cell> cells)
    {
        mCells = std::move(cells);
        // New content (e.g. drilled into a group) starts at scroll 0 — a fresh view,
        // not an animated move of the same list.
        mScrollX.set(0.0); mScrollTarget = 0.0; mScrollLastTarget = 0.0;
        mHoverCell = -1;
        for (auto &iv : mThumbs) iv->visible = false;
        for (int i = 0; i < (int)mCells.size(); ++i)
        {
            const Cell &c = mCells[i];
            if (c.group || c.thumbSlot < 0 || c.thumbSlot >= (int)mThumbs.size()) continue;
            auto &iv = mThumbs[c.thumbSlot];
            iv->visible = true;
            iv->x.set(cellX(i));
            iv->y.set((kHeight - kCellH) * 0.5);
            iv->width.set(kPhotoW);
            iv->height.set(kCellH);
        }
    }

    void Filmstrip::setSelection(std::vector<int> selCells, int primaryCell)
    {
        mSel = std::move(selCells);
        mPrimary = primaryCell;  // advance() slides the ring to it
    }

    void Filmstrip::advance(double nowMs)
    {
        if (mPrimary >= 0)
        {
            if (!mRingInit) { mRingPos.set(mPrimary); mRingInit = true; mRingTarget = mPrimary; }
            else if (mPrimary != mRingTarget)
            {
                mRingPos.animateTo(mPrimary, 200.0, Easing::EaseOutCubic, nowMs);
                mRingTarget = mPrimary;
            }
            mRingPos.update(nowMs);
        }
        // Ease the horizontal scroll toward its target (set by scrollBy) and keep the
        // thumbnails positioned at the animated offset every frame (R-G-1).
        if (mScrollTarget != mScrollLastTarget)
        {
            mScrollX.animateTo(mScrollTarget, 180.0, Easing::EaseOutCubic, nowMs);
            mScrollLastTarget = mScrollTarget;
        }
        mScrollX.update(nowMs);
        positionThumbs();

        // Hover fade for the cell under the pointer.
        if (!isHovered()) mHoverCell = -1;
        const bool hov = mHoverCell >= 0;
        if (hov != mHoverPrev) { mHoverPrev = hov; mHoverAmt.animateTo(hov ? 1.0 : 0.0, interaction::kHoverMs, Easing::EaseOutCubic, nowMs); }
        mHoverAmt.update(nowMs);

        Segment::advance(nowMs);
    }

    void Filmstrip::scrollBy(double delta)
    {
        double contentW = kPadX;
        for (int i = 0; i < (int)mCells.size(); ++i) contentW += cellW(i) + kGap;
        const double maxScroll = std::max(0.0, contentW - width.value());
        // Just move the TARGET; advance() eases mScrollX toward it and repositions thumbs.
        mScrollTarget = std::min(maxScroll, std::max(0.0, mScrollTarget - delta));
    }

    bool Filmstrip::handleGesture(const Gesture &g, const Point &local)
    {
        const int cell = cellAt(local.x);
        if (g.type == Gesture::Type::Move) { mHoverCell = cell; return true; }  // track hovered cell
        // Right-click ALWAYS opens the context menu -- even on empty strip space
        // (cell == -1), so "add photo to this group" is reachable anywhere in the
        // browse section, not only on a thumbnail.
        if (g.type == Gesture::Type::RightClick) { if (onContext) onContext(cell, g.pos.x, g.pos.y); return true; }
        if (cell < 0) return Segment::handleGesture(g, local);
        if (g.type == Gesture::Type::Click) { if (onSelect) onSelect(cell, g.shift, g.ctrl); return true; }
        if (g.type == Gesture::Type::DoubleClick) { if (onActivate) onActivate(cell); return true; }
        return Segment::handleGesture(g, local);
    }

    void Filmstrip::onPaint(IRenderTarget &t) const
    {
        const double w = width.value();
        drawRoundedRect(t, Rect{0, 0, w, kHeight}, 0.0, Paint::filled(palette::filmstripBg()));
        t.beginPath(); t.moveTo(0, 0); t.lineTo(w, 0); t.setStroke(palette::border(), 1.0); t.strokePath();

        for (int i = 0; i < (int)mCells.size(); ++i)
        {
            const Cell &c = mCells[i];
            const double x = cellX(i), cw = cellW(i);
            const double y = (kHeight - kCellH) * 0.5;
            if (x + cw < 0 || x > w) continue;  // offscreen

            if (c.group)
            {
                // Dashed border chip (approximated with short dash segments -- the
                // HAL's strokePath has no native dash pattern).
                const Rect r{x, y, cw, kCellH};
                drawRoundedRect(t, r, 2.0, Paint::filled(palette::folderChipBg()));
                t.setStroke(Color{palette::border().r, palette::border().g, palette::border().b, 0.6}, 1.0);
                const double perim[4][4] = {{r.x, r.y, r.x + r.w, r.y}, {r.x + r.w, r.y, r.x + r.w, r.y + r.h},
                                            {r.x + r.w, r.y + r.h, r.x, r.y + r.h}, {r.x, r.y + r.h, r.x, r.y}};
                for (auto &seg : perim)
                {
                    const double dx = seg[2] - seg[0], dy = seg[3] - seg[1];
                    const double len = std::sqrt(dx * dx + dy * dy);
                    const int dashes = std::max(1, (int)(len / 6.0));
                    for (int d = 0; d < dashes; d += 2)
                    {
                        const double t0 = (double)d / dashes, t1 = std::min(1.0, (double)(d + 1) / dashes);
                        t.beginPath();
                        t.moveTo(seg[0] + dx * t0, seg[1] + dy * t0);
                        t.lineTo(seg[0] + dx * t1, seg[1] + dy * t1);
                        t.strokePath();
                    }
                }
                t.setFill(palette::mutedForeground());
                t.drawText(c.name, x + cw * 0.5 - c.name.size() * 9.0 * 0.3, y + kCellH * 0.5 - 1.0, 9.0, font::sans());
                const std::string countStr = std::to_string(c.count) + " items";
                t.setFill(Color{palette::mutedForeground().r, palette::mutedForeground().g, palette::mutedForeground().b, 0.4});
                t.drawText(countStr, x + cw * 0.5 - countStr.size() * 8.0 * 0.3, y + kCellH * 0.5 + 11.0, 8.0, font::sans());
            }
            // Photo cells: the thumbnail is a child ImageView; its selection outline,
            // sliding primary ring and name bar are drawn in onOverlay (below) so
            // they sit ABOVE the thumbnail instead of behind it.
        }
    }

    void Filmstrip::onOverlay(IRenderTarget &t) const
    {
        // Drawn in the overlay pass -> on top of the thumbnail ImageViews (children
        // render after onPaint). Clipped to the strip so a scrolled-off cell's ring
        // doesn't spill over the photo/breadcrumb above.
        const double w = width.value();
        if (mCells.empty()) return;
        t.save();
        t.clipRect(0, 0, w, kHeight);
        const Color pr = palette::primary();
        const double ry = (kHeight - kCellH) * 0.5;

        // Hover wash on the cell under the pointer (not the primary — it has its ring).
        const double hv = mHoverAmt.value();
        if (mHoverCell >= 0 && mHoverCell < (int)mCells.size() && mHoverCell != mPrimary && hv > 0.001)
        {
            const double hx = cellX(mHoverCell), hw = cellW(mHoverCell);
            drawRoundedRect(t, Rect{hx, ry, hw, kCellH}, radius::control(), Paint::filled(palette::hoverWash(hv)));
        }

        for (int i = 0; i < (int)mCells.size(); ++i)
        {
            if (mCells[i].group || i == mPrimary) continue;
            const double x = cellX(i), cw = cellW(i);
            if (x + cw < 0 || x > w) continue;
            const bool selected = std::find(mSel.begin(), mSel.end(), i) != mSel.end();
            const double o = selected ? 2.0 : 1.0;
            t.setStroke(selected ? Color{pr.r, pr.g, pr.b, 0.65} : Color{1, 1, 1, 0.08}, selected ? 2.0 : 1.0);
            t.beginPath();
            t.moveTo(x - o, ry - o); t.lineTo(x + cw + o, ry - o);
            t.lineTo(x + cw + o, ry + kCellH + o); t.lineTo(x - o, ry + kCellH + o); t.closePath();
            t.strokePath();
        }

        // Primary ring at its animated position: slides to the newly-selected photo
        // and follows the cell during scroll (cellX() includes the scroll offset).
        if (mPrimary >= 0 && mPrimary < (int)mCells.size())
        {
            const double p = std::clamp(mRingPos.value(), 0.0, (double)(mCells.size() - 1));
            const int a = (int)std::floor(p), b = std::min((int)mCells.size() - 1, a + 1);
            const double f = p - a;
            const double rx = cellX(a) + (cellX(b) - cellX(a)) * f;
            const double rw = cellW(a) + (cellW(b) - cellW(a)) * f;
            const double o = 3.0;
            t.setStroke(pr, 2.0);
            t.beginPath();
            t.moveTo(rx - o, ry - o); t.lineTo(rx + rw + o, ry - o);
            t.lineTo(rx + rw + o, ry + kCellH + o); t.lineTo(rx - o, ry + kCellH + o); t.closePath();
            t.strokePath();
            if (!mCells[mPrimary].group)
            {
                const std::string &label = mCells[mPrimary].name;
                drawRoundedRect(t, Rect{rx, ry + kCellH - 12.0, rw, 12.0}, 0.0,
                                Paint::filled(Color{pr.r, pr.g, pr.b, 0.8}));
                t.setFill(palette::white());
                t.drawText(label, rx + rw * 0.5 - label.size() * 7.0 * 0.3, ry + kCellH - 3.0, 7.0, font::sansMedium());
            }
        }
        t.restore();
    }
}
}
