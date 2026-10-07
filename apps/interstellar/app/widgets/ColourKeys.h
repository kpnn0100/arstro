/*
 *  interstellar_v1 — the colour keys a curve can drive, in the order and words of Grade's own
 *  Basic/Detail panel (R-ANIM-3, amended: they are keyed from the timeline's key lane). The address
 *  is `<source>.<filter>.<key>` — Cosmo's EditParamsIO keys, unchanged (law 1).
 */
#pragma once

namespace arstro
{
namespace interstellar_v1
{
    struct ColourKey
    {
        const char *key;     // "basic.exposure"
        const char *label;   // "Exposure"
        const char *group;   // where Animate… lists it: Grade's panels, in Grade's words
    };

    inline const ColourKey *colourKeys(int &count)
    {
        static const ColourKey k[] = {
            {"basic.exposure", "Exposure", "Light"}, {"basic.contrast", "Contrast", "Light"}, {"basic.highlights", "Highlights", "Light"},
            {"basic.shadows", "Shadows", "Light"}, {"basic.whites", "Whites", "Light"}, {"basic.blacks", "Blacks", "Light"},
            {"basic.temp", "Temperature", "Colour"}, {"basic.tint", "Tint", "Colour"}, {"basic.vibrance", "Vibrance", "Colour"},
            {"basic.saturation", "Saturation", "Colour"}, {"basic.texture", "Texture", "Presence"}, {"basic.clarity", "Clarity", "Presence"},
            {"basic.dehaze", "Dehaze", "Presence"}, {"basic.grainAmount", "Grain Amount", "Presence"}, {"basic.grainSize", "Grain Size", "Presence"},
            {"detail.sharpenAmount", "Sharpen Amount", "Detail"}, {"detail.sharpenRadius", "Sharpen Radius", "Detail"},
            {"detail.sharpenMasking", "Sharpen Masking", "Detail"}, {"detail.nrLuminance", "NR Luminance", "Detail"},
            {"detail.nrColor", "NR Colour", "Detail"}, {"detail.lensDistortion", "Distortion", "Detail"}, {"detail.lensCA", "Defringe", "Detail"},
            {"detail.lensVignette", "Vignette", "Detail"},
            // R-ANIM-6: shapes — keyed like numbers, edited in Grade at the key's frame
            {"curve.curve", "Tone Curve", "Curves & Wheels"}, {"curve.curveR", "Red Curve", "Curves & Wheels"}, {"curve.curveG", "Green Curve", "Curves & Wheels"},
            {"curve.curveB", "Blue Curve", "Curves & Wheels"}, {"grade.grade0", "Shadows Wheel", "Curves & Wheels"}, {"grade.grade1", "Midtones Wheel", "Curves & Wheels"},
            {"grade.grade2", "Highlights Wheel", "Curves & Wheels"}, {"mixer.mixer0", "Mixer Hue", "Curves & Wheels"}, {"mixer.mixer1", "Mixer Saturation", "Curves & Wheels"},
            {"mixer.mixer2", "Mixer Luminance", "Curves & Wheels"}, {"xform.crop", "Crop", "Crop"},
        };
        count = (int)(sizeof k / sizeof k[0]);
        return k;
    }
}
}
