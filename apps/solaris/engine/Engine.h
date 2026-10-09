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
        void route(const Target &t, const double *L, const double *R, int n, double gain, PortBuffers &out, int outOffset);
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
    };
}
}
}
