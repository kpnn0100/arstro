/*
 *  Arstro ImageProcessing Library
 *
 *  Detection: run a subject detector over one photograph and return WHERE it found the
 *  subject, as closed loops of normalised points (R-AISEG-19..24).
 *
 *  ── Why this is a function and not a stage of the render ──
 *
 *  Until 2026-09-03 a Detect mask was segmented inside `applyMaskStack`, on every frame. That
 *  had three consequences and all of them were invisible:
 *
 *    * the cost hid inside the frame time, so a Detect mask made every later slider drag
 *      slower and nothing said why;
 *    * the answer moved when the photo did — the classifier read the fully adjusted pixels, so
 *      pushing exposure up a stop re-decided which pixels were skin and quietly replaced a mask
 *      the photographer had already accepted;
 *    * there was no moment at which to report progress, because there was no operation, only a
 *      side effect of looking at the picture.
 *
 *  Segmentation is a FINDING, not a parameter. A finding costs real time, can be wrong, and is
 *  worth being told about. So it happens once, when asked, over the framed but UNADJUSTED
 *  pixels (R-AISEG-24), and what comes back is geometry that becomes the mask (R-AISEG-21).
 *
 *  ── The pipeline, which is deepgaze's (R-AISEG-23) ──
 *
 *    preparing -> colour -> regions -> shapes -> outline
 *
 *  `Segmenter` answers the `colour` stage — a range gate seeding a histogram back-projection —
 *  and everything after it is here, because it is the same whoever answered: an installed model
 *  (R-AISEG-15) enters at exactly the same point and is thresholded, cleaned and cut into blobs
 *  by the same code. A promise to the photographer implemented twice is two promises.
 */
#pragma once
#include "Contour.h"
#include "Segmenter.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
    /** What a detection found. */
    struct DetectionResult
    {
        /** The regions, as closed loops in normalised framed-image coordinates — the same 0..1
         *  space every mask's geometry lives in. A loop inside a loop is a HOLE: the fill is
         *  even-odd, so a pair of sunglasses stays unselected. */
        std::vector<ContourLoop> regions;
        /** How much of the frame the regions cover, 0..1. Measured off the region plane rather
         *  than summed from the loops, because summing loop areas cannot subtract a hole without
         *  a winding convention this deliberately does not keep. */
        float coverage = 0.f;
        /** False when NOBODY has a model for this subject — the built-in does not (R-AISEG-22)
         *  and no installed model claimed it. Distinct from "found nothing", which is
         *  `handled == true` with no regions, and the two need different words in a UI. */
        bool handled = false;
        /** Which detector answered: "built-in", or an installed model's name (R-AISEG-15). */
        std::string by;
    };

    /** Reported once per stage: a NAMED stage and the fraction complete after it.
     *
     *  Named and not just numbered, because the useful thing to show someone who is waiting is
     *  what is happening, and because a detection that stalls has to be diagnosable from the log
     *  alone (R-AISEG-20). The strings are stable: `preparing`, `colour`, `regions`, `shapes`,
     *  `outline`. */
    using DetectionProgress = std::function<void(const char *stage, float fraction)>;

    /** Detect `subject` in `framedLinear` — linear-light RGB, cropped and rotated but otherwise
     *  UNADJUSTED (R-AISEG-24).
     *
     *  `seg` is the optional real-model seam, asked first and free to decline (R-AISEG-6).
     *  `sensitivity` is 0..1 and is the one knob (R-AISEG-5); it moves the threshold in the
     *  `regions` stage, for the built-in and for a model alike.
     *
     *  Runs to completion on the calling thread. It is called from the render worker, which is
     *  what keeps `pump()` free of it (R-SVC-6); the progress callback is how the caller learns
     *  anything before it returns, so it must not block. */
    DetectionResult detectSubjectRegions(const Image &framedLinear, SemanticSubject subject,
                                         float sensitivity, ISegmenter *seg = nullptr,
                                         const DetectionProgress &onProgress = DetectionProgress());

    namespace detect
    {
        /** Morphological radii, as fractions of the analysis short edge (never pixel counts, for
         *  the reason everything else here is relative).
         *
         *  OPENING first, to erase specks; then CLOSING, to fill the pinholes a specular
         *  highlight leaves in a nose or a forehead. deepgaze does only the opening, and a
         *  likelihood map that has been opened but not closed is lacy — the contour traced round
         *  it is a hundred little excursions into the middle of a cheek, which is exactly the
         *  "not good" this rework was reported for. Closing is the smaller radius of the two by
         *  design: large enough for a highlight, small enough not to weld a hand to a face. */
        constexpr float kOpenFraction = 0.006f;
        constexpr float kCloseFraction = 0.010f;

        /** A blob is kept when it is at least this fraction of the WHOLE FRAME and at least this
         *  fraction of the LARGEST blob. Both, because either alone gets one case wrong: the
         *  absolute floor alone keeps every scrap in a photo full of skin, and the relative floor
         *  alone keeps a speck in a photo whose largest blob is itself a speck.
         *
         *  deepgaze's `BinaryMaskAnalyser` keeps only the single largest contour. Skin is a face
         *  AND two hands, so that rule would throw away most of what was asked for. */
        constexpr float kMinBlobFraction = 0.0008f;
        /** 3% and not something larger, because the relative rule is there to drop scraps and
         *  the absolute floor above already drops specks. A hand is roughly a tenth of the area
         *  of the face it belongs to and an eye is less; a rule tight enough to feel decisive
         *  throws them away, which is the failure this pair of floors was written to avoid. */
        constexpr float kRelBlobFraction = 0.03f;

        /** Douglas-Peucker tolerance for the stored loops, in normalised units — about 1.5 px at
         *  the 1024 px analysis size (R-AISEG-14 as amended). */
        constexpr float kSimplifyTolerance = 0.0015f;

        /** Sigma of the blur applied to the binary region plane before it is traced, as a
         *  fraction of the short edge. Without it the contour is a staircase along pixel edges,
         *  and no amount of simplification afterwards turns a staircase into a jawline. */
        constexpr float kPreTraceBlurFraction = 0.0025f;

        /** A traced loop is dropped below this many points or this normalised area. */
        constexpr int kMinLoopPoints = 8;
        constexpr float kMinLoopArea = 0.0004f;

        /** Erode / dilate a binary plane (values 0 or 1) by a square structuring element of
         *  `radius`, separably. Exported for the unit tests, which check the pair against
         *  hand-built planes — a morphology bug is invisible in a photograph and obvious in a
         *  5x5 grid. A square rather than deepgaze's ellipse: separable min/max is O(pixels)
         *  instead of O(pixels x radius^2), and at these radii the two differ by less than the
         *  blur that follows them. */
        void erodePlane(std::vector<Pixel> &plane, int w, int h, int radius);
        void dilatePlane(std::vector<Pixel> &plane, int w, int h, int radius);

        /** Zero every connected component (8-connected) that fails the two rules above. Returns
         *  the number of components kept. */
        int keepSignificantBlobs(std::vector<Pixel> &plane, int w, int h);
    }
}
