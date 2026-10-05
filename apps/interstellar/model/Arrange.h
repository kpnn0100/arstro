/*
 *  interstellar_model — Arrange: the cut operations (R-TL-3), every one derived-aware.
 *
 *  None of these touches a node directly. Each is expressed as `setFields` / `dropNode` / a
 *  local add, so the version rule holds by construction rather than by each operation
 *  remembering it: trimming an inherited clip in a derived timeline records a `#tlset`, splitting
 *  one records a `#tlset out` on the original plus a LOCAL right half — and the base, and every
 *  sibling version, never see either.
 *
 *  Two rules every operation keeps:
 *   - **Refuse, never clamp.** An edit that would leave a clip with no frames, or a transition
 *     longer than a neighbour, is refused with the reason. A clamped edit is an edit the user did
 *     not make, discovered later in a render.
 *   - **All or nothing.** A multi-node operation (split, roll, ripple) validates first and, should
 *     a later step still fail, restores the document — a half-applied roll is a corrupted cut.
 *
 *  Times are TIMELINE seconds unless named otherwise (`slip` takes source seconds). They live in a
 *  nested namespace because `move` and `remove` are names the standard library already owns.
 */
#pragma once
#include "Versions.h"

namespace arstro
{
namespace interstellar
{
namespace arrange
{
    enum class Edge { Head, Tail };

    /** A local track in `timeline`, ordered after every track it already shows. `name` "" picks
     *  v0, v1, … (or a0, a1, … for audio). */
    bool addTrack(Project &, const NodeId &timeline, const std::string &kind, const std::string &name, NodeId &outId,
                  std::string &err);

    /** A local clip of rack node `src` (id or bind name) on `track`, which may be inherited.
     *  Source range [in, out), placed at `at`. */
    bool addClip(Project &, const NodeId &timeline, const NodeId &track, const NodeId &src, double in, double out,
                 double at, const std::string &name, NodeId &outId, std::string &err);

    /** Move one edge to timeline time `t`. Trimming the HEAD moves `in` and `at` together, so the
     *  frames that remain stay exactly where they were on the timeline. */
    bool trim(Project &, const NodeId &timeline, const NodeId &clip, Edge edge, double t, std::string &err);

    /** Cut at timeline time `t`, strictly inside the clip. The original keeps the left half (by
     *  `setFields`, so an inherited original gets a `#tlset out`), the right half is a new LOCAL
     *  clip, and a transition leaving the original now leaves the right half. */
    bool split(Project &, const NodeId &timeline, const NodeId &clip, double t, NodeId &outRight, std::string &err);

    /** New position `at`, and optionally a new track ("" = stay). */
    bool move(Project &, const NodeId &timeline, const NodeId &clip, double at, const NodeId &track, std::string &err);

    /** Two ADJACENT clips on one track: the cut between them moves to `t` — the left's out and
     *  the right's in (and at) move together, so the total length is unchanged. */
    bool roll(Project &, const NodeId &timeline, const NodeId &left, const NodeId &right, double t, std::string &err);

    /** Slide the source window by `delta` SOURCE seconds; length and timeline position kept. */
    bool slip(Project &, const NodeId &timeline, const NodeId &clip, double delta, std::string &err);

    /** Remove a clip; with `ripple`, every later clip on its track shifts left by its length. */
    bool remove(Project &, const NodeId &timeline, const NodeId &clip, bool ripple, std::string &err);

    /** R-EDT-1, three-point INSERT: `src` [in, out) placed at `at` on `track`. A clip of that track
     *  spanning `at` is split there, and everything of the timeline at or after `at` — every track's
     *  clips and every placed sound — moves right by the new clip's length, so sync holds. */
    bool insertEdit(Project &, const NodeId &timeline, const NodeId &track, const NodeId &src, double in, double out,
                    double at, NodeId &outId, std::string &err);
    /** R-EDT-1, OVERWRITE: what lies on `track` in [at, at + out - in) is cut away — split at both
     *  ends, the inside removed — and the clip placed there. Nothing else moves. */
    bool overwriteEdit(Project &, const NodeId &timeline, const NodeId &track, const NodeId &src, double in, double out,
                       double at, NodeId &outId, std::string &err);

    /** A local transition between two clips of one track, `a` before `b`. Refused when longer
     *  than either neighbour — it would consume the clip (project-format §7). */
    bool addTransition(Project &, const NodeId &timeline, const NodeId &a, const NodeId &b, const std::string &kind,
                       double dur, NodeId &outId, std::string &err);
}
}
}
