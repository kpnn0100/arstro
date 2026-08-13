#include "EnvPanel.h"
#include "Chrome.h"
#include <cmath>
#include <vector>

namespace arstro
{
namespace pulsar
{
    using namespace artboard;

    namespace
    {
        constexpr double kGX = 14.0, kGY = 34.0, kGW = 312.0, kGH = 150.0;
        double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
        double clampPM1(double v) { return v < -1 ? -1 : (v > 1 ? 1 : v); }
        double dist2(const Point &a, const Point &b) { const double dx = a.x - b.x, dy = a.y - b.y; return dx * dx + dy * dy; }
        // monotone 0->1 shape with a bend; c=0 linear, c>0 fast-start, c<0 slow-start
        double shape(double t, double c) { return std::pow(t < 0 ? 0 : (t > 1 ? 1 : t), std::exp(-c * 1.8)); }
        double segDur(double v) { return 0.004 + v * 1.4; } // seconds
    }

    EnvPanel::EnvPanel(const Theme &theme, const Color &accent, const std::string &title)
        : mAccent(accent), mTitle(title)
    {
        (void)theme;
        width.set(340.0);
        height.set(250.0);

        mBadge = std::make_shared<ModSourceBadge>(sourceId(), accent, "ENV");
        mBadge->x.set(width.value() - 60.0); mBadge->y.set(8.0);
        addChild(mBadge);
    }

    void EnvPanel::setAssignSink(std::function<void(int, const Color &, const Point &)> fn)
    {
        mBadge->onAssign = fn;
    }

    EnvPanel::Geom EnvPanel::geom() const
    {
        Geom g;
        g.x0 = kGX + 10.0; g.x1 = kGX + kGW - 10.0;
        g.top = kGY + 10.0; g.bot = kGY + kGH - 10.0;
        g.hold = (g.x1 - g.x0) * 0.18;
        g.region = (g.x1 - g.x0 - g.hold) / 3.0;
        g.nAx = g.x0 + mA * g.region;
        g.nDx = g.x0 + g.region + mD * g.region;
        g.nDy = g.bot - mS * (g.bot - g.top);
        g.susX = g.nDx + g.hold;
        g.nRx = g.susX + mR * g.region;
        return g;
    }

    double EnvPanel::levelAtTime(double tSec, bool releasing, double relFrom, double relT) const
    {
        if (releasing)
        {
            const double rd = segDur(mR);
            return relT >= rd ? 0.0 : relFrom * (1.0 - shape(relT / rd, mCR));
        }
        const double ad = segDur(mA), dd = segDur(mD);
        if (tSec < ad) return shape(tSec / ad, mCA);            // 0 -> 1
        if (tSec < ad + dd) return 1.0 + (mS - 1.0) * shape((tSec - ad) / dd, mCD); // 1 -> S
        return mS;                                              // sustain
    }

    void EnvPanel::setGate(bool on)
    {
        if (on && !mGate) mElapsed = 0.0;
        if (!on && mGate) { mRelElapsed = 0.0; mRelFrom = mLevel; }
        mGate = on;
    }

    void EnvPanel::advance(double nowMs)
    {
        double dt = mLastMs < 0.0 ? 0.0 : (nowMs - mLastMs) / 1000.0;
        mLastMs = nowMs;
        if (dt > 0.05) dt = 0.05;
        if (mGate) { mElapsed += dt; mLevel = levelAtTime(mElapsed, false, 0, 0); mRelFrom = mLevel; }
        else { mRelElapsed += dt; mLevel = levelAtTime(0, true, mRelFrom, mRelElapsed); }
        Segment::advance(nowMs);
    }

    void EnvPanel::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawPanelChrome(t, w, h, mAccent, mTitle);

        drawRoundedRect(t, Rect{kGX, kGY, kGW, kGH}, 6.0,
                        Paint::filledStroked(Color{0, 0, 0, 0.35}, Color{1, 1, 1, 0.08}, 1.0));
        const Geom g = geom();
        auto Y = [&](double lvl) { return g.bot - lvl * (g.bot - g.top); };

        // sample the four segments into one polyline
        std::vector<Point> pts;
        auto addSeg = [&](double xa, double la, double xb, double lb, double c) {
            const int n = 18;
            for (int s = 0; s <= n; ++s)
            {
                const double tt = (double)s / n;
                const double lvl = la + (lb - la) * shape(tt, c);
                pts.push_back({xa + (xb - xa) * tt, Y(lvl)});
            }
        };
        addSeg(g.x0, 0.0, g.nAx, 1.0, mCA);   // attack
        addSeg(g.nAx, 1.0, g.nDx, mS, mCD);   // decay
        pts.push_back({g.susX, Y(mS)});       // sustain hold
        addSeg(g.susX, mS, g.nRx, 0.0, mCR);  // release

        // filled area + glow line
        t.beginPath(); t.moveTo(pts[0].x, g.bot);
        for (auto &p : pts) t.lineTo(p.x, p.y);
        t.lineTo(pts.back().x, g.bot); t.closePath();
        t.setLinearFill(0, g.top, 0, g.bot,
                        Color{mAccent.r, mAccent.g, mAccent.b, 0.22},
                        Color{mAccent.r, mAccent.g, mAccent.b, 0.0});
        t.fillPath();
        const double passW[3] = {6.0, 4.0, 2.5}, passA[3] = {0.06, 0.12, 0.22};
        for (int pass = 0; pass < 3; ++pass)
        {
            t.beginPath(); t.moveTo(pts[0].x, pts[0].y);
            for (size_t i = 1; i < pts.size(); ++i) t.lineTo(pts[i].x, pts[i].y);
            t.setStroke(Color{mAccent.r, mAccent.g, mAccent.b, passA[pass]}, passW[pass]);
            t.strokePath();
        }
        t.beginPath(); t.moveTo(pts[0].x, pts[0].y);
        for (size_t i = 1; i < pts.size(); ++i) t.lineTo(pts[i].x, pts[i].y);
        t.setStroke(mAccent, 2.0); t.strokePath();

        // curve handles (segment midpoints) + draggable A/D/S/R nodes
        drawCircle(t, (g.x0 + g.nAx) * 0.5, Y(shape(0.5, mCA)), 2.6, Paint::filled(Color{1, 1, 1, 0.5}));
        drawCircle(t, (g.nAx + g.nDx) * 0.5, Y(1.0 + (mS - 1.0) * shape(0.5, mCD)), 2.6, Paint::filled(Color{1, 1, 1, 0.5}));
        drawCircle(t, (g.susX + g.nRx) * 0.5, Y(mS * (1.0 - shape(0.5, mCR))), 2.6, Paint::filled(Color{1, 1, 1, 0.5}));
        for (const Point &p : {Point{g.nAx, g.top}, Point{g.nDx, g.nDy}, Point{g.nRx, g.bot}})
            drawCircle(t, p.x, p.y, 4.0, Paint::filledStroked(mAccent, Color{0, 0, 0, 0.5}, 1.0));

        // playhead while gated
        if (mGate || mLevel > 0.001)
        {
            const double px = g.x0 + (g.x1 - g.x0) * 0.5; // indicative; level is the truth
            (void)px;
            drawCircle(t, g.x1, Y(mLevel), 3.2, Paint::filled(Color{1, 1, 1, 0.95}));
        }
    }

    bool EnvPanel::handleGesture(const Gesture &g0, const Point &lp)
    {
        using T = Gesture::Type;
        const Geom g = geom();
        auto Y = [&](double lvl) { return g.bot - lvl * (g.bot - g.top); };
        const double hit = 121.0; // 11px

        if (g0.type == T::Down)
        {
            mDrag = 0;
            if (dist2(lp, {g.nAx, g.top}) <= hit) mDrag = 1;
            else if (dist2(lp, {g.nDx, g.nDy}) <= hit) mDrag = 2;
            else if (dist2(lp, {g.nRx, g.bot}) <= hit) mDrag = 3;
            else if (dist2(lp, {(g.x0 + g.nAx) * 0.5, Y(shape(0.5, mCA))}) <= hit) { mDrag = 4; mDragRef = mCA; }
            else if (dist2(lp, {(g.nAx + g.nDx) * 0.5, Y(1.0 + (mS - 1.0) * shape(0.5, mCD))}) <= hit) { mDrag = 5; mDragRef = mCD; }
            else if (dist2(lp, {(g.susX + g.nRx) * 0.5, Y(mS * (1.0 - shape(0.5, mCR)))}) <= hit) { mDrag = 6; mDragRef = mCR; }
            return true;
        }
        if (g0.type == T::Drag && mDrag)
        {
            switch (mDrag)
            {
            case 1: mA = clamp01((lp.x - g.x0) / g.region); break;
            case 2:
                mD = clamp01((lp.x - (g.x0 + g.region)) / g.region);
                mS = clamp01((g.bot - lp.y) / (g.bot - g.top));
                break;
            case 3: mR = clamp01((lp.x - g.susX) / g.region); break;
            default: // curve handles: drag toward/away from the segment's linear midpoint
            {
                double la, lb;
                if (mDrag == 4) { la = 0.0; lb = 1.0; }
                else if (mDrag == 5) { la = 1.0; lb = mS; }
                else { la = mS; lb = 0.0; }
                const double linMid = (la + lb) * 0.5;
                const double cursorLvl = (g.bot - lp.y) / (g.bot - g.top);
                double span = std::fabs(lb - la); if (span < 0.05) span = 0.05;
                const double norm = (cursorLvl - linMid) / (span * 0.5);
                double &c = (mDrag == 4 ? mCA : (mDrag == 5 ? mCD : mCR));
                c = clampPM1(norm * (lb > la ? 1.0 : -1.0));
                break;
            }
            }
            return true;
        }
        if (g0.type == T::Up || g0.type == T::Drop) { mDrag = 0; return true; }
        return Segment::handleGesture(g0, lp);
    }
}
}
