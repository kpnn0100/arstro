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
    };

    inline const ColourKey *colourKeys(int &count)
    {
        static const ColourKey k[] = {
            {"basic.exposure", "Exposure"}, {"basic.contrast", "Contrast"}, {"basic.highlights", "Highlights"},
            {"basic.shadows", "Shadows"}, {"basic.whites", "Whites"}, {"basic.blacks", "Blacks"},
            {"basic.temp", "Temperature"}, {"basic.tint", "Tint"}, {"basic.vibrance", "Vibrance"},
            {"basic.saturation", "Saturation"}, {"basic.texture", "Texture"}, {"basic.clarity", "Clarity"},
            {"basic.dehaze", "Dehaze"}, {"basic.grainAmount", "Grain Amount"}, {"basic.grainSize", "Grain Size"},
            {"detail.sharpenAmount", "Sharpen Amount"}, {"detail.sharpenRadius", "Sharpen Radius"},
            {"detail.sharpenMasking", "Sharpen Masking"}, {"detail.nrLuminance", "NR Luminance"},
            {"detail.nrColor", "NR Colour"}, {"detail.lensDistortion", "Distortion"}, {"detail.lensCA", "Defringe"},
            {"detail.lensVignette", "Vignette"},
            // R-ANIM-6: shapes — keyed like numbers, edited in Grade at the key's frame
            {"curve.curve", "Tone Curve"}, {"curve.curveR", "Red Curve"}, {"curve.curveG", "Green Curve"},
            {"curve.curveB", "Blue Curve"}, {"grade.grade0", "Shadows Wheel"}, {"grade.grade1", "Midtones Wheel"},
            {"grade.grade2", "Highlights Wheel"}, {"mixer.mixer0", "Mixer Hue"}, {"mixer.mixer1", "Mixer Saturation"},
            {"mixer.mixer2", "Mixer Luminance"}, {"xform.crop", "Crop"},
        };
        count = (int)(sizeof k / sizeof k[0]);
        return k;
    }
}
}
