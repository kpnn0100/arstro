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

    namespace
    {
        /** Far outside anything a pointer can reach on a sane canvas, and finite. R-MASK-5 wants
         *  no *design* limit; this is only arithmetic hygiene. */
        constexpr float kGeomBound = 8.0f;
        float clampGeom(float v)
        {
            if (!(v > -kGeomBound)) return -kGeomBound;   // also catches NaN
            if (v > kGeomBound) return kGeomBound;
            return v;
        }
        /** A radius of zero is not a small mask, it is a division the engine has to guard. */
        float positiveRadius(float r) { return r > 1e-4f ? (r < kGeomBound ? r : kGeomBound) : 1e-4f; }
    }

    Point MaskOverlay::normToLocal(float nx, float ny) const
    {
        return Point{mFitted.x + nx * mFitted.w, mFitted.y + ny * mFitted.h};
    }
    void MaskOverlay::localToNorm(const Point &p, float &nx, float &ny) const
    {
        nx = mFitted.w > 0 ? (float)((p.x - mFitted.x) / mFitted.w) : 0.f;
        ny = mFitted.h > 0 ? (float)((p.y - mFitted.y) / mFitted.h) : 0.f;
        // R-MASK-5: NOT clamped to 0..1. Normalised framed-image coordinates are a coordinate
        // space, not a boundary — a radial mask bigger than the frame, or centred off-frame, and
        // a gradient entering from off-canvas are ordinary tools. A vignette that darkens every
        // corner equally cannot be built from an ellipse trapped inside the frame.
        //
        // This clamp was the whole of the reported defect: the engine's maskCoverage never
        // bounded mask geometry, so out-of-frame masks have always RENDERED correctly — the
        // handle simply could not be dragged there, which read as "the size stops at the border".
        //
        // What remains is a sanity bound, not a design limit. How far a handle can actually go is
        // set by the canvas the pointer can reach; this only keeps a degenerate `mFitted` (a
        // near-zero fitted rect, mid-transition) from turning a stray pixel into a huge or
        // non-finite coordinate that would then be persisted into a project file.
        nx = clampGeom(nx);
        ny = clampGeom(ny);
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
        if (mMask.type == MP::Path) return pickPathHandle(local);
        return 99;  // brush: any press paints
    }

    Point MaskOverlay::pathPointAt(int i) const
    {
        if (i < 0 || i >= (int)mMask.path.size()) return Point{0, 0};
        const arstro::CurvePoint &p = mMask.path[(std::size_t)i];
        return normToLocal(p.x, p.y);
    }

    int MaskOverlay::pickPathHandle(const Point &local) const
    {
        auto near = [&](Point h) { return std::hypot(local.x - h.x, local.y - h.y) <= kPick; };
        // Handles first: a tangent handle sits close to its point and would otherwise be
        // unreachable, exactly as in the curve editor.
        for (int i = 0; i < (int)mMask.path.size(); ++i)
        {
            const arstro::CurvePoint &p = mMask.path[(std::size_t)i];
            if (!p.smooth) continue;
            if (near(normToLocal(p.x + p.ix, p.y + p.iy))) return kDragIn + i;
            if (near(normToLocal(p.x + p.ox, p.y + p.oy))) return kDragOut + i;
        }
        for (int i = 0; i < (int)mMask.path.size(); ++i)
            if (near(pathPointAt(i))) return kDragPoint + i;
        // Empty canvas: this is where a point is PLACED. Returning the new point's own drag id
        // means the press that created it can go straight on to position it, so placing and
        // adjusting are one gesture rather than a click followed by a hunt for what you made.
        return kDragPoint + (int)mMask.path.size();
    }

    void MaskOverlay::applyPathDrag(int handle, const Point &local)
    {
        float nx, ny; localToNorm(local, nx, ny);
        const int kind = handle - handle % 1000;
        const std::size_t i = (std::size_t)(handle % 1000);
        if (kind == kDragPoint)
        {
            // A press on empty canvas asks for a point that does not exist yet. Appending here
            // rather than in handleGesture keeps the "where did the drag land" arithmetic in one
            // place — and a drag that begins off the end of the list is exactly a new point.
            if (i >= mMask.path.size())
            {
                if (i > mMask.path.size()) return;
                arstro::CurvePoint p;
                p.x = nx; p.y = ny;
                mMask.path.push_back(p);
            }
            else
            {
                // Moving a point carries its handles with it: they are stored as OFFSETS, so
                // this is free — and it is the behaviour a pen tool has everywhere.
                mMask.path[i].x = nx;
                mMask.path[i].y = ny;
            }
        }
        else if (i < mMask.path.size())
        {
            arstro::CurvePoint &p = mMask.path[i];
            p.smooth = true;
            const float dx = nx - p.x, dy = ny - p.y;
            if (kind == kDragIn) { p.ix = dx; p.iy = dy; p.ox = -dx; p.oy = -dy; }
            else                 { p.ox = dx; p.oy = dy; p.ix = -dx; p.iy = -dy; }
            // Mirrored, not independent: a point whose two sides bend in unrelated directions
            // is a corner with extra steps, and cosmo already has corners — leaving `smooth`
            // false is how you ask for one. Mirroring is what makes a hand-drawn outline
            // continuous, which is the whole reason to reach for the handles at all.
        }
        if (onChange) onChange(mMask);
    }

    void MaskOverlay::applyDrag(int handle, const Point &local)
    {
        if (mMask.type == MP::Path) { applyPathDrag(handle, local); return; }
        float nx, ny; localToNorm(local, nx, ny);
        if (mMask.type == MP::Radial)
        {
            if (handle == 0) { mMask.cx = nx; mMask.cy = ny; }
            // R-MASK-5: the radius is whatever the drag says, past the image edge included.
            else if (handle == 1) { mMask.rx = positiveRadius(std::fabs(nx - mMask.cx)); }
            else if (handle == 2) { mMask.ry = positiveRadius(std::fabs(ny - mMask.cy)); }
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
        {
            // The modifier is read at the PRESS and held for the whole gesture, the same rule
            // CurvePanel follows: GTK reports Alt on every event, but a user who lets go of the
            // key mid-drag has not asked for the drag to change meaning (D-50).
            mDragAlt = g.alt;
            mDrag = pickHandle(local);
            if (mMask.type == MP::Path && mDragAlt && mDrag >= kDragPoint && mDrag < kDragIn)
            {
                // Alt on an EXISTING point pulls its tangents instead of moving it. Alt on empty
                // canvas still places a point: there is nothing there to bend.
                const int i = mDrag - kDragPoint;
                if (i < (int)mMask.path.size()) mDrag = kDragOut + i;
            }
            if (mDrag == 99) applyDrag(mDrag, local);  // brush: paint on press
            if (mMask.type == MP::Path && mDrag >= kDragPoint)
                applyDrag(mDrag, local);               // place the point on the press, not on a drag
            return mDrag >= 0;
        }
        case Gesture::Type::DoubleClick:
        {
            // Remove a point. Only on a point — a double-click on empty canvas has already
            // placed two points through the Down handler, and deleting one of those would make
            // the tool feel like it was fighting the user.
            if (mMask.type != MP::Path) return false;
            for (int i = 0; i < (int)mMask.path.size(); ++i)
                if (std::hypot(local.x - pathPointAt(i).x, local.y - pathPointAt(i).y) <= kPick)
                {
                    mMask.path.erase(mMask.path.begin() + i);
                    mDrag = -1;
                    if (onChange) onChange(mMask);
                    return true;
                }
            return false;
        }
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

    void MaskOverlay::advance(double nowMs)
    {
        // R-G-1: the boundary appears and disappears; it does not blink. The target is "there is
        // an outline and this overlay is live", and the tween is started HERE because the setter
        // has no clock. 180 ms, the same figure everything else on this canvas fades at.
        mOutlineTarget = (mActive && !mOutline.empty()) ? 1.0 : 0.0;
        if (mOutlineTarget != mOutlineLastTarget)
        {
            mOutlineFade.animateTo(mOutlineTarget, 180.0, Easing::EaseOutCubic, nowMs);
            mOutlineLastTarget = mOutlineTarget;
        }
        mOutlineFade.update(nowMs);
        Segment::advance(nowMs);
    }

    void MaskOverlay::onPaint(IRenderTarget &t) const
    {
        // The computed boundary is drawn even while the overlay is fading OUT, which is why it
        // is above the `mActive` early return: a fade needs something to fade.
        const double fade = mOutlineFade.value();
        if (fade > 0.004 && !mOutline.empty())
        {
            // R-AISEG-18. The same accent and the same 1.5 px stroke a drawn path gets, because
            // it is the same thing to the photographer — the edge of the mask — and a second
            // visual language for "where the mask is" would be a distinction without a
            // difference. Mapped through `normToLocal` like every other mask, so it tracks zoom
            // and pan for free (R-MASK-3).
            //
            // NOT closed with closePath(): a region that runs off the edge of the frame gives an
            // open chain, and closing it would draw a straight line across the photo joining two
            // points that are only neighbours in the trace order.
            t.setStroke(Color{mAccent.r, mAccent.g, mAccent.b, (float)(mAccent.a * fade)}, 1.5);
            for (const auto &loop : mOutline)
            {
                if (loop.size() < 2) continue;
                t.beginPath();
                for (std::size_t i = 0; i < loop.size(); ++i)
                {
                    const Point p = normToLocal(loop[i].first, loop[i].second);
                    if (i == 0) t.moveTo(p.x, p.y);
                    else t.lineTo(p.x, p.y);
                }
                t.strokePath();
            }
        }
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
        else if (mMask.type == MP::Path)
        {
            // The outline, from the ENGINE's flattener — so the shape on screen is the shape
            // that renders, down to the segment count (R-MASK-6).
            const auto poly = arstro::maskPathPolygon(mMask.path);
            if (poly.size() >= 3)
            {
                t.setStroke(line, 1.5);
                t.beginPath();
                for (std::size_t i = 0; i < poly.size(); ++i)
                {
                    const Point p = normToLocal(poly[i].first, poly[i].second);
                    if (i == 0) t.moveTo(p.x, p.y);
                    else t.lineTo(p.x, p.y);
                }
                t.closePath();
                t.strokePath();
            }
            else if (mMask.path.size() == 2)
            {
                // Two points is not a shape yet, and drawing nothing would look broken. The
                // half-drawn outline is dimmer, which is the honest signal: this renders
                // nothing until there is a third point.
                t.setStroke(Color{line.r, line.g, line.b, 0.45f}, 1.5);
                const Point a = pathPointAt(0), b = pathPointAt(1);
                t.beginPath(); t.moveTo(a.x, a.y); t.lineTo(b.x, b.y); t.strokePath();
            }
            // Tangent handles, for the smooth points only — an outline of plain corners shows
            // no handles at all, so the affordance appears exactly when it means something.
            for (const auto &p : mMask.path)
            {
                if (!p.smooth) continue;
                const Point hi = normToLocal(p.x + p.ix, p.y + p.iy);
                const Point ho = normToLocal(p.x + p.ox, p.y + p.oy);
                t.setStroke(Color{line.r, line.g, line.b, 0.5f}, 1.0);
                t.beginPath(); t.moveTo(hi.x, hi.y); t.lineTo(ho.x, ho.y); t.strokePath();
                drawCircle(t, hi.x, hi.y, kHandle * 0.6, dot);
                drawCircle(t, ho.x, ho.y, kHandle * 0.6, dot);
            }
            for (int i = 0; i < (int)mMask.path.size(); ++i)
            {
                const Point c = pathPointAt(i);
                drawCircle(t, c.x, c.y, kHandle, dot);
            }
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
