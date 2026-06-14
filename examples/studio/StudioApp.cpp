#include "StudioApp.h"
#include <cmath>

namespace arstro
{
namespace examples
{
    using namespace artboard;

    // ---- palette ----
    static const Color kBg = Color::hex(0x11141a);
    static const Color kPanel = Color::hex(0x1b1f29);
    static const Color kBorder = Color::hex(0x2a3040);
    static const Color kAccent = Color::hex(0x5cc8ff);
    static const Color kAccent2 = Color::hex(0xff8a5c);
    static const Color kMuted = Color::hex(0x8b94a7);

    // ---- layout (design size 900 x 520) ----
    static const Rect kScope{24, 64, 420, 180};
    static const Rect kSpec{24, 260, 420, 200};
    static const Rect kEnv{464, 64, 412, 180};
    static const Rect kKnobs{464, 260, 412, 96};
    static const Rect kKeysPanel{464, 372, 412, 88};

    static Color blend(const Color &a, const Color &b, double t)
    {
        return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1.0};
    }

    static std::shared_ptr<Rectangle> panel(const Rect &r)
    {
        return std::make_shared<Rectangle>(r, Paint::filledStroked(kPanel, kBorder, 1.0), 10.0);
    }
    static std::shared_ptr<Text> label(const std::string &s, double x, double y)
    {
        return std::make_shared<Text>(s, Point{x, y}, 12.0, kMuted);
    }

    StudioApp::StudioApp(double width, double height) : mW(width), mH(height), mBoard(Size{width, height})
    {
        AudioConfig::instance().setChannelCount(2);
        mRing.assign((size_t)kScope.w, 0.0f);
        for (auto &k : mKnob) k.set(0.0);
        for (auto &g : mKeyGlow) g.set(0.0);
        mBoard.setBackground(kBg);
        buildStaticScene();

        // Input: recognizer -> router; targets registered in buildStaticScene.
        mRecognizer.setSink([this](const artboard::Gesture &g) { mRouter.route(g); });
    }

    void StudioApp::applyKnob(int k, double v01)
    {
        if (v01 < 0) v01 = 0;
        if (v01 > 1) v01 = 1;
        mKnobTarget[k] = v01; // indicator is driven by the caller (snap on drag, animate on reset)
        switch (k)
        {
        case 0: mSynth.applyParam(GROUP_REVERB + RV_MIX, v01); break;
        case 1: mSynth.applyParam(GROUP_REVERB + RV_WIDTH, v01); break;
        case 2: mSynth.applyParam(GROUP_OVERDRIVE + OD_DRIVE, 1.0 + v01 * 10.0); break;
        case 3: mSynth.applyParam(GROUP_CHORUS + CH_MIX, v01); break;
        }
    }

    void StudioApp::pointer(int kind, double x, double y, int button, double timeMs)
    {
        RawPointer::Kind k = kind == 0 ? RawPointer::Kind::Down
                             : kind == 2 ? RawPointer::Kind::Up
                                         : RawPointer::Kind::Move;
        PointerButton b = button == 2 ? PointerButton::Right : PointerButton::Left;
        mNowMs = timeMs;
        mRecognizer.feed(RawPointer{k, Point{x, y}, b, timeMs});
    }

    double StudioApp::knobAngle(double v01) const
    {
        // 270° sweep with the gap at the bottom; angle in radians (y-down screen).
        return (135.0 + v01 * 270.0) * M_PI / 180.0;
    }

    void StudioApp::buildStaticScene()
    {
        // Title
        mBoard.add(std::make_shared<Text>("ARSTRO STUDIO", Point{24, 42}, 22.0, kAccent));
        mBoard.add(std::make_shared<Text>("DSP x Artboard", Point{210, 42}, 13.0, kMuted));

        // Panels + section labels
        mBoard.add(panel(kScope)); mBoard.add(label("OSCILLOSCOPE", kScope.x + 12, kScope.y + 20));
        mBoard.add(panel(kSpec));  mBoard.add(label("SPECTRUM", kSpec.x + 12, kSpec.y + 20));
        mBoard.add(panel(kEnv));   mBoard.add(label("ENVELOPE", kEnv.x + 12, kEnv.y + 20));
        mBoard.add(panel(kKnobs)); mBoard.add(label("MACRO", kKnobs.x + 12, kKnobs.y + 18));
        mBoard.add(panel(kKeysPanel)); mBoard.add(label("KEYS", kKeysPanel.x + 12, kKeysPanel.y + 18));

        // Oscilloscope: centre grid line + waveform polyline
        double oy = kScope.y + kScope.h * 0.6;
        mBoard.add(std::make_shared<Line>(Point{kScope.x + 12, oy}, Point{kScope.right() - 12, oy}, kBorder, 1.0));
        mWave = std::make_shared<Polyline>();
        mWave->paint = Paint::stroked(kAccent, 2.0);
        mBoard.add(mWave);

        // Spectrum: one bar per band
        double bx = kSpec.x + 14, bw = (kSpec.w - 28) / kBands;
        for (int i = 0; i < kBands; ++i)
        {
            auto bar = std::make_shared<Rectangle>(Rect{bx + i * bw + 1, kSpec.bottom() - 16, bw - 3, 2},
                                                   Paint::filled(kAccent), 2.0);
            mBars.push_back(bar);
            mBoard.add(bar);
        }

        // Envelope: translucent filled area (Polyline) + smooth spline stroke (Path)
        const double ex = kEnv.x + 16, ew = kEnv.w - 32;
        const double etop = kEnv.y + 40, ebot = kEnv.bottom() - 18;
        const double A = 0.12, D = 0.18, S = 0.6, R = 0.30; // ADSR (fractions of width)
        std::vector<Point> pts = {
            {ex, ebot},
            {ex + A * ew, etop},
            {ex + (A + D) * ew, ebot - S * (ebot - etop)},
            {ex + 0.70 * ew, ebot - S * (ebot - etop)},
            {ex + ew, ebot}};
        auto area = std::make_shared<Polyline>();
        area->points = pts; area->closed = true;
        area->paint = Paint::filled(Color{kAccent2.r, kAccent2.g, kAccent2.b, 0.18});
        mBoard.add(area);
        auto curve = std::make_shared<Path>();
        curve->paint = Paint::stroked(kAccent2, 2.0);
        curve->spline(pts);
        mBoard.add(curve);

        // Knobs: dial + arc track + indicator line + label
        const char *names[4] = {"REVERB", "WIDTH", "DRIVE", "CHORUS"};
        for (int i = 0; i < 4; ++i)
        {
            double cx = kKnobs.x + 70 + i * 95, cy = kKnobs.y + 56, r = 26;
            mBoard.add(std::make_shared<Ellipse>(Point{cx, cy}, r, r, Paint::filledStroked(kBg, kBorder, 2.0)));
            // arc track (sampled polyline along the 270° sweep)
            auto track = std::make_shared<Polyline>();
            for (int s = 0; s <= 24; ++s)
            {
                double a = knobAngle((double)s / 24.0);
                track->points.push_back(Point{cx + std::cos(a) * (r + 5), cy + std::sin(a) * (r + 5)});
            }
            track->paint = Paint::stroked(kBorder, 2.0);
            mBoard.add(track);
            auto ind = std::make_shared<Line>(Point{cx, cy}, Point{cx, cy}, kAccent, 3.0);
            mKnobInd[i] = ind;
            mBoard.add(ind);
            mBoard.add(std::make_shared<Text>(names[i], Point{cx - 24, cy + 44}, 11.0, kMuted));

            // Knob input: drag to set, double-click to reset, right-click to randomize.
            int k = i;
            double def = mKnobTarget[i];
            auto target = std::make_unique<RectTarget>(
                Rect{cx - 30, cy - 30, 60, 60}, [this, k, def](const Gesture &g) {
                    using T = Gesture::Type;
                    if (g.type == T::DragStart) mKnobBase[k] = mKnobTarget[k];
                    else if (g.type == T::Drag) { applyKnob(k, mKnobBase[k] + (g.start.y - g.pos.y) / 180.0); mKnob[k].set(mKnobTarget[k]); }
                    else if (g.type == T::DoubleClick) { applyKnob(k, def); mKnob[k].animateTo(def, 250.0, Easing::EaseOutQuad, mNowMs); }
                    else if (g.type == T::RightClick) { mRng = mRng * 1103515245u + 12345u; applyKnob(k, ((mRng >> 16) & 0x7fff) / 32767.0); mKnob[k].animateTo(mKnobTarget[k], 200.0, Easing::EaseOutQuad, mNowMs); }
                });
            mRouter.add(target.get());
            mTargets.push_back(std::move(target));
        }

        // Keyboard: 12 keys
        double kx = kKeysPanel.x + 12, kw = (kKeysPanel.w - 24) / kKeys;
        for (int i = 0; i < kKeys; ++i)
        {
            bool black = (i == 1 || i == 3 || i == 6 || i == 8 || i == 10);
            Rect kr{kx + i * kw + 1, kKeysPanel.y + 30, kw - 2, 48};
            auto key = std::make_shared<Rectangle>(kr, Paint::filledStroked(black ? kBg : kBorder, kBorder, 1.0), 3.0);
            mKeyRects[i] = key;
            mBoard.add(key);

            // Key input: press-and-hold plays the note (Down -> on, Up -> off).
            int midi = 60 + i;
            auto target = std::make_unique<RectTarget>(kr, [this, midi](const Gesture &g) {
                if (g.type == Gesture::Type::Down) noteOn(midi, 0.85);
                else if (g.type == Gesture::Type::Up) noteOff(midi);
            });
            mRouter.add(target.get());
            mTargets.push_back(std::move(target));
        }
    }

    void StudioApp::renderAudio(float *interleaved, int frames)
    {
        std::vector<double> blk;
        mSynth.renderBlockDouble(blk, frames);
        const int ch = AudioConfig::instance().channelCount();
        for (int i = 0; i < frames; ++i)
        {
            for (int c = 0; c < ch; ++c)
                interleaved[i * ch + c] = (float)blk[i * ch + c];
            mRing.erase(mRing.begin());
            mRing.push_back((float)blk[i * ch]);
        }
    }

    void StudioApp::updateAudioDriven()
    {
        // Oscilloscope: map ring to the scope panel.
        const double oy = kScope.y + kScope.h * 0.6, amp = kScope.h * 0.32;
        mWave->points.clear();
        for (size_t i = 0; i < mRing.size(); ++i)
        {
            double x = kScope.x + 12 + (double)i / (mRing.size() - 1) * (kScope.w - 24);
            mWave->points.push_back(Point{x, oy - mRing[i] * amp});
        }

        // Spectrum: naive DFT at log-spaced bands, with peak-hold decay.
        const int N = (int)mRing.size() < 512 ? (int)mRing.size() : 512;
        const double sr = AudioConfig::instance().sampleRate();
        const size_t off = mRing.size() - N;
        for (int b = 0; b < kBands; ++b)
        {
            double fHz = 80.0 * std::pow(8000.0 / 80.0, (double)b / (kBands - 1));
            double w = 2.0 * M_PI * fHz / sr;
            double re = 0, im = 0;
            for (int n = 0; n < N; ++n)
            {
                double s = mRing[off + n];
                re += s * std::cos(w * n);
                im -= s * std::sin(w * n);
            }
            double mag = std::sqrt(re * re + im * im) * 2.0 / N;
            double lvl = mag * 6.0; // display gain
            if (lvl > 1.0) lvl = 1.0;
            mSpec[b] = lvl > mSpec[b] ? lvl : mSpec[b] * 0.85; // attack instant, decay slow
            double h = 2.0 + mSpec[b] * (kSpec.h - 40);
            mBars[b]->rect.h = h;
            mBars[b]->rect.y = kSpec.bottom() - 16 - h;
            mBars[b]->paint.fill = blend(kAccent, kAccent2, mSpec[b]);
        }
    }

    void StudioApp::render(IRenderTarget &target, double nowMs)
    {
        mNowMs = nowMs;
        if (!mIntroStarted)
        {
            mIntroStarted = true;
            for (int i = 0; i < 4; ++i)
                mKnob[i].animateTo(mKnobTarget[i], 900.0, Easing::EaseInOutCubic, nowMs);
        }

        updateAudioDriven();

        // Knob indicators (intro-animated value).
        for (int i = 0; i < 4; ++i)
        {
            double v = mKnob[i].update(nowMs);
            double cx = kKnobs.x + 70 + i * 95, cy = kKnobs.y + 56;
            double a = knobAngle(v);
            mKnobInd[i]->a = Point{cx, cy};
            mKnobInd[i]->b = Point{cx + std::cos(a) * 24, cy + std::sin(a) * 24};
        }

        // Key glow (animated on note on/off).
        for (int i = 0; i < kKeys; ++i)
        {
            double g = mKeyGlow[i].update(nowMs);
            bool black = (i == 1 || i == 3 || i == 6 || i == 8 || i == 10);
            mKeyRects[i]->paint.fill = blend(black ? kBg : kBorder, kAccent, g);
        }

        mBoard.render(target, nowMs);
    }

    void StudioApp::noteOn(int midi, double vel)
    {
        mSynth.noteOn(midi, vel);
        int k = ((midi % 12) + 12) % 12;
        mKeyGlow[k].animateTo(1.0, 60.0, Easing::EaseOutQuad, mNowMs);
    }
    void StudioApp::noteOff(int midi)
    {
        mSynth.noteOff(midi);
        int k = ((midi % 12) + 12) % 12;
        mKeyGlow[k].animateTo(0.0, 400.0, Easing::EaseOutQuad, mNowMs);
    }
}
}
