#include "ParamRegistry.h"
#include "Effects.h"

namespace arstro
{
namespace interstellar
{
    using O = ParamOwner;
    using PK = ParamKind;

    const std::vector<ParamDef> &paramDefs()
    {
        // Engine units and ranges: what Cosmo stores in the .cmp. Ranges are the editable span the
        // panels expose (apps/cosmo/EditControls.h through its unit conversions), not hard clamps.
        static const std::vector<ParamDef> defs = {
            // ── basic ──
            {O::Cosmo, "<bind>.basic.exposure", "basic", "exposure", PK::Scalar, "EV", -5, 5, 0, "Exposure."},
            {O::Cosmo, "<bind>.basic.contrast", "basic", "contrast", PK::Scalar, "", -100, 100, 0, "Contrast."},
            {O::Cosmo, "<bind>.basic.highlights", "basic", "highlights", PK::Scalar, "", -100, 100, 0, "Highlights."},
            {O::Cosmo, "<bind>.basic.shadows", "basic", "shadows", PK::Scalar, "", -100, 100, 0, "Shadows."},
            {O::Cosmo, "<bind>.basic.whites", "basic", "whites", PK::Scalar, "", -100, 100, 0, "Whites."},
            {O::Cosmo, "<bind>.basic.blacks", "basic", "blacks", PK::Scalar, "", -100, 100, 0, "Blacks."},
            {O::Cosmo, "<bind>.basic.temp", "basic", "temp", PK::Scalar, "K", 2000, 19500, 6500,
             "White balance temperature, Kelvin. Group offsets stack by their offset from 6500."},
            {O::Cosmo, "<bind>.basic.tint", "basic", "tint", PK::Scalar, "", -150, 150, 0, "White balance tint."},
            {O::Cosmo, "<bind>.basic.vibrance", "basic", "vibrance", PK::Scalar, "", -100, 100, 0, "Vibrance."},
            {O::Cosmo, "<bind>.basic.saturation", "basic", "saturation", PK::Scalar, "", -100, 100, 0, "Saturation."},
            {O::Cosmo, "<bind>.basic.texture", "basic", "texture", PK::Scalar, "", -100, 100, 0, "Texture."},
            {O::Cosmo, "<bind>.basic.clarity", "basic", "clarity", PK::Scalar, "", -100, 100, 0, "Clarity."},
            {O::Cosmo, "<bind>.basic.dehaze", "basic", "dehaze", PK::Scalar, "", 0, 100, 0, "Dehaze."},
            {O::Cosmo, "<bind>.basic.grainAmount", "basic", "grainAmount", PK::Scalar, "", 0, 100, 0,
             "Grain amount; seeded from (source, frame) so a render is reproducible (R-RENDER-2)."},
            {O::Cosmo, "<bind>.basic.grainSize", "basic", "grainSize", PK::Scalar, "", 0, 100, 0, "Grain size."},
            // ── detail ──
            {O::Cosmo, "<bind>.detail.sharpenAmount", "detail", "sharpenAmount", PK::Scalar, "", 0, 150, 0, "Sharpen amount."},
            {O::Cosmo, "<bind>.detail.sharpenRadius", "detail", "sharpenRadius", PK::Scalar, "px", 0.5, 3, 1, "Sharpen radius."},
            {O::Cosmo, "<bind>.detail.sharpenMasking", "detail", "sharpenMasking", PK::Scalar, "", 0, 100, 0, "Sharpen masking."},
            {O::Cosmo, "<bind>.detail.nrLuminance", "detail", "nrLuminance", PK::Scalar, "", 0, 100, 0, "Luminance noise reduction."},
            {O::Cosmo, "<bind>.detail.nrColor", "detail", "nrColor", PK::Scalar, "", 0, 100, 0, "Colour noise reduction."},
            {O::Cosmo, "<bind>.detail.lensDistortion", "detail", "lensDistortion", PK::Scalar, "", -100, 100, 0, "Lens distortion."},
            {O::Cosmo, "<bind>.detail.lensCA", "detail", "lensCA", PK::Scalar, "", 0, 100, 0, "Defringe."},
            {O::Cosmo, "<bind>.detail.lensVignette", "detail", "lensVignette", PK::Scalar, "", -100, 100, 0, "Vignette."},
            // ── curve ──
            {O::Cosmo, "<bind>.curve.curve", "curve", "curve", PK::Points, "x,y;…", 0, 1, 0,
             "Master tone curve: control points `x,y[,ix,iy,ox,oy];…` in 0..1."},
            {O::Cosmo, "<bind>.curve.curveLog", "curve", "curveLog", PK::Bool, "", 0, 1, 1, "Curve in log domain."},
            {O::Cosmo, "<bind>.curve.curveR", "curve", "curveR", PK::Points, "x,y;…", 0, 1, 0, "Red channel curve."},
            {O::Cosmo, "<bind>.curve.curveG", "curve", "curveG", PK::Points, "x,y;…", 0, 1, 0, "Green channel curve."},
            {O::Cosmo, "<bind>.curve.curveB", "curve", "curveB", PK::Points, "x,y;…", 0, 1, 0, "Blue channel curve."},
            // ── mixer ──
            {O::Cosmo, "<bind>.mixer.mixer0", "mixer", "mixer0", PK::Points, "x,y;…", 0, 1, 0, "Colour mixer: hue curve."},
            {O::Cosmo, "<bind>.mixer.mixer1", "mixer", "mixer1", PK::Points, "x,y;…", 0, 1, 0, "Colour mixer: saturation curve."},
            {O::Cosmo, "<bind>.mixer.mixer2", "mixer", "mixer2", PK::Points, "x,y;…", 0, 1, 0, "Colour mixer: luminance curve."},
            {O::Cosmo, "<bind>.mixer.mixerSpread", "mixer", "mixerSpread", PK::Scalar, "", 0, 100, 25,
             "How far the mixer's hue selection reaches into the neighbourhood."},
            // ── grade ──
            {O::Cosmo, "<bind>.grade.grade0", "grade", "grade0", PK::Triple, "h,s,l", 0, 0, 0, "Shadows wheel: hue°, sat, lum."},
            {O::Cosmo, "<bind>.grade.grade1", "grade", "grade1", PK::Triple, "h,s,l", 0, 0, 0, "Midtones wheel: hue°, sat, lum."},
            {O::Cosmo, "<bind>.grade.grade2", "grade", "grade2", PK::Triple, "h,s,l", 0, 0, 0, "Highlights wheel: hue°, sat, lum."},
            {O::Cosmo, "<bind>.grade.balance", "grade", "balance", PK::Scalar, "", -100, 100, 0, "Wheel balance."},
            {O::Cosmo, "<bind>.grade.remapEnable", "grade", "remapEnable", PK::Bool, "", 0, 1, 0, "Hue remap on."},
            {O::Cosmo, "<bind>.grade.remapSrc", "grade", "remapSrc", PK::Scalar, "°", 0, 360, 0, "Hue remap: source hue."},
            {O::Cosmo, "<bind>.grade.remapRange", "grade", "remapRange", PK::Scalar, "°", 0, 180, 30, "Hue remap: range."},
            {O::Cosmo, "<bind>.grade.remapDst", "grade", "remapDst", PK::Scalar, "°", 0, 360, 0, "Hue remap: destination hue."},
            {O::Cosmo, "<bind>.grade.remapStrength", "grade", "remapStrength", PK::Scalar, "", 0, 100, 0, "Hue remap: strength."},
            // ── xform — the SOURCE's framing (R-FX-4: not a clip's geom.crop) ──
            {O::Cosmo, "<bind>.xform.crop", "xform", "crop", PK::Quad, "x,y,w,h", 0, 1, 0,
             "Source crop, normalised. Part of the look, one per source (R-FX-4)."},
            {O::Cosmo, "<bind>.xform.rotation", "xform", "rotation", PK::Scalar, "°", -45, 45, 0, "Straighten."},
            {O::Cosmo, "<bind>.xform.quarterTurns", "xform", "quarterTurns", PK::Int, "", 0, 3, 0, "Quarter turns clockwise."},

            // ── Interstellar's data about a rack node (never colour) ──
            {O::RackObj, "<bind>.weight", "", "weight", PK::Scalar, "0..1", 0, 1, 1,
             "Grade weight: a continuous bypass, blending ungraded→graded (R-RACK-4)."},
            {O::RackObj, "<bind>.bypass", "", "bypass", PK::Bool, "", 0, 1, 0, "Cosmo's bypass for the node."},
            {O::RackObj, "<bind>.parallelMix", "", "parallelMix", PK::Scalar, "0..1", 0, 1, 1,
             "A parallel node's share: how much of its difference from the input is added to its source's result (R-CLR-3)."},
            {O::RackObj, "<bind>.frame", "", "frame", PK::Scalar, "s", 0, 0, 0,
             "Reference frame a video source is graded on (R-RACK-3). Same as `rack frame`."},
            {O::RackObj, "<bind>.input", "", "input", PK::Text, "rec709|srgb|linear|logc3|logc4|slog3|vlog|clog3|log3g10|bmdfilm5", 0, 0, 0,
             "What the source IS: its input colour transform into the working space, before Cosmo grades it "
             "(R-COLOR-2). The media's interpretation, never a grade."},
            {O::RackObj, "<bind>.lut", "", "lut", PK::Text, "<file.cube>|none", 0, 0, 0,
             "An input LUT on the source, after its input transform and before Cosmo (R-COLOR-5); `none` clears it."},

            // ── clip (arrangement; on a derived version a write becomes a #tlset) ──
            {O::Clip, "<clip>.at", "", "at", PK::Scalar, "s", 0, 0, 0, "Timeline position of the clip's first frame."},
            {O::Clip, "<clip>.in", "", "in", PK::Scalar, "s", 0, 0, 0, "Source in-point."},
            {O::Clip, "<clip>.out", "", "out", PK::Scalar, "s", 0, 0, 0, "Source out-point (exclusive)."},
            {O::Clip, "<clip>.speed", "", "speed", PK::Scalar, "×", 0.1, 8, 1, "Playback speed."},
            {O::Clip, "<clip>.angle", "", "angle", PK::Int, "", 0, 99, 0,
             "A multicam clip's angle: the placed timeline's video track it shows; 0 = all of it (R-EDT-5)."},
            {O::Clip, "<clip>.opacity", "", "opacity", PK::Scalar, "0..1", 0, 1, 1, "Clip opacity."},
            {O::Clip, "<clip>.blend", "", "blend", PK::Text, "normal|add|multiply|screen", 0, 0, 0, "Blend mode."},
            {O::Clip, "<clip>.fit", "", "fit", PK::Text, "contain|cover|stretch", 0, 0, 0, "How the source fits the frame."},
            {O::Clip, "<clip>.geom.x", "", "geom.x", PK::Scalar, "frame", -2, 2, 0, "Horizontal offset, in frame widths."},
            {O::Clip, "<clip>.geom.y", "", "geom.y", PK::Scalar, "frame", -2, 2, 0, "Vertical offset, in frame heights."},
            {O::Clip, "<clip>.geom.scale", "", "geom.scale", PK::Scalar, "×", 0.01, 10, 1, "Scale."},
            {O::Clip, "<clip>.geom.rotation", "", "geom.rotation", PK::Scalar, "°", -360, 360, 0, "Rotation about the anchor."},
            {O::Clip, "<clip>.geom.anchor.x", "", "geom.anchor.x", PK::Scalar, "0..1", 0, 1, 0.5, "Anchor x."},
            {O::Clip, "<clip>.geom.anchor.y", "", "geom.anchor.y", PK::Scalar, "0..1", 0, 1, 0.5, "Anchor y."},
            {O::Clip, "<clip>.geom.crop.x", "", "geom.crop.x", PK::Scalar, "0..1", 0, 1, 0, "Clip reframe crop x (R-FX-4)."},
            {O::Clip, "<clip>.geom.crop.y", "", "geom.crop.y", PK::Scalar, "0..1", 0, 1, 0, "Clip reframe crop y."},
            {O::Clip, "<clip>.geom.crop.w", "", "geom.crop.w", PK::Scalar, "0..1", 0, 1, 1, "Clip reframe crop width."},
            {O::Clip, "<clip>.geom.crop.h", "", "geom.crop.h", PK::Scalar, "0..1", 0, 1, 1, "Clip reframe crop height."},
            {O::Clip, "<clip>.name", "", "name", PK::Text, "", 0, 0, 0, "Clip name (what an address spells)."},

            // ── track ──
            {O::Track, "<track>.opacity", "", "opacity", PK::Scalar, "0..1", 0, 1, 1, "Track opacity."},
            {O::Track, "<track>.blend", "", "blend", PK::Text, "normal|add|multiply|screen", 0, 0, 0, "Track blend mode."},
            {O::Track, "<track>.mute", "", "mute", PK::Bool, "", 0, 1, 0, "Mute (video: hidden)."},
            {O::Track, "<track>.gain", "", "gain", PK::Scalar, "dB", -60, 12, 0, "Audio track gain."},
            {O::Track, "<track>.name", "", "name", PK::Text, "", 0, 0, 0, "Track name."},

            // ── temporal effect ──
            {O::Fx, "<fx>.radius", "", "radius", PK::Int, "frames", 0, 8, 1, "Temporal footprint (R-VOL-4)."},
            {O::Fx, "<fx>.strength", "", "strength", PK::Scalar, "0..1", 0, 1, 0.5, "Denoise strength."},
            {O::Fx, "<fx>.shutter", "", "shutter", PK::Scalar, "°", 0, 360, 180, "Frame-blend shutter angle."},
            {O::Fx, "<fx>.at", "", "at", PK::Scalar, "s", 0, 0, 0, "Freeze: the source time held."},

            // ── audio clip (suite schema, docs/audio-format.md) ──
            // ── suite-schema audio track (R-AUD-5 amended: its mix) ──
            {O::AudioTrack, "<atrack>.gain", "", "gain", PK::Scalar, "dB", -60, 12, 0, "Track gain."},
            {O::AudioTrack, "<atrack>.pan", "", "pan", PK::Scalar, "-1..1", -1, 1, 0, "Balance: -1 left, +1 right; centre is unity."},
            {O::AudioTrack, "<atrack>.mute", "", "mute", PK::Bool, "", 0, 1, 0, "Mute."},
            {O::AudioTrack, "<atrack>.solo", "", "solo", PK::Bool, "", 0, 1, 0, "Solo: while any track is soloed only soloed tracks sound."},
            {O::AudioTrack, "<atrack>.name", "", "name", PK::Text, "", 0, 0, 0, "Track name."},
            {O::AudioClip, "<aclip>.at", "", "at", PK::Scalar, "s", 0, 0, 0, "Timeline position."},
            {O::AudioClip, "<aclip>.in", "", "in", PK::Scalar, "s", 0, 0, 0, "Source in-point."},
            {O::AudioClip, "<aclip>.out", "", "out", PK::Scalar, "s", 0, 0, 0, "Source out-point."},
            {O::AudioClip, "<aclip>.gain", "", "gain", PK::Scalar, "dB", -60, 12, 0, "Clip gain."},
            {O::AudioClip, "<aclip>.fade", "", "fade", PK::Scalar, "s", 0, 10, 0, "Fade in and out length."},

            // ── caption (R-DLV-1) ──
            {O::Caption, "<caption>.at", "", "at", PK::Scalar, "s", 0, 0, 0, "Timeline time the words appear."},
            {O::Caption, "<caption>.dur", "", "dur", PK::Scalar, "s", 0, 0, 0, "How long they stay (more than zero)."},
            {O::Caption, "<caption>.text", "", "text", PK::Text, "", 0, 0, 0, "The words; \\n is a new line."},
            {O::Caption, "<caption>.name", "", "name", PK::Text, "", 0, 0, 0, "Caption name (what an address spells)."},

            // ── project header ──
            {O::Project, "project.masterGain", "", "masterGain", PK::Scalar, "dB", -60, 12, 0, "Master bus gain."},
            {O::Project, "project.name", "", "name", PK::Text, "", 0, 0, 0, "Project display name."},
        };
        // R-FX-5: every plugin's parameters, generated from the ONE catalog (render/Effects.h) — so a
        // new plugin is documented the moment it exists, and no second list can drift.
        static const std::vector<ParamDef> all = [] {
            std::vector<ParamDef> v = defs;
            v.push_back({O::Effect, "<effect>.enabled", "", "enabled", PK::Bool, "0|1", 0, 1, 1, "The plugin is on (R-FX-5)."});
            v.push_back({O::Effect, "<effect>.mix", "", "mix", PK::Scalar, "0..1", 0, 1, 1, "The plugin's output mixed over its input."});
            for (const auto &t : render::effectCatalog())
                for (const auto &d : t.params)
                    v.push_back({O::Effect, "<effect:" + t.type + ">." + d.key, "", d.key, PK::Scalar, d.unit.empty() ? std::string("0..1") : d.unit,
                                 d.min, d.max, d.def, t.label + ": " + d.label + (d.unit == "px" ? " (source pixels; scales with the proxy)" : "") + "."});
            return v;
        }();
        return all;
    }

    const ParamDef *cosmoKey(const std::string &key)
    {
        for (const auto &d : paramDefs())
            if (d.owner == O::Cosmo && d.key == key) return &d;
        return nullptr;
    }

    const ParamDef *ownerField(ParamOwner owner, const std::string &key)
    {
        for (const auto &d : paramDefs())
            if (d.owner == owner && d.key == key) return &d;
        return nullptr;
    }

    const char *ownerName(ParamOwner o)
    {
        switch (o)
        {
            case O::Cosmo: return "cosmo";
            case O::RackObj: return "rackobj";
            case O::Clip: return "clip";
            case O::Track: return "track";
            case O::Fx: return "fx";
            case O::Effect: return "effect";
            case O::AudioClip: return "aclip";
            case O::AudioTrack: return "atrack";
            case O::Caption: return "caption";
            case O::Project: return "project";
        }
        return "?";
    }

    const char *kindName(ParamKind k)
    {
        switch (k)
        {
            case PK::Scalar: return "scalar";
            case PK::Bool: return "bool";
            case PK::Int: return "int";
            case PK::Points: return "points";
            case PK::Triple: return "triple";
            case PK::Quad: return "quad";
            case PK::Text: return "text";
        }
        return "?";
    }

    const std::vector<std::string> &colourFilters()
    {
        static const std::vector<std::string> f = {"basic", "detail", "curve", "mixer", "grade", "xform"};
        return f;
    }
}
}
