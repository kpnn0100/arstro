/*
 *  Arstro ImageProcessing Library
 *
 *  Volume: a video clip's source seen as a lazy (x, y, t) object (R-VOL). A 10 s 1080p clip
 *  is 6 GB in linear float and a 4K one 24 GB, so the volume is an INTERFACE over frames that
 *  materialise on demand, never storage (R-VOL-1).
 *
 *  ── Why the accessor is a window and not a point (R-VOL-2) ──
 *
 *  There is no `at(x, y, t)`. A scalar accessor hides an unbounded decode behind an innocent
 *  call; the window makes the cost visible at the call site and lets residency be bounded by the
 *  range the caller asked for. A caller asks for [t0, t1] once per output frame and gets every
 *  frame in it at once.
 *
 *  ── Why a view is an array of frame POINTERS (R-VOL-3) ──
 *
 *  The frames already exist in the cache. A contiguous window would memcpy each one in: 8 MB of
 *  RGBA8 per 1080p frame, so a radius-2 window copies ~41 MB per output frame (125 MB in the
 *  float layout the architecture doc first sketched) — more than the filter itself costs.
 *  Pointing at the cache's own buffers is free, and the indirection vanishes once a processor
 *  hoists `row(dt, y)` out of its x loop. `VolumeView` is therefore plain data: no virtuals, no
 *  ownership, nothing a hot loop has to call through.
 *
 *  ── Why the frames are SOURCE-space, ungraded RGBA8 (R-VOL-5) ──
 *
 *  Index t is a SOURCE frame, not a timeline frame: a 2x clip's timeline neighbours are two
 *  source frames apart, which is wrong for anything temporal. Frames are ungraded because a
 *  cached graded neighbour would make the answer depend on the grade and drag every neighbour's
 *  parameters into the cache key. They are straight RGBA8 because that is what the host's
 *  decoder hands out (FrameRGBA matches it byte for byte), so the cache holds exactly one copy
 *  of each decoded frame and the provider can decode straight into a cache buffer.
 *
 *  ── The decisions made inside the contract ──
 *
 *    * window(t0, t1) is INCLUSIVE at both ends: [t0, t1], so `frames = t1 - t0 + 1`. A temporal
 *      footprint {before, after} around t is then window(t - before, t + after) with no +1 to
 *      forget, which is the only arithmetic a caller ever does with it.
 *    * window() is STRICT: a range outside [0, frames) is refused, not clamped. The edge policy
 *      (replicate the edge frame) belongs to the driver (temporalWindow / renderTemporal in
 *      TemporalOps.h), which builds a padded view of replicated POINTERS on top of a strict one.
 *      Keeping the volume strict means a caller bug that walks off the clip fails loudly instead
 *      of quietly rendering the last frame forever.
 *    * LIFETIME RULE: a view is valid until the next window() call on the same volume (or its
 *      destruction). window() is the only thing that may evict, so this is the whole rule, and
 *      it is why the cache can recycle a departing frame's buffer for the next decode. A failed
 *      window() also ends the previous view: `out` is reset before anything else happens.
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace arstro
{
    /** One decoded frame: straight (non-premultiplied) RGBA8, tightly packed, row-major —
     *  byte-identical to the host decoder's output so it can decode straight into it. */
    struct FrameRGBA
    {
        std::vector<uint8_t> rgba;
        int width = 0, height = 0;
    };

    /** How many SOURCE frames a processor reads either side of the one it writes.
     *  {0,0} is a point/spatial processor — all seventeen of today's stages (R-VOL-4). */
    struct TemporalFootprint
    {
        int before = 0, after = 0;
    };

    /** The window a set of processors needs when they ALL read the source volume: the widest
     *  reach on each side. NOTE: this is right for side-by-side readers of the source; a
     *  temporal op that reads ANOTHER temporal op's output needs the footprints ADDED, because
     *  each of its neighbours is itself a window. */
    inline TemporalFootprint uniteFootprints(TemporalFootprint a, TemporalFootprint b)
    {
        return {a.before > b.before ? a.before : b.before, a.after > b.after ? a.after : b.after};
    }

    struct VolumeExtent
    {
        int width = 0, height = 0;
        long long frames = 0;   ///< source frame count; valid t is [0, frames)
        double fps = 24.0;      ///< carried for the host; the engine works in frame indices
    };

    /**
     *  A materialised window: plain data, no virtuals, no ownership. `frame[i]` is source frame
     *  t0 + i — or, in a view the driver padded at a clip end, source frame
     *  clamp(t0 + i, 0, frames - 1), so `t0` may then be negative. Valid until the next
     *  window() call on the volume that produced it.
     */
    struct VolumeView
    {
        /** 33 = radius 16 either side. Bounded so the view is a fixed-size value that lives on
         *  the stack and is copied without allocating. */
        static constexpr int kMaxWindow = 33;
        static constexpr int kChannels = 4;   ///< RGBA8, always

        const uint8_t *frame[kMaxWindow] = {};
        int frames = 0, width = 0, height = 0;
        long long t0 = 0;               ///< source index of frame[0]
        std::ptrdiff_t rowStride = 0;   ///< bytes between rows

        /** Header-inline so it folds into the caller's loop: call it once per row, never per
         *  pixel. */
        const uint8_t *row(int dt, int y) const { return frame[dt] + (std::size_t)y * rowStride; }
    };

    /** The lazy (x, y, t) object. Not thread-safe: one renderer drives one volume. */
    class Volume
    {
    public:
        virtual ~Volume() = default;
        virtual VolumeExtent extent() const = 0;

        /** Materialise source frames [t0, t1] — INCLUSIVE both ends — into `out`.
         *  Returns false (and leaves `out` empty) for t0 < 0, t1 >= frames, t1 < t0, more than
         *  kMaxWindow frames, or a frame the source could not produce.
         *  LIFETIME RULE: the view is valid until the next window() call on this volume. */
        virtual bool window(long long t0, long long t1, VolumeView &out) = 0;
    };

    /**
     *  CachedVolume: the lazy volume over the host's decoder.
     *
     *  ── Why residency is the current window and nothing more ──
     *
     *  R-VOL-4 bounds residency by the declared footprint, never by clip length. After
     *  window(t0, t1) returns, the resident set is EXACTLY frames [t0, t1]; every other frame is
     *  gone. A forward walk (t, t+1, …) shares all but one frame between consecutive windows, so
     *  it still decodes each source frame exactly once — in ascending order, which is the only
     *  order a sequential decoder can serve without seeking. The cost of not retaining more is
     *  that stepping BACKWARDS by one frame re-decodes one frame (a seek); that is the honest
     *  trade for a bound that is a number.
     *
     *  ── Why a departing frame's buffer is recycled, not freed ──
     *
     *  At 1080p a frame is 8 MB, at 4K 33 MB. Freeing the frame that leaves the window and
     *  allocating one for the frame that enters it would be an munmap/mmap plus thousands of
     *  page faults per output frame — milliseconds, on the hot path, for nothing. So the leaving
     *  frame's buffer is handed to the provider to decode the entering frame into, and only
     *  buffers left over when the window SHRINKS are released.
     *
     *  ── Why the cap is a refusal, not a soft limit ──
     *
     *  `capBytes` is the hard memory ceiling (a radius-16 window at 8K is 4.4 GB). A window that
     *  cannot fit under it is refused rather than allowed to overshoot, because a published bound
     *  that is quietly exceeded is not a bound. Peak residency during a window() call is
     *  max(previous window, new window), both of which were checked against the cap.
     */
    class CachedVolume : public Volume
    {
    public:
        /** Decode source frame `frame` into `out` (which may already hold a recycled buffer of
         *  the right size — decode into it, do not replace it). Must yield extent().width x
         *  extent().height RGBA8, and must be a pure function of `frame` (R-VOL-7). */
        using Provider = std::function<bool(long long frame, FrameRGBA &out)>;

        CachedVolume(VolumeExtent ext, Provider p, std::size_t capBytes);

        VolumeExtent extent() const override { return mExt; }
        bool window(long long t0, long long t1, VolumeView &out) override;

        /** Bytes held by resident frame buffers right now (their capacity, i.e. real memory). */
        std::size_t residentBytes() const;
        std::size_t residentFrames() const { return mResident.size(); }
        /** Provider calls made so far. A forward walk makes exactly one per source frame. */
        long long decodes() const { return mDecodes; }
        std::size_t capBytes() const { return mCap; }
        /** Bytes of one frame: width * height * 4. */
        std::size_t frameBytes() const;

        /** Release every resident frame (e.g. the clip left the timeline). Ends any view. */
        void clear();

    private:
        struct Entry
        {
            long long t = 0;
            FrameRGBA frame;
        };

        Entry *find(long long t);

        VolumeExtent mExt;
        Provider mProvider;
        std::size_t mCap = 0;
        long long mDecodes = 0;
        /** At most kMaxWindow entries, so a linear scan beats any map. */
        std::vector<Entry> mResident;
        /** Buffers of frames leaving the window, alive only inside window(). */
        std::vector<FrameRGBA> mSpare;
    };
}
