/*
 *  Arstro Synth — a full-function compact synth whose UI follows the supplied
 *  React design: a 536x240 "module" with a status bar, five colour-coded pages
 *  (HOME / OSC / ENV / FX / SET), a bottom nav, draggable knobs, and animated
 *  per-page visualisers. Every control drives the live DSP SynthEngine.
 *
 *  The UI is drawn immediate-mode in a fixed 536x240 design space that is scaled
 *  to fill whatever surface the host provides; pointer coordinates are mapped back
 *  into design space. Platform-free: only Artboard's IRenderTarget + the DSP engine.
 *
 *  Threading: note/param input is queued lock-free into the engine, so the UI
 *  thread may drive it while a separate audio thread calls renderAudio().
 */
#pragma once
#include "../../core/Artboard/include/artboard/artboard.h"
#include "../../core/DigitalSignalProcessing/src/synth_dsp.h"
#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace arstro
{
namespace examples
{
    class SynthApp
    {
    public:
        SynthApp(double width, double height);

        void setSampleRate(double sr) { AudioConfig::instance().setSampleRate(sr); }
        void renderAudio(float *interleaved, int frames);          // audio thread
        void render(artboard::IRenderTarget &target, double nowMs); // UI thread

        // UI thread input.
        void key(int code, bool down);   // letters play notes; arrows navigate
        void pointer(int kind, double x, double y, int button, double timeMs);

        // Kept for hosts that still call them directly.
        void noteOn(int midi, double vel);
        void noteOff(int midi);

    private:
        // ---- design-space layout ----
        static constexpr double DW = 536.0, DH = 240.0;
        static constexpr double kStatusH = 24.0, kNavH = 28.0;

        enum Page { Home = 0, Osc, Env, Fx, Set, PageCount };

        struct KnobRef
        {
            double cx, cy, r;
            double *value;   // 0..max
            double max;
            std::string label;
            std::string unit;
        };

        // ---- transforms / hit-testing ----
        artboard::Transform designToScreen() const;
        artboard::Point toDesign(double sx, double sy) const;
        std::vector<KnobRef> pageKnobs();      // interactive knobs for the active page
        void selectPage(int p);
        // The smoothed (spring) display value of a knob-driven parameter, so visualisers
        // animate in lockstep with the knob instead of snapping to the raw target.
        double knobDisplay(const double *v) const;

        // ---- DSP application ----
        void applyMaster();
        void applyOsc();
        void applyEnv();
        void applyFx(int i);
        void applyAll();

        // ---- drawing ----
        void drawStatusBar(artboard::IRenderTarget &t, double frame);
        void drawNav(artboard::IRenderTarget &t);
        void drawPage(artboard::IRenderTarget &t, double frame); // dispatches the active page
        void drawHome(artboard::IRenderTarget &t, double x0, double frame);
        void drawOsc(artboard::IRenderTarget &t, double x0, double frame);
        void drawEnv(artboard::IRenderTarget &t, double x0, double frame);
        void drawFx(artboard::IRenderTarget &t, double x0, double frame);
        void drawSet(artboard::IRenderTarget &t, double x0);
        void drawKnob(artboard::IRenderTarget &t, double cx, double cy, double r,
                      double *value, double max, const artboard::Color &color, const std::string &label, bool active);
        void drawValueOverlay(artboard::IRenderTarget &t, double value, const std::string &unit,
                              const artboard::Color &color, double opacity);

        double mW, mH;
        arstro::SynthEngine mSynth;
        double mNowMs = 0.0;
        double mLastMs = -1.0, mDt = 0.0; // frame delta (seconds) for spring/ease integration

        // Per-knob UI animation: a critically-damped spring chases the target value (so the
        // visual eases with smooth, never-jumping velocity even when the drag reverses), plus
        // an eased focus highlight. Keyed by the parameter pointer the knob drives.
        struct KnobAnim { double display = 0, vel = 0, highlight = 0, scale = 1.0; bool init = false; };
        std::unordered_map<const double *, KnobAnim> mKnobAnim;

        // Global accent colour eased toward the current page / selected-FX colour, so colour
        // changes (page switch, FX tab) glide instead of snapping.
        artboard::Color mAccent;
        bool mAccentInit = false;

        // Value-overlay (the big centre number) fade in/out.
        double mOverlayAmt = 0.0;
        double mOverlayValue = 0.0;
        std::string mOverlayUnit;
        artboard::Color mOverlayColor;

        // host surface mapping
        double mScale = 1.0, mOffX = 0.0, mOffY = 0.0;

        // page + transition
        int mPage = Home;
        int mPrevPage = Home;
        int mSlideDir = 1;                       // +1 sliding right, -1 left
        artboard::AnimatedProperty mSlide{1.0};  // 0 = mid-slide, 1 = settled
        bool mMidiBlink = false;
        double mBlinkAccum = 0.0;

        // ---- parameter state (0..max in UI units) ----
        double mVol = 75, mPan = 50;
        int mWave = 1;                 // 0 Sine 1 Saw 2 Sqr 3 Tri (DSP plays Saw)
        double mLevel = 80, mDetune = 12, mSpread = 30, mVoices = 3;
        double mAttack = 10, mDecay = 30, mSustain = 70, mRelease = 40;
        int mFxSel = 0;
        std::array<std::array<double, 3>, 5> mFx{{{65, 40, 20}, {35, 50, 40}, {80, 40, 30}, {70, 35, 25}, {60, 60, 40}}};
        std::array<bool, 5> mFxBypass{{false, true, false, false, false}};
        int mSetSel = 0;

        // ---- audio-driven meters/scope ----
        std::mutex mAudioMutex;
        double mMeterL = 0.0, mMeterR = 0.0;
        std::array<float, 512> mScope{}; // recent ch0 samples for live overlay
        size_t mScopeHead = 0;

        // ---- interaction ----
        struct Drag { bool active = false; double *value = nullptr; double max = 1; double startY = 0, startVal = 0; } mDrag;
        int mActiveKnob = -1; // index into pageKnobs() while dragging (for overlay)
        std::vector<int> mHeldNotes;
        artboard::Animator mAnimator;
        bool mIntroDone = false;
    };
}
}
