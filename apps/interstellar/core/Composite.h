/*
 *  interstellar_core — Composite: steps 6-8 of the frame pipeline (R-COMP).
 *
 *      6  per-layer geometry   geom.crop -> fit -> scale/rotate about anchor -> translate
 *      7  composite            bottom track to top, per-clip blend + opacity, transitions
 *      8  output               the finished RGBA8 raster
 *
 *  Layers arrive already GRADED (step 5), so a composite is a composite of graded frames and
 *  never of raw ones (R-COMP-1). Colour ran BEFORE geometry, which is why a reframe can never
 *  change a pixel's colour by changing which pixels the colour stages saw — and why a source's
 *  `xform.crop` and a clip's `geom.crop` are different addresses (R-COMP-3).
 *
 *  Straight (non-premultiplied) RGBA8 throughout, matching `RenderService::Frame`, so a frame
 *  can go from the engine to here to a writer with no conversion.
 */
#pragma once
#include "Project.h"
#include <cstdint>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
    struct Raster
    {
        std::vector<uint8_t> rgba;
        int width = 0, height = 0;
        void allocate(int w, int h, uint8_t fill = 0)
        {
            width = w; height = h;
            rgba.assign((size_t)w * h * 4, fill);
        }
        bool empty() const { return width <= 0 || height <= 0 || rgba.empty(); }
    };

    class Composite
    {
    public:
        struct Layer
        {
            const Raster *source = nullptr;   // an already-graded frame
            Geom geom;
            Fit fit = Fit::Contain;
            double opacity = 1.0;
            Blend blend = Blend::Normal;
        };

        /** Place one graded layer into `out`, honouring crop, fit, scale, rotation, anchor and
         *  translation. Nearest-neighbour sampling: v1 states its sampling rather than implying
         *  a quality it does not have. */
        static void placeLayer(const Layer &l, Raster &out);

        /** Bottom to top. `out` is allocated to the project raster and cleared first. */
        static void compose(const std::vector<Layer> &bottomToTop, int width, int height, Raster &out);

        /** A transition is a two-layer mix, resolved here rather than in the timeline. */
        static void mix(const Raster &a, const Raster &b, double tB, Raster &out);

        /** One channel through a blend mode, both operands 0..1. Exposed so a test can assert
         *  the maths without building a raster. */
        static double blendChannel(Blend mode, double base, double over);

    private:
        static void blendPixel(Blend mode, double opacity, const uint8_t *src, uint8_t *dst);
    };
}
}
