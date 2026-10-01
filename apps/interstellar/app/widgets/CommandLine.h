/*
 *  interstellar_v1 — CommandLine: the one place a moved control becomes TEXT (project-format §8).
 *
 *  The front end reports intent only as command lines, so every number and every name it sends is
 *  formatted somewhere; formatted in two places it drifts the first time a key is added, silently,
 *  because both spellings parse. This file is that somewhere — the role cosmo's EditCommands plays
 *  for cosmo's two shells — and it touches no widget and no model.
 *
 *  Numbers are `precision(7)`, EditParamsIO's own, so a value read from a project goes back in as
 *  the same number. Curve and mixer point lists use EditParamsIO's `formatCurvePoints` directly,
 *  so a curve dragged here is spelled exactly as a `.cmp` spells it. An argument containing a
 *  space is double-quoted (and an embedded quote escaped) — `timeline new "Social 30"`.
 *
 *  The GRADE ADDRESS is `<bindName>.<filter>.<key>`: filter ∈ basic|detail|mixer|curve|grade|xform,
 *  key = EditParamsIO's key, the same keys cosmo's RightColumn sends (see GradeInspector).
 */
#pragma once
#include "engine/EditParams.h"
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace interstellar_v1
{
namespace cmd
{
    using Fields = std::vector<std::pair<std::string, std::string>>;

    /** Seven significant digits; -0 and denormal noise print as 0. */
    std::string num(double v);
    /** A time argument, quantised to the frame grid at `fps` (a cut lands ON a frame). */
    std::string seconds(double t, double fps);
    /** Double-quote when the argument holds whitespace, a quote, or is empty. */
    std::string quote(const std::string &arg);
    /** What a person typed, made a legal BIND NAME (letters, digits, `_`; not starting with a
     *  digit; at most 32) — timeline names are bind names (project-format §3), so "Festival cut"
     *  is sent as Festival_cut rather than refused. Empty in → empty out. */
    std::string bindName(const std::string &typed);
    /** EditParamsIO's spelling of a curve / mixer point list. */
    std::string points(const std::vector<arstro::CurvePoint> &pts);
    /** `set b.f.k=v b.f.k2=v2 …` — one line, several addresses (§8: `set <address>=<value> …`). */
    std::string gradeSet(const std::string &bindName, const std::string &filter, const Fields &fields);

    /** HH:MM:SS:FF at `fps` — the monitor's and the transport's mono timecode. */
    std::string timecode(double t, double fps);
}
}
}
