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
        return x - mScrollX;
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
        mPrimary = primaryCell;
    }

    void Filmstrip::scrollBy(double delta)
    {
        double contentW = kPadX;
        for (int i = 0; i < (int)mCells.size(); ++i) contentW += cellW(i) + kGap;
        const double maxScroll = std::max(0.0, contentW - width.value());
        mScrollX = std::min(maxScroll, std::max(0.0, mScrollX - delta));
        // Re-run setCells' positioning with the new scroll offset.
        for (int i = 0; i < (int)mCells.size(); ++i)
        {
            const Cell &c = mCells[i];
            if (c.group || c.thumbSlot < 0 || c.thumbSlot >= (int)mThumbs.size()) continue;
            mThumbs[c.thumbSlot]->x.set(cellX(i));
        }
    }

    bool Filmstrip::handleGesture(const Gesture &g, const Point &local)
    {
        const int cell = cellAt(local.x);
        if (cell < 0) return Segment::handleGesture(g, local);
        if (g.type == Gesture::Type::Click) { if (onSelect) onSelect(cell, g.shift, g.ctrl); return true; }
        if (g.type == Gesture::Type::DoubleClick) { if (onActivate) onActivate(cell); return true; }
        if (g.type == Gesture::Type::RightClick) { if (onContext) onContext(cell, g.pos.x, g.pos.y); return true; }
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

            const bool selected = std::find(mSel.begin(), mSel.end(), i) != mSel.end();
            const bool primary = (i == mPrimary);

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
            else
            {
                if (primary)
                {
                    // ring-[2px] ring-primary ring-offset-[2px]: stroke 2px outside a
                    // 2px transparent gap from the cell edge.
                    t.setStroke(palette::primary(), 2.0);
                    t.beginPath();
                    const double o = 3.0;  // offset + half stroke width
                    t.moveTo(x - o, y - o); t.lineTo(x + cw + o, y - o);
                    t.lineTo(x + cw + o, y + kCellH + o); t.lineTo(x - o, y + kCellH + o);
                    t.closePath();
                    t.strokePath();

                    const std::string &label = c.name;
                    drawRoundedRect(t, Rect{x, y + kCellH - 12.0, cw, 12.0}, 0.0,
                                    Paint::filled(Color{palette::primary().r, palette::primary().g, palette::primary().b, 0.8}));
                    t.setFill(palette::white());
                    t.drawText(label, x + cw * 0.5 - label.size() * 7.0 * 0.3, y + kCellH - 3.0, 7.0, font::sansMedium());
                }
                else if (selected)
                {
                    t.setStroke(Color{1, 1, 1, 0.2}, 1.0);
                    t.beginPath(); t.moveTo(x, y); t.lineTo(x + cw, y); t.lineTo(x + cw, y + kCellH); t.lineTo(x, y + kCellH); t.closePath();
                    t.strokePath();
                }
                else
                {
                    t.setStroke(Color{1, 1, 1, 0.08}, 1.0);
                    t.beginPath(); t.moveTo(x, y); t.lineTo(x + cw, y); t.lineTo(x + cw, y + kCellH); t.lineTo(x, y + kCellH); t.closePath();
                    t.strokePath();
                }
            }
        }
    }
}
}
