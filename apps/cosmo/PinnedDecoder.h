/*
 *  Cosmo by arstro — the ONE way the host constructs a decoder (R-CPU-2c, D-41).
 *
 *  `NativeImageDecoder` decodes; this pins the thread it is decoding on first. That is the
 *  entire class, and the reason it exists is that the pin used to be somebody else's job.
 *
 *  D-12's fix hung `pinNestedOpenMPForThisThread()` on `CosmoService::setWorkerInit`, which
 *  `OrderedParallelLoad` calls on each pool worker. Correct, and only for the pool: opening
 *  one photo (`openImageFile`), opening a `.cosmo`, and the synchronous workspace load all
 *  decode on the GTK **main thread**, and `cosmo-cc info` / `params --print` decode on their
 *  own — six call sites that each constructed a bare `NativeImageDecoder` and were therefore
 *  outside the CPU budget entirely. Measured on 16 cores: a bare decode took 4.6 cores and 20
 *  OS threads at `cpuPercent=25` and 4.5 cores at 100% — identical, so the setting did
 *  nothing to it — while `project` and `render` on the same box tracked the budget (3.3 ->
 *  7.7 cores). The control, the same decode with the team pinned before `exec`, was 1.5 cores
 *  and 9 threads.
 *
 *  This is D-11's lesson applied a second time: the answer to "the limit does not limit" was
 *  not a better clamp, it was a single owner. So the pin stops being a hook a caller must
 *  remember and becomes a property of decoding — a caller cannot construct a decoder without
 *  it, because there is no other decoder in the host to construct.
 *
 *  Host layer on purpose, and it has to be: `OmpPin` needs `dlsym`/`GetProcAddress` and
 *  `getenv`, none of which `cosmo_core` is allowed to carry. That is also why this is a
 *  wrapper rather than a change inside `NativeImageDecoder`.
 */
#pragma once
#include "OmpPin.h"
#include "core/ThreadBudget.h"
#include "core/decode/NativeImageDecoder.h"

#include <memory>
#include <string>

namespace arstro
{
namespace cosmo_v2
{
    class PinnedDecoder : public arstro::cosmo::IImageDecoder
    {
    public:
        /** With no budget attached the nested team is **1**, which is the POOL's answer:
         *  `ThreadBudget::decodeWorkers()` workers each opening a team of one is exactly the
         *  budget, and anything wider is the oversubscription R-CPU-2(c) exists to stop. So
         *  `makePinnedDecoder()` — what every `setDecoderFactory` hands the service — leaves
         *  this null on purpose.
         *
         *  Attach a budget for a decoder that runs ALONE, off the pool: the host's own
         *  `Host::decoder` (opening one photo, a `.cosmo`, the workspace load) and
         *  `cosmo-cc info`. There is no outer parallelism there to oversubscribe against, so
         *  a team of one would spend a single core of a budget that allows `total()` — and
         *  it measurably does: hard-coding 1 made opening a 24 MP ARW take 1.85 s against
         *  0.79 s, at 100% as much as at 25%. Fixing a budget violation by leaving the
         *  budget unused is not fixing it. */
        void setBudget(const arstro::cosmo::ThreadBudget *b) { mBudget = b; }

        arstro::cosmo::DecodedImage decodeFile(const std::string &path,
                                              arstro::cosmo::Fidelity f) override
        {
            // Pinned exactly as the full-fidelity overload is: a cheaper demosaic is still a
            // demosaic, and LibRaw's OpenMP team is sized from the environment either way
            // (R-CPU-2c / D-41).
            pinNestedOpenMPForThisThread(teamSize());
            return mInner.decodeFile(path, f);
        }

        arstro::cosmo::DecodedImage decodeFile(const std::string &path) override
        {
            pinNestedOpenMPForThisThread(teamSize());
            return mInner.decodeFile(path);
        }

        arstro::cosmo::DecodedImage decodeThumb(const std::string &path, int maxEdge) override
        {
            // Pinned too, even though the RAW path here reads the camera's embedded preview
            // and does no demosaic: it falls back to a full `decodeRaw` when a file carries
            // no preview, and a path that is *usually* cheap is not a path that may be
            // outside the budget.
            pinNestedOpenMPForThisThread(teamSize());
            return mInner.decodeThumb(path, maxEdge);
        }

        /** NOT pinned, and the only forwarder here that is not: `readMetadata` parses a header
         *  and never demosaics, so there is no OpenMP team to size. Forwarded all the same,
         *  because the default in the base returns nothing — which is exactly how the metadata
         *  panel came up empty on a file whose Exif a standalone harness read fine. */
        arstro::cosmo::ImageMetadata readMetadata(const std::string &path) override
        {
            return mInner.readMetadata(path);
        }

        void setProgress(Progress p) override { mInner.setProgress(std::move(p)); }

        // Static passthroughs so a caller asking about RAW support does not need to reach
        // past this type to the one it wraps — reaching past it is how the pin got skipped.
        static bool isRawExtension(const std::string &path)
        {
            return arstro::cosmo::NativeImageDecoder::isRawExtension(path);
        }
        static bool rawSupported() { return arstro::cosmo::NativeImageDecoder::rawSupported(); }

    private:
        /** Re-read on EVERY decode, not cached: R-CPU-3 says a budget change takes effect on
         *  the next load and the next render, and re-applying the ICV is what makes that true
         *  for a thread that was pinned under the old value. */
        int teamSize() const { return mBudget ? mBudget->total() : 1; }

        arstro::cosmo::NativeImageDecoder mInner;
        const arstro::cosmo::ThreadBudget *mBudget = nullptr;
    };

    /** What every `setDecoderFactory` in the host hands the service. One spelling, so a new
     *  front end copies the pinned one rather than the bare one. */
    inline std::unique_ptr<arstro::cosmo::IImageDecoder> makePinnedDecoder()
    {
        return std::unique_ptr<arstro::cosmo::IImageDecoder>(new PinnedDecoder());
    }
}
}
