#include "LfoCurve.h"
#include <cmath>

namespace arstro
{
namespace pulsar
{
    using namespace artboard;

    namespace
    {
        double clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
        double dist2(const Point &a, const Point &b)
        {
            const double dx = a.x - b.x, dy = a.y - b.y;
            return dx * dx + dy * dy;
        }
        // cubic bezier scalar
        double bez(double a, double b, double c, double d, double t)
        {
            const double u = 1.0 - t;
            return u * u * u * a + 3.0 * u * u * t * b + 3.0 * u * t * t * c + t * t * t * d;
        }
    }

    LfoCurve::LfoCurve()
    {
        width.set(300.0);
        height.set(150.0);
        // default: a smooth sine made of 5 nodes with horizontal tangents
        mNodes = {{0.0, 0.0, 0.12, 0.0},
                  {0.25, 1.0, 0.10, 0.0},
                  {0.5, 0.0, 0.12, 0.0},
                  {0.75, -1.0, 0.10, 0.0},
                  {1.0, 0.0, 0.12, 0.0}};
    }

    double LfoCurve::sx(double n) const { return padX() + n * (width.value() - 2 * padX()); }
    double LfoCurve::sy(double n) const { return height.value() * 0.5 - n * (height.value() * 0.5 - padY()); }
    double LfoCurve::nx(double px) const { return clamp((px - padX()) / (width.value() - 2 * padX()), 0.0, 1.0); }
    double LfoCurve::ny(double py) const { return clamp((height.value() * 0.5 - py) / (height.value() * 0.5 - padY()), -1.0, 1.0); }
    Point LfoCurve::nodeScreen(int i) const { return {sx(mNodes[i].x), sy(mNodes[i].y)}; }
    Point LfoCurve::handleScreen(int i, int dir) const
    {
        return {sx(mNodes[i].x + dir * mNodes[i].hx), sy(mNodes[i].y + dir * mNodes[i].hy)};
    }

    double LfoCurve::evalSegmentY(int i, double targetX) const
    {
        const Node &a = mNodes[i], &b = mNodes[i + 1];
        const double x0 = a.x, x1 = a.x + a.hx, x2 = b.x - b.hx, x3 = b.x;
        const double y0 = a.y, y1 = a.y + a.hy, y2 = b.y - b.hy, y3 = b.y;
        double lo = 0.0, hi = 1.0;
        for (int it = 0; it < 18; ++it) // bisect t so bezier-x == targetX (x assumed monotonic)
        {
            const double t = 0.5 * (lo + hi);
            (bez(x0, x1, x2, x3, t) < targetX ? lo : hi) = t;
        }
        const double t = 0.5 * (lo + hi);
        return bez(y0, y1, y2, y3, t);
    }

    double LfoCurve::valueAt(double phase01) const
    {
        const double x = clamp(phase01, 0.0, 1.0);
        for (size_t i = 0; i + 1 < mNodes.size(); ++i)
            if (x <= mNodes[i + 1].x + 1e-9)
                return clamp(evalSegmentY((int)i, x), -1.0, 1.0);
        return mNodes.back().y;
    }

    void LfoCurve::advance(double nowMs)
    {
        const double dt = mLastMs < 0.0 ? 0.0 : (nowMs - mLastMs) / 1000.0;
        mLastMs = nowMs;
        mReveal.advance(dt, 20.0);
        Segment::advance(nowMs);
    }

    void LfoCurve::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        // The frame + zero line stay stable; the curve content fades in on tab select.
        const double rv = mReveal.value();
        auto fade = [rv](Color c) { c.a *= rv; return c; };
        drawRoundedRect(t, Rect{0, 0, w, h}, 6.0,
                        Paint::filledStroked(Color{0, 0, 0, 0.35}, Color{1, 1, 1, 0.08}, 1.0));
        // zero line
        t.beginPath(); t.moveTo(padX(), sy(0.0)); t.lineTo(w - padX(), sy(0.0));
        t.setStroke(Color{1, 1, 1, 0.08}, 1.0); t.strokePath();

        // sample the whole curve into a polyline
        std::vector<Point> pts;
        const int steps = (int)(w - 2 * padX());
        for (int s = 0; s <= steps; ++s)
        {
            const double x = (double)s / steps;
            pts.push_back({sx(x), sy(valueAt(x))});
        }

        // filled area + glow
        t.beginPath(); t.moveTo(pts[0].x, sy(0.0));
        for (auto &p : pts) t.lineTo(p.x, p.y);
        t.lineTo(pts.back().x, sy(0.0)); t.closePath();
        t.setLinearFill(0, padY(), 0, h - padY(),
                        fade(Color{mAccent.r, mAccent.g, mAccent.b, 0.16}),
                        fade(Color{mAccent.r, mAccent.g, mAccent.b, 0.0}));
        t.fillPath();
        const double passW[3] = {6.0, 4.0, 2.5};
        const double passA[3] = {0.06, 0.12, 0.22};
        for (int pass = 0; pass < 3; ++pass)
        {
            t.beginPath(); t.moveTo(pts[0].x, pts[0].y);
            for (size_t i = 1; i < pts.size(); ++i) t.lineTo(pts[i].x, pts[i].y);
            t.setStroke(fade(Color{mAccent.r, mAccent.g, mAccent.b, passA[pass]}), passW[pass]);
            t.strokePath();
        }
        t.beginPath(); t.moveTo(pts[0].x, pts[0].y);
        for (size_t i = 1; i < pts.size(); ++i) t.lineTo(pts[i].x, pts[i].y);
        t.setStroke(fade(mAccent), 2.0); t.strokePath();

        // selected node's tangent handles
        if (mSel >= 0)
        {
            const Point n = nodeScreen(mSel);
            for (int dir : {+1, -1})
            {
                const Point hsd = handleScreen(mSel, dir);
                t.beginPath(); t.moveTo(n.x, n.y); t.lineTo(hsd.x, hsd.y);
                t.setStroke(fade(Color{1, 1, 1, 0.4}), 1.0); t.strokePath();
                drawCircle(t, hsd.x, hsd.y, 3.0, Paint::filled(fade(Color{1, 1, 1, 0.7})));
            }
        }
        // node dots
        for (size_t i = 0; i < mNodes.size(); ++i)
        {
            const Point p = nodeScreen((int)i);
            const bool sel = (int)i == mSel;
            drawCircle(t, p.x, p.y, sel ? 5.0 : 4.0,
                       Paint::filledStroked(fade(sel ? Color{1, 1, 1, 0.95} : mAccent), fade(Color{0, 0, 0, 0.5}), 1.0));
        }

        // playhead
        if (mActive)
        {
            const double px = sx(clamp(mPhase, 0.0, 1.0)), py = sy(valueAt(mPhase));
            drawCircle(t, px, py, 6.0, Paint::filled(fade(Color{mAccent.r, mAccent.g, mAccent.b, 0.18})));
            drawCircle(t, px, py, 3.2, Paint::filled(fade(Color{1, 1, 1, 0.95})));
        }
    }

    bool LfoCurve::handleGesture(const Gesture &g, const Point &lp)
    {
        using T = Gesture::Type;
        const double hitR2 = 100.0; // 10px

        if (g.type == T::DoubleClick)
        {
            // remove an interior node if hit, else insert a node at this x
            for (size_t i = 1; i + 1 < mNodes.size(); ++i)
                if (dist2(lp, nodeScreen((int)i)) <= hitR2)
                {
                    mNodes.erase(mNodes.begin() + i);
                    mSel = -1;
                    return true;
                }
            const double cx = nx(lp.x);
            Node nn{cx, valueAt(cx), 0.08, 0.0};
            for (size_t i = 0; i + 1 < mNodes.size(); ++i)
                if (cx <= mNodes[i + 1].x) { mNodes.insert(mNodes.begin() + i + 1, nn); mSel = (int)i + 1; break; }
            return true;
        }
        if (g.type == T::Down)
        {
            mDrag = 0;
            if (mSel >= 0) // handles of the selected node take priority
            {
                if (dist2(lp, handleScreen(mSel, +1)) <= hitR2) { mDrag = 2; return true; }
                if (dist2(lp, handleScreen(mSel, -1)) <= hitR2) { mDrag = 3; return true; }
            }
            for (size_t i = 0; i < mNodes.size(); ++i)
                if (dist2(lp, nodeScreen((int)i)) <= hitR2) { mSel = (int)i; mDrag = 1; return true; }
            mSel = -1;
            return true;
        }
        if (g.type == T::Drag && mSel >= 0 && mDrag)
        {
            Node &n = mNodes[mSel];
            if (mDrag == 1) // move node
            {
                n.y = ny(lp.y);
                const bool ends = (mSel == 0 || mSel == (int)mNodes.size() - 1);
                if (!ends)
                {
                    const double lo = mNodes[mSel - 1].x + 0.01, hi = mNodes[mSel + 1].x - 0.01;
                    n.x = clamp(nx(lp.x), lo, hi);
                }
            }
            else // drag a tangent handle (symmetric); store as the out-handle
            {
                const double sign = (mDrag == 2) ? 1.0 : -1.0;
                n.hx = clamp(sign * (nx(lp.x) - n.x), 0.0, 0.4);
                n.hy = sign * (ny(lp.y) - n.y);
            }
            return true;
        }
        if (g.type == T::Up || g.type == T::Drop) { mDrag = 0; return true; }
        return Segment::handleGesture(g, lp);
    }
}
}
