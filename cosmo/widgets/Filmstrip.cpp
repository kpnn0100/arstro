#include "Filmstrip.h"
#include "../Theme.h"
#include "Icons.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace { double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); } }

    Filmstrip::Filmstrip() { clipToBounds = true; height.set(kHeight); }

    void Filmstrip::addThumb(const uint8_t *rgba, int w, int h)
    {
        std::shared_ptr<ImageView> iv;
        if (mThumbCount < (int)mThumbs.size())  // reuse a pooled ImageView (Segment has no removeChild)
        {
            iv = mThumbs[mThumbCount];
        }
        else
        {
            iv = std::make_shared<ImageView>();
            iv->setFit(ImageView::Fit::Cover);
            addChild(iv);
            mThumbs.push_back(iv);
        }
        iv->setImage(rgba, w, h);
        iv->visible = false;  // setCells decides visibility/position
        ++mThumbCount;
    }

    void Filmstrip::clearThumbs()
    {
        mThumbCount = 0;
        for (auto &iv : mThumbs) iv->visible = false;  // hide the pool; setCells re-shows the active ones
        mCells.clear();
        mSel.clear();
        mPrimary = -1;
        mRingInit = false;
        mHover.clear();
        mScrollX.set(0.0); mScrollTarget = 0.0; mScrollLastTarget = 0.0;
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
        mHover.clear();
        // Re-seed the bypass fades to the new cells' resting state: a rebuilt strip is
        // a fresh view, so an already-disabled cell reads disabled immediately rather
        // than fading in from nothing (R-BYPASS-5).
        mByAmt.assign(mCells.size(), 0.0);
        for (size_t i = 0; i < mCells.size(); ++i) mByAmt[i] = mCells[i].bypassed ? 1.0 : 0.0;
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

        // Per-cell hover fade for the cell under the pointer.
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        advanceBypassFades(nowMs);  // R-BYPASS-5
        mLastMs = nowMs;
        mSpinMs = nowMs;            // R-LOADUX-2: drives the pending-cell spinner
        mLoadFrac.update(nowMs);
        mLoadFade.update(nowMs);

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

    void Filmstrip::setLoadProgress(int done, int total)
    {
        mLoadDone = done; mLoadTotal = total;
        const bool streaming = total > 0 && done < total;
        mLoadFrac.animateTo(total > 0 ? (double)done / total : 0.0, 200.0, Easing::EaseOutCubic, mLastMs);
        mLoadFade.animateTo(streaming ? 1.0 : 0.0, streaming ? 160.0 : 320.0, Easing::EaseOutCubic, mLastMs);
    }

    void Filmstrip::scrollCellIntoView(int cell)
    {
        if (cell < 0 || cell >= (int)mCells.size()) return;
        // cellX() includes the live scroll, so work in unscrolled content space.
        double left = kPadX;
        for (int i = 0; i < cell; ++i) left += cellW(i) + kGap;
        const double right = left + cellW(cell);
        const double view = width.value();
        double target = mScrollTarget;
        if (left - kPadX < target) target = std::max(0.0, left - kPadX);
        else if (right + kPadX > target + view) target = right + kPadX - view;
        double contentW = kPadX;
        for (int i = 0; i < (int)mCells.size(); ++i) contentW += cellW(i) + kGap;
        mScrollTarget = std::min(std::max(0.0, contentW - view), std::max(0.0, target));
    }

    bool Filmstrip::handleGesture(const Gesture &g, const Point &local)
    {
        const int cell = cellAt(local.x);
        if (g.type == Gesture::Type::Move) { mHover.setHovered(cell); return true; }  // track hovered cell
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
            else if (c.loading)
            {
                // R-LOADUX-2: the photo is known but its pixels have not arrived. A dim
                // plate + a rotating arc reads as "still coming", where an empty cell
                // would read as "missing". (Placeholder for the supplied animation.)
                const Rect r{x, y, cw, kCellH};
                drawRoundedRect(t, r, radius::control(), Paint::filled(palette::folderChipBg()));
                const double cx = x + cw * 0.5, cy = y + kCellH * 0.5;
                const double rad = 9.0;
                t.setStroke(palette::whiteAlpha(0.10), 2.0);
                t.beginPath();
                t.moveTo(cx + rad, cy);
                t.quadTo(cx + rad, cy + rad, cx, cy + rad);
                t.quadTo(cx - rad, cy + rad, cx - rad, cy);
                t.quadTo(cx - rad, cy - rad, cx, cy - rad);
                t.quadTo(cx + rad, cy - rad, cx + rad, cy);
                t.closePath();
                t.strokePath();
                // The travelling head of the arc: one quadrant sweeping once per 900ms.
                const double a0 = std::fmod(mSpinMs, 900.0) / 900.0 * 6.28318530718;
                t.setStroke(palette::primaryAlpha(0.85), 2.0);
                t.beginPath();
                const int kSeg = 8;
                for (int sgi = 0; sgi <= kSeg; ++sgi)
                {
                    const double a = a0 + (double)sgi / kSeg * 1.5707963268;   // a quarter turn
                    const double px = cx + std::cos(a) * rad, py = cy + std::sin(a) * rad;
                    if (sgi == 0) t.moveTo(px, py); else t.lineTo(px, py);
                }
                t.strokePath();
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

        // Per-cell hover wash (each cell cross-fades; the primary has its own ring).
        for (int i = 0; i < (int)mCells.size(); ++i)
        {
            if (i == mPrimary) continue;
            const double hv = mHover.amount(i);
            if (hv <= 0.001) continue;
            const double hx = cellX(i), hw = cellW(i);
            if (hx + hw < 0 || hx > w) continue;
            drawRoundedRect(t, Rect{hx, ry, hw, kCellH}, radius::control(), Paint::filled(palette::hoverWash(hv)));
        }

        // R-BYPASS-5: a cell whose filter is disabled is washed dark and badged, so
        // the state reads from the strip without opening the context menu. Eased in
        // and out by advanceBypassFades() (R-G-1).
        for (int i = 0; i < (int)mCells.size(); ++i)
        {
            const double by = bypassAmount(i);
            if (by <= 0.001) continue;
            const double bx = cellX(i), bw = cellW(i);
            if (bx + bw < 0 || bx > w) continue;
            drawRoundedRect(t, Rect{bx, ry, bw, kCellH}, radius::control(),
                            Paint::filled(Color{0.02, 0.02, 0.02, 0.52 * by}));
            const Rect badge{bx + 4.0, ry + 4.0, 12.0, 12.0};
            drawRoundedRect(t, badge, radius::control(), Paint::filled(Color{0.02, 0.02, 0.02, 0.72 * by}));
            icon::ban(t, Rect{badge.x + 2.0, badge.y + 2.0, badge.w - 4.0, badge.h - 4.0},
                      palette::whiteAlpha(0.72 * by), 1.1);
        }

        for (int i = 0; i < (int)mCells.size(); ++i)
        {
            if (mCells[i].group || i == mPrimary) continue;
            const double x = cellX(i), cw = cellW(i);
            if (x + cw < 0 || x > w) continue;
            const bool selected = std::find(mSel.begin(), mSel.end(), i) != mSel.end();
            const double o = selected ? 2.0 : 1.0;
            // A bypassed cell's ring fades from accent toward neutral grey: the accent
            // means "active", which a disabled filter is not (R-BYPASS-5).
            const Color selRing = lerpColor(Color{pr.r, pr.g, pr.b, 0.65},
                                            palette::whiteAlpha(0.3), bypassAmount(i));
            t.setStroke(selected ? selRing : Color{1, 1, 1, 0.08}, selected ? 2.0 : 1.0);
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
            t.setStroke(lerpColor(pr, palette::whiteAlpha(0.42), bypassAmount(mPrimary)), 2.0);
            t.beginPath();
            t.moveTo(rx - o, ry - o); t.lineTo(rx + rw + o, ry - o);
            t.lineTo(rx + rw + o, ry + kCellH + o); t.lineTo(rx - o, ry + kCellH + o); t.closePath();
            t.strokePath();
            if (!mCells[mPrimary].group)
            {
                const std::string &label = mCells[mPrimary].name;
                drawRoundedRect(t, Rect{rx, ry + kCellH - 12.0, rw, 12.0}, 0.0,
                                Paint::filled(lerpColor(Color{pr.r, pr.g, pr.b, 0.8},
                                                        Color{0.16, 0.16, 0.16, 0.85}, bypassAmount(mPrimary))));
                t.setFill(palette::white());
                t.drawText(label, rx + rw * 0.5 - label.size() * 7.0 * 0.3, ry + kCellH - 3.0, 7.0, font::sansMedium());
            }
        }

        // R-LOADUX-3: while a project is still streaming in, a slim determinate bar along
        // the strip's top edge with the count beside it, so there is always an answer to
        // "how much is left?". Eased in and out; gone once the last photo lands.
        const double lf = mLoadFade.value();
        if (lf > 0.004)
        {
            const Color acc = palette::primary();
            drawRoundedRect(t, Rect{0, 0, w, 2.0}, 0.0, Paint::filled(palette::whiteAlpha(0.07 * lf)));
            drawRoundedRect(t, Rect{0, 0, w * clamp01(mLoadFrac.value()), 2.0}, 0.0,
                            Paint::filled(Color{acc.r, acc.g, acc.b, 0.9 * lf}));
            // Bottom-right: the one band the cells and their rings never reach (cells sit
            // 12..74 of 86, and the primary ring extends to 77).
            const std::string txt = "Loading " + std::to_string(mLoadDone) + " of " + std::to_string(mLoadTotal);
            const double tw = t.measureText(txt, 9.0, font::sansMedium());
            t.setFill(palette::whiteAlpha(0.5 * lf));
            t.drawText(txt, w - kPadX - tw, kHeight - 3.0, 9.0, font::sansMedium());
        }
        t.restore();
    }

    // ── R-BYPASS-5: per-cell "filter disabled" fade ──

    void Filmstrip::advanceBypassFades(double nowMs)
    {
        if (mByAmt.size() != mCells.size()) mByAmt.resize(mCells.size(), 0.0);
        constexpr double kDurMs = 160.0;
        const bool rm = artboard::reducedMotion();
        const double dt = (mByLastMs < 0.0) ? 0.0 : (nowMs - mByLastMs);
        const double step = rm ? 1.0 : std::min(1.0, std::max(0.0, dt / kDurMs));
        mByLastMs = nowMs;
        for (size_t i = 0; i < mByAmt.size(); ++i)
        {
            const double tgt = mCells[i].bypassed ? 1.0 : 0.0;
            if (mByAmt[i] < tgt) mByAmt[i] = std::min(tgt, mByAmt[i] + step);
            else if (mByAmt[i] > tgt) mByAmt[i] = std::max(tgt, mByAmt[i] - step);
        }
    }

    double Filmstrip::bypassAmount(int cell) const
    {
        if (cell < 0 || cell >= (int)mByAmt.size()) return 0.0;
        const double t = mByAmt[cell];
        return t * t * (3.0 - 2.0 * t);  // smoothstep, same ease as HoverFade
    }
}
}
