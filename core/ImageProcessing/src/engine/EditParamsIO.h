/*
 *  Arstro ImageProcessing Library
 *
 *  EditParamsIO: plain-text (de)serialization of EditParams. Keeps persistence in
 *  the engine (UI-independent) so a sidecar/session file written by the photo editor
 *  can be read back by anything — another tool, a batch run, a video editor. The
 *  format is line-based `key=value`; deserialize tolerates missing/unknown keys
 *  (they keep their defaults), so it is forward/backward compatible.
 */
#pragma once
#include "EditParams.h"
#include <string>

namespace arstro
{
    /** Serialize an EditParams to a text blob (no surrounding context). */
    std::string serializeParams(const EditParams &p);

    /** Parse a text blob into `out` (tolerant; unknown/missing keys keep defaults). */
    bool deserializeParams(const std::string &text, EditParams &out);

    /** ── Non-finite parameter guards (D-36) ─────────────────────────────────────────
     *
     *  A NaN or an infinity in an `EditParams` field is what turns everything downstream
     *  into NaN pixels, and a NaN pixel used to index a LUT out of bounds and kill the
     *  process. The processors are hardened now, but hardening only downgrades a crash to a
     *  wrong picture — the value should never have got that far.
     *
     *  These live here, with `deserializeParams`, because that is where a bad number enters:
     *  a `.cmp`/`.cosmo` project, an `.apf` preset, or a `set` command (which goes through
     *  the same codec by design). Two functions rather than one, because a FILE and a
     *  COMMAND deserve different answers.
     */

    /** Name of the first scalar that is not a finite number, or nullptr if all are.
     *  For the STRICT path: a `set` carrying a bad value is refused with a message, because
     *  nothing legitimate sends one and a refusal is debuggable. */
    const char *firstNonFiniteParam(const EditParams &p);

    /** Replace every non-finite scalar with its neutral value; returns how many were
     *  replaced. For the LENIENT path: a project or preset with one stray `nan` must still
     *  open — refusing to load somebody's work over one key is the worse failure — so the
     *  field is neutralised and the count reported so the caller can say it happened
     *  instead of repairing in silence. */
    int sanitizeParams(EditParams &p);

    /** ── The control-point list codec ───────────────────────────────────────────────
     *
     *  `x,y[,ix,iy,ox,oy];…` — the format every tone curve, every mixer curve and a path
     *  mask's outline is written in (six numbers = a SMOOTH point with tangent handles, two =
     *  a corner). Exported because a front end that needs to write one of those lists without
     *  serializing a whole `EditParams` — `mask set path=…` is the case that forced it — must
     *  use this parser rather than a second one that drifts (R-SVC-5). */
    std::string formatCurvePoints(const std::vector<CurvePoint> &pts);
    std::vector<CurvePoint> parseCurvePoints(const std::string &s);

    /** ── The mask blob codec ────────────────────────────────────────────────────────
     *
     *  `geometry | localAdjust | dabs | path` — one mask as one line's worth of text, the
     *  value of a `mask=` key in a project and of a `mask` value in a preset.
     *
     *  Exported for the same reason the control-point codec above was, and after the same
     *  lesson: `EditParamsApf` kept its own hand-written copy of this format, the project
     *  side grew a fourth group for a drawn path, and the preset side did not — so a path
     *  mask was silently dropped by every preset (D-57). Two hand-maintained representations
     *  of one format drift the first time one of them grows a field, which is precisely what
     *  R-SVC-5 says and precisely what happened. There is one now. */
    std::string formatMaskBlob(const MaskParams &m);
    MaskParams parseMaskBlob(const std::string &s);

    /** How many scalars the two functions above walk. Exists only so a test can assert the
     *  count and fail when a new `EditParams` field is added without being listed. */
    int guardedParamScalarCount();
}
