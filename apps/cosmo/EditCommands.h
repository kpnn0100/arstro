/*
 *  cosmo_v2 by arstro — the ONE place a moved control becomes a Command (R-TOUCH-1, R-SVC-2/5).
 *
 *  cosmo has two front ends — the desktop shell and the touch shell — and they must not each own
 *  their own idea of how a slider turns into text. A Command is text (R-SVC-5), so every value a
 *  view sends is formatted somewhere; formatted in two places it drifts the first time a parameter
 *  is added, and the drift is silent because both spellings parse.
 *
 *  So the formatting and the command construction live here, shared, and a view is left with only
 *  the part that is genuinely its own: WHEN to send and where to route it (App, or straight to the
 *  service it holds). Nothing here touches a session, a service or a widget — it is pure value
 *  translation, which is also what makes it testable without either shell.
 *
 *  The number format is `precision(7)`, matching what `EditParamsIO` writes, so a value that came
 *  out of a project file goes back in as the same number.
 */
#pragma once
#include "core/service/Command.h"
#include "engine/EditParams.h"
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
namespace editcmd
{
    using Fields = std::vector<std::pair<std::string, std::string>>;

    /** Seven significant digits — EditParamsIO's own precision. */
    std::string num(double v);
    /** A curve/mixer point list as EditParamsIO writes it: "x,y" for a corner point,
     *  "x,y,ix,iy,ox,oy" for a smooth one, ';'-joined. */
    std::string pointsStr(const std::vector<arstro::CurvePoint> &pts);
    /** The geometry group of a mask blob (its first '|'-section) — for creating a NEW mask, whose
     *  adjust is identity and whose dab list is empty. */
    std::string maskBlob(const arstro::MaskParams &m);
    /** Every field `mask set` accepts, for a whole MaskParams the view is handing over. */
    Fields maskFields(const arstro::MaskParams &m);
    /** The twelve `adjust.*` keys, for a whole LocalAdjust. */
    Fields adjustFields(const arstro::LocalAdjust &a);

    /** `set k=v …` — the parameter write every slider, chip and toggle goes through. */
    cosmo::Command set(Fields fields);
    cosmo::Command setNumber(const std::string &key, double value);
    /** `set <key>=<points>` for `curve`, `curve.r/g/b`, `mixer.hue/sat/lum`. */
    cosmo::Command setPoints(const std::string &key, const std::vector<arstro::CurvePoint> &pts);
    /** `set mask=<blob>` — appends a new mask. */
    cosmo::Command addMask(const arstro::MaskParams &m);
    cosmo::Command maskSet(int index, Fields fields);
    cosmo::Command maskDelete(int index);
    /**
     *  The generic write: "make the parameters look like `to`", as the set of keys that differ
     *  from `from`. Both sides are serialised with `serializeParams` and compared line by line,
     *  so it needs no per-field table and can never accept a smaller set of keys than a project
     *  file does (R-SVC-5's one-codec rule, used in both directions).
     *
     *  This exists for a view that edits a whole `EditParams` — the touch shell's tray hands over
     *  a mutated copy rather than naming the field that moved — and it keeps such a view honest:
     *  what leaves is still a `Command` carrying only what changed, not a session write.
     *
     *  **Masks are excluded on purpose.** `mask=` lines APPEND on parse (EditParamsIO), so a
     *  diff that emitted them would add a mask instead of editing one. Mask edits go through
     *  `maskSet` / `maskDelete` / `addMask`, which is what the desktop already does.
     *  Returns a `None` command when nothing differs, so a caller can skip a no-op.
     */
    cosmo::Command diff(const arstro::EditParams &from, const arstro::EditParams &to);

    /** `settings set k=v …` — previewEdge / threads / useGpu / cpuPercent. */
    cosmo::Command settings(Fields fields);
    cosmo::Command select(int node);
    cosmo::Command undo();
    cosmo::Command redo();
    cosmo::Command screen(const char *name);   // "home" | "editor"
}
}
}
