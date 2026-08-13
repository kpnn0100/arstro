#include "MaskOverlay.h"
#include "../Theme.h"
#include <cmath>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;
    using MP = arstro::MaskParams;

    namespace { constexpr double kHandle = 6.0; constexpr double kPick = 12.0; }

    MaskOverlay::MaskOverlay(const Color &accent) : mAccent(accent) {}

    Point MaskOverlay::normToLocal(float nx, float ny) const
    {
        return Point{mFitted.x + nx * mFitted.w, mFitted.y + ny * mFitted.h};
    }
    void MaskOverlay::localToNorm(const Point &p, float &nx, float &ny) const
    {
        nx = mFitted.w > 0 ? (float)((p.x - mFitted.x) / mFitted.w) : 0.f;
        ny = mFitted.h > 0 ? (float)((p.y - mFitted.y) / mFitted.h) : 0.f;
        if (nx < 0) nx = 0; else if (nx > 1) nx = 1;
        if (ny < 0) ny = 0; else if (ny > 1) ny = 1;
    }

    bool MaskOverlay::hitTestSelf(const Point &p) const
    {
        if (!mActive) return false;   // click-through when no mask is being edited
        return p.x >= 0 && p.x <= width.value() && p.y >= 0 && p.y <= height.value();
    }

    int MaskOverlay::pickHandle(const Point &local) const
    {
        auto near = [&](Point h) { return std::hypot(local.x - h.x, local.y - h.y) <= kPick; };
        if (mMask.type == MP::Radial)
        {
            if (near(normToLocal(mMask.cx + mMask.rx, mMask.cy))) return 1;  // edge X
            if (near(normToLocal(mMask.cx, mMask.cy + mMask.ry))) return 2;  // edge Y
            // inside the ellipse -> move centre
            float nx, ny; localToNorm(local, nx, ny);
            float dx = (nx - mMask.cx) / (mMask.rx > 1e-4f ? mMask.rx : 1e-4f);
            float dy = (ny - mMask.cy) / (mMask.ry > 1e-4f ? mMask.ry : 1e-4f);
            if (dx * dx + dy * dy <= 1.f) return 0;
            return -1;
        }
        if (mMask.type == MP::Linear)
        {
            if (near(normToLocal(mMask.x0, mMask.y0))) return 0;
            if (near(normToLocal(mMask.x1, mMask.y1))) return 1;
            return -1;
        }
        return 99;  // brush: any press paints
    }

    void MaskOverlay::applyDrag(int handle, const Point &local)
    {
        float nx, ny; localToNorm(local, nx, ny);
        if (mMask.type == MP::Radial)
        {
            if (handle == 0) { mMask.cx = nx; mMask.cy = ny; }
            else if (handle == 1) { mMask.rx = std::fabs(nx - mMask.cx); }
            else if (handle == 2) { mMask.ry = std::fabs(ny - mMask.cy); }
        }
        else if (mMask.type == MP::Linear)
        {
            if (handle == 0) { mMask.x0 = nx; mMask.y0 = ny; }
            else { mMask.x1 = nx; mMask.y1 = ny; }
        }
        else  // brush
        {
            arstro::BrushDab d; d.x = nx; d.y = ny; d.radius = (float)mBrushRadius; d.flow = 1.f;
            mMask.dabs.push_back(d);
        }
        if (onChange) onChange(mMask);
    }

    bool MaskOverlay::handleGesture(const Gesture &g, const Point &local)
    {
        if (!mActive) return false;
        switch (g.type)
        {
        case Gesture::Type::Down:
            mDrag = pickHandle(local);
            if (mDrag == 99) applyDrag(mDrag, local);  // brush: paint on press
            return mDrag >= 0;
        case Gesture::Type::DragStart:
        case Gesture::Type::Drag:
            if (mDrag >= 0) { applyDrag(mDrag, local); return true; }
            return false;
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
            mDrag = -1;
            return true;
        default:
            return false;
        }
    }

    static void strokeEllipse(IRenderTarget &t, double cx, double cy, double rx, double ry)
    {
        const double k = 0.5522847498;
        t.beginPath();
        t.moveTo(cx + rx, cy);
        t.cubicTo(cx + rx, cy + ry * k, cx + rx * k, cy + ry, cx, cy + ry);
        t.cubicTo(cx - rx * k, cy + ry, cx - rx, cy + ry * k, cx - rx, cy);
        t.cubicTo(cx - rx, cy - ry * k, cx - rx * k, cy - ry, cx, cy - ry);
        t.cubicTo(cx + rx * k, cy - ry, cx + rx, cy - ry * k, cx + rx, cy);
        t.closePath();
        t.strokePath();
    }

    void MaskOverlay::onPaint(IRenderTarget &t) const
    {
        if (!mActive) return;
        const Color line = mAccent;
        // A coloured dot ringed by the dark photo-stage colour so handles read on any photo.
        const Paint dot = Paint::filledStroked(mAccent, palette::canvasBg(), 1.5);

        if (mMask.type == MP::Radial)
        {
            Point c = normToLocal(mMask.cx, mMask.cy);
            double rx = mMask.rx * mFitted.w, ry = mMask.ry * mFitted.h;
            t.setStroke(line, 1.5);
            strokeEllipse(t, c.x, c.y, rx, ry);
            drawCircle(t, c.x, c.y, kHandle * 0.7, dot);
            drawCircle(t, c.x + rx, c.y, kHandle, dot);
            drawCircle(t, c.x, c.y + ry, kHandle, dot);
        }
        else if (mMask.type == MP::Linear)
        {
            Point p0 = normToLocal(mMask.x0, mMask.y0), p1 = normToLocal(mMask.x1, mMask.y1);
            double dx = p1.x - p0.x, dy = p1.y - p0.y;
            double len = std::hypot(dx, dy); if (len < 1e-3) len = 1e-3;
            double px = -dy / len, py = dx / len;          // perpendicular unit
            const double guide = std::max(mFitted.w, mFitted.h);
            t.setStroke(line, 1.5);
            for (Point p : {p0, p1})                        // 0% and 100% boundary lines
            {
                t.beginPath();
                t.moveTo(p.x - px * guide, p.y - py * guide);
                t.lineTo(p.x + px * guide, p.y + py * guide);
                t.strokePath();
            }
            t.setStroke(Color{line.r, line.g, line.b, 0.5f}, 1.0);  // axis
            t.beginPath(); t.moveTo(p0.x, p0.y); t.lineTo(p1.x, p1.y); t.strokePath();
            drawCircle(t, p0.x, p0.y, kHandle, dot);
            drawCircle(t, p1.x, p1.y, kHandle, dot);
        }
        else  // brush
        {
            t.setStroke(Color{line.r, line.g, line.b, 0.7f}, 1.5);
            for (const auto &d : mMask.dabs)
            {
                Point c = normToLocal(d.x, d.y);
                strokeEllipse(t, c.x, c.y, d.radius * mFitted.w, d.radius * mFitted.h);
            }
        }
    }
}
}
