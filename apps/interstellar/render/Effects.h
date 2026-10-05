/*
 *  interstellar_render — Effects: the image-processing plugins that follow Cosmo in a rack node's
 *  stack (R-FX-5, R-FX-6). Knows no project, like everything in this library: an effect is a type,
 *  a mix and named numbers, applied to a raster in place.
 *
 *  The CATALOG is the one list of what exists — each type's label, family and parameters with their
 *  default, range and unit. The service validates against it, the model publishes it, the UI builds
 *  its sliders from it; nobody keeps a second list.
 *
 *  Sizes (radius, length) are in SOURCE pixels and the caller passes `scale` = output pixels per
 *  source pixel, so a 640-px proxy and a 4K render blur by the same share of the picture.
 *
 *  Blur, in kinds:
 *   * Gaussian — three box passes (σ = radius / 2), the standard O(1)-per-pixel approximation; at
 *     three passes it is within a few per cent of a true Gaussian, invisible in a picture.
 *   * Box — one separable sliding-window pass: a flat average, the hard-edged look.
 *   * Directional — the average along a line of `length` at `angle` (a motion blur).
 *   * Zoom — the average along the ray to a centre (a push-in streak); `amount` is the share of the
 *     distance to the centre that is smeared.
 *   * Spin — the average along an arc of `angle` degrees about a centre.
 *  The line and ray kinds sample bilinearly, rows run in parallel. Alpha is carried, never blurred
 *  into a different shape (it is the frame's, and frames here are opaque).
 *
 *  A deep raster (R-COLOR-1) runs the same kernels at 16 bits per component.
 */
#pragma once
#include "Raster.h"
#include <map>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
namespace render
{
    struct EffectParamDef
    {
        std::string key, label;
        double def = 0.0, min = 0.0, max = 1.0;
        std::string unit;                       // "px" (source pixels), "deg", "" (a share 0..1)
    };

    struct EffectTypeDef
    {
        std::string type, label, family;
        std::vector<EffectParamDef> params;
    };

    /** Every plugin there is, in menu order. */
    const std::vector<EffectTypeDef> &effectCatalog();
    /** One type's definition, or nullptr. */
    const EffectTypeDef *effectType(const std::string &type);

    /** One plugin as the render path runs it. */
    struct EffectRun
    {
        std::string type;
        double mix = 1.0;
        std::map<std::string, double> p;        // every catalog key, defaults filled in
        double get(const std::string &k) const
        {
            const auto it = p.find(k);
            return it == p.end() ? 0.0 : it->second;
        }
    };

    /** Apply one effect to `img` in place. `scale` = output px per source px. Returns false only
     *  for an unknown type (the frame is left as it was). */
    bool applyEffect(const EffectRun &e, double scale, Raster &img);
}
}
}
