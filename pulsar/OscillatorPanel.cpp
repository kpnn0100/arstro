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
    }

    OscillatorPanel::OscillatorPanel(std::string name, const Theme &theme, const Color &accent)
        : mName(std::move(name)), mAccent(accent), mDimAccent(accent),
          mBaseKnob(theme.knob), mBaseSlider(theme.slider)
    {
        const double W = 300.0, H = 314.0;
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

        // POSITION timeline slider under the wave
        mPosSlider = std::make_shared<Slider>(mBaseSlider);
        mPosSlider->setValue(mPosition);
        mPosSlider->x.set(10.0); mPosSlider->y.set(160.0);
        mPosSlider->width.set(W - 20.0); mPosSlider->height.set(16.0);
        addChild(mPosSlider);

        auto makeKnob = [&](const char *lbl, double mn, double mx, double init, double *slot, bool isPhase) {
            auto k = std::make_shared<Knob>(mBaseKnob);
            k->label = lbl;
            k->setRange(mn, mx);
            k->setValue(init);
            k->width.set(62.0); k->height.set(54.0);
            k->onChange = [slot, isPhase, disp](double v) { *slot = v; if (isPhase) disp->setPhase(v); };
            mKnobs.push_back(k);
            return k;
        };

        // voice stepper (short, 2-digit box)
        mVoiceStepper = std::make_shared<Stepper>();
        mVoiceStepper->setColor(mAccent);
        mVoiceStepper->setRange(1, 16);
        mVoiceStepper->setValue(1);
        mVoiceStepper->setLabel("voice");
        mVoiceStepper->width.set(60.0); mVoiceStepper->height.set(54.0);
        mVoiceStepper->onChange = [this](int v) { mVoice = v; };

        // UNISON group (left of the top row): voice, detune, stereo
        mUnison = std::make_shared<Row>();
        mUnison->spacing = 4.0;
        mUnison->x.set(10.0); mUnison->y.set(188.0);
        mUnison->addChild(mVoiceStepper);
        mUnison->addChild(makeKnob("detune", 0, 1, 0.2, &mDetune, false));
        mUnison->addChild(makeKnob("stereo", 0, 1, 0.0, &mStereo, false));
        addChild(mUnison);

        // OCTAVE box — separate segment on the right of the top row
        mOctaveStepper = std::make_shared<Stepper>();
        mOctaveStepper->setColor(mAccent);
        mOctaveStepper->setRange(-4, 4);
        mOctaveStepper->setValue(0);
        mOctaveStepper->setLabel("oct");
        mOctaveStepper->width.set(60.0); mOctaveStepper->height.set(54.0);
        mOctaveStepper->x.set(W - 10.0 - 60.0); mOctaveStepper->y.set(188.0);
        mOctaveStepper->onChange = [this](int v) { mOctave = v; };
        addChild(mOctaveStepper);

        // bottom row: phase+random (left), pan+level (right)
        mPhaseGrp = std::make_shared<Row>();
        mPhaseGrp->spacing = 4.0;
        mPhaseGrp->x.set(10.0); mPhaseGrp->y.set(250.0);
        mPhaseGrp->addChild(makeKnob("phase", 0, 1, 0.0, &mPhase, true));
        mPhaseGrp->addChild(makeKnob("rnd", 0, 1, 0.0, &mRandom, false));
        addChild(mPhaseGrp);

        mOutGrp = std::make_shared<Row>();
        mOutGrp->spacing = 4.0;
        mOutGrp->x.set(W - 10.0 - 128.0); mOutGrp->y.set(250.0);
        mOutGrp->addChild(makeKnob("pan", 0, 1, 0.5, &mPan, false));
        mOutGrp->addChild(makeKnob("level", 0, 1, 0.8, &mLevel, false));
        addChild(mOutGrp);

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
        ss.thumb.paint.fill = mDimAccent;   // thumb shares the osc colour
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
        t.setFill(mMuted ? Color{1, 1, 1, 0.3} : mDimAccent);
        t.drawText(mName, 34.0, 22.0, 15.0);

        // faint group backgrounds
        const Rect groups[4] = {
            {6, 184, 200, 62}, {224, 184, 70, 62}, // UNISON, OCTAVE
            {6, 246, 136, 62}, {156, 246, 138, 62}}; // PHASE, OUTPUT
        for (const Rect &g : groups)
            drawRoundedRect(t, g, 8.0, Paint::filled(Color{1, 1, 1, 0.03}));
    }
}
}
