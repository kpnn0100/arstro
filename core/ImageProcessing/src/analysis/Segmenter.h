/*
 *  Arstro ImageProcessing Library
 *
 *  Segmenter: decide, for every pixel, how strongly it belongs to a named SUBJECT —
 *  sky, skin, foliage, water, hair — and return that as a 0..1 coverage plane. It is
 *  what a `MaskParams::Semantic` mask asks instead of asking its geometry (R-AISEG).
 *
 *  ── What this is, and what it is not ──
 *
 *  There is no neural network here and no model file. `arstro_image` has to stay
 *  portable to WASM and Android, carries no assets and opens no files, and a
 *  segmentation network is tens of megabytes of weights plus a runtime. What this is
 *  instead is a per-class STATISTICAL MODEL: a handful of soft criteria over colour,
 *  position in the frame and local structure, multiplied into a score, softened over
 *  a neighbourhood, and thresholded. That is what image segmentation was before deep
 *  learning, it is genuinely good at these five classes, and it is not a network.
 *  Both halves of that sentence belong in the UI copy as much as here.
 *
 *  ── Three decisions worth knowing before changing anything ──
 *
 *  1. **The features are read display-referred, not in linear light** (R-AISEG-3).
 *     Every published threshold for a skin tone, a blue sky or foliage is stated in
 *     gamma-encoded terms, and so is human judgement about "how bright" and "how
 *     saturated". The engine works in linear light for good reasons; a perceptual
 *     threshold is the one thing those reasons do not cover, so the feature pass
 *     encodes as it reads.
 *  2. **A pixel's class is decided with its neighbours** (R-AISEG-4). The raw
 *     per-pixel score is speckled — the same fact R-MIXER-5 is about, met again — so
 *     the score plane is softened BEFORE it is thresholded. A lone pixel that reads
 *     as sky inside a roof must not become a hole.
 *  3. **Every radius is a fraction of the short edge**, never a pixel count, because
 *     the same mask is rendered at 200..1600 px and at full size and the preview has
 *     to keep predicting the export (R-PREVIEW).
 *
 *  Stateless and allocation-light on purpose: a mask's coverage is rebuilt per render
 *  like every other mask's, so there is no cache to invalidate and no way for a stale
 *  mask to outlive the photo it was computed from.
 */
#pragma once
#include "../base/Image.h"
#include "../base/Pixel.h"
#include <string>
#include <vector>

namespace arstro
{
    /** The subjects a semantic mask can ask for (R-AISEG-2).
     *
     *  Their reliability is NOT equal and nothing downstream should pretend it is: Sky and
     *  Foliage are strong, Skin and Water are good, and **Hair is the weakest** — it is found
     *  as *dark, not very saturated, highly textured*, which also describes a wool coat. That
     *  is a classifier being a classifier, and the answer to it is that the mask is invertible,
     *  stackable and editable like any other. */
    enum class SemanticSubject
    {
        Sky = 0,
        Skin = 1,
        Foliage = 2,
        Water = 3,
        Hair = 4,
        Count = 5
    };

    /** The ONE codec for a subject's name, in both directions (R-SVC-5, R-AISEG-8).
     *
     *  `parseSemanticSubject` accepts the name (`sky`, case-insensitively) or the number, so a
     *  script can say `subject=sky` and be read a year later; the project file keeps the number
     *  because the mask blob is a fixed group of floats and a second textual format for one
     *  field is what R-SVC-5 forbids. Returns false and leaves `out` alone on anything else. */
    const char *semanticSubjectName(SemanticSubject s);
    bool parseSemanticSubject(const std::string &text, SemanticSubject &out);

    /** The seam a real model plugs into (R-AISEG-6).
     *
     *  A host that can run ONNX, NCNN or Core ML implements this and installs it on the engine,
     *  exactly the way `IImageDecoder` and `IComputeBackend` are installed; the built-in becomes
     *  the fallback for whatever it declines. The seam is here NOW rather than later because it
     *  is what makes "the built-in is a default, not the definition of the feature" true, and
     *  because retrofitting a seam under a shipped mask type would mean migrating projects.
     *
     *  `segment` returns false to DECLINE — a model that was not trained on this subject, or an
     *  image it cannot take — and the caller falls back to the built-in. Declining is a normal
     *  answer, not an error, for the same reason a compute backend declines a stage it cannot do. */
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
        /** Softening radius of the score plane, as a fraction of the short edge (R-AISEG-4). */
        constexpr float kRegulariseFraction = 0.006f;
        /** Radius the local-structure feature is measured against, same units. Hair is found by
         *  texture and sky by the absence of it, so both need a scale, and it has to be relative
         *  for the same reason everything else here is. */
        constexpr float kStructureFraction = 0.0015f;

        /** The short edge the analysis actually runs at. A coverage plane is smooth by
         *  construction (R-AISEG-4 softens it over 0.6% of the short edge), so deciding it on a
         *  1600 px preview and again on a 4000 px export is paying several times over for a
         *  picture of the same regions — and, worse, getting a slightly DIFFERENT answer each
         *  time, because the structure feature is measured against a radius that scales with the
         *  render. Capping the analysis makes the two identical instead of merely similar, which
         *  is R-PREVIEW's whole promise, and it is why this is a correctness constant and not a
         *  performance one. Measured on a 16-thread desktop, release build: **36.7 ms -> 15.6 ms**
         *  on a 1600x1066 preview and **300 ms -> 44.8 ms** on a 10.7 Mpx export (the remainder
         *  there is the downscale and the upsample, both of which touch every pixel once).
         *
         *  Generous on purpose. The reduction factor is an integer, so a typical photo is
         *  analysed at 1/2 or 1/3 — enough to keep the fine structure Hair is found by, which a
         *  tighter cap would average away before the classifier ever saw it. */
        constexpr int kAnalysisEdge = 1024;

        /** The built-in classifier: `subject`'s coverage of `img`, into `out` (sized w*h).
         *  Always answers — it is the fallback, so it has nothing to decline to. */
        void builtinCoverage(const Image &img, SemanticSubject subject, float sensitivity,
                             std::vector<Pixel> &out);
    }
}
