#include "OscillatorPanel.h"
#include <cmath>
#include <functional>

namespace arstro
{
namespace pulsar
{
    using namespace artboard;

    namespace
    {
        Color scale(const Color &c, double f) { return Color{c.r * f, c.g * f, c.b * f, 1.0}; }

        // Geometry. Knobs sit on a grid; the inter-group gap (G) equals the inter-row
        // gap, so vertical and horizontal spacing match by construction.
        constexpr double kMargin = 10.0;
        constexpr double kCellW = 58.0, kCellH = 48.0, kCellGap = 6.0; // within a group
        constexpr double kG = OscillatorPanel::kSectionGap;            // group gap == row gap (tight)
        constexpr double kUnisonW = 4 * kCellW + 3 * kCellGap;         // voice+detune+blend+stereo
        constexpr double kPairW = 2 * kCellW + kCellGap;               // 2-knob group
        // vertical stack under the title
        constexpr double kComboY = 34.0, kComboH = 26.0;               // wavetable shape
        constexpr double kScapeY = 64.0, kScapeH = 120.0;              // 3D wavetable scape
        constexpr double kSliderY = 190.0, kSliderH = 16.0;            // POSITION timeline
        constexpr double kWarpY = 212.0, kWarpH = 24.0;                // WARP-mode selector
        constexpr double kColY = 244.0;                                // top of the knob grid
        constexpr double kRow2Y = kColY + kCellH + kG;
        // group-background x positions on row 2 (warp | phase | output)
        constexpr double kWarpX = kMargin;
        constexpr double kPhaseX = kMargin + kCellW + kG;
        constexpr double kOutX = kPhaseX + kPairW + kG;
    }

    OscillatorPanel::OscillatorPanel(std::string name, const Theme &theme, const Color &accent)
        : mName(std::move(name)), mAccent(accent), mDimAccent(accent),
          mBaseKnob(theme.knob), mBaseSlider(theme.slider)
    {
        const double rowW = kCellW + kG + kPairW + kG + kPairW; // row2 (warp|phase|output) is the widest
        const double W = kMargin + rowW + kMargin;
        const double H = kRow2Y + kCellH + 8.0;
        width.set(W);
        height.set(H);

        mMute = std::make_shared<MuteButton>();
        mMute->setColor(mAccent);
        mMute->x.set(10.0); mMute->y.set(7.0);
        mMute->onChange = [this](bool audioOn) { mMuted = !audioOn; };
        addChild(mMute);

        mDisplay = std::make_shared<WaveDisplay>();
        mDisplay->x.set(10.0); mDisplay->y.set(kScapeY);
        mDisplay->width.set(W - 20.0); mDisplay->height.set(kScapeH);
        mDisplay->setColor(mAccent);
        addChild(mDisplay);
        WaveDisplay *disp = mDisplay.get();

        mPosSlider = std::make_shared<Slider>(mBaseSlider);
        mPosSlider->setValue(mPosition);
        mPosSlider->setDefault(mPosition); // double-click recenters the wavetable position
        mPosSlider->x.set(10.0); mPosSlider->y.set(kSliderY);
        mPosSlider->width.set(W - 20.0); mPosSlider->height.set(kSliderH);
        addChild(mPosSlider);

        // A knob with an explicit onChange (handles the cross-wiring to the display).
        auto makeKnob = [&](const char *lbl, double init, std::function<void(double)> cb) {
            auto k = std::make_shared<Knob>(mBaseKnob);
            k->label = lbl;
            k->setRange(0.0, 1.0);
            k->setValue(init);
            k->setDefault(init); // double-click restores this nominal value
            k->width.set(kCellW); k->height.set(kCellH);
            k->onChange = std::move(cb);
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
        mVoiceStepper->onChange = [this](int v) { mVoice = v; syncUnison(); };
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

        // row 1: UNISON {voice, detune, blend, stereo} | OCTAVE
        auto unison = group(kUnisonW, {mVoiceStepper,
                                       makeKnob("detune", 0.2, [this](double v) { mDetune = v; syncUnison(); }),
                                       makeKnob("blend", 0.5, [this](double v) { mBlend = v; syncUnison(); }),
                                       makeKnob("stereo", 0.0, [this](double v) { mStereo = v; })});
        auto row1 = std::make_shared<Row>(); row1->spacing = kG;
        row1->addChild(unison);
        row1->addChild(mOctaveStepper);

        // row 2: WARP {warp} | PHASE {phase, rnd} | OUTPUT {pan, level}
        auto warpGrp = group(kCellW, {makeKnob("warp", 0.0, [this, disp](double v) { mWarpAmt = v; disp->setWarpAmount(v); })});
        auto phaseGrp = group(kPairW, {makeKnob("phase", 0.0, [this, disp](double v) { mPhase = v; disp->setPhase(v); }),
                                       makeKnob("rnd", 0.0, [this](double v) { mRandom = v; })});
        auto outGrp = group(kPairW, {makeKnob("pan", 0.5, [this](double v) { mPan = v; }),
                                     makeKnob("level", 0.8, [this](double v) { mLevel = v; })});
        auto row2 = std::make_shared<Row>(); row2->spacing = kG;
        row2->addChild(warpGrp);
        row2->addChild(phaseGrp);
        row2->addChild(outGrp);

        auto col = std::make_shared<Column>();
        col->spacing = kG; // inter-row offset == the inter-group gap
        col->x.set(kMargin); col->y.set(kColY);
        col->addChild(row1);
        col->addChild(row2);
        addChild(col);

        syncUnison();

        // selectors — added LAST so the open drop-downs draw on top of everything
        mWarpCombo = std::make_shared<ComboBox>(theme.combo);
        mWarpCombo->setOptions({"WARP: OFF", "SYNC", "BEND", "PWM", "MIRROR"});
        mWarpCombo->setSelectedIndex(WaveDisplay::WarpOff);
        mWarpCombo->x.set(10.0); mWarpCombo->y.set(kWarpY);
        mWarpCombo->width.set(W - 20.0); mWarpCombo->height.set(kWarpH);
        mWarpCombo->onChange = [disp](int idx) { if (disp) disp->setWarp(idx); };
        addChild(mWarpCombo);

        // wavetable shape selector — snaps the POSITION slider to a morph stop
        // (the slider remains the source of truth, fed to the display each frame).
        mCombo = std::make_shared<ComboBox>(theme.combo);
        mCombo->setOptions({"SINE", "TRI", "SAW", "SQUARE"});
        mCombo->setSelectedIndex(0);
        mCombo->x.set(10.0); mCombo->y.set(kComboY);
        mCombo->width.set(W - 20.0); mCombo->height.set(kComboH);
        Slider *slider = mPosSlider.get();
        mCombo->onChange = [slider](int idx) { slider->setValue(idx / 3.0); };
        addChild(mCombo);
    }

    void OscillatorPanel::syncUnison()
    {
        if (mDisplay) mDisplay->setUnison(mVoice, mDetune, mBlend);
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
        // Quiet chassis matching Chrome.cpp: one neutral surface + a single hairline.
        drawRoundedRect(t, Rect{0, 0, W, H}, 12.0,
                        Paint::filledStroked(Color::hex(0x141824), Color{1, 1, 1, 0.07}, 1.0));
        // molded top-edge highlight
        t.beginPath(); t.moveTo(13.0, 1.0); t.lineTo(W - 13.0, 1.0);
        t.setStroke(Color{1, 1, 1, 0.05}, 1.0); t.strokePath();

        // title: neutral ink, single pass (no faux-bold overdraw); leaves room for the mute toggle
        t.setFill(mMuted ? Color{1, 1, 1, 0.28} : Color::hex(0xdfe4ee));
        t.drawText(mName, 34.0, 23.0, 13.0);
        // short accent tab — identity signal, dims with mute
        drawRoundedRect(t, Rect{34.0, 30.0, 20.0, 2.0}, 1.0, Paint::filled(mDimAccent));

        // group backgrounds (NOT behind the octave box)
        const Color bg{1, 1, 1, 0.02};
        drawRoundedRect(t, Rect{kMargin - 2, kColY - 2, kUnisonW + 4, kCellH + 4}, 8.0, Paint::filled(bg)); // UNISON
        drawRoundedRect(t, Rect{kWarpX - 2, kRow2Y - 2, kCellW + 4, kCellH + 4}, 8.0, Paint::filled(bg));   // WARP
        drawRoundedRect(t, Rect{kPhaseX - 2, kRow2Y - 2, kPairW + 4, kCellH + 4}, 8.0, Paint::filled(bg));  // PHASE
        drawRoundedRect(t, Rect{kOutX - 2, kRow2Y - 2, kPairW + 4, kCellH + 4}, 8.0, Paint::filled(bg));    // OUTPUT
    }
}
}
