#include "SynthApp.h"
#include <cmath>

namespace arstro
{
namespace examples
{
    using namespace artboard;

    // ---- palette ----
    static const Color kBg = Color::hex(0x0c0e13);
    static const Color kPanel = Color::hex(0x161a23);
    static const Color kBorder = Color::hex(0x2a3040);
    static const Color kAccent = Color::hex(0x5cc8ff);
    static const Color kAccent2 = Color::hex(0xff8a5c);
    static const Color kInk = Color::hex(0xe6e9ef);
    static const Color kMuted = Color::hex(0x8b94a7);
    static const Color kSurface = Color::hex(0x222838);
    static const Color kWhiteKey = Color::hex(0x222838);
    static const Color kBlackKey = Color::hex(0x10131b);

    static Color withAlpha(const Color &c, double a) { return Color{c.r, c.g, c.b, a}; }
    static Color mix(const Color &a, const Color &b, double t)
    {
        return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0};
    }

    // Theme tuned to the synth's blue palette (built once in the ctor).
    static Theme makeSynthTheme()
    {
        Theme th = Theme::basicTheme();
        th.knob.dial = {Paint::filledStroked(kBg, kBorder, 2.0), 999.0};
        th.knob.trackColor = kSurface;
        th.knob.valueColor = kAccent;
        th.knob.indicatorColor = kAccent;
        th.knob.label = {kMuted, 11.0};

        th.toggle.trackOff = {Paint::filledStroked(kSurface, kBorder, 1.0), 999.0};
        th.toggle.trackOn = {Paint::filledStroked(kAccent, kAccent, 1.0), 999.0};
        th.toggle.thumb = {Paint::filledStroked(kInk, kBorder, 1.0), 999.0};

        th.progress.track = {Paint::filledStroked(kSurface, kBorder, 1.0), 6.0};
        th.progress.fill = {Paint::filled(kAccent), 6.0};

        th.combo.field = {Paint::filledStroked(kPanel, kBorder, 1.0), 8.0};
        th.combo.popup = {Paint::filledStroked(kPanel, kAccent, 1.0), 8.0};
        th.combo.rowSelected = {Paint::filled(Color::hex(0x33405a)), 0.0};
        th.combo.text = {kInk, 14.0};
        th.combo.caretColor = kAccent;

        th.tab.tabIdle = {Paint::filledStroked(kSurface, kBorder, 1.0), 8.0};
        th.tab.tabActive = {Paint::filledStroked(kAccent, kBorder, 1.0), 8.0};
        th.tab.label = {kInk, 13.0};

        th.scroll.viewport = {Paint::filledStroked(kPanel, kBorder, 1.0), 8.0};
        th.scroll.track = {Paint::filled(kSurface), 4.0};
        th.scroll.thumb = {Paint::filled(kAccent), 4.0};

        th.graph.background = {Paint::filledStroked(kPanel, kBorder, 1.0), 8.0};
        th.graph.gridColor = kSurface;
        th.graph.lineColor = kAccent;
        th.graph.fillColor = withAlpha(kAccent, 0.16);
        th.graph.lineWidth = 2.0;

        th.label = {kInk, 14.0};
        return th;
    }

    static bool isBlackKey(int i) { return i == 1 || i == 3 || i == 6 || i == 8 || i == 10; }

    // ───────────────────────── KeyboardSegment ─────────────────────────
    KeyboardSegment::KeyboardSegment()
    {
        width.set(860.0);
        height.set(92.0);
        for (auto &g : mGlow)
            g.set(0.0);
    }

    int KeyboardSegment::keyIndexAt(const Point &local) const
    {
        const double pad = 8.0;
        const double kw = (width.value() - 2 * pad) / kKeys;
        if (local.y < 24.0 || local.y > height.value() - 6.0)
            return -1;
        const int i = (int)((local.x - pad) / kw);
        return (i >= 0 && i < kKeys) ? i : -1;
    }

    void KeyboardSegment::glow(int midi, bool on)
    {
        const int idx = midi - baseMidi;
        if (idx < 0 || idx >= kKeys)
            return;
        mGlow[idx].animateTo(on ? 1.0 : 0.0, on ? 70.0 : 420.0, Easing::EaseOutQuad, mNowMs);
    }

    void KeyboardSegment::advance(double nowMs)
    {
        mNowMs = nowMs;
        for (auto &g : mGlow)
            g.update(nowMs);
        Segment::advance(nowMs);
    }

    void KeyboardSegment::onPaint(IRenderTarget &t) const
    {
        const double pad = 8.0;
        const double kw = (width.value() - 2 * pad) / kKeys;
        const double top = 24.0;
        const double kh = height.value() - top - 6.0;
        for (int i = 0; i < kKeys; ++i)
        {
            const Color base = isBlackKey(i) ? kBlackKey : kWhiteKey;
            const Color fill = mix(base, kAccent, mGlow[i].value());
            drawRoundedRect(t, Rect{pad + i * kw + 1.0, top, kw - 2.0, kh},
                            4.0, Paint::filledStroked(fill, kBorder, 1.0));
        }
        t.setFill(kMuted);
        t.drawText("A W S E D F T G Y H U J K", pad, 16.0, 11.0);
    }

    bool KeyboardSegment::handleGesture(const Gesture &g, const Point &localPoint)
    {
        using T = Gesture::Type;
        if (g.type == T::Down)
        {
            const int idx = keyIndexAt(localPoint);
            if (idx >= 0)
            {
                mMouseNote = baseMidi + idx;
                if (onNoteOn)
                    onNoteOn(mMouseNote);
            }
            return true;
        }
        if (g.type == T::Up || g.type == T::Drop)
        {
            if (mMouseNote >= 0)
            {
                if (onNoteOff)
                    onNoteOff(mMouseNote);
                mMouseNote = -1;
            }
            return true;
        }
        return Segment::handleGesture(g, localPoint);
    }

    // ───────────────────────────── SynthApp ─────────────────────────────
    SynthApp::SynthApp(double width, double height)
        : mW(width), mH(height), mTheme(makeSynthTheme())
    {
        AudioConfig::instance().setChannelCount(2);
        mRing.assign(4096, 0.0f);
        mReveal.set(0.0);
        buildUi();
        mRecognizer.setSink([this](const Gesture &g) { mRoot->onGesture(g); });
    }

    void SynthApp::applyPreset(int index)
    {
        struct P { double drive, chorus, reverb; };
        static const P presets[3] = {{1.0, 0.10, 0.20}, {4.0, 0.30, 0.35}, {2.0, 0.60, 0.70}};
        const P &p = presets[index < 0 || index > 2 ? 0 : index];
        mSynth.pushParam(GROUP_OVERDRIVE + OD_DRIVE, p.drive);
        mSynth.pushParam(GROUP_CHORUS + CH_MIX, p.chorus);
        mSynth.pushParam(GROUP_REVERB + RV_MIX, p.reverb);
    }

    void SynthApp::buildUi()
    {
        mRoot = std::make_shared<Segment>();
        mRoot->width.set(mW);
        mRoot->height.set(mH);

        auto bg = std::make_shared<RectangleSegment>();
        bg->style = {Paint::filled(kBg), 0.0};
        bg->width.set(mW);
        bg->height.set(mH);
        bg->inputTransparent = true;
        mRoot->addChild(bg);

        auto title = std::make_shared<LabelSegment>();
        title->text = "ARSTRO SYNTH";
        title->style = {kAccent, 22.0};
        title->x.set(20.0); title->y.set(16.0);
        title->width.set(260.0); title->height.set(28.0);
        mRoot->addChild(title);

        // Oscilloscope + spectrum (LineGraph) and a level meter (ProgressBar).
        mScope = std::make_shared<LineGraph>(mTheme.graph);
        mScope->x.set(20.0); mScope->y.set(54.0);
        mScope->width.set(540.0); mScope->height.set(152.0);
        mScope->setRange(-1.0, 1.0);
        mScope->setGridLines(4);
        mRoot->addChild(mScope);

        mMeter = std::make_shared<ProgressBar>(mTheme.progress);
        mMeter->x.set(20.0); mMeter->y.set(214.0);
        mMeter->width.set(540.0); mMeter->height.set(12.0);
        mRoot->addChild(mMeter);

        mSpectrum = std::make_shared<LineGraph>(mTheme.graph);
        mSpectrum->x.set(20.0); mSpectrum->y.set(236.0);
        mSpectrum->width.set(540.0); mSpectrum->height.set(120.0);
        mSpectrum->setRange(0.0, 1.0);
        mSpectrum->setGridLines(3);
        mRoot->addChild(mSpectrum);

        // TabView: TONE / FX / HELP.
        auto tabs = std::make_shared<TabView>(mTheme.tab);
        tabs->x.set(580.0); tabs->y.set(54.0);
        tabs->width.set(300.0); tabs->height.set(302.0);
        tabs->tabHeight = 30.0;

        auto label = [&](const std::string &s, double x, double y, double size, const Color &c) {
            auto l = std::make_shared<LabelSegment>();
            l->text = s; l->style = {c, size};
            l->x.set(x); l->y.set(y); l->width.set(200.0); l->height.set(size + 4.0);
            return l;
        };

        // -- TONE page --
        auto tone = std::make_shared<Segment>();
        tone->width.set(300.0); tone->height.set(260.0);
        auto driveKnob = std::make_shared<Knob>(mTheme.knob);
        driveKnob->label = "DRIVE";
        driveKnob->setValue(0.3);
        driveKnob->x.set(20.0); driveKnob->y.set(12.0);
        driveKnob->onChange = [this](double v) { mSynth.pushParam(GROUP_OVERDRIVE + OD_DRIVE, 1.0 + v * 10.0); };
        tone->addChild(driveKnob);
        tone->addChild(label("PRESET", 110.0, 16.0, 11.0, kMuted));
        auto preset = std::make_shared<ComboBox>(mTheme.combo);
        preset->setOptions({"Clean", "Warm", "Space"});
        preset->x.set(110.0); preset->y.set(30.0);
        preset->width.set(170.0); preset->height.set(30.0);
        preset->onChange = [this](int i) { applyPreset(i); };
        tone->addChild(preset);
        tone->addChild(label("DRIVE FX", 110.0, 92.0, 11.0, kMuted));
        auto driveFx = std::make_shared<ToggleSwitch>(mTheme.toggle);
        driveFx->setOn(true);
        driveFx->x.set(110.0); driveFx->y.set(104.0);
        driveFx->onChange = [this](bool on) { mSynth.pushParam(GROUP_OVERDRIVE + FX_BYPASS, on ? 0.0 : 1.0); };
        tone->addChild(driveFx);
        tabs->addPage("TONE", tone);

        // -- FX page --
        auto fx = std::make_shared<Segment>();
        fx->width.set(300.0); fx->height.set(260.0);
        auto chorusKnob = std::make_shared<Knob>(mTheme.knob);
        chorusKnob->label = "CHORUS";
        chorusKnob->setValue(0.25);
        chorusKnob->x.set(20.0); chorusKnob->y.set(12.0);
        chorusKnob->onChange = [this](double v) { mSynth.pushParam(GROUP_CHORUS + CH_MIX, v); };
        fx->addChild(chorusKnob);
        auto reverbKnob = std::make_shared<Knob>(mTheme.knob);
        reverbKnob->label = "REVERB";
        reverbKnob->setValue(0.3);
        reverbKnob->x.set(110.0); reverbKnob->y.set(12.0);
        reverbKnob->onChange = [this](double v) { mSynth.pushParam(GROUP_REVERB + RV_MIX, v); };
        fx->addChild(reverbKnob);
        fx->addChild(label("REVERB FX", 20.0, 104.0, 11.0, kMuted));
        auto reverbFx = std::make_shared<ToggleSwitch>(mTheme.toggle);
        reverbFx->setOn(true);
        reverbFx->x.set(20.0); reverbFx->y.set(116.0);
        reverbFx->onChange = [this](bool on) { mSynth.pushParam(GROUP_REVERB + FX_BYPASS, on ? 0.0 : 1.0); };
        fx->addChild(reverbFx);
        tabs->addPage("FX", fx);

        // -- HELP page (ScrollView of text rows) --
        auto help = std::make_shared<Segment>();
        help->width.set(300.0); help->height.set(260.0);
        auto scroll = std::make_shared<ScrollView>(mTheme.scroll);
        scroll->x.set(8.0); scroll->y.set(8.0);
        scroll->width.set(284.0); scroll->height.set(244.0);
        auto content = std::make_shared<Segment>();
        content->width.set(264.0);
        const char *lines[] = {
            "ARSTRO BASIC SYNTH", "",
            "Keys A W S E D F T G Y H U J K",
            "play one octave (C..C).", "",
            "TONE tab: DRIVE knob, PRESET",
            "drop-down, and a DRIVE FX toggle.", "",
            "FX tab: CHORUS and REVERB knobs",
            "plus a REVERB FX toggle.", "",
            "Drag a knob vertically to change it.",
            "Double-click a knob to reset.", "",
            "The scope + spectrum are LineGraphs;",
            "the level bar is a ProgressBar;",
            "this list is a ScrollView.", "",
            "Everything is drawn by Artboard;",
            "the sound is Arstro DSP."};
        int n = (int)(sizeof(lines) / sizeof(lines[0]));
        for (int i = 0; i < n; ++i)
            content->addChild(label(lines[i], 10.0, 8.0 + i * 20.0, 12.0, i == 0 ? kAccent : kInk));
        content->height.set(8.0 + n * 20.0);
        scroll->setContent(content);
        scroll->setContentHeight(8.0 + n * 20.0);
        help->addChild(scroll);
        tabs->addPage("HELP", help);

        mRoot->addChild(tabs);

        // Keyboard.
        mKeyboard = std::make_shared<KeyboardSegment>();
        mKeyboard->x.set(20.0); mKeyboard->y.set(372.0);
        mKeyboard->width.set(860.0); mKeyboard->height.set(92.0);
        mKeyboard->onNoteOn = [this](int m) { noteOn(m, 0.85); };
        mKeyboard->onNoteOff = [this](int m) { noteOff(m); };
        mRoot->addChild(mKeyboard);

        // Panel frame behind the keyboard for a clean look.
        applyPreset(0);
    }

    void SynthApp::pointer(int kind, double x, double y, int button, double timeMs)
    {
        RawPointer::Kind k = kind == 0 ? RawPointer::Kind::Down
                             : kind == 2 ? RawPointer::Kind::Up
                                         : RawPointer::Kind::Move;
        PointerButton b = button == 2 ? PointerButton::Right : PointerButton::Left;
        mNowMs = timeMs;
        mRecognizer.feed(RawPointer{k, Point{x, y}, b, timeMs});
    }

    void SynthApp::renderAudio(float *interleaved, int frames)
    {
        std::vector<double> blk;
        mSynth.renderBlockDouble(blk, frames);
        const int ch = AudioConfig::instance().channelCount();
        std::lock_guard<std::mutex> lock(mScopeMutex);
        for (int i = 0; i < frames; ++i)
        {
            for (int c = 0; c < ch; ++c)
                interleaved[i * ch + c] = (float)blk[i * ch + c];
            mRing[mRingHead] = (float)blk[i * ch];
            mRingHead = (mRingHead + 1) % mRing.size();
        }
    }

    void SynthApp::render(IRenderTarget &target, double nowMs)
    {
        mNowMs = nowMs;
        if (!mIntroStarted)
        {
            mIntroStarted = true;
            mReveal.animate(Tween::range(0.0, 1.0, 600.0).withEasing(Easing::EaseOutCubic), nowMs);
        }
        mReveal.update(nowMs);
        mAnimator.advance(nowMs);
        mRoot->advance(nowMs);

        // Snapshot the scope ring + compute level and spectrum.
        std::vector<float> snap(mRing.size());
        {
            std::lock_guard<std::mutex> lock(mScopeMutex);
            const size_t n = mRing.size();
            for (size_t i = 0; i < n; ++i)
                snap[i] = mRing[(mRingHead + i) % n];
        }
        const size_t total = snap.size();

        // Oscilloscope: last kScopePoints samples.
        std::vector<double> scope(kScopePoints);
        double peak = 0.0;
        for (int i = 0; i < kScopePoints; ++i)
        {
            const float s = snap[total - kScopePoints + i];
            scope[i] = s * mReveal.value();
            const double a = s < 0 ? -s : s;
            if (a > peak) peak = a;
        }
        mScope->setSeries(scope);
        mLevel += (peak - mLevel) * 0.2;
        mMeter->setValue(mLevel);

        // Spectrum: naive DFT at log-spaced bands.
        const int N = 512;
        const double sr = AudioConfig::instance().sampleRate();
        const size_t off = total - N;
        std::vector<double> bands(kBands);
        for (int b = 0; b < kBands; ++b)
        {
            const double fHz = 80.0 * std::pow(8000.0 / 80.0, (double)b / (kBands - 1));
            const double w = 2.0 * M_PI * fHz / sr;
            double re = 0, im = 0;
            for (int k = 0; k < N; ++k)
            {
                const double s = snap[off + k];
                re += s * std::cos(w * k);
                im -= s * std::sin(w * k);
            }
            double mag = std::sqrt(re * re + im * im) * 2.0 / N * 6.0;
            if (mag > 1.0) mag = 1.0;
            mSpec[b] = mag > mSpec[b] ? mag : mSpec[b] * 0.85;
            bands[b] = mSpec[b] * mReveal.value();
        }
        mSpectrum->setSeries(bands);

        mRoot->render(target);
    }

    void SynthApp::noteOn(int midi, double vel)
    {
        mSynth.pushNoteOn(midi, vel);
        if (mKeyboard)
            mKeyboard->glow(midi, true);
    }

    void SynthApp::noteOff(int midi)
    {
        mSynth.pushNoteOff(midi);
        if (mKeyboard)
            mKeyboard->glow(midi, false);
    }
}
}
