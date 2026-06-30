#include "Filmstrip.h"
#include "../Chrome.h"
#include "../CosmoTheme.h"
#include <algorithm>

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace
    {
        constexpr double kPad = 8.0;
        constexpr double kCellW = 96.0;
        constexpr double kCellH = 64.0;
        constexpr double kGap = 8.0;
        constexpr double kInset = 3.0;
        double cellX(int i) { return kPad + i * (kCellW + kGap); }
    }

    Filmstrip::Filmstrip(const Color &accent) : mAccent(accent) { height.set(kCellH + 2 * kPad); }

    void Filmstrip::addThumb(const uint8_t *rgba, int w, int h)
    {
        auto v = std::make_shared<ImageView>();
        v->setImage(rgba, w, h);
        v->width.set(kCellW - 2 * kInset);
        v->height.set(kCellH - 2 * kInset);
        v->visible = false;  // shown only when its slot appears as a cell in the current group
        mThumbs.push_back(v);
        addChild(v);
    }

    void Filmstrip::setCells(std::vector<Cell> cells)
    {
        mCells = std::move(cells);
        placeThumbs();
    }

    void Filmstrip::placeThumbs()
    {
        for (auto &th : mThumbs) th->visible = false;
        for (int i = 0; i < (int)mCells.size(); ++i)
        {
            const Cell &c = mCells[i];
            if (!c.group && c.thumbSlot >= 0 && c.thumbSlot < (int)mThumbs.size())
            {
                auto &th = mThumbs[c.thumbSlot];
                th->visible = true;
                th->x.set(cellX(i) + kInset);
                th->y.set(kPad + kInset);
            }
        }
    }

    void Filmstrip::setSelection(const std::vector<int> &sel, int primary) { mSel = sel; mPrimary = primary; }

    void Filmstrip::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, radius::panel(), Paint::filledStroked(palette::panel(), palette::line(), 1.0));
        for (int i = 0; i < (int)mCells.size(); ++i)
        {
            const Cell &c = mCells[i];
            const Rect cell{cellX(i), kPad, kCellW, kCellH};
            drawRoundedRect(t, cell, radius::control(), Paint::filled(palette::bg()));
            if (c.group)
            {
                // folder chip: a tabbed rectangle + name + member count
                const double fx = cell.x + 14, fy = cell.y + 16, fw = kCellW - 28, fh = 26;
                drawRoundedRect(t, Rect{fx, fy - 6, fw * 0.5, 6}, 2.0, Paint::filled(palette::surface()));
                drawRoundedRect(t, Rect{fx, fy, fw, fh}, radius::control(), Paint::filled(palette::surface()));
                t.setFill(palette::ink());
                t.drawText(c.name, cell.x + 10, cell.y + kCellH - 16, 10.0);
                t.setFill(palette::faint());
                t.drawText("(" + std::to_string(c.count) + ")", cell.x + 10, cell.y + kCellH - 4, 9.0);
            }
            const bool sel = std::find(mSel.begin(), mSel.end(), i) != mSel.end();
            if (i == mPrimary)
                drawRoundedRect(t, cell, radius::control(), Paint::stroked(mAccent, 2.0));
            else if (sel)
                drawRoundedRect(t, cell, radius::control(), Paint::stroked(Color{mAccent.r, mAccent.g, mAccent.b, 0.55f}, 1.5));
        }
    }

    int Filmstrip::cellAt(const Point &local) const
    {
        if (local.y < kPad || local.y > kPad + kCellH) return -1;
        for (int i = 0; i < (int)mCells.size(); ++i)
            if (local.x >= cellX(i) && local.x <= cellX(i) + kCellW) return i;
        return -1;
    }

    bool Filmstrip::handleGesture(const Gesture &g, const Point &localPoint)
    {
        const int i = cellAt(localPoint);
        if (i < 0) return Segment::handleGesture(g, localPoint);
        if (g.type == Gesture::Type::DoubleClick) { if (onActivate) onActivate(i); return true; }
        if (g.type == Gesture::Type::RightClick) { if (onContext) onContext(i, g.pos.x, g.pos.y); return true; }
        if (g.type == Gesture::Type::Click) { if (onSelect) onSelect(i, g.shift, g.ctrl); return true; }
        return Segment::handleGesture(g, localPoint);
    }
}
}
