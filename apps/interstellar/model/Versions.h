/*
 *  interstellar_model — Versions: a timeline is a version, resolved live (R-VER, project-format §3).
 *
 *      resolve(timeline) = resolve(base)        ← recursively, unless this one's cut is frozen
 *                            minus every #tldrop
 *                            with  every #tlset applied
 *                            plus  every node declared here outright
 *
 *  The one rule everything here exists to keep: **a derived timeline stores a DELTA, never a
 *  copy** (R-G-3, R-VER-1). So every edit is derived-aware — editing a node this timeline OWNS
 *  edits it in place; editing a node it INHERITS records a `#tlset` or `#tldrop` and touches
 *  nothing else. A version that silently acquired a copy of a base clip would stop receiving the
 *  base's re-cuts for that clip and nobody would know why; the test suite proves that cannot
 *  happen by counting the project's clips across an inherited edit.
 *
 *  Colour behaves identically (§3.2): `gradeDeltas` walks the same chain, nearest timeline wins
 *  per key, and a pinned timeline stops the walk exactly as a frozen cut stops resolution.
 *
 *  A delta whose target is gone is DANGLING: reported by `resolve` and `rebase`, never silently
 *  dropped (R-VER-4). Pruning is a deliberate act, and only of deltas — never of a user's own
 *  clips.
 *
 *  Every function takes timelines and nodes by id OR bind name, and refuses — naming the problem
 *  — rather than guessing. A refused call changes nothing.
 */
#pragma once
#include "Project.h"
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace interstellar
{
    /** Something in a timeline that points at nothing — or at something it can no longer apply to.
     *
     *  `kind` says what dangles, and so what `rebase(prune)` may do about it:
     *    "tlset" / "tldrop" / "tlgrade" — a delta whose target is gone. Prunable.
     *    "transition"                   — this timeline's transition lost a neighbour or its track.
     *                                     Prunable: a dissolve between nothing means nothing.
     *    "clip" / "aclip"               — this timeline's OWN clip whose track is gone. Reported
     *                                     only: it is the user's content, so re-homing it is theirs.
     *    "conflict"                     — the target exists but the delta no longer fits it (an
     *                                     override that leaves a clip no frames after a base trim,
     *                                     a transition now longer than its neighbour). Reported only.
     *  `delta` names the culprit: a local node's id, or for a delta node `deltaId(kind, tl, node)`. */
    struct Dangling
    {
        NodeId delta;
        NodeId target;
        std::string kind;
        std::string why;
    };

    /** "tlset:tl_2:clp_5" — the identity of a delta node, which has no id of its own. */
    std::string deltaId(const std::string &kind, const NodeId &timeline, const NodeId &node);

    struct ResolvedTimeline
    {
        NodeId timeline;
        std::vector<Track> tracks;              // by order, then id
        std::vector<Clip> clips;                // by at, then order, then id
        std::vector<Transition> transitions;    // by id
        std::vector<Marker> markers;            // by at, then id
        std::vector<Caption> captions;          // by at, then id (R-DLV-1)
        std::vector<ATrack> audioTracks;        // by order, then id
        std::vector<AClip> audioClips;          // by at, then id
        /** Every node in the result. `Dangling` marks a node that is shown (so the editor can
         *  offer to fix it) but must not be rendered: its anchor is missing or it has no frames. */
        std::map<NodeId, Provenance> provenance;
        /** Problems THIS timeline owns — its deltas and its own nodes. A node inherited already
         *  broken is marked Dangling in `provenance` but listed in its owner's resolution. */
        std::vector<Dangling> dangling;
    };

    bool resolve(const Project &, const NodeId &timeline, ResolvedTimeline &out, std::string &err);

    /** R-EDT-4: a clip may place another TIMELINE. Would a clip of `into` placing `src` put a timeline
     *  inside itself — because `src` (or anything it places, at any depth) is `into` or a version of
     *  it, which inherits the very clip? True = refused, with `err` naming the loop. */
    bool nestingRefused(const Project &, const NodeId &into, const NodeId &src, std::string &err);
    /** Every timeline that contains itself through nesting — only a hand-edited .isp can (every edit
     *  is checked above). `lint` names them; the render places nothing for the clip that closes it. */
    std::vector<NodeId> nestingCycles(const Project &);

    /** Colour overrides for one rack node in one timeline, accumulated up the base chain —
     *  nearest timeline wins per key. Nearest first. A PINNED timeline contributes its own
     *  deltas and stops the walk: the pin freezes the base's colour, and the base's own deltas are
     *  part of that colour (the caller, who owns the rack snapshot at the pin, supplies it). */
    std::vector<std::pair<std::string, double>> gradeDeltas(const Project &, const NodeId &timeline, const NodeId &rackObj);

    /** A new version of `base` ("" = a new root). It holds nothing — "my base, plus no changes". */
    bool newTimeline(Project &, const std::string &name, const NodeId &base, NodeId &outId, std::string &err);

    /** Set one field of a node as `timeline` sees it. OWNED -> edited in place. INHERITED -> a
     *  `#tlset` carrying just that field; setting it back to the base's value removes the
     *  override, so an undone divergence leaves no delta behind. Refused: unknown or non-editable
     *  keys (listing the editable ones), colour on a clip, a value that does not parse, a
     *  reference outside this timeline, and any result with no frames. */
    bool setField(Project &, const NodeId &timeline, const NodeId &node, const std::string &key,
                  const std::string &value, std::string &err);
    /** `setField` for several keys at once, validated together and applied together — a trim
     *  moves `in` and `at` as one edit, never as two where the first may stand alone. */
    bool setFields(Project &, const NodeId &timeline, const NodeId &node, const Fields &kv, std::string &err);

    /** Remove a node from `timeline`. OWNED -> erased, with this timeline's own nodes that hang
     *  on it (clips on a track, transitions touching a clip, a clip's #fx). INHERITED -> a
     *  `#tldrop` (its `#tlset` goes, it would override nothing), plus this timeline's own nodes
     *  that hang on it. Other versions' deltas on an erased node are left to dangle — visibly. */
    bool dropNode(Project &, const NodeId &timeline, const NodeId &node, std::string &err);

    /** Colour override: `delta` on top of the rack's value for `key`, in this version. Refused
     *  on a pinned version, naming the commit — a pin that yields is not a pin (R-VER-3). */
    bool setGrade(Project &, const NodeId &timeline, const NodeId &rackObj, const std::string &key, double delta,
                  std::string &err);
    /** Remove an override (`key` = "" removes them all) so the key inherits again. */
    bool clearGrade(Project &, const NodeId &timeline, const NodeId &rackObj, const std::string &key, std::string &err);

    bool pinColour(Project &, const NodeId &timeline, const std::string &commit, std::string &err);
    bool unpinColour(Project &, const NodeId &timeline, std::string &err);

    /** Stop inheriting the base's arrangement: materialise this timeline's resolved arrangement
     *  as LOCAL nodes (each copy names its origin in `from=`), fold its #tlset/#tldrop into them,
     *  re-point versions derived from this one at the copies, and set cut=frozen. Refused while
     *  anything dangles — reconcile first, or the freeze would bake the breakage in. */
    bool freezeCut(Project &, const NodeId &timeline, std::string &err);
    /** Follow the base again, by DIFF against the base's present resolution: a copy identical to
     *  its origin is removed, a changed one becomes a #tlset of just the changed fields, an origin
     *  with no copy becomes a #tldrop, and anything without an origin stays local. Freeze then
     *  thaw with no edits in between returns the same deltas. */
    bool thawCut(Project &, const NodeId &timeline, std::string &err);

    struct RebaseReport
    {
        std::vector<Dangling> dangling;         // everything found, before any pruning
        int pruned = 0;                         // removed — or, on a dry run, that would be
        std::string message;
    };
    /** Reconcile a version with its base. Liveness already keeps it current; rebase does the part
     *  liveness cannot — report every dangling delta, and with `prune` remove the prunable ones
     *  (see Dangling::kind). Colour-pin advancement is the caller's, who owns the rack snapshot. */
    RebaseReport rebase(Project &, const NodeId &timeline, bool prune, bool dryRun);

    /** Human-readable: what this version changes against its base. For a frozen cut, the changes
     *  a thaw would record. */
    std::string diff(const Project &, const NodeId &timeline);
}
}
