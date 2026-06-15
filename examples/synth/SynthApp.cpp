#include "SynthApp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace examples
{
    using namespace artboard;

    namespace
    {
        constexpr double kPi = 3.14159265358979323846;

        // Per-page accent colours (from the React design).
        const Color kPageColor[5] = {
            Color::hex(0xff3b5c), Color::hex(0x00d4ff), Color::hex(0xbf5af2),
            Color::hex(0xffd600), Color::hex(0x00ff88)};
        const char *kPageName[5] = {"HOME", "OSC", "ENV", "FX", "SET"};

        const Color kBg = Color::hex(0x111111);
        const Color kInk = Color::hex(0xffffff);

        Color a(const Color &c, double alpha) { return Color{c.r, c.g, c.b, alpha}; }
        Color white(double alpha) { return Color{1, 1, 1, alpha}; }
        Color mixCol(const Color &x, const Color &y, double t)
        {
            return Color{x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t,
                         x.a + (y.a - x.a) * t};
        }

        void textAt(IRenderTarget &t, const std::string &s, double x, double base, double size, const Color &c)
        {
            t.setFill(c);
            t.drawText(s, x, base, size);
        }
        void textCentered(IRenderTarget &t, const std::string &s, double cx, double base, double size, const Color &c)
        {
            textAt(t, s, cx - s.size() * size * 0.28, base, size, c);
        }

        // Critically-damped spring step: x chases target with continuous (never-jumping)
        // velocity, so reversing the drag decelerates and reverses smoothly — no competing
        // tweens, no instant velocity change.
        void springStep(double &x, double &v, double target, double dt, double omega)
        {
            if (dt <= 0.0) return;
            if (dt > 0.05) dt = 0.05; // clamp big frame gaps
            const double accel = -2.0 * omega * v - omega * omega * (x - target);
            v += accel * dt;
            x += v * dt;
        }
        // Exponential ease toward target with time-constant tau (s) — used for fades.
        void easeStep(double &x, double target, double dt, double tau)
        {
            if (dt <= 0.0 || tau <= 0.0) { x = target; return; }
            x += (target - x) * (1.0 - std::exp(-dt / tau));
        }

        void strokePts(IRenderTarget &t, const std::vector<Point> &p, const Color &c, double w)
        {
            if (p.size() < 2) return;
            t.beginPath();
            t.moveTo(p[0].x, p[0].y);
            for (size_t i = 1; i < p.size(); ++i) t.lineTo(p[i].x, p[i].y);
            t.setStroke(c, w);
            t.strokePath();
        }

        // Soft glow = many overlapping translucent passes whose width grows and alpha
        // fades on a smooth (gaussian-like) curve, under a crisp full-colour core. Enough
        // passes that the halo reads as a continuous blur, not discrete rings. Halo alpha
        // scales with the core colour's own alpha so dimmed lines glow proportionally less.
        void glowStroke(IRenderTarget &t, const std::vector<Point> &p, const Color &c, double coreW)
        {
            const double ca = c.a;
            const int N = 6;                      // halo layers
            const double maxW = coreW * 3.5;      // outermost halo width (tight blur)
            for (int i = N; i >= 1; --i)
            {
                double f = (double)i / N;         // 1 (outer) .. ~0 (inner)
                double w = coreW + (maxW - coreW) * f;
                double alpha = ca * 0.15 * std::exp(-2.6 * f); // bright near core, faint outside
                strokePts(t, p, Color{c.r, c.g, c.b, alpha}, w);
            }
            strokePts(t, p, c, coreW); // crisp core
        }

        // Fill a circle at (cx,cy) radius R with the current paint (path only).
        void circlePath(IRenderTarget &t, double cx, double cy, double R)
        {
            const double k = 0.5522847498307936, ox = R * k, oy = R * k;
            t.beginPath();
            t.moveTo(cx - R, cy);
            t.cubicTo(cx - R, cy - oy, cx - ox, cy - R, cx, cy - R);
            t.cubicTo(cx + ox, cy - R, cx + R, cy - oy, cx + R, cy);
            t.cubicTo(cx + R, cy + oy, cx + ox, cy + R, cx, cy + R);
            t.cubicTo(cx - ox, cy + R, cx - R, cy + oy, cx - R, cy);
            t.closePath();
        }

        // One smooth radial-gradient blob: colour@peak at the centre fading CONTINUOUSLY to
        // zero opacity at R. A single shape via the HAL gradient — no stacked rings.
        void radialBlob(IRenderTarget &t, double cx, double cy, double R, const Color &c, double peak)
        {
            t.setRadialFill(cx, cy, R, Color{c.r, c.g, c.b, c.a * peak}, Color{c.r, c.g, c.b, 0.0});
            circlePath(t, cx, cy, R);
            t.fillPath();
        }

        // True glow along a path: a few overlapping gradient blobs (each fades smoothly to
        // zero, so overlap is seamless — halo radiates in all directions) + a crisp core.
        void glowAlongPath(IRenderTarget &t, const std::vector<Point> &p, const Color &c, double haloR, double coreW)
        {
            const double step = haloR * 0.8 < 1.0 ? 1.0 : haloR * 0.8;
            for (size_t i = 1; i < p.size(); ++i)
            {
                double dx = p[i].x - p[i - 1].x, dy = p[i].y - p[i - 1].y;
                double len = std::hypot(dx, dy);
                int n = (int)(len / step); if (n < 1) n = 1;
                for (int k = 0; k <= n; ++k)
                {
                    double f = (double)k / n;
                    radialBlob(t, p[i - 1].x + dx * f, p[i - 1].y + dy * f, haloR, c, 0.3);
                }
            }
            strokePts(t, p, c, coreW);
        }

        // Glowing filled dot: ONE smooth gradient halo (fades to zero) + crisp + hot core.
        void glowDot(IRenderTarget &t, double cx, double cy, double r, const Color &c)
        {
            radialBlob(t, cx, cy, r * 3.0, c, 0.4);
            drawCircle(t, cx, cy, r, Paint::filled(c));
            drawCircle(t, cx, cy, r * 0.5, Paint::filled(Color{(c.r + 1) * 0.5, (c.g + 1) * 0.5, (c.b + 1) * 0.5, 0.9}));
        }

        // ---- vector icons (no font glyphs, so they render identically everywhere) ----
        void waveIcon(IRenderTarget &t, int type, double cx, double cy, double w, double h, const Color &c)
        {
            std::vector<Point> p;
            const double x0 = cx - w / 2, x1 = cx + w / 2;
            if (type == 0) // sine
                for (int i = 0; i <= 16; ++i) { double f = i / 16.0; p.push_back({x0 + f * w, cy - std::sin(f * 6.28318) * h * 0.5}); }
            else if (type == 1) // saw
                { p = {{x0, cy + h / 2}, {x1, cy - h / 2}, {x1, cy + h / 2}}; }
            else if (type == 2) // square
                { double m = cx; p = {{x0, cy + h / 2}, {x0, cy - h / 2}, {m, cy - h / 2}, {m, cy + h / 2}, {x1, cy + h / 2}, {x1, cy - h / 2}}; }
            else // triangle
                { p = {{x0, cy + h / 2}, {cx, cy - h / 2}, {x1, cy + h / 2}}; }
            strokePts(t, p, c, 1.3);
        }

        void pageIcon(IRenderTarget &t, int page, double cx, double cy, double s, const Color &c)
        {
            const double h = s * 0.5;
            if (page == 0) // HOME: house (roof + body)
            {
                strokePts(t, {{cx - h, cy}, {cx, cy - h}, {cx + h, cy}}, c, 1.3);
                strokePts(t, {{cx - h * 0.7, cy}, {cx - h * 0.7, cy + h}, {cx + h * 0.7, cy + h}, {cx + h * 0.7, cy}}, c, 1.3);
            }
            else if (page == 1) // OSC: sine
            {
                std::vector<Point> p; for (int i = 0; i <= 16; ++i) { double f = i / 16.0; p.push_back({cx - h + f * s, cy - std::sin(f * 6.28318) * h * 0.7}); }
                strokePts(t, p, c, 1.3);
            }
            else if (page == 2) // ENV: ADSR curve
                strokePts(t, {{cx - h, cy + h}, {cx - h * 0.4, cy - h}, {cx, cy}, {cx + h * 0.5, cy}, {cx + h, cy + h}}, c, 1.3);
            else if (page == 3) // FX: gear (ring + radial ticks)
            {
                drawCircle(t, cx, cy, h * 0.55, Paint::stroked(c, 1.3));
                for (int k = 0; k < 6; ++k) { double ang = k * 6.28318 / 6; strokePts(t, {{cx + std::cos(ang) * h * 0.55, cy + std::sin(ang) * h * 0.55}, {cx + std::cos(ang) * h, cy + std::sin(ang) * h}}, c, 1.3); }
            }
            else // SET: three horizontal lines (≡)
                for (int k = -1; k <= 1; ++k) strokePts(t, {{cx - h, cy + k * h * 0.6}, {cx + h, cy + k * h * 0.6}}, c, 1.3);
        }

        void fxIcon(IRenderTarget &t, int fx, double cx, double cy, double s, const Color &c)
        {
            const double h = s * 0.5;
            if (fx == 0) // CMP: bar squeezed by two inward arrows
            {
                strokePts(t, {{cx, cy - h}, {cx, cy + h}}, c, 1.3);
                strokePts(t, {{cx - h, cy - h * 0.4}, {cx - h * 0.3, cy}, {cx - h, cy + h * 0.4}}, c, 1.3);
                strokePts(t, {{cx + h, cy - h * 0.4}, {cx + h * 0.3, cy}, {cx + h, cy + h * 0.4}}, c, 1.3);
            }
            else if (fx == 1) // DRV: clipped (flat-top) wave
                strokePts(t, {{cx - h, cy + h}, {cx - h * 0.5, cy - h}, {cx + h * 0.5, cy - h}, {cx + h, cy + h}}, c, 1.3);
            else if (fx == 2) // CHR: two offset wavy lines
            {
                for (int o = 0; o < 2; ++o) { std::vector<Point> p; for (int i = 0; i <= 12; ++i) { double f = i / 12.0; p.push_back({cx - h + f * s, cy - h * 0.4 + o * h * 0.8 - std::sin(f * 9.4) * h * 0.3}); } strokePts(t, p, c, 1.2); }
            }
            else if (fx == 3) // DLY: echoing bars (decreasing)
                for (int k = 0; k < 4; ++k) { double bx = cx - h + k * (s / 3.5); double bh = h * (1.0 - k * 0.22); strokePts(t, {{bx, cy - bh}, {bx, cy + bh}}, Color{c.r, c.g, c.b, 1.0 - k * 0.22}, 1.4); }
            else // RVB: decaying reverb tail
            {
                std::vector<Point> p; for (int k = 0; k <= 6; ++k) { double bx = cx - h + k * (s / 6.0); double bh = h * std::exp(-k * 0.35); p.push_back({bx, cy - bh}); p.push_back({bx, cy + bh}); p.push_back({bx, cy}); }
                strokePts(t, p, c, 1.2);
            }
        }
    }
}
}

// The unnamed-namespace array size trick above is just to keep the colour table
// file-local; expose a clean accessor.
namespace arstro { namespace examples {
    static const artboard::Color &pageColor(int p) { return kPageColor[p < 0 ? 0 : (p > 4 ? 4 : p)]; }

    SynthApp::SynthApp(double width, double height) : mW(width), mH(height)
    {
        AudioConfig::instance().setChannelCount(2);
        mSlide.set(0.0);
        applyAll();
    }

    // ───────────────────────── transforms ─────────────────────────
    Transform SynthApp::designToScreen() const
    {
        const double s = (mW / DW < mH / DH) ? mW / DW : mH / DH;
        const double ox = (mW - DW * s) * 0.5;
        const double oy = (mH - DH * s) * 0.5;
        return Transform::translation(ox, oy).mul(Transform::scaling(s, s));
    }
    Point SynthApp::toDesign(double sx, double sy) const
    {
        return designToScreen().inverse().apply(Point{sx, sy});
    }

    void SynthApp::selectPage(int p)
    {
        if (p < 0) p = 0;
        if (p >= PageCount) p = PageCount - 1;
        if (p == mPage) return;
        mPrevPage = mPage;
        mPage = p;
        mActiveKnob = -1;
        mDrag.active = false;
        mSlide.animate(Tween::range(0.0, 1.0, 220.0).withEasing(Easing::EaseOutCubic), mNowMs);
    }

    // ───────────────────────── DSP application ─────────────────────────
    void SynthApp::applyMaster() { mSynth.pushParam(GROUP_MASTER + MASTER_LEVEL, mVol / 100.0); }

    void SynthApp::applyOsc()
    {
        const double lvl = mLevel / 100.0;
        const double det = mDetune / 100.0 * 50.0;
        const double spd = mSpread / 100.0 * 50.0;
        int v = (int)(mVoices + 0.5); if (v < 1) v = 1; if (v > 5) v = 5;
        for (uint16_t grp : {(uint16_t)GROUP_OSC1, (uint16_t)GROUP_OSC2})
        {
            mSynth.pushParam(grp + OSC_LEVEL, grp == GROUP_OSC1 ? lvl : lvl * 0.6);
            mSynth.pushParam(grp + OSC_DETUNE, det);
            mSynth.pushParam(grp + OSC_SPREAD, spd);
            mSynth.pushParam(grp + OSC_VOICE_COUNT, v);
            mSynth.pushParam(grp + OSC_WAVEFORM, mWave); // 0 Sine 1 Saw 2 Square 3 Triangle
        }
    }

    void SynthApp::applyEnv()
    {
        const double atk = 1.0 + mAttack / 100.0 * 800.0;
        const double dec = 1.0 + mDecay / 100.0 * 800.0;
        const double sus = mSustain / 100.0;
        const double rel = 1.0 + mRelease / 100.0 * 1000.0;
        for (uint16_t grp : {(uint16_t)GROUP_OSC1, (uint16_t)GROUP_OSC2})
        {
            mSynth.pushParam(grp + OSC_ATTACK, atk);
            mSynth.pushParam(grp + OSC_DECAY, dec);
            mSynth.pushParam(grp + OSC_SUSTAIN, sus);
            mSynth.pushParam(grp + OSC_RELEASE, rel);
        }
    }

    void SynthApp::applyFx(int i)
    {
        const auto &p = mFx[i];
        switch (i)
        {
        case 0: // CMP
            mSynth.pushParam(GROUP_COMPRESSOR + CMP_THRESHOLD, -(p[0] / 100.0 * 60.0));
            mSynth.pushParam(GROUP_COMPRESSOR + CMP_RATIO, 1.0 + p[1] / 100.0 * 9.0);
            mSynth.pushParam(GROUP_COMPRESSOR + CMP_ATTACK, 1.0 + p[2] / 100.0 * 100.0);
            mSynth.pushParam(GROUP_COMPRESSOR + FX_BYPASS, mFxBypass[0] ? 1.0 : 0.0);
            break;
        case 1: // DRV
            mSynth.pushParam(GROUP_OVERDRIVE + OD_DRIVE, 1.0 + p[0] / 100.0 * 10.0);
            mSynth.pushParam(GROUP_OVERDRIVE + OD_TONE, 200.0 + p[1] / 100.0 * 8000.0);
            mSynth.pushParam(GROUP_OVERDRIVE + OD_LEVEL, p[2] / 100.0);
            mSynth.pushParam(GROUP_OVERDRIVE + FX_BYPASS, mFxBypass[1] ? 1.0 : 0.0);
            break;
        case 2: // CHR
            mSynth.pushParam(GROUP_CHORUS + CH_RATE, p[0] / 100.0 * 8.0);
            mSynth.pushParam(GROUP_CHORUS + CH_DEPTH, p[1] / 100.0 * 8.0);
            mSynth.pushParam(GROUP_CHORUS + CH_MIX, p[2] / 100.0);
            mSynth.pushParam(GROUP_CHORUS + FX_BYPASS, mFxBypass[2] ? 1.0 : 0.0);
            break;
        case 3: // DLY (repeater)
            mSynth.pushParam(GROUP_REPEATER + RP_DELAY, 20.0 + p[0] / 100.0 * 800.0);
            mSynth.pushParam(GROUP_REPEATER + RP_FEEDBACK, p[1] / 100.0 * 0.95);
            mSynth.pushParam(GROUP_REPEATER + RP_MIX, p[2] / 100.0);
            mSynth.pushParam(GROUP_REPEATER + FX_BYPASS, mFxBypass[3] ? 1.0 : 0.0);
            break;
        case 4: // RVB
            mSynth.pushParam(GROUP_REVERB + RV_DECAY, 100.0 + p[0] / 100.0 * 4000.0);
            mSynth.pushParam(GROUP_REVERB + RV_WIDTH, p[1] / 100.0);
            mSynth.pushParam(GROUP_REVERB + RV_MIX, p[2] / 100.0);
            mSynth.pushParam(GROUP_REVERB + FX_BYPASS, mFxBypass[4] ? 1.0 : 0.0);
            break;
        default: break;
        }
    }

    void SynthApp::applyAll()
    {
        applyMaster(); applyOsc(); applyEnv();
        for (int i = 0; i < 5; ++i) applyFx(i);
    }

    // ───────────────────────── knob layout ─────────────────────────
    std::vector<SynthApp::KnobRef> SynthApp::pageKnobs()
    {
        std::vector<KnobRef> k;
        switch (mPage)
        {
        case Home:
            k.push_back({372, 168, 22, &mVol, 100, "vol", "%"});
            k.push_back({448, 168, 22, &mPan, 100, "pan", "%"});
            break;
        case Osc:
            k.push_back({176, 174, 18, &mLevel, 100, "lvl", "%"});
            k.push_back({258, 174, 18, &mDetune, 100, "det", "c"});
            k.push_back({340, 174, 18, &mSpread, 100, "spd", "%"});
            k.push_back({422, 174, 18, &mVoices, 8, "voc", ""});
            break;
        case Env:
            k.push_back({118, 174, 18, &mAttack, 100, "atk", "ms"});
            k.push_back({230, 174, 18, &mDecay, 100, "dec", "ms"});
            k.push_back({342, 174, 18, &mSustain, 100, "sus", "%"});
            k.push_back({454, 174, 18, &mRelease, 100, "rel", "ms"});
            break;
        case Fx:
        {
            static const char *fxLabel[5][3] = {
                {"thr", "rat", "atk"}, {"drv", "ton", "mix"}, {"rte", "dep", "mix"},
                {"tim", "fbk", "mix"}, {"dec", "sz", "mix"}};
            static const char *fxUnit[5][3] = {
                {"dB", ":1", "ms"}, {"%", "%", "%"}, {"Hz", "%", "%"},
                {"ms", "%", "%"}, {"s", "%", "%"}};
            for (int i = 0; i < 3; ++i)
                k.push_back({492, 78.0 + i * 52.0, 18, &mFx[mFxSel][i], 100, fxLabel[mFxSel][i], fxUnit[mFxSel][i]});
            break;
        }
        default: break;
        }
        return k;
    }

    // ───────────────────────── input ─────────────────────────
    void SynthApp::pointer(int kind, double x, double y, int button, double timeMs)
    {
        mNowMs = timeMs;
        const Point d = toDesign(x, y);

        if (kind == 0) // down
        {
            if (d.y >= DH - kNavH) // nav bar
            {
                selectPage((int)(d.x / (DW / PageCount)));
                return;
            }
            // page-specific hot regions
            if (mPage == Osc)
            {
                for (int i = 0; i < 4; ++i)
                {
                    const double bx = 8, by = 152 + i * 13;
                    if (d.x >= bx && d.x <= bx + 30 && d.y >= by && d.y <= by + 11) { mWave = i; applyOsc(); return; }
                }
            }
            if (mPage == Fx && d.y >= kStatusH && d.y < kStatusH + 40)
            {
                int tab = (int)(d.x / (DW / 5));
                if (tab >= 0 && tab < 5)
                {
                    if (tab == mFxSel) { mFxBypass[tab] = !mFxBypass[tab]; applyFx(tab); }
                    else mFxSel = tab;
                    return;
                }
            }
            if (mPage == Set)
            {
                // 2 columns x 4 rows
                int col = d.x < DW / 2 ? 0 : 1;
                int row = (int)((d.y - kStatusH - 8) / 24);
                if (row < 0) row = 0; if (row > 3) row = 3;
                mSetSel = col * 4 + row;
                return;
            }
            // knobs
            auto knobs = pageKnobs();
            for (size_t i = 0; i < knobs.size(); ++i)
            {
                const auto &kn = knobs[i];
                if (std::hypot(d.x - kn.cx, d.y - kn.cy) <= kn.r + 6)
                {
                    mDrag.active = true; mDrag.value = kn.value; mDrag.max = kn.max;
                    mDrag.startY = d.y; mDrag.startVal = *kn.value;
                    mActiveKnob = (int)i;
                    return;
                }
            }
        }
        else if (kind == 1) // move
        {
            if (mDrag.active && mDrag.value)
            {
                double v = mDrag.startVal + (mDrag.startY - d.y) / 120.0 * mDrag.max;
                if (v < 0) v = 0; if (v > mDrag.max) v = mDrag.max;
                *mDrag.value = v;
                switch (mPage)
                {
                case Home: applyMaster(); break;
                case Osc: applyOsc(); break;
                case Env: applyEnv(); break;
                case Fx: applyFx(mFxSel); break;
                default: break;
                }
            }
        }
        else // up
        {
            mDrag.active = false; mActiveKnob = -1;
        }
    }

    void SynthApp::key(int code, bool down)
    {
        if (down && (code == 37 || code == 39)) { selectPage(mPage + (code == 39 ? 1 : -1)); return; }
        if (down && (code == 38 || code == 40))
        {
            const int delta = code == 40 ? 1 : -1;
            if (mPage == Set) { mSetSel = (mSetSel + delta + 8) % 8; }
            else if (mPage == Fx) { mFxSel = (mFxSel + delta + 5) % 5; }
            return;
        }
        static const char *row = "awsedftgyhujk"; // 13 keys, C..C
        for (int i = 0; row[i]; ++i)
        {
            if (code == (int)row[i])
            {
                const int note = 60 + i;
                if (down) noteOn(note, 0.85);
                else noteOff(note);
                return;
            }
        }
    }

    void SynthApp::noteOn(int midi, double vel)
    {
        for (int n : mHeldNotes) if (n == midi) return;
        mHeldNotes.push_back(midi);
        mSynth.pushNoteOn(midi, vel);
    }
    void SynthApp::noteOff(int midi)
    {
        for (size_t i = 0; i < mHeldNotes.size(); ++i)
            if (mHeldNotes[i] == midi) { mHeldNotes.erase(mHeldNotes.begin() + i); break; }
        mSynth.pushNoteOff(midi);
    }

    // ───────────────────────── audio ─────────────────────────
    void SynthApp::renderAudio(float *interleaved, int frames)
    {
        std::vector<double> blk;
        mSynth.renderBlockDouble(blk, frames);
        const int ch = AudioConfig::instance().channelCount();
        double peakL = 0, peakR = 0;
        std::lock_guard<std::mutex> lock(mAudioMutex);
        for (int i = 0; i < frames; ++i)
        {
            float l = (float)blk[i * ch];
            float r = (float)blk[i * ch + (ch > 1 ? 1 : 0)];
            interleaved[i * 2] = l;
            interleaved[i * 2 + 1] = r;
            peakL = std::max(peakL, (double)std::fabs(l));
            peakR = std::max(peakR, (double)std::fabs(r));
            mScope[mScopeHead] = l;
            mScopeHead = (mScopeHead + 1) % mScope.size();
        }
        mMeterL += (peakL - mMeterL) * 0.3;
        mMeterR += (peakR - mMeterR) * 0.3;
    }

    // ───────────────────────── drawing helpers ─────────────────────────
    void SynthApp::drawKnob(IRenderTarget &t, double cx, double cy, double r,
                            double *value, double max, const Color &color, const std::string &label, bool active)
    {
        // Per-knob spring: the displayed value eases toward the real target (UI only); the
        // focus highlight fades in/out. Both integrate with the frame delta so velocity is
        // continuous and reversing a drag never snaps.
        KnobAnim &an = mKnobAnim[value];
        if (!an.init) { an.display = *value; an.highlight = active ? 1.0 : 0.0; an.init = true; }
        springStep(an.display, an.vel, *value, mDt, 16.0);
        easeStep(an.highlight, active ? 1.0 : 0.0, mDt, 0.09);
        const double hl = an.highlight;

        double v01 = an.display / max;
        if (v01 < 0) v01 = 0; if (v01 > 1) v01 = 1;
        drawCircle(t, cx, cy, r, Paint::filledStroked(white(0.03), white(0.10 + 0.10 * hl), 1.5));

        // Angle convention (matches SoftKnob): an up-pointing vector rotated by the
        // value angle, so value 0 sits at lower-left (≈7 o'clock), max at lower-right
        // (≈5 o'clock), and the unused gap is at the BOTTOM.
        auto pt = [&](double deg, double rad_) {
            double th = deg * kPi / 180.0;
            return Point{cx + std::sin(th) * rad_, cy - std::cos(th) * rad_};
        };
        const double d0 = -135.0, d1 = -135.0 + v01 * 270.0;
        std::vector<Point> arc;
        const int steps = 2 + (int)(v01 * 26);
        for (int s = 0; s <= steps; ++s)
            arc.push_back(pt(d0 + (d1 - d0) * (double)s / steps, r - 5));
        if (v01 > 0.001 && arc.size() >= 2)
            glowAlongPath(t, arc, a(color, 0.85 + 0.15 * hl), 4.0, 2.6); // true halo, brighter on focus
        drawCircle(t, cx, cy, r - 9, Paint::filled(a(color, 0.06 + 0.12 * hl)));
        const Point tip = pt(d1, r - 4);
        // indicator: glowing line (isotropic halo) + bright tip
        glowAlongPath(t, {{cx, cy}, tip}, color, 4.0, 2.0);
        glowDot(t, tip.x, tip.y, 2.4, color);
        textCentered(t, label, cx, cy + r + 9, 8.0, mixCol(white(0.35), color, hl));
    }

    void SynthApp::drawValueOverlay(IRenderTarget &t, double value, const std::string &unit, const Color &color, double opacity)
    {
        if (opacity <= 0.01) return;
        const double cx = DW * 0.5, cy = 96;
        char buf[32]; std::snprintf(buf, sizeof buf, "%d%s", (int)std::lround(value), unit.c_str());
        std::string s = buf;
        const double bw = 14 + s.size() * 14, bh = 32;
        // a slight upward drift as it fades in adds life
        const double dy = (1.0 - opacity) * 6.0;
        drawRoundedRect(t, Rect{cx - bw / 2, cy - bh / 2 + dy, bw, bh}, 6.0,
                        Paint::filledStroked(Color{0, 0, 0, 0.7 * opacity}, a(color, 0.5 * opacity), 1.0));
        textCentered(t, s, cx, cy + 7 + dy, 22.0, a(color, opacity));
    }

    void SynthApp::drawStatusBar(IRenderTarget &t, double frame)
    {
        const Color color = pageColor(mPage);
        drawRoundedRect(t, Rect{0, 0, DW, kStatusH}, 0.0, Paint::filled(a(color, 0.06)));
        t.beginPath(); t.moveTo(0, kStatusH); t.lineTo(DW, kStatusH); t.setStroke(a(color, 0.25), 1.0); t.strokePath();
        textAt(t, kPageName[mPage], 12, 16, 9.0, white(0.6));
        // MIDI activity dot
        const Color dotC = mMidiBlink ? Color::hex(0x00ff88) : white(0.1);
        drawCircle(t, DW - 58, 12, 3.0, Paint::filled(dotC));
        textAt(t, "MIDI", DW - 50, 15, 8.0, white(0.25));
        // signal bars (bottom-aligned)
        for (int i = 0; i < 4; ++i)
        {
            double h = 5.0 + i * 2.0;
            drawRoundedRect(t, Rect{DW - 18 + i * 4.0, 18.0 - h, 3.0, h}, 1.0,
                            Paint::filled(white(i < 3 ? 0.25 + i * 0.12 : 0.06)));
        }
    }

    void SynthApp::drawNav(IRenderTarget &t)
    {
        const double y0 = DH - kNavH, tw = DW / PageCount;
        drawRoundedRect(t, Rect{0, y0, DW, kNavH}, 0.0, Paint::filled(Color{0, 0, 0, 0.4}));
        for (int p = 0; p < PageCount; ++p)
        {
            const Color c = pageColor(p);
            const bool act = p == mPage;
            if (act)
            {
                drawRoundedRect(t, Rect{p * tw, y0, tw, kNavH}, 0.0, Paint::filled(a(c, 0.12)));
                drawRoundedRect(t, Rect{p * tw, y0, tw, 2.0}, 0.0, Paint::filled(c));
            }
            pageIcon(t, p, p * tw + tw / 2, y0 + kNavH / 2, 14.0, act ? c : white(0.25));
        }
    }

    // ───────────────────────── pages ─────────────────────────
    void SynthApp::drawHome(IRenderTarget &t, double, double frame)
    {
        const Color color = pageColor(Home);
        // left info panel
        const double px = 12;
        t.beginPath(); t.moveTo(150, kStatusH); t.lineTo(150, DH - kNavH); t.setStroke(white(0.07), 1.0); t.strokePath();
        textAt(t, "PATCH", px, kStatusH + 16, 8.0, white(0.25));
        textAt(t, "INIT PATCH", px, kStatusH + 31, 12.0, kInk);
        textAt(t, "REC", px, kStatusH + 46, 8.0, a(color, ((int)(frame * 0.4) % 2) ? 1.0 : 0.3));
        const char *labels[3] = {"VOC", "CPU", "SR"};
        char vals[3][16];
        std::snprintf(vals[0], 16, "%d/8", mSynth.activeVoices());
        std::snprintf(vals[1], 16, "%d%%", 20 + (int)(mMeterL * 60));
        std::snprintf(vals[2], 16, "48k");
        for (int i = 0; i < 3; ++i)
        {
            double ry = kStatusH + 76 + i * 22;
            textAt(t, labels[i], px, ry, 9.0, white(0.28));
            textAt(t, vals[i], 110, ry, 12.0, kInk);
            t.beginPath(); t.moveTo(px, ry + 6); t.lineTo(138, ry + 6); t.setStroke(white(0.07), 1.0); t.strokePath();
        }
        // right: output meters + knobs
        textAt(t, "OUTPUT", 170, kStatusH + 16, 9.0, white(0.2));
        const double mL = mMeterL, mR = mMeterR;
        const char *ml[2] = {"L", "R"};
        double mv[2] = {mL, mR};
        for (int i = 0; i < 2; ++i)
        {
            double by = kStatusH + 34 + i * 18;
            textAt(t, ml[i], 170, by + 8, 10.0, white(0.35));
            drawRoundedRect(t, Rect{184, by, 320, 8}, 4.0, Paint::filled(white(0.07)));
            double w = 320 * std::min(1.0, mv[i] * 1.4);
            Color bc = mv[i] > 0.8 ? Color::hex(0xff3b5c) : color;
            if (w > 1) drawRoundedRect(t, Rect{184, by, w, 8}, 4.0, Paint::filled(bc));
        }
        auto knobs = pageKnobs();
        for (size_t i = 0; i < knobs.size(); ++i)
            drawKnob(t, knobs[i].cx, knobs[i].cy, knobs[i].r, knobs[i].value, knobs[i].max, color, knobs[i].label, mActiveKnob == (int)i);
    }

    void SynthApp::drawOsc(IRenderTarget &t, double, double frame)
    {
        const Color color = pageColor(Osc);
        const double vy0 = kStatusH, vy1 = 148; // viz region (leaves room for controls)
        drawRoundedRect(t, Rect{0, vy0, DW, vy1 - vy0}, 0.0, Paint::filled(Color{0, 0, 0, 0.3}));

        auto sampleWave = [&](double p) {
            double q = std::fmod(p, 2 * kPi); if (q < 0) q += 2 * kPi;
            switch (mWave)
            {
            case 1: return (q / (2 * kPi) - 0.5);                  // saw
            case 2: return std::sin(p) > 0 ? -0.5 : 0.5;           // square
            case 3: return std::asin(std::sin(p)) / (kPi / 2) * -0.55; // tri
            default: return -std::sin(p) * 0.6;                    // sine
            }
        };
        const double H = vy1 - vy0, midY = vy0 + H * 0.5;
        const double tphase = frame * 0.045;
        const double amp = (mLevel / 100.0) * H * 0.42 * (0.55 + 0.45 * std::min(1.0, mMeterL * 1.5)) + 3;
        const int voices = std::max(1, (int)(mVoices + 0.5));
        const double phaseSpread = (mSpread / 100.0) * kPi * 0.8;
        // saw jumps once per cycle (2π), square flips twice (π); inserting the edge at its
        // EXACT sub-pixel x keeps the discontinuity gliding smoothly as the phase scrolls
        // (otherwise the vertical edge snaps to the sample grid and stutters).
        const double edgePeriod = mWave == 1 ? 2 * kPi : (mWave == 2 ? kPi : 0.0);
        const double step = 3.0;
        for (int v = 0; v < voices; ++v)
        {
            double tv = voices == 1 ? 0 : ((double)v / (voices - 1) - 0.5) * 2;
            double phaseOff = tv * phaseSpread;
            double freqShift = tv * (mDetune / 100.0) * 0.012;
            double alpha = v == voices / 2 ? 1.0 : 0.4 - std::fabs(tv) * 0.12;
            const double pscale = kPi * 4 * (1 + freqShift) / DW;
            auto phaseAt = [&](double x) { return x * pscale + tphase + phaseOff; };
            std::vector<Point> pts;
            pts.push_back({0.0, midY + sampleWave(phaseAt(0)) * amp});
            for (double x = step; x <= DW; x += step)
            {
                const double pPrev = phaseAt(x - step), pCur = phaseAt(x);
                if (edgePeriod > 0.0)
                {
                    double b = std::ceil(pPrev / edgePeriod) * edgePeriod;
                    for (; b < pCur; b += edgePeriod)
                    {
                        double xc = (x - step) + (b - pPrev) / (pCur - pPrev) * step;
                        pts.push_back({xc, midY + sampleWave(b - 1e-6) * amp}); // top of edge
                        pts.push_back({xc, midY + sampleWave(b + 1e-6) * amp}); // bottom of edge
                    }
                }
                pts.push_back({x, midY + sampleWave(pCur) * amp});
            }
            glowStroke(t, pts, a(color, alpha), 1.8);
        }
        // controls strip — waveform selector drawn as wave-shape icons
        t.beginPath(); t.moveTo(0, vy1); t.lineTo(DW, vy1); t.setStroke(white(0.08), 1.0); t.strokePath();
        for (int i = 0; i < 4; ++i)
        {
            double bx = 8, by = 152 + i * 13;
            bool on = mWave == i;
            drawRoundedRect(t, Rect{bx, by, 30, 11}, 2.0,
                            Paint::filledStroked(on ? a(color, 0.16) : white(0.04), on ? color : white(0.08), 1.0));
            waveIcon(t, i, bx + 15, by + 5.5, 18, 6, on ? color : white(0.4));
        }
        auto knobs = pageKnobs();
        for (size_t i = 0; i < knobs.size(); ++i)
            drawKnob(t, knobs[i].cx, knobs[i].cy, knobs[i].r, knobs[i].value, knobs[i].max, color, knobs[i].label, mActiveKnob == (int)i);
    }

    void SynthApp::drawEnv(IRenderTarget &t, double, double frame)
    {
        const Color color = pageColor(Env);
        const double vy0 = kStatusH, vy1 = 148;
        drawRoundedRect(t, Rect{0, vy0, DW, vy1 - vy0}, 0.0, Paint::filled(Color{0, 0, 0, 0.3}));
        const double pad = 18, W2 = DW - pad * 2, H2 = (vy1 - vy0) - pad * 2, base = vy0 + pad + H2;
        const double A = (mAttack / 100.0) * W2 * 0.25 + W2 * 0.04;
        const double D = (mDecay / 100.0) * W2 * 0.2 + W2 * 0.04;
        const double S = mSustain / 100.0;
        const double R = (mRelease / 100.0) * W2 * 0.25 + W2 * 0.06;
        double hold = W2 - A - D - R; if (hold < 8) hold = 8;
        std::vector<Point> pts = {
            {pad, base}, {pad + A, vy0 + pad}, {pad + A + D, base - H2 * S},
            {pad + A + D + hold, base - H2 * S}, {pad + A + D + hold + R, base}};
        // fill
        t.beginPath(); t.moveTo(pts[0].x, pts[0].y);
        for (auto &p : pts) t.lineTo(p.x, p.y);
        t.lineTo(pts.back().x, base); t.closePath();
        t.setFill(a(color, 0.10)); t.fillPath();
        glowStroke(t, pts, color, 2.0);
        // playhead
        double tt = std::fmod(frame * 0.012, 1.0);
        double total = 0; for (size_t i = 1; i < pts.size(); ++i) total += std::hypot(pts[i].x - pts[i-1].x, pts[i].y - pts[i-1].y);
        double target = tt * total, cum = 0, px = pts[0].x, py = pts[0].y;
        for (size_t i = 1; i < pts.size(); ++i)
        {
            double seg = std::hypot(pts[i].x - pts[i-1].x, pts[i].y - pts[i-1].y);
            if (cum + seg >= target) { double f = seg > 0 ? (target - cum) / seg : 0; px = pts[i-1].x + f * (pts[i].x - pts[i-1].x); py = pts[i-1].y + f * (pts[i].y - pts[i-1].y); break; }
            cum += seg;
        }
        glowDot(t, px, py, 3.0, color);
        t.beginPath(); t.moveTo(0, vy1); t.lineTo(DW, vy1); t.setStroke(white(0.08), 1.0); t.strokePath();
        auto knobs = pageKnobs();
        for (size_t i = 0; i < knobs.size(); ++i)
            drawKnob(t, knobs[i].cx, knobs[i].cy, knobs[i].r, knobs[i].value, knobs[i].max, color, knobs[i].label, mActiveKnob == (int)i);
    }

    void SynthApp::drawFx(IRenderTarget &t, double, double frame)
    {
        static const uint32_t fxCol[5] = {0xff3b5c, 0xff9500, 0x00d4ff, 0xffd600, 0x00ff88};
        const Color color = Color::hex(fxCol[mFxSel]);
        const double tw = DW / 5;
        // tabs
        for (int i = 0; i < 5; ++i)
        {
            Color c = Color::hex(fxCol[i]);
            bool sel = i == mFxSel;
            if (sel) { drawRoundedRect(t, Rect{i * tw, kStatusH, tw, 40}, 0.0, Paint::filled(a(c, 0.10)));
                       drawRoundedRect(t, Rect{i * tw, kStatusH + 38, tw, 2}, 0.0, Paint::filled(c)); }
            double op = mFxBypass[i] ? 0.3 : 1.0;
            fxIcon(t, i, i * tw + tw / 2, kStatusH + 20, 18.0, a(sel ? c : white(0.35), op));
        }
        // viz region
        const double vy0 = kStatusH + 40, vy1 = DH - kNavH;
        const double H = vy1 - vy0, midY = vy0 + H * 0.5, W = DW - 70;
        const double tp = frame * 0.04;
        auto stroke = [&](std::vector<Point> &pts, const Color &c, double w) {
            if (pts.size() < 2) return;
            t.beginPath(); t.moveTo(pts[0].x, pts[0].y);
            for (size_t i = 1; i < pts.size(); ++i) t.lineTo(pts[i].x, pts[i].y);
            t.setStroke(c, w); t.strokePath();
        };
        auto &p = mFx[mFxSel];
        if (mFxSel == 0) { // CMP
            double thr = 1 - p[0] / 100.0;
            std::vector<Point> in, out;
            for (int x = 0; x <= (int)W; x += 2) { double raw = std::sin((x / W) * kPi * 6 + tp); in.push_back({(double)x, midY - raw * H * 0.42});
                double comp = std::fabs(raw) > thr ? (raw < 0 ? -1 : 1) * (thr + (std::fabs(raw) - thr) * 0.3) : raw; out.push_back({(double)x, midY - comp * H * 0.42}); }
            stroke(in, white(0.2), 1.5); glowStroke(t, out, color, 2.0);
        } else if (mFxSel == 1) { // DRV
            double drive = p[0] / 100.0 * 3 + 1;
            std::vector<Point> pts;
            for (int x = 0; x <= (int)W; x += 2) { double raw = std::sin((x / W) * kPi * 6 + tp) * drive; double cl = std::max(-1.0, std::min(1.0, raw)); pts.push_back({(double)x, midY - cl * H * 0.42}); }
            glowStroke(t, pts, color, 2.0);
        } else if (mFxSel == 2) { // CHR
            double depth = p[1] / 100.0 * 0.04;
            for (int s = 0; s < 3; ++s) { double off = (s - 1) * depth * 30; std::vector<Point> pts;
                for (int x = 0; x <= (int)W; x += 2) pts.push_back({(double)x, midY - std::sin((x / W) * kPi * 6 + tp + off) * H * 0.38});
                glowStroke(t, pts, a(color, s == 1 ? 1.0 : 0.4), 1.5); }
        } else if (mFxSel == 3) { // DLY
            double fbk = p[1] / 100.0;
            for (int rep = 0; rep < 4; ++rep) { double al = std::pow(fbk, rep) * 0.9; double shift = rep * (W * 0.22); std::vector<Point> pts;
                for (int x = 0; x <= (int)(W - shift); x += 2) pts.push_back({x + shift, midY - std::sin((x / W) * kPi * 5 + tp) * H * (0.38 - rep * 0.07)});
                stroke(pts, a(color, al), 1.5 - rep * 0.3); }
        } else { // RVB
            double dec = p[0] / 100.0;
            for (int i = 0; i < 40; ++i) { double x = (double)i / 40 * W; double amp = std::exp(-(double)i / 40 * (1 - dec) * 4) * H * 0.38;
                double n = std::sin(i * 12.9898 + tp) * amp; // deterministic pseudo-noise
                std::vector<Point> bar = {{x, midY - n}, {x, midY + n}};
                stroke(bar, a(color, std::exp(-(double)i / 40 * 2)), 1.5); }
        }
        t.beginPath(); t.moveTo(DW - 70, vy0); t.lineTo(DW - 70, vy1); t.setStroke(white(0.07), 1.0); t.strokePath();
        auto knobs = pageKnobs();
        for (size_t i = 0; i < knobs.size(); ++i)
            drawKnob(t, knobs[i].cx, knobs[i].cy, knobs[i].r, knobs[i].value, knobs[i].max, color, knobs[i].label, mActiveKnob == (int)i);
    }

    void SynthApp::drawSet(IRenderTarget &t, double)
    {
        const Color color = pageColor(Set);
        struct S { const char *l, *v; };
        static const S items[8] = {{"SR", "48k"}, {"BIT", "24"}, {"BUF", "128"}, {"OVS", "ON"},
                                    {"MID", "USB"}, {"CH", "ALL"}, {"VER", "3.0.0"}, {"CRC", "8472"}};
        t.beginPath(); t.moveTo(DW / 2, kStatusH); t.lineTo(DW / 2, DH - kNavH); t.setStroke(white(0.08), 1.0); t.strokePath();
        for (int i = 0; i < 8; ++i)
        {
            int col = i / 4, rowi = i % 4;
            double x0 = col * (DW / 2), ry = kStatusH + 8 + rowi * 24;
            bool sel = i == mSetSel;
            if (sel) drawRoundedRect(t, Rect{x0, ry, DW / 2, 22}, 0.0, Paint::filled(a(color, 0.10)));
            textAt(t, items[i].l, x0 + 14, ry + 15, 9.0, sel ? color : white(0.28));
            textAt(t, items[i].v, x0 + DW / 2 - 58, ry + 15, 12.0, sel ? color : white(0.7));
        }
    }

    // ───────────────────────── frame ─────────────────────────
    void SynthApp::render(IRenderTarget &target, double nowMs)
    {
        mNowMs = nowMs;
        mDt = mLastMs < 0 ? 0.0 : (nowMs - mLastMs) / 1000.0; // frame delta in seconds
        mLastMs = nowMs;
        if (!mIntroDone) { mIntroDone = true; mSlide.animate(Tween::range(0.0, 1.0, 320.0).withEasing(Easing::EaseOutCubic), nowMs); }
        const double slide = mSlide.update(nowMs);
        mAnimator.advance(nowMs);

        // Capture the dragged knob's (smoothed) value for the centre overlay, then fade the
        // overlay in while dragging and out after release — so it never pops.
        if (mDrag.active && mActiveKnob >= 0)
        {
            auto knobs = pageKnobs();
            if (mActiveKnob < (int)knobs.size())
            {
                auto it = mKnobAnim.find(knobs[mActiveKnob].value);
                mOverlayValue = it != mKnobAnim.end() && it->second.init ? it->second.display : *knobs[mActiveKnob].value;
                mOverlayUnit = knobs[mActiveKnob].unit;
                mOverlayColor = pageColor(mPage);
            }
        }
        easeStep(mOverlayAmt, mDrag.active ? 1.0 : 0.0, mDt, 0.08);
        // MIDI blink follows held notes / recent activity.
        mBlinkAccum += 1;
        mMidiBlink = !mHeldNotes.empty() && ((int)(nowMs / 120) % 2 == 0);
        const double frame = nowMs * 0.06;

        const Transform d = designToScreen();
        const Color color = pageColor(mPage);

        // background + module border
        target.save();
        target.setTransform(d);
        drawRoundedRect(target, Rect{0, 0, DW, DH}, 0.0, Paint::filled(kBg));
        drawRoundedRect(target, Rect{0.5, 0.5, DW - 1, DH - 1}, 10.0, Paint::stroked(a(color, 0.35), 1.0));
        target.restore();

        // page content (slide in). NOT clipped, so knob/visualiser glow is never cut;
        // the status bar + nav are drawn on top afterwards to cover any faint bleed.
        const double dx = (1.0 - slide) * 14.0;
        target.save();
        target.setTransform(d.mul(Transform::translation(dx, 0)));
        switch (mPage)
        {
        case Home: drawHome(target, 0, frame); break;
        case Osc: drawOsc(target, 0, frame); break;
        case Env: drawEnv(target, 0, frame); break;
        case Fx: drawFx(target, 0, frame); break;
        case Set: drawSet(target, 0); break;
        }
        drawValueOverlay(target, mOverlayValue, mOverlayUnit, mOverlayColor, mOverlayAmt);
        target.restore();

        // chrome on top
        target.save();
        target.setTransform(d);
        drawStatusBar(target, frame);
        drawNav(target);
        target.restore();
    }
}
}
