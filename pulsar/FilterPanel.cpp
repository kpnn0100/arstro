#include "FilterPanel.h"
#include "Chrome.h"
#include <cmath>
#include <functional>
#include <vector>

namespace arstro
{
namespace pulsar
{
    using namespace artboard;

    FilterPanel::FilterPanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        width.set(261.0);
        height.set(198.0);

        mTypeCombo = std::make_shared<ComboBox>(theme.combo);
        mTypeCombo->setOptions({"LP 12", "LP 24", "BAND", "HIGH", "NOTCH"});
        mTypeCombo->setSelectedIndex(0);
        mTypeCombo->x.set(14.0); mTypeCombo->y.set(32.0);
        mTypeCombo->width.set(233.0); mTypeCombo->height.set(24.0);
        mTypeCombo->onChange = [this](int idx) { mType = idx; };
        addChild(mTypeCombo);

        auto knob = [&](const char *lbl, double init, std::function<void(double)> cb, double x) {
            auto k = std::make_shared<Knob>(theme.knob);
            k->label = lbl;
            k->setRange(0.0, 1.0); k->setValue(init); k->setDefault(init);
            k->x.set(x); k->y.set(140.0);
            k->width.set(54.0); k->height.set(44.0);
            k->onChange = std::move(cb);
            mKnobs.push_back(k);
            addChild(k);
            return k;
        };
        // 4 compact knobs across: x = 12 + i*58 (54 wide + 4 gap)
        knob("cutoff", 0.6, [this](double v) { mCutoff = v; }, 12.0);
        knob("reso", 0.15, [this](double v) { mReso = v; }, 70.0);
        knob("drive", 0.0, [this](double v) { mDrive = v; }, 128.0);
        knob("env", 0.3, [this](double v) { mEnv = v; }, 186.0);
    }

    // Visual magnitude response (not audio DSP). x and cutoff are log-freq positions
    // in [0,1]; returns roughly [0,1.5].
    double FilterPanel::gain(double x) const
    {
        const double c = mCutoffDisp.value(), reso = mResoDisp.value(), eps = 1e-3;
        double g;
        switch (mType)
        {
        case 0: case 1: // LP 12 / 24
        {
            const double order = (mType == 1) ? 4.0 : 2.0;
            g = 1.0 / std::sqrt(1.0 + std::pow(x / (c + eps), 2.0 * order));
            break;
        }
        case 2: // band-pass
        {
            const double d = (x - c) / 0.18;
            g = 1.0 / std::sqrt(1.0 + d * d * d * d);
            break;
        }
        case 3: // high-pass
            g = 1.0 / std::sqrt(1.0 + std::pow((c + eps) / (x + eps), 4.0));
            break;
        default: // notch
        {
            const double d = (x - c) / 0.10;
            g = (d * d) / (1.0 + d * d);
            break;
        }
        }
        if (mType != 4) // resonant peak near cutoff
        {
            const double rb = (x - c) / 0.04;
            g += reso * 1.1 * std::exp(-rb * rb);
        }
        return g < 0.0 ? 0.0 : (g > 1.5 ? 1.5 : g);
    }

    void FilterPanel::advance(double nowMs)
    {
        const double dt = mLastMs < 0.0 ? 0.0 : (nowMs - mLastMs) / 1000.0;
        mLastMs = nowMs;
        mCutoffDisp.setTarget(mCutoff);
        mResoDisp.setTarget(mReso);
        mCutoffDisp.advance(dt, 16.0);
        mResoDisp.advance(dt, 16.0);
        Segment::advance(nowMs);
    }

    void FilterPanel::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawPanelChrome(t, w, h, mAccent, "FILTER");

        const double bx = 14.0, by = 62.0, bw = w - 28.0, bh = 70.0;
        drawRoundedRect(t, Rect{bx, by, bw, bh}, 6.0,
                        Paint::filledStroked(Color{0, 0, 0, 0.35}, Color{1, 1, 1, 0.08}, 1.0));
        const double x0 = bx + 8, x1 = bx + bw - 8, top = by + 8, bot = by + bh - 8;
        auto yFor = [&](double g) { return bot - (g / 1.5) * (bot - top); };

        // cutoff marker line
        const double cx = x0 + mCutoffDisp.value() * (x1 - x0);
        t.beginPath(); t.moveTo(cx, top); t.lineTo(cx, bot);
        t.setStroke(Color{1, 1, 1, 0.10}, 1.0); t.strokePath();

        // build the response polyline
        std::vector<Point> pts;
        for (double x = x0; x <= x1; x += 2.0)
            pts.push_back({x, yFor(gain((x - x0) / (x1 - x0)))});
        if (pts.size() < 2) return;

        // filled area under the curve (gradient to transparent)
        t.beginPath(); t.moveTo(pts[0].x, bot);
        for (auto &p : pts) t.lineTo(p.x, p.y);
        t.lineTo(pts.back().x, bot); t.closePath();
        t.setLinearFill(0, top, 0, bot,
                        Color{mAccent.r, mAccent.g, mAccent.b, 0.22},
                        Color{mAccent.r, mAccent.g, mAccent.b, 0.0});
        t.fillPath();

        // glowing curve
        const double passW[3] = {6.0, 4.0, 2.5};
        const double passA[3] = {0.06, 0.12, 0.22};
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
    }
}
}
