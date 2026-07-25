/*
 *  Arstro Piano — a one-octave playable keyboard UI: draws a piano keyboard with
 *  Artboard and sounds it with the physical-modeling PianoEngine (8-voice pool
 *  of PianoVoice sharing one PianoBridge — see
 *  DigitalSignalProcessing/src/physical/README.md).
 *
 *  Platform-free: only Artboard's IRenderTarget + PianoEngine. Mirrors
 *  examples/synth/SynthApp's shape (same render()/renderAudio()/key() split) so
 *  it runs under the same native/web host pattern; unlike SynthApp it has no
 *  pages/knobs, just the keyboard + pedal indicators + a small output meter.
 *
 *  Threading: unlike SynthEngine, PianoEngine has no internal lock-free queue —
 *  DigitalSignalProcessing/apps/piano_demo/main.cpp only ever drives it from one
 *  thread. This app has two (GTK UI thread + ALSA audio thread), so key()
 *  (UI thread) only enqueues a Cmd into a small mutex-guarded queue; ALL
 *  PianoEngine mutation (noteOnMidi/noteOff/pedals) happens inside
 *  renderAudio() on the audio thread, which drains the queue first. This keeps
 *  every PianoEngine/PianoVoice access on a single thread without needing to
 *  touch PianoEngine itself.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "../../DigitalSignalProcessing/apps/piano_demo/PianoEngine.h"
#include <array>
#include <mutex>
#include <vector>

namespace arstro
{
namespace examples
{
    class PianoApp
    {
    public:
        PianoApp(double width, double height);

        void setSampleRate(double sr) { AudioConfig::instance().setSampleRate(sr); }
        void renderAudio(float *interleaved, int frames);          // audio thread
        void render(artboard::IRenderTarget &target, double nowMs); // UI thread

        // UI thread input. code follows the DOM-keyCode-ish convention already
        // used by examples/synth (letters -> ASCII, arrows -> 37-40); this app
        // also recognizes 16=Shift, 17=Ctrl, 32=Space for the three pedals.
        void key(int code, bool down);

    private:
        static constexpr int kSemitones = 13; // one octave inclusive, C..C
        static constexpr double DW = 720.0, DH = 300.0; // design space

        void drawKeyboard(artboard::IRenderTarget &t);
        void drawPedals(artboard::IRenderTarget &t);
        void drawHeader(artboard::IRenderTarget &t);
        void drawMeter(artboard::IRenderTarget &t);
        artboard::Transform designToScreen() const;

        int midiFor(int semitone) const { return 12 * (mBaseOctave + 1) + semitone; }

        // Commands enqueued by key() (UI thread), applied to mEngine exclusively
        // inside renderAudio() (audio thread) — see class-doc threading note.
        enum class CmdType { NoteOn, NoteOff, Sustain, Sostenuto, UnaCorda };
        struct Cmd { CmdType type; int note = 0; double vel = 0.0; bool on = false; };
        std::mutex mCmdMutex;
        std::vector<Cmd> mPendingCmds;
        void enqueue(Cmd c);

        double mW, mH;
        PianoEngine mEngine;

        int mBaseOctave = 4; // 'a' = C4 (MIDI 60), matches examples/synth's `60 + i` default
        bool mSustain = false, mUnaCorda = false, mSostenuto = false;
        std::array<bool, kSemitones> mKeyHeld{}; // UI-thread-only, for drawing

        std::mutex mAudioMutex;
        double mMeterL = 0.0, mMeterR = 0.0;
    };
}
}
