/*
 *  interstellar_v1 — the colour keys a curve can drive, in the order and words of Grade's own panels
 *  (R-ANIM-9: their group is where Grade shows them — a Basic/Detail section, or the Curve, Mixer,
 *  Wheels or Xform tab). The address is `<source>.<filter>.<key>` — Cosmo's EditParamsIO keys,
 *  unchanged (law 1).
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
        const char *group;   // where Grade shows it, in Grade's words
    };

    inline const ColourKey *colourKeys(int &count)
    {
        static const ColourKey k[] = {
            {"basic.exposure", "Exposure", "Tone"}, {"basic.contrast", "Contrast", "Tone"}, {"basic.highlights", "Highlights", "Tone"},
            {"basic.shadows", "Shadows", "Tone"}, {"basic.whites", "Whites", "Tone"}, {"basic.blacks", "Blacks", "Tone"},
            {"basic.temp", "Temperature", "Colour"}, {"basic.tint", "Tint", "Colour"}, {"basic.vibrance", "Vibrance", "Colour"},
            {"basic.saturation", "Saturation", "Colour"}, {"basic.texture", "Texture", "Presence"}, {"basic.clarity", "Clarity", "Presence"},
            {"basic.dehaze", "Dehaze", "Presence"}, {"basic.grainAmount", "Grain Amount", "Effects"}, {"basic.grainSize", "Grain Size", "Effects"},
            {"detail.sharpenAmount", "Sharpen Amount", "Sharpening"}, {"detail.sharpenRadius", "Sharpen Radius", "Sharpening"},
            {"detail.sharpenMasking", "Sharpen Masking", "Sharpening"}, {"detail.nrLuminance", "NR Luminance", "Noise Reduction"},
            {"detail.nrColor", "NR Colour", "Noise Reduction"}, {"detail.lensDistortion", "Distortion", "Lens"}, {"detail.lensCA", "Defringe", "Lens"},
            {"detail.lensVignette", "Vignette", "Lens"},
            // R-ANIM-6: shapes — keyed like numbers (fixed only, R-ANIM-10), edited in Grade at the key's frame
            {"curve.curve", "Tone Curve", "Curve"}, {"curve.curveR", "Red Curve", "Curve"}, {"curve.curveG", "Green Curve", "Curve"},
            {"curve.curveB", "Blue Curve", "Curve"}, {"mixer.mixer0", "Mixer Hue", "Mixer"}, {"mixer.mixer1", "Mixer Saturation", "Mixer"},
            {"mixer.mixer2", "Mixer Luminance", "Mixer"}, {"grade.grade0", "Shadows Wheel", "Wheels"}, {"grade.grade1", "Midtones Wheel", "Wheels"},
            {"grade.grade2", "Highlights Wheel", "Wheels"}, {"xform.crop", "Crop", "Xform"},
        };
        count = (int)(sizeof k / sizeof k[0]);
        return k;
    }
}
}
