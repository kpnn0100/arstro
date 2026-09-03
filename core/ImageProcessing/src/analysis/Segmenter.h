/*
 *  Arstro ImageProcessing Library
 *
 *  Segmenter: decide, for every pixel, how strongly it belongs to a named SUBJECT, and
 *  return that as a 0..1 likelihood plane. It is what a detection asks of the picture
 *  before it turns the answer into a mask (R-AISEG, `Detection.h`).
 *
 *  ── What this is, and what it is not ──
 *
 *  There is no neural network here and no model file. `arstro_image` has to stay
 *  portable to WASM and Android, carries no assets and opens no files, and a
 *  segmentation network is tens of megabytes of weights plus a runtime. What this is
 *  instead is a CLASSICAL COLOUR DETECTOR, and since 2026-09-03 it is the one described
 *  in `github.com/mpatacchiola/deepgaze` — a generous range gate feeding a histogram
 *  back-projection fitted to the pixels of THIS photograph (R-AISEG-23). `ISegmenter` is
 *  the seam a host with a real model installs; this is the default, not the definition.
 *
 *  ── Three decisions worth knowing before changing anything ──
 *
 *  1. **The features are read display-referred, not in linear light** (R-AISEG-3).
 *     Every published threshold for a skin tone is stated in gamma-encoded terms, and so
 *     is human judgement about "how bright" and "how saturated" something is. The engine
 *     works in linear light for good reasons; a perceptual threshold is the one thing
 *     those reasons do not cover, so the feature pass encodes as it reads.
 *  2. **This produces a LIKELIHOOD, not a region** (R-AISEG-23). Thresholding it,
 *     cleaning it up morphologically and cutting it into blobs is `Detection`'s job,
 *     because those steps are the same whoever answered — the built-in or an installed
 *     model — and a promise to the photographer implemented twice is two promises.
 *  3. **Every radius is a fraction of the short edge**, never a pixel count, because the
 *     same detector runs on images of every size and must give the same answer.
 *
 *  Stateless and allocation-light on purpose: there is no cache to invalidate and no way
 *  for a stale answer to outlive the photo it was computed from.
 */
#pragma once
#include "../base/Image.h"
#include "../base/Pixel.h"
#include <string>
#include <vector>

namespace arstro
{
    /** The subjects a Detect mask can ask for.
     *
     *  **Only `Skin` is built in** (R-AISEG-22, 2026-09-03). Sky, Foliage, Water and Hair were
     *  withdrawn: five subjects of unequal quality made the whole feature read as unreliable,
     *  because a photographer meets whichever one they try first and judges the tool by it. They
     *  come back when each has been built and measured on its own terms.
     *
     *  Their VALUES stay, and that is deliberate: R-AISEG-9 says the number is what a project file
     *  stores, so removing one would silently re-point every mask ever saved. A project holding a
     *  withdrawn subject reopens as a Detect mask with no regions — the same answer a build that
     *  predates the feature gives. An installed model may still declare any of them
     *  (R-AISEG-15). */
    enum class SemanticSubject
    {
        Sky = 0,        // withdrawn from the built-in (R-AISEG-22)
        Skin = 1,       // the one the built-in answers for
        Foliage = 2,    // withdrawn
        Water = 3,      // withdrawn
        Hair = 4,       // withdrawn
        /** Appended, never inserted, for the reason above. Person is here because it is what a
         *  real MODEL can be had for under a permissive licence (R-AISEG-15/16); the built-in
         *  declines it, since "is this a person" is not a question colour and texture can
         *  answer. */
        Person = 5,
        Count = 6
    };

    /** The ONE codec for a subject's name, in both directions (R-SVC-5, R-AISEG-8).
     *
     *  `parseSemanticSubject` accepts the name (`skin`, case-insensitively) or the number, so a
     *  script can say `subject=skin` and be read a year later; the project file keeps the number
     *  because the mask blob is a fixed group of floats and a second textual format for one
     *  field is what R-SVC-5 forbids. Returns false and leaves `out` alone on anything else —
     *  including a withdrawn subject's NAME, which stays parseable on purpose so an old script
     *  fails loudly at the point of use rather than silently detecting skin. */
    const char *semanticSubjectName(SemanticSubject s);
    bool parseSemanticSubject(const std::string &text, SemanticSubject &out);

    /** The seam a real model plugs into (R-AISEG-6).
     *
     *  A host that can run ONNX, NCNN or Core ML implements this and installs it on the engine,
     *  exactly the way `IImageDecoder` and `IComputeBackend` are installed; the built-in becomes
     *  the fallback for whatever it declines. The seam is here NOW rather than later because it
     *  is what makes "the built-in is a default, not the definition of the feature" true.
     *
     *  `segment` returns false to DECLINE — a model that was not trained on this subject, or an
     *  image it cannot take — and the caller falls back to the built-in. Declining is a normal
     *  answer, not an error, for the same reason a compute backend declines a stage it cannot do.
     *
     *  Note what `sensitivity` is NOT for: a model must not threshold with it. It is passed so a
     *  model that has a genuine confidence knob can use one, and the 0..1 plane that comes back
     *  is a LIKELIHOOD which `Detection` thresholds at `sensitivity` like any other (R-AISEG-23). */
    class ISegmenter
    {
    public:
        virtual ~ISegmenter() = default;
        /** `img` is linear-light RGB(A). Fill `out` with w*h values in 0..1 and return true, or
         *  return false to decline. `sensitivity` is 0..1 (R-AISEG-5). */
        virtual bool segment(const Image &img, SemanticSubject subject, float sensitivity,
                             std::vector<Pixel> &out) = 0;
        /** For the log line and the model dump, so which segmenter answered is never a guess. */
        virtual const char *name() const = 0;
    };

    namespace segment
    {
        /** Softening radius of the likelihood plane, as a fraction of the short edge
         *  (R-AISEG-4). */
        constexpr float kRegulariseFraction = 0.006f;

        /** The short edge the analysis actually runs at (R-AISEG-4). A detection decided on a
         *  1600 px preview and again on a 4000 px export is paying several times over for a
         *  picture of the same regions, and getting a slightly DIFFERENT answer each time. Capping
         *  it makes the two identical rather than merely similar. Generous on purpose: the
         *  reduction factor is an integer, so a typical photo is analysed at 1/2 or 1/3. */
        constexpr int kAnalysisEdge = 1024;

        /** ── the deepgaze range gate (R-AISEG-23 step 1) ──────────────────────────────────
         *
         *  HSV bounds that admit anything that could possibly be skin, in real units rather
         *  than OpenCV's. deepgaze's own skin example uses `min [0,58,50]` / `max [30,255,255]`
         *  over H 0..180, S 0..255, V 0..255; these are those numbers converted, and they are
         *  written as constants because the whole point of the gate is that it is a published,
         *  checkable rule rather than a set of magic numbers tuned against one photograph.
         *
         *  Deliberately generous. The gate's job is not to be right — it is to be a SEED for the
         *  histogram that follows, and a seed that has excluded half the faces in the frame
         *  builds a model of the other half. */
        constexpr float kSkinHueMin = 0.f;        // deepgaze  0 * 2
        constexpr float kSkinHueMax = 60.f;       // deepgaze 30 * 2
        constexpr float kSkinSatMin = 58.f / 255.f;
        constexpr float kSkinSatMax = 1.f;
        constexpr float kSkinValMin = 50.f / 255.f;

        /** The gate has to admit enough pixels for a histogram to mean anything. Below this
         *  fraction of the frame the honest answer is "no skin here": building a colour model
         *  out of a few hundred stray pixels and back-projecting it over the whole photo is how
         *  a detector confidently selects a brick wall. */
        constexpr float kMinSeedFraction = 0.0015f;

        /** True when the built-in has a model for `subject` at all — Skin, and nothing else
         *  (R-AISEG-22). A front end uses it to say so; `builtinScore` uses it to return an
         *  empty plane rather than a wrong one. */
        bool builtinHandles(SemanticSubject subject);

        /** The built-in classifier's raw 0..1 LIKELIHOOD for `subject` over `img`, into `out`
         *  (sized w*h). Not a region: see decision 2 at the top of this file.
         *
         *  Runs at `img`'s own size — the caller downscales to `kAnalysisEdge` first, because it
         *  is the caller that knows whether it is going to do anything else at that size. */
        void builtinScore(const Image &img, SemanticSubject subject, std::vector<Pixel> &out);

        /** Turn a raw 0..1 likelihood plane into a binary-ish region, in place: soften it, then
         *  threshold it at `sensitivity` (R-AISEG-4/5).
         *
         *  Exported so that **an installed model uses the same one**. Sensitivity is a promise to
         *  the photographer — 0 takes only what is certain, 1 takes anything suspected — and a
         *  promise implemented twice is two different promises. This is the same rule R-SVC-5
         *  states for the command grammar, applied to a number instead of a string. */
        void scoreToCoverage(std::vector<Pixel> &score, int w, int h, float sensitivity);

        /** Bilinear resample of a scalar plane. Exported for the same reason: a model runs at its
         *  own input size and its answer has to reach the analysis size, and a second resampler
         *  would be a second set of edge conventions. */
        void resamplePlane(const std::vector<Pixel> &src, int sw, int sh,
                           std::vector<Pixel> &dst, int dw, int dh);

        /** Box-average `img` down by an integer factor, in linear light — which is the only place
         *  averaging pixels is physically meaningful, and the reason the engine works in linear
         *  light at all. Exported because `Detection` decides the analysis size. */
        Image downscaleForAnalysis(const Image &img, int factor);
    }
}
