#include "OscillatorPanel.h"
#include <cmath>

namespace arstro
{
namespace pulsar
{
    using namespace artboard;

    namespace
    {
        struct KnobDef { const char *label; double min, max, init; double x, y; };
        // Grouped layout: UNISON {detune, stereo}, OUTPUT {pan, level}, PHASE {phase, random}.
        // (voice is a Stepper, handled separately; position is a slider under the wave.)
        enum { Detune = 0, Stereo, Pan, Level, Phase, Random, KnobCount };
        const KnobDef kDefs[KnobCount] = {
            {"detune", 0, 1, 0.2, 103, 186}, {"stereo", 0, 1, 0.0, 196, 186},
            {"pan", 0, 1, 0.5, 10, 252},     {"level", 0, 1, 0.8, 103, 252},
            {"phase", 0, 1, 0.0, 10, 318},   {"rnd", 0, 1, 0.0, 103, 318}};

        Color scale(const Color &c, double f) { return Color{c.r * f, c.g * f, c.b * f, 1.0}; }
    }

    OscillatorPanel::OscillatorPanel(std::string name, const Theme &theme, const Color &accent)
        : mName(std::move(name)), mAccent(accent), mDimAccent(accent),
          mBaseKnob(theme.knob), mBaseSlider(theme.slider)
    {
        const double W = 300.0, H = 384.0;
        width.set(W);
        height.set(H);

        // mute square (left of name); default checked = audio on
        mMute = std::make_shared<MuteButton>();
        mMute->setColor(mAccent);
        mMute->x.set(10.0); mMute->y.set(7.0);
        mMute->onChange = [this](bool audioOn) { mMuted = !audioOn; };
        addChild(mMute);

        // waveform display (below the selector)
        mDisplay = std::make_shared<WaveDisplay>();
        mDisplay->x.set(10.0); mDisplay->y.set(64.0);
        mDisplay->width.set(W - 20.0); mDisplay->height.set(92.0);
        mDisplay->setColor(mAccent);
        mDisplay->setShape(WaveDisplay::Morph3D);
        addChild(mDisplay);
        WaveDisplay *disp = mDisplay.get();

        // POSITION as a long slider beneath the wave (timeline)
        mPosSlider = std::make_shared<Slider>(mBaseSlider);
        mPosSlider->setValue(mPosition);
        mPosSlider->x.set(10.0); mPosSlider->y.set(160.0);
        mPosSlider->width.set(W - 20.0); mPosSlider->height.set(16.0);
        addChild(mPosSlider);

        // UNISON: voice stepper
        mVoiceStepper = std::make_shared<Stepper>();
        mVoiceStepper->setColor(mAccent);
        mVoiceStepper->setRange(1, 16);
        mVoiceStepper->setValue(1);
        mVoiceStepper->setLabel("voice");
        mVoiceStepper->x.set(10.0); mVoiceStepper->y.set(186.0);
        mVoiceStepper->onChange = [this](int v) { mVoice = v; };
        addChild(mVoiceStepper);

        // the six knobs
        double *store[KnobCount] = {&mDetune, &mStereo, &mPan, &mLevel, &mPhase, &mRandom};
        for (int i = 0; i < KnobCount; ++i)
        {
            auto k = std::make_shared<Knob>(mBaseKnob);
            k->label = kDefs[i].label;
            k->setRange(kDefs[i].min, kDefs[i].max);
            k->setValue(kDefs[i].init);
            k->x.set(kDefs[i].x); k->y.set(kDefs[i].y);
            k->width.set(90.0); k->height.set(54.0);
            double *slot = store[i];
            const bool isPhase = (i == Phase);
            k->onChange = [slot, isPhase, disp](double v) {
                *slot = v;
                if (isPhase) disp->setPhase(v); // editing phase shifts the wave
            };
            mKnobs.push_back(k);
            addChild(k);
        }

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
        // accent eases between full and dim with the mute fade
        mDimAccent = scale(mAccent, 0.35 + 0.65 * mMuteFade);

        // restyle the accent-bearing children to the eased colour
        KnobStyle ks = mBaseKnob;
        ks.valueColor = mDimAccent;
        ks.indicatorColor = mDimAccent;
        for (auto &k : mKnobs) k->setStyle(ks);
        SliderStyle ss = mBaseSlider;
        ss.rangeFill.paint.fill = mDimAccent;
        ss.thumb.paint.stroke = mDimAccent;
        mPosSlider->setStyle(ss);
        mVoiceStepper->setColor(mDimAccent);
        mDisplay->setColor(mDimAccent);

        // POSITION slider drives the (3D) morph table position
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
        // faint group backgrounds (UNISON / OUTPUT / PHASE)
        const double gy[3] = {184.0, 250.0, 316.0};
        for (double y : gy)
            drawRoundedRect(t, Rect{8.0, y, W - 16.0, 60.0}, 8.0, Paint::filled(Color{1, 1, 1, 0.03}));
    }
}
}
