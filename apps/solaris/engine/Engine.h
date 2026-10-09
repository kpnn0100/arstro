/*
 *  solaris_engine — Engine: renders a MixGraph, block after block (R-MIX, R-PLAY-1, R-RENDER-1).
 *
 *  Per strip, in index order (= processing order): sum what earlier strips routed and sent to it;
 *  add its audio regions (Interstellar's linear-amplitude fades) or play its instrument (the block
 *  split at every note event, so a note starts on its exact sample — R-DSP-5); run its rack; tap
 *  pre-fader sends; apply fader and balance pan (unity at centre — Interstellar's law, R-MIX-11);
 *  tap post-fader sends; add into its output. Then the master: its rack, its gain, its ports.
 *
 *  It hosts DSP; it implements none (R-DSP-1). Every device comes from `arstro::DeviceRegistry`.
 *
 *  **Deterministic** (R-RENDER-1): the same graph rendered from the same position gives the same
 *  samples however the caller chops the frames. Devices are WARMED with one engine block of
 *  silence when the engine is built, because the DSP library smooths every parameter write over a
 *  block: without the warm-up, the first block of every render would carry a ramp from each
 *  device's default to the project's value.
 *
 *  **Latency compensated** (R-MIX-17, plugin delay compensation). A device reports how late its
 *  output is (`Device::latency()` — the limiter's lookahead); a strip's rack adds its devices'. At
 *  build the engine walks the graph in processing order: every signal arriving at a strip's input
 *  (its own clips or notes, earlier strips' outputs, sends and keys), at the master and at the ports
 *  is DELAYED to the latest of them, so they all meet in time — a delay line per connection, sized
 *  at build, so rendering never allocates. A bypassed device still delays by its latency (the
 *  timing never jumps with a bypass); a key reaching a compressor behind a latent device in its own
 *  rack is delayed by that device too. What comes out — every port and the captured master — is
 *  `outputLatency()` samples behind the song: an offline render trims it, live playback subtracts it
 *  from the heard position. A strip's meters and stem are `stripLatency(i)` behind. The latencies are
 *  read once, at build, with the bindings at time zero applied: a latency an automation moves while
 *  the song plays is compensated at its starting value (a live `set` of it builds a new engine).
 */
#pragma once
#include "MixGraph.h"
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
class Device;
namespace solaris
{
namespace engine
{
    struct Meter
    {
        float peak[2] = {0, 0};      // the last render call
        float rms[2] = {0, 0};
        float maxPeak[2] = {0, 0};   // since the engine was built or `clearPeaks()`
    };

    /** The engine's output: one buffer per port, `channels × frames`, overwritten by `render`. */
    struct PortBuffers
    {
        std::vector<std::vector<std::vector<float>>> ports; // [port][channel][frame]
    };

    /** A fixed stereo delay, sized when the engine is built (R-MIX-17) — rendering never allocates. */
    struct DelayLine
    {
        std::vector<double> l, r;
        int w = 0;
        int length() const { return (int)l.size(); }
        void size(int n)
        {
            l.assign((size_t)(n > 0 ? n : 0), 0.0);
            r.assign((size_t)(n > 0 ? n : 0), 0.0);
            w = 0;
        }
        /** In place: each sample out is the one that went in `length()` samples ago. */
        void step(double &a, double &b)
        {
            const double ya = l[(size_t)w], yb = r[(size_t)w];
            l[(size_t)w] = a;
            r[(size_t)w] = b;
            a = ya;
            b = yb;
            if (++w == (int)l.size()) w = 0;
        }
        void run(double *a, double *b, int n)
        {
            if (l.empty()) return;
            for (int i = 0; i < n; ++i) step(a[i], b[i]);
        }
    };

    class Engine
    {
    public:
        static constexpr int kBlock = 128;
        static constexpr int kControl = 64; // bindings are evaluated at every multiple of this sample

        /** Build every device. False + `err` on a graph that routes backward or names an unknown
         *  device type (a registry type missing from this build would be a silent part). */
        bool build(const MixGraph &graph, std::string &err);
        const MixGraph &graph() const { return mGraph; }

        long long position() const { return mPos; }
        /** Jump: every instrument goes silent and its state clears; effect tails ring on. */
        void seek(long long sample);

        /** Render `frames` from the current position into `out` and advance. */
        void render(int frames, PortBuffers &out);

        /** Capture these strips' post-fader outputs on every render (stems); [] = none. */
        void captureStrips(const std::vector<int> &strips) { mCapture = strips; }
        /** The k-th captured strip's last render: [2][frames]. */
        const std::vector<std::vector<float>> &captured(size_t k) const { return mCaptured[k]; }
        /** Capture the master bus (after its rack and gain) on every render — the mixdown. */
        void captureMaster(bool on) { mCaptureMaster = on; }
        const std::vector<std::vector<float>> &capturedMaster() const { return mCapturedMaster; }

        /** R-MIX-17: samples every port and the captured master lag the song — the most any path
         *  to an output carries; an offline render trims it, live playback subtracts it. */
        int outputLatency() const { return mOutLatency; }
        /** Samples strip `i`'s output (its meters, its stem) lags the song. */
        int stripLatency(int strip) const
        {
            return strip >= 0 && strip < (int)mStripLatency.size() ? mStripLatency[(size_t)strip] : 0;
        }
        /** A device's latency now (strip −1 = the master's rack); 0 when there is no such device. */
        int deviceLatency(int strip, int device) const;

        const std::vector<Meter> &stripMeters() const { return mStripMeters; }
        const Meter &masterMeter() const { return mMasterMeter; }
        void clearPeaks();

        /** Size `out` for blocks up to `maxFrames`, so `render` never allocates (R-PLAY-2). */
        void prepare(PortBuffers &out, int maxFrames) const;

        // ── live edits, from the thread that renders (the player drains them between blocks) ──
        void setStripGain(int strip, double linear);
        void setStripPan(int strip, double pan);
        void setStripSilent(int strip, bool silent);
        void setMasterGain(double linear) { mGraph.masterGain = linear; }
        void setDeviceBypass(int strip, int device, bool bypass);  // strip −1 = the master's rack

        /** A device parameter, live (registry name). False when there is no such strip/device/name. */
        bool setDeviceParam(int strip, int device, const std::string &name, double value);
        bool setMasterDeviceParam(int device, const std::string &name, double value);
        /** Every binding's value as last evaluated (R-AUTO-7), in the graph's bind order. */
        const std::vector<double> &bindValues() const { return mBindValues; }

        Engine();
        ~Engine();

    private:
        struct StripState;
        void renderPiece(long long p0, int n, PortBuffers &out, int outOffset);
        void route(const Target &t, const double *L, const double *R, int n, double gain, PortBuffers &out, int outOffset,
                   DelayLine *delay = nullptr);
        void routeKey(const Target &t, const double *L, const double *R, int n, double gain, DelayLine *delay = nullptr);
        /** `L·gain` through `delay` into the scratch pair; returns false (nothing done) for no delay. */
        bool delayed(const double *&L, const double *&R, int n, double &gain, DelayLine *delay);
        void compensate(); // R-MIX-17: size every delay line from the devices' latencies (build)
        void meter(Meter &m, const double *L, const double *R, int n);
        void evalBinds(long long sample, bool immediate); // `immediate`: no ramp (build, seek)

        MixGraph mGraph;
        std::vector<std::unique_ptr<StripState>> mStrips;
        std::vector<std::unique_ptr<Device>> mMasterRack;
        std::vector<double> mMasterL, mMasterR;
        std::vector<Meter> mStripMeters;
        Meter mMasterMeter;
        std::vector<int> mCapture;
        std::vector<std::vector<std::vector<float>>> mCaptured;
        bool mCaptureMaster = false;
        std::vector<std::vector<float>> mCapturedMaster;
        long long mPos = 0;
        std::vector<double> mVars, mBindValues, mApplied;   // sized at build: evaluation never allocates
        bool mMasterBound = false;
        double mMasterA = 1, mMasterB = 1;
        long long mMasterRamp = 0;
        // R-MIX-17: latency compensation, sized at build
        int mOutLatency = 0;
        std::vector<int> mStripLatency;
        std::vector<DelayLine> mMasterBypass;   // per master device: its latency, run while it is bypassed
        DelayLine mMasterDelay;                 // the master up to outputLatency (a strip straight to a port came later)
        std::vector<double> mDlyL, mDlyR, mZero; // scratch for a delayed connection; silence to drain one
    };
}
}
}
