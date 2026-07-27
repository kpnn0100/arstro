#include "PianoApp.h"
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
        const Color kBg = Color::hex(0x111111);
        const Color kInk = Color::hex(0xffffff);
        const Color kAccent = Color::hex(0x00d4ff);
        const Color kWhiteIdle = Color::hex(0xf2f2f2);
        const Color kBlackIdle = Color::hex(0x1a1a1a);

        Color mixCol(const Color &x, const Color &y, double t)
        {
            return Color{x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t, x.b + (y.b - x.b) * t,
                         x.a + (y.a - x.a) * t};
        }
        Color white(double alpha) { return Color{1, 1, 1, alpha}; }
        Color a(const Color &c, double alpha) { return Color{c.r, c.g, c.b, alpha}; }

        void textAt(IRenderTarget &t, const std::string &s, double x, double base, double size, const Color &c)
        {
            t.setFill(c);
            t.drawText(s, x, base, size);
        }
        void textCentered(IRenderTarget &t, const std::string &s, double cx, double base, double size, const Color &c)
        {
            textAt(t, s, cx - s.size() * size * 0.28, base, size, c);
        }

        // One octave, C..C inclusive (13 keys) — same QWERTY row examples/synth uses,
        // so the two apps share one muscle-memory layout:
        //   white keys: a s d f g h j k  (C D E F G A B C)
        //   black keys:  w e   t y u     (C# D# F# G# A#)
        constexpr const char kRow[] = "awsedftgyhujk"; // index = semitone 0..12
        constexpr const char *kNoteName[13] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C"};
        constexpr int kWhiteSemitone[8] = {0, 2, 4, 5, 7, 9, 11, 12};
        struct BlackSpec { int semitone; int afterWhite; };
        constexpr BlackSpec kBlack[5] = {{1, 0}, {3, 1}, {6, 3}, {8, 4}, {10, 5}};

        constexpr double kKbX0 = 40, kKbY0 = 84, kWhiteH = 160, kBlackH = 100;
    }

    PianoApp::PianoApp(double width, double height) : mW(width), mH(height)
    {
        AudioConfig::instance().setChannelCount(2);
    }

    Transform PianoApp::designToScreen() const
    {
        const double s = (mW / DW < mH / DH) ? mW / DW : mH / DH;
        const double ox = (mW - DW * s) * 0.5;
        const double oy = (mH - DH * s) * 0.5;
        return Transform::translation(ox, oy).mul(Transform::scaling(s, s));
    }

    void PianoApp::enqueue(Cmd c)
    {
        // Drops silently if the queue is somehow full (256 pending key events
        // would mean the audio thread has been stalled for seconds — losing a
        // keystroke is the right failure there, and blocking would be the wrong one).
        mCmds.push(c);
    }

    // ───────────────────────── input (UI thread) ─────────────────────────
    void PianoApp::key(int code, bool down)
    {
        if (code == 16) { mUnaCorda = down; enqueue({CmdType::UnaCorda, 0, 0, down}); return; }  // Shift
        if (code == 17) { mSostenuto = down; enqueue({CmdType::Sostenuto, 0, 0, down}); return; } // Ctrl
        if (code == 32) { mSustain = down; enqueue({CmdType::Sustain, 0, 0, down}); return; }     // Space

        if (down && code == (int)'z') { mBaseOctave = std::max(0, mBaseOctave - 1); return; }
        if (down && code == (int)'x') { mBaseOctave = std::min(8, mBaseOctave + 1); return; }

        for (int i = 0; i < kSemitones; ++i)
        {
            if (code != (int)kRow[i]) continue;
            if (mKeyHeld[i] == down) return; // de-dupe (host also guards OS auto-repeat)
            mKeyHeld[i] = down;
            int midi = midiFor(i);
            if (down) enqueue({CmdType::NoteOn, midi, 0.85, true});
            else enqueue({CmdType::NoteOff, midi, 0.0, false});
            return;
        }
    }

    // ───────────────────────── audio thread ─────────────────────────
    void PianoApp::renderAudio(float *interleaved, int frames)
    {
        // Drain queued UI commands here — the ONLY thread that ever touches
        // mEngine (see class-doc threading note). Lock-free pop, no allocation.
        Cmd c;
        while (mCmds.pop(c))
        {
            switch (c.type)
            {
            case CmdType::NoteOn: mEngine.noteOnMidi(c.note, c.vel); break;
            case CmdType::NoteOff: mEngine.noteOff(c.note); break;
            case CmdType::Sustain: mEngine.setSustainPedal(c.on); break;
            case CmdType::Sostenuto: mEngine.setSostenutoPedal(c.on); break;
            case CmdType::UnaCorda: mEngine.setUnaCorda(c.on); break;
            }
        }

        // Straight into the caller's buffer: no allocation, and no round trip
        // through 16-bit PCM just to convert back to float for the sink.
        mEngine.renderBlockFloat(interleaved, frames);

        double peakL = 0, peakR = 0;
        for (int i = 0; i < frames; ++i)
        {
            peakL = std::max(peakL, (double)std::fabs(interleaved[i * 2]));
            peakR = std::max(peakR, (double)std::fabs(interleaved[i * 2 + 1]));
        }
        const double smoothL = mMeterL.load(std::memory_order_relaxed) +
                               (peakL - mMeterL.load(std::memory_order_relaxed)) * 0.3;
        const double smoothR = mMeterR.load(std::memory_order_relaxed) +
                               (peakR - mMeterR.load(std::memory_order_relaxed)) * 0.3;
        mMeterL.store(smoothL, std::memory_order_relaxed);
        mMeterR.store(smoothR, std::memory_order_relaxed);
    }

    // ───────────────────────── drawing ─────────────────────────
    void PianoApp::drawHeader(IRenderTarget &t)
    {
        textAt(t, "ARSTRO PIANO", 40, 26, 16.0, kInk);
        textAt(t, "physical-modeling — hammer + string + soundboard, not samples", 40, 42, 8.5, white(0.35));
        char buf[64];
        std::snprintf(buf, sizeof buf, "OCTAVE %d", mBaseOctave);
        textAt(t, buf, DW - 150, 26, 12.0, kAccent);
        textAt(t, "Z / X to shift", DW - 150, 40, 8.0, white(0.35));
    }

    void PianoApp::drawKeyboard(IRenderTarget &t)
    {
        const double whiteW = (DW - 2 * kKbX0) / 8.0;
        const double blackW = whiteW * 0.6;

        // White keys first (background layer), then black keys on top.
        for (int i = 0; i < 8; ++i)
        {
            const int semi = kWhiteSemitone[i];
            const double x = kKbX0 + i * whiteW;
            const bool held = mKeyHeld[semi];
            const Color fill = held ? mixCol(kWhiteIdle, kAccent, 0.55) : kWhiteIdle;
            drawRoundedRect(t, Rect{x + 1, kKbY0, whiteW - 2, kWhiteH}, 4.0,
                            Paint::filledStroked(fill, Color{0, 0, 0, 0.4}, 1.0));
            const Color labelC = Color{0, 0, 0, held ? 0.65 : 0.45};
            textCentered(t, std::string(1, kRow[semi]), x + whiteW / 2, kKbY0 + kWhiteH - 28, 11.0, labelC);
            textCentered(t, kNoteName[semi], x + whiteW / 2, kKbY0 + kWhiteH - 12, 9.0, labelC);
        }
        for (const auto &b : kBlack)
        {
            const double x = kKbX0 + (b.afterWhite + 1) * whiteW - blackW / 2;
            const bool held = mKeyHeld[b.semitone];
            const Color fill = held ? mixCol(kBlackIdle, kAccent, 0.65) : kBlackIdle;
            drawRoundedRect(t, Rect{x, kKbY0, blackW, kBlackH}, 3.0,
                            Paint::filledStroked(fill, Color{0, 0, 0, 0.6}, 1.0));
            textCentered(t, std::string(1, kRow[b.semitone]), x + blackW / 2, kKbY0 + kBlackH - 22, 10.0, white(0.8));
            textCentered(t, kNoteName[b.semitone], x + blackW / 2, kKbY0 + kBlackH - 9, 7.5, white(0.55));
        }
    }

    void PianoApp::drawPedals(IRenderTarget &t)
    {
        struct P { const char *label; const char *key; bool on; };
        const P pedals[3] = {
            {"SUSTAIN", "Space", mSustain},
            {"UNA CORDA", "Shift", mUnaCorda},
            {"SOSTENUTO", "Ctrl", mSostenuto},
        };
        const double y0 = kKbY0 + kWhiteH + 18, w = 180, h = 34, gap = 16;
        double x = kKbX0;
        for (const auto &p : pedals)
        {
            const Color fill = p.on ? a(kAccent, 0.22) : white(0.03);
            const Color stroke = p.on ? kAccent : white(0.12);
            drawRoundedRect(t, Rect{x, y0, w, h}, 6.0, Paint::filledStroked(fill, stroke, 1.2));
            textCentered(t, p.label, x + w / 2, y0 + 15, 9.5, p.on ? kAccent : white(0.55));
            textCentered(t, p.key, x + w / 2, y0 + 28, 7.5, white(0.3));
            x += w + gap;
        }
    }

    void PianoApp::drawMeter(IRenderTarget &t)
    {
        const double mL = mMeterL.load(std::memory_order_relaxed);
        const double mR = mMeterR.load(std::memory_order_relaxed);
        const double x0 = kKbX0 + 3 * (180 + 16) + 8, y0 = kKbY0 + kWhiteH + 18, w = 130, h = 14;
        const char *label[2] = {"L", "R"};
        double val[2] = {mL, mR};
        for (int i = 0; i < 2; ++i)
        {
            const double y = y0 + i * (h + 6);
            textAt(t, label[i], x0 - 14, y + 11, 9.0, white(0.35));
            drawRoundedRect(t, Rect{x0, y, w, h}, 4.0, Paint::filled(white(0.06)));
            double bw = w * std::min(1.0, val[i] * 1.3);
            Color c = val[i] > 0.85 ? Color::hex(0xff3b5c) : kAccent;
            if (bw > 1) drawRoundedRect(t, Rect{x0, y, bw, h}, 4.0, Paint::filled(c));
        }
    }

    void PianoApp::render(IRenderTarget &target, double /*nowMs*/)
    {
        const Transform d = designToScreen();
        target.save();
        target.setTransform(d);
        drawRoundedRect(target, Rect{0, 0, DW, DH}, 0.0, Paint::filled(kBg));
        drawRoundedRect(target, Rect{0.5, 0.5, DW - 1, DH - 1}, 10.0, Paint::stroked(a(kAccent, 0.3), 1.0));

        drawHeader(target);
        drawKeyboard(target);
        drawPedals(target);
        drawMeter(target);

        target.restore();
    }
}
}
