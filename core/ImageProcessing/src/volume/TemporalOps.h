/*
 *  Arstro ImageProcessing Library
 *
 *  TemporalOps: the three v1 temporal effects (R-FX-2) and the driver that feeds them one
 *  window per output frame. Each effect is inexpressible per frame and proves a different part
 *  of R-VOL:
 *
 *    * TemporalDenoise (radius k) — the quality lever that matters most on real footage;
 *    * FrameBlend      (radius k) — a shutter / frame average; proves windowing;
 *    * freezeRemap     (radius 0) — proves the time axis is addressable. It is a remap of t,
 *      NOT an op: holding a frame changes WHICH source frame is read, not how pixels combine.
 *
 *  ── Why a processor declares a footprint and writes ONE frame ──
 *
 *  `footprint()` is how residency stays bounded by the radius and not the clip (R-VOL-4): the
 *  driver asks the volume for exactly that window and nothing else. `process()` writes the single
 *  frame at `centre`, reading its neighbours through the view's raw row pointers. The inner loops
 *  are row-hoisted plain pointer arithmetic split over row bands with par::parallelFor — the
 *  Sharpen style — never a per-pixel virtual (R-VOL-3: ~4 ms of dispatch per stage per 1080p
 *  frame is fatal at 24 fps).
 *
 *  ── Edge policy: the edge frame is REPLICATED ──
 *
 *  At a clip end the missing neighbours are copies of the edge frame — done by pointing several
 *  view slots at the same cache buffer, so it costs nothing. Replication was chosen over
 *  "shrink the window" because it keeps every op's arithmetic identical at every t (a fixed
 *  2k+1 taps, a fixed centre index), and it is what a shutter physically means at a cut: the
 *  first frame was all there was. The consequence, documented rather than hidden: near a clip
 *  end the edge frame carries more weight — a FrameBlend at t = 0 is (3·f0 + f1 + f2) / 5.
 *  Ops also clamp their taps into the view they are given, so an op handed a narrower view than
 *  its radius (a hand-built one, or a union window centred elsewhere) replicates the same way
 *  instead of reading past the array.
 *
 *  ── Colour domain ──
 *
 *  The volume holds the decoder's 8-bit code values (Volume.h), and both ops work on them
 *  directly. For denoise that is the domain the noise is seen in. For FrameBlend it is the
 *  NLE "frame blending" convention, not a physical shutter: a physical shutter integrates linear
 *  light, which brightens moving highlights relative to this. Listed in NOTES.md as a known limit.
 */
#pragma once
#include "Volume.h"

namespace arstro
{
    class TemporalOp
    {
    public:
        virtual ~TemporalOp() = default;
        virtual TemporalFootprint footprint() const = 0;
        /** Write the ONE output frame at view slot `centre` into `out` (resized to the view's
         *  width x height, tightly packed RGBA8). An invalid view or centre leaves `out` empty. */
        virtual void process(const VolumeView &v, int centre, FrameRGBA &out) const = 0;
    };

    /** Largest radius a symmetric op can declare and still fit one view. */
    constexpr int kMaxTemporalRadius = (VolumeView::kMaxWindow - 1) / 2;

    /**
     *  TemporalDenoise: a DIFFERENCE-GATED temporal average. It is NOT motion-compensated.
     *
     *  ── Why a gate, and what it cannot do ──
     *
     *  Averaging 2k+1 frames divides the noise stddev by up to sqrt(2k+1) where the scene is
     *  static — and smears anything that moves into a ghost, because without optical flow the
     *  filter has no idea WHERE a moving object went. The gate is the cheap honest substitute:
     *  each neighbour pixel's weight falls with its difference from the centre pixel, so a
     *  neighbour that is the same surface plus noise is averaged in and a neighbour that is a
     *  different surface (the object moved) is left out. Static regions denoise; moving edges do
     *  not smear. What it cannot do: denoise the moving object itself (its neighbours are gated
     *  out, so it keeps its noise), or reject motion whose contrast is below the gate (a
     *  low-contrast edge moving over a similar background ghosts partially). Motion compensation
     *  — aligning each neighbour to the centre before the gate — is the fix and is not in v1.
     *
     *  ── The weighting formula ──
     *
     *  Per pixel, per neighbour frame n (the centre itself has weight 1):
     *
     *      d   = (|ΔR| + 2|ΔG| + |ΔB|) / 4                 code values, Δ = neighbour − centre
     *      h   = strength · kGateAtFullStrength             the gate, in code values
     *                                                       (rounded to a quarter code value)
     *      w_n = max(0, 1 − (d / h)²)²                      Tukey's biweight
     *      out = (centre + Σ w_n · neighbour_n) / (1 + Σ w_n)   per RGB channel, in float
     *
     *  (TemporalOps.cpp evaluates the same w_n in integer units, (G − m)(G + m) / G² with
     *  m = min(4d, G) and G = round(4h), so the clamp is branch-free and the loop vectorises.)
     *
     *  One distance for all three channels, so a pixel's channels are weighted alike and the
     *  filter cannot shift hue; absolute differences are summed BEFORE weighting, so an
     *  isoluminant change (red moving over green of equal luma) still registers; green counts
     *  double as the luma-dominant, least-noisy channel. The biweight has compact support: a
     *  neighbour more than h away contributes exactly nothing, so a high-contrast moving edge
     *  leaves no ghost at all rather than a faint one (a Gaussian gate never reaches zero).
     *  Alpha is the centre frame's, untouched — it is not noisy. strength = 0 is an exact copy
     *  of the centre frame.
     */
    class TemporalDenoise : public TemporalOp
    {
    public:
        /** Gate width at strength 1, in code values of d. Wide enough that on 8-bit noise of
         *  stddev 8 a static neighbour keeps ~0.92 of full weight on average (E[d] ≈ 9, so
         *  E[(d/h)²] ≈ 0.04); narrow enough that a moving edge of more than 48/255 ≈ 19%
         *  contrast contributes nothing at all. */
        static constexpr float kGateAtFullStrength = 48.f;

        explicit TemporalDenoise(int radius = 2, float strength = 0.5f);

        void setRadius(int k);           ///< clamped to [0, kMaxTemporalRadius]
        void setStrength(float s);       ///< clamped to [0, 1]
        int radius() const { return mRadius; }
        float strength() const { return mStrength; }

        TemporalFootprint footprint() const override { return {mRadius, mRadius}; }
        void process(const VolumeView &v, int centre, FrameRGBA &out) const override;

    private:
        int mRadius = 2;
        float mStrength = 0.5f;
    };

    /**
     *  FrameBlend: the plain mean of the 2k+1 frames around t — a frame average / synthetic
     *  shutter. All four channels are averaged (correct for opaque video, which decoded frames
     *  are; a translucent straight-alpha source would want a premultiplied mean). Summed in
     *  integers (exact) and rounded to nearest — with 2k+1 taps the mean is never exactly .5,
     *  so the result is the exact rounded mean, which the tests assert with zero tolerance.
     */
    class FrameBlend : public TemporalOp
    {
    public:
        explicit FrameBlend(int radius = 1);
        void setRadius(int k);           ///< clamped to [0, kMaxTemporalRadius]
        int radius() const { return mRadius; }

        TemporalFootprint footprint() const override { return {mRadius, mRadius}; }
        void process(const VolumeView &v, int centre, FrameRGBA &out) const override;

    private:
        int mRadius = 1;
    };

    /** Freeze as a time remap: plays to `freezeAt`, then holds it (an NLE's "add frame hold").
     *  Apply it to t BEFORE taking the window, so a held frame that is also denoised is
     *  denoised with its REAL neighbours rather than with copies of itself — and, because the
     *  remapped window never moves, a held stretch costs no decodes after its first frame. */
    inline long long freezeRemap(long long t, long long freezeAt) { return t < freezeAt ? t : freezeAt; }

    /**
     *  The driver's window step, exposed so a chain can union its footprints and take ONE
     *  window: materialise footprint `fp` around source frame t, clamped to the clip, and build
     *  a padded view of exactly before + 1 + after slots in which out-of-clip slots point at the
     *  edge frame (replication — see the header comment). `centre` receives t's slot (= before).
     *  The padded view's t0 is t − before and may be negative. Fails for t outside the clip, a
     *  negative footprint, or a window wider than kMaxWindow. Same lifetime rule as window().
     */
    bool temporalWindow(Volume &vol, long long t, TemporalFootprint fp, VolumeView &out, int &centre);

    /** Render source frame t through one temporal op: take ONE window for the op's footprint
     *  (temporalWindow) and run the op on it. */
    bool renderTemporal(Volume &vol, const TemporalOp &op, long long t, FrameRGBA &out);
}
