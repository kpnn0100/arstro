#include "EnvPanel.h"
#include "Chrome.h"
#include <cmath>
#include <functional>

namespace arstro
{
namespace pulsar
{
    using namespace artboard;

    EnvPanel::EnvPanel(const Theme &theme, const Color &accent, const std::string &title)
        : mAccent(accent), mTitle(title)
    {
        width.set(340.0);
        height.set(230.0);

        struct Spec { const char *lbl; double init; double *slot; };
        Spec specs[4] = {{"A", mA, &mA}, {"D", mD, &mD}, {"S", mS, &mS}, {"R", mR, &mR}};
        for (int i = 0; i < 4; ++i)
        {
            double *slot = specs[i].slot;
            auto k = std::make_shared<Knob>(theme.knob);
            k->label = specs[i].lbl;
            k->setRange(0.0, 1.0); k->setValue(specs[i].init); k->setDefault(specs[i].init);
            k->x.set(45.0 + i * 64.0); k->y.set(162.0);
            k->width.set(58.0); k->height.set(48.0);
            k->onChange = [slot](double v) { *slot = v; };
            mKnobs.push_back(k);
            addChild(k);
        }
    }

    void EnvPanel::advance(double nowMs)
    {
        double dt = mLastMs < 0.0 ? 0.0 : (nowMs - mLastMs) / 1000.0;
        mLastMs = nowMs;
        if (dt > 0.0)
        {
            if (dt > 0.05) dt = 0.05;
            const double omega = 16.0;
            auto spring = [&](double &d, double &v, double target) {
                const double acc = -2.0 * omega * v - omega * omega * (d - target);
                v += acc * dt; d += v * dt;
            };
            spring(mAd, mAv, mA); spring(mDd, mDv, mD);
            spring(mSd, mSv, mS); spring(mRd, mRv, mR);
        }
        Segment::advance(nowMs);
    }

    void EnvPanel::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawPanelChrome(t, w, h, mAccent, mTitle);

        const double bx = 14.0, by = 34.0, bw = w - 28.0, bh = 120.0;
        drawRoundedRect(t, Rect{bx, by, bw, bh}, 6.0,
                        Paint::filledStroked(Color{0, 0, 0, 0.35}, Color{1, 1, 1, 0.08}, 1.0));
        const double x0 = bx + 10, x1 = bx + bw - 10, top = by + 10, bot = by + bh - 10;
        auto yFor = [&](double lvl) { return bot - lvl * (bot - top); };

        // segment widths: A, D, fixed sustain hold, R — normalized to the box width
        const double hold = 0.22;
        const double total = mAd + mDd + hold + mRd + 1e-3;
        const double pxA = (mAd / total) * (x1 - x0);
        const double pxD = (mDd / total) * (x1 - x0);
        const double pxH = (hold / total) * (x1 - x0);
        const double pxR = (mRd / total) * (x1 - x0);

        const Point p0{x0, yFor(0.0)};                 // start
        const Point pA{x0 + pxA, yFor(1.0)};           // attack peak
        const Point pD{pA.x + pxD, yFor(mSd)};         // decay → sustain
        const Point pS{pD.x + pxH, yFor(mSd)};         // sustain hold
        const Point pR{pS.x + pxR, yFor(0.0)};         // release → 0

        // filled area under the envelope (gradient floor)
        t.beginPath();
        t.moveTo(p0.x, bot);
        t.lineTo(p0.x, p0.y); t.lineTo(pA.x, pA.y); t.lineTo(pD.x, pD.y);
        t.lineTo(pS.x, pS.y); t.lineTo(pR.x, pR.y); t.lineTo(pR.x, bot);
        t.closePath();
        t.setLinearFill(0, top, 0, bot,
                        Color{mAccent.r, mAccent.g, mAccent.b, 0.22},
                        Color{mAccent.r, mAccent.g, mAccent.b, 0.0});
        t.fillPath();

        // glowing envelope line
        const double passW[3] = {6.0, 4.0, 2.5};
        const double passA[3] = {0.06, 0.12, 0.22};
        auto stroke = [&](const Color &c, double wd) {
            t.beginPath();
            t.moveTo(p0.x, p0.y); t.lineTo(pA.x, pA.y); t.lineTo(pD.x, pD.y);
            t.lineTo(pS.x, pS.y); t.lineTo(pR.x, pR.y);
            t.setStroke(c, wd); t.strokePath();
        };
        for (int pass = 0; pass < 3; ++pass)
            stroke(Color{mAccent.r, mAccent.g, mAccent.b, passA[pass]}, passW[pass]);
        stroke(mAccent, 2.0);

        // node dots at the segment joints
        for (const Point &p : {pA, pD, pR})
            drawCircle(t, p.x, p.y, 3.0, Paint::filled(mAccent));
    }
}
}
