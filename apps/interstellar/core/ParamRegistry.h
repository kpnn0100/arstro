/*
 *  interstellar_core — ParamRegistry: the whole parameter ADDRESS SPACE, as a table (R-API-1).
 *
 *  An address names one value an agent can write with `set` and read with `get`/`eval`:
 *
 *      s_day01.basic.exposure      a rack node's colour key   owner: cosmo   (writes THROUGH to the .cmp)
 *      s_day01.weight              Interstellar's data about a rack node      owner: rackobj
 *      shotA.at   shotA.geom.x     a clip's arrangement field                 owner: clip
 *      v0.opacity                  a track field                              owner: track
 *      fx_1.radius                 a temporal effect field                    owner: fx
 *      project.masterGain          a project header field                     owner: project
 *
 *  The colour keys ARE Cosmo's `EditParamsIO` keys — the `<filter>` segment only groups them the
 *  way Cosmo's panels do (basic | detail | curve | mixer | grade | xform) and is checked, so a key
 *  under the wrong filter is refused naming the right one rather than silently accepted. Units and
 *  ranges are the ENGINE's (what `.cmp` stores), not a slider track's: an agent writes what Cosmo
 *  stores, and `docs/api.json` says what that is.
 *
 *  A name that is both a rack bind name and a clip name is ambiguous and refused; `rack:`/`clip:`/
 *  `track:` prefixes disambiguate.
 */
#pragma once
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
    enum class ParamOwner { Cosmo, RackObj, Clip, Track, Fx, AudioClip, Project, Effect, AudioTrack };

    enum class ParamKind
    {
        Scalar,     // a number — additive as a version override
        Bool,       // 0 | 1
        Int,
        Points,     // a control-point list `x,y;x,y…` — replaced, not added, as an override
        Triple,     // `h,s,l` (grade wheels)
        Quad,       // `x,y,w,h` (crop)
        Text,       // a name or an enum
    };

    struct ParamDef
    {
        ParamOwner owner;
        std::string pattern;     // "<bind>.basic.exposure"
        std::string filter;      // "basic" (cosmo only), else ""
        std::string key;         // "exposure" — the EditParamsIO key, or the field name
        ParamKind kind;
        std::string unit;        // "EV", "K", "s", "0..1", "dB", …
        double min = 0, max = 0, neutral = 0;
        std::string doc;
    };

    const std::vector<ParamDef> &paramDefs();

    /** The Cosmo key's definition by its EditParamsIO name, or nullptr. */
    const ParamDef *cosmoKey(const std::string &key);
    /** A non-cosmo field of `owner` by name (`weight`, `geom.x`), or nullptr. */
    const ParamDef *ownerField(ParamOwner owner, const std::string &key);

    const char *ownerName(ParamOwner o);
    const char *kindName(ParamKind k);
    /** Every colour filter name, in panel order. */
    const std::vector<std::string> &colourFilters();
}
}
