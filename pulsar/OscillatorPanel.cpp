#include "OscillatorPanel.h"
#include <cmath>

namespace arstro
{
namespace pulsar
{
    using namespace artboard;

    namespace
    {
        Color scale(const Color &c, double f) { return Color{c.r * f, c.g * f, c.b * f, 1.0}; }

        // Geometry. Each row is two groups; the inter-group gap (G) equals the inter-row
        // gap, so the vertical and horizontal spacing match by construction.
        constexpr double kMargin = 10.0;
        constexpr double kCellW = 58.0, kCellH = 48.0, kCellGap = 6.0; // within a group
        constexpr double kG = OscillatorPanel::kSectionGap;            // group gap == row gap (tight)
        constexpr double kColY = 178.0;                                // nudged up a little
        constexpr double kUnisonW = 3 * kCellW + 2 * kCellGap;         // voice+detune+stereo
        constexpr double kPairW = 2 * kCellW + kCellGap;               // 2-knob group
        constexpr double kRow2Y = kColY + kCellH + kG;
    }

    OscillatorPanel::OscillatorPanel(std::string name, const Theme &theme, const Color &accent)
        : mName(std::move(name)), mAccent(accent), mDimAccent(accent),
          mBaseKnob(theme.knob), mBaseSlider(theme.slider)
    {
        const double rowW = kUnisonW + kG + kCellW;   // == kPairW + kG + kPairW (both 244)
        const double W = kMargin + rowW + kMargin;     // widened to fit the group gap
        const double H = kRow2Y + kCellH + 8.0;
        width.set(W);
        height.set(H);

        mMute = std::make_shared<MuteButton>();
        mMute->setColor(mAccent);
        mMute->x.set(10.0); mMute->y.set(7.0);
        mMute->onChange = [this](bool audioOn) { mMuted = !audioOn; };
        addChild(mMute);

        mDisplay = std::make_shared<WaveDisplay>();
        mDisplay->x.set(10.0); mDisplay->y.set(64.0);
        mDisplay->width.set(W - 20.0); mDisplay->height.set(92.0);
        mDisplay->setColor(mAccent);
        mDisplay->setShape(WaveDisplay::Morph3D);
        addChild(mDisplay);
        WaveDisplay *disp = mDisplay.get();

        mPosSlider = std::make_shared<Slider>(mBaseSlider);
        mPosSlider->setValue(mPosition);
        mPosSlider->setDefault(mPosition); // double-click recenters the wavetable position
        mPosSlider->x.set(10.0); mPosSlider->y.set(160.0);
        mPosSlider->width.set(W - 20.0); mPosSlider->height.set(16.0);
        addChild(mPosSlider);

        auto makeKnob = [&](const char *lbl, double mn, double mx, double init, double *slot, bool isPhase) {
            auto k = std::make_shared<Knob>(mBaseKnob);
            k->label = lbl;
            k->setRange(mn, mx);
            k->setValue(init);
            k->setDefault(init); // double-click restores this nominal value
            k->width.set(kCellW); k->height.set(kCellH);
            k->onChange = [slot, isPhase, disp](double v) { *slot = v; if (isPhase) disp->setPhase(v); };
            mKnobs.push_back(k);
            return k;
        };
        auto makeStepper = [&](const char *lbl, int lo, int hi, int init) {
            auto s = std::make_shared<Stepper>();
            s->setColor(mAccent);
            s->setRange(lo, hi);
            s->setValue(init);
            s->setLabel(lbl);
            s->width.set(kCellW); s->height.set(kCellH);
            return s;
        };

        mVoiceStepper = makeStepper("voice", 1, 16, 1);
        mVoiceStepper->onChange = [this](int v) { mVoice = v; };
        mOctaveStepper = makeStepper("oct", -4, 4, 0);
        mOctaveStepper->onChange = [this](int v) { mOctave = v; };

        // A fixed-width container holding leaf controls at local cell positions.
        auto group = [&](double w, std::vector<std::shared_ptr<Segment>> items) {
            auto g = std::make_shared<Segment>();
            g->width.set(w); g->height.set(kCellH);
            for (size_t i = 0; i < items.size(); ++i)
            {
                items[i]->x.set(i * (kCellW + kCellGap));
                items[i]->y.set(0.0);
                g->addChild(items[i]);
            }
            return g;
        };

        // Each row = Row(spacing=G) of two groups; both rows stacked by a Column(spacing=G).
        // Column spacing == Row spacing == kG, so vertical and horizontal gaps match.
        auto unison = group(kUnisonW, {mVoiceStepper,
                                       makeKnob("detune", 0, 1, 0.2, &mDetune, false),
                                       makeKnob("stereo", 0, 1, 0.0, &mStereo, false)});
        auto row1 = std::make_shared<Row>(); row1->spacing = kG;
        row1->addChild(unison);
        row1->addChild(mOctaveStepper); // single box, separated by the group gap

        auto phaseGrp = group(kPairW, {makeKnob("phase", 0, 1, 0.0, &mPhase, true),
                                       makeKnob("rnd", 0, 1, 0.0, &mRandom, false)});
        auto outGrp = group(kPairW, {makeKnob("pan", 0, 1, 0.5, &mPan, false),
                                     makeKnob("level", 0, 1, 0.8, &mLevel, false)});
        auto row2 = std::make_shared<Row>(); row2->spacing = kG;
        row2->addChild(phaseGrp);
        row2->addChild(outGrp);

        auto col = std::make_shared<Column>();
        col->spacing = kG; // inter-row offset == the inter-group gap
        col->x.set(kMargin); col->y.set(kColY);
        col->addChild(row1);
        col->addChild(row2);
        addChild(col);

        // waveform selector — added LAST so the open drop-down draws on top
        mCombo = std::make_shared<ComboBox>(theme.combo);
        mCombo->setOptions({"SINE", "TRI", "SAW", "SQUARE", "3D"});
        mCombo->setSelectedIndex(WaveDisplay::Morph3D);
        mCombo->x.set(10.0); mCombo->y.set(34.0);
        mCombo->width.set(W - 20.0); mCombo->height.set(26.0);
        mCombo->onChange = [disp](int idx) { if (disp) disp->setShape(idx); };
        addChild(mCombo);
    }

    void OscillatorPanel::advance(double nowMs)
    {
        double dt = mLastMs < 0.0 ? 0.0 : (nowMs - mLastMs) / 1000.0;
        mLastMs = nowMs;
        if (dt > 0.0)
        {
            if (dt > 0.05) dt = 0.05;
            const double target = mMuted ? 0.0 : 1.0;
            mMuteFade += (target - mMuteFade) * (1.0 - std::exp(-dt / 0.12));
        }
        mDimAccent = scale(mAccent, 0.35 + 0.65 * mMuteFade);

        KnobStyle ks = mBaseKnob;
        ks.valueColor = mDimAccent;
        ks.indicatorColor = mDimAccent;
        for (auto &k : mKnobs) k->setStyle(ks);
        SliderStyle ss = mBaseSlider;
        ss.rangeFill.paint.fill = mDimAccent;
        ss.thumb.paint.fill = mDimAccent;
        ss.thumb.paint.stroke = mDimAccent;
        mPosSlider->setStyle(ss);
        mVoiceStepper->setColor(mDimAccent);
        mOctaveStepper->setColor(mDimAccent);
        mDisplay->setColor(mDimAccent);

        mDisplay->setPosition(mPosSlider->value());

        Segment::advance(nowMs);
    }

    void OscillatorPanel::onPaint(IRenderTarget &t) const
    {
        const double W = width.value(), H = height.value();
        drawRoundedRect(t, Rect{0, 0, W, H}, 10.0,
                        Paint::filledStroked(Color::hex(0x14161c), Color{mDimAccent.r, mDimAccent.g, mDimAccent.b, 0.35}, 1.0));
        drawRoundedRect(t, Rect{0, 0, W, 3.0}, 0.0, Paint::filled(mDimAccent));
        // faux-bold: overdraw with sub-pixel offsets to thicken the strokes (no HAL weight)
        t.setFill(mMuted ? Color{1, 1, 1, 0.3} : mDimAccent);
        for (double ox : {0.0, 0.6})
            for (double oy : {0.0, 0.5})
                t.drawText(mName, 34.0 + ox, 22.0 + oy, 15.0);

        // group backgrounds (NOT behind the octave box)
        const Color bg{1, 1, 1, 0.03};
        const double outX = kMargin + kPairW + kG; // OUTPUT group left
        drawRoundedRect(t, Rect{kMargin - 2, kColY - 2, kUnisonW + 4, kCellH + 4}, 8.0, Paint::filled(bg)); // UNISON
        drawRoundedRect(t, Rect{kMargin - 2, kRow2Y - 2, kPairW + 4, kCellH + 4}, 8.0, Paint::filled(bg)); // PHASE
        drawRoundedRect(t, Rect{outX - 2, kRow2Y - 2, kPairW + 4, kCellH + 4}, 8.0, Paint::filled(bg));     // OUTPUT
    }
}
}
