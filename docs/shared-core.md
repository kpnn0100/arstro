# Nebula — the Arstro shared project, versioning & resource core

> Status: **concept / specification**. Nebula is the proposed shared library that carries the
> project model and version control for Cosmo, Interstellar, and Solaris. This document defines
> the model and the mechanisms; it is idea-level, not an implementation. Illustrative text
> sketches show the *format shape*, not a final grammar.

Nebula sits beside Artboard / ImageProcessing / DigitalSignalProcessing as a shared, UI-free
library. Where those provide **drawing** and **media processing**, Nebula provides **the
project**: how an edit is stored, versioned, merged, embedded, and how its heavy media is
referenced. Every app supplies its own **schema** (what node types exist, what fields they carry)
on top of Nebula's common machinery.

Contents: [1. Project model](#1-the-project-model) · [2. Text format](#2-the-text-project-format)
· [3. Versioning & living branches](#3-versioning--living-branches) · [4. Semantic merge](#4-semantic-3-way-merge)
· [5. Cross-app embedding](#5-cross-app-embedding--propagation) · [6. Project merge](#6-project-merge-combining-two-projects)
· [7. Resource pool](#7-the-resource-pool) · [8. Per-app schemas](#8-what-each-app-plugs-in)

---

## 1. The project model

A Nebula project is a **graph of nodes**, not a document of text lines. Text is only its
serialization (§2). The model is shared by all three apps:

- **Node** — the unit of everything: a group, an image, a clip, a track, an effect, an embed, a
  keyframe. Each node has:
  - a **stable id** (never reused, never reordered-dependent) — the anchor for merge and history;
  - a **type** (app-defined: `image`, `clip`, `track`, `grade`, `embed`, …);
  - **fields** (`key = value`, scalars/enums/short lists) — the non-destructive parameters;
  - **children** (ids), forming the tree/graph;
  - an explicit **order key** where order matters (timeline position, z-index) — so ordering is
    *data*, not *line position*, and reordering never fights a field edit in a merge.
- **Non-destructive.** A node references sources + carries parameters; it never contains pixels or
  samples. Rendering is `sources + params → output`, on demand, by the media engine.
- **Deterministic serialization.** Nodes serialize in a canonical order (by id) with canonical
  number formatting, so the same model always produces byte-identical text — a precondition for
  clean diffs and for hashing a commit.

This is a generalization of what Cosmo already does: its `.cmp` workspace is a text tree of
`#group` / `#image` nodes plus per-image `#hnode` history — Nebula lifts that into the shared
layer and gives the other apps the same treatment.

## 2. The text project format

One line = one fact. Sections are marked by a `#type id` header; fields follow as `key = value`;
children reference ids. Illustrative shape (not final grammar):

```
arstro-project = 1
app = interstellar          ; which schema validates this file
id = prj_7f3a               ; the project's own stable id

#track id=trk_a  kind=video  order=0
#track id=trk_b  kind=video  order=1
#track id=trk_m  kind=audio  order=2

#clip id=clp_12  track=trk_a  order=0  in=0.000  out=4.200  src=res:9c1f… 
  grade = exposure:0.3 contrast:-10 curve:0,0;0.5,0.6;1,1
  blend = normal
#clip id=clp_13  track=trk_a  order=1  in=4.200  out=7.000  src=res:9c1f…
  grade = exposure:0.1 temp:5200
  blend = over  opacity:0.8

#embed id=emb_look  target=cosmo:prj_c0a1  branch=main  as=look
#embed id=emb_song  target=solaris:prj_50a1 branch=main  as=audio  track=trk_m
```

**Merge-friendliness rules** (why this text merges well):

- **Stable ids** mean a node is identified by `id=…`, not by where it sits in the file, so moving
  or inserting nodes elsewhere doesn't collide with editing this one.
- **Order is a field** (`order=`, `in=`/`out=`), never file position, so re-sequencing a timeline
  is a set of field edits, not a wholesale reshuffle of lines.
- **One fact per line** keeps a line-based 3-way merge from over-reporting conflicts, and gives
  the semantic merge a clean unit to reason about.
- **Canonical formatting** means "no change" is literally no diff.
- **References, not copies** (`src=res:<hash>`, `target=<app>:<projid>`) keep media and embeds out
  of the text entirely.

The format degrades gracefully: unknown keys are preserved verbatim (forward-compatible), and a
missing optional section takes its default — the same tolerance Cosmo's reader already has.

## 3. Versioning & living branches

Every project is a small **repository**. Nebula's VCS mirrors git's shape (commits form a DAG;
branches are named pointers) but its **merge/rebase drivers are semantic** (§4), operating on the
parsed node graph rather than raw lines. (It may be *implemented* on git with custom merge
drivers, or as a self-contained store; the semantics below are what matter.)

- **Commit** — an immutable snapshot of the node graph + a parent link + a label. (Cosmo's
  per-image history tree is exactly this idea, per image; Nebula generalizes it to the whole
  project.)
- **Branch** — a named pointer to a commit. `main` is the trunk; feature branches fork from it.
- **Base** — a feature branch records the branch it forked from (`base = main`). This is what
  makes it a *living* branch.

### Auto-rebase (the "living branch")

The headline behavior — *"every new commit on the base branch reaches the feature branch too"*:

```
When base branch B advances (new commit Bn):
  for each feature branch F with base = B:
    replay F's own commits onto Bn   (semantic 3-way rebase, §4)
    if clean:
      move F's base marker to Bn and keep F's commits on top   → F silently gains B's new work
    if conflict:
      pause the rebase, mark F "needs attention",
      surface exactly the conflicting nodes/fields (F's commits are NOT lost)
```

So a feature branch is continuously *"main + my changes"*. The composer's new bar or the
director's new grade flows into every open cut branch the moment it lands — unless it touches the
same node/field the branch changed, in which case only that spot is flagged. This is the same
guarantee across app boundaries via embeds (§5).

Design notes:
- **Idempotent & safe.** A rebase that would conflict never silently drops work; it stops and
  reports. The feature branch's commits are always recoverable.
- **Opt-out.** A branch can pin to a specific base commit (`base = main@<commit>`) to freeze it
  (e.g. lock the delivery cut) instead of following the tip.
- **Cheap.** Commits are small text deltas; a rebase is a semantic replay over a handful of nodes,
  not a media re-render. Media is untouched (it lives in the pool, §7).

## 4. Semantic 3-way merge

Because a project is a graph of stable-id nodes, Nebula merges the **model**, not the text — so it
resolves far more automatically than a line diff, and reserves conflicts for genuine clashes.

Given a common ancestor **O** and two sides **A**, **B**, node by node (keyed by id):

| Situation | Result |
|-----------|--------|
| Node added on one side only | keep it |
| Node added on both with the **same id** (shared ancestor) | merge their fields (recurse) |
| Node added on both with **different ids** | keep both |
| Node deleted on one side, untouched on the other | delete it |
| Node deleted on one side, **edited** on the other | conflict (delete-vs-edit) |
| Field changed on one side only | take the change |
| Field changed on both sides to the **same** value | take it (no conflict) |
| Field changed on both sides to **different** values | **field-level conflict** (only that field) |
| Order/position changed | reconcile via the `order`/time fields; reordering ≠ field conflict |

- **Conflicts are field-scoped**, not file-scoped: two people editing the same clip's `opacity`
  and `in-point` do **not** conflict; two people setting the same clip's `opacity` differently do.
- **Type-aware resolvers** can go further: two keyframe lists on the same curve union by time;
  two dab-lists on the same brush mask concatenate; numeric automation can offer an "average" or
  "prefer newer" policy where that is safe. Apps register resolvers for their node types.
- **A line-based `git merge` still works** as a fallback (the format is line-clean), but the
  semantic driver is the default and is what makes cross-project propagation robust.

## 5. Cross-app embedding & propagation

An **embed** node references *another project*, at a *branch*, playing a *role* in the host:

```
#embed id=emb_song  target=solaris:prj_50a1  branch=main  as=audio  track=trk_m
```

- `target = <app>:<projectId>` — the source project (resolved via the project registry / pool).
- `branch = <name>` (or `<name>@<commit>` to pin) — **what it follows**.
- `as = <role>` — how the host interprets it: Interstellar understands `as=audio` (a Solaris
  song → the timeline's audio bed) and `as=look` (a Cosmo grade → a colour layer).
- optional placement fields (`track=`, `in=`/`out=`, `offset=`) — where it sits in the host.

**Propagation is the auto-rebase of §3 applied across the boundary.** The embed's `branch=main`
means the host tracks the source's `main` tip. When that tip advances:

```
source project S: main advances
  → every host project embedding S@main re-resolves the embed
  → the host's own living feature branches auto-rebase over the new embed content
      clean    → host updates automatically (the MV's audio/colour refreshes)
      conflict → the host branch flags the affected embed/clip only
```

So the composer editing the song in Solaris updates the MV in Interstellar with no manual
re-import — the exact behavior the workflow calls for — and a genuine clash (a cut that depends on
a now-deleted bar) is surfaced precisely rather than silently breaking.

Embeds are **recursive** (an Interstellar project could embed another Interstellar project) and
**resolvable at a pinned commit** for reproducible deliveries. They are conceptually git
submodules that *follow a branch* and are reconciled by the semantic engine instead of a manual
pointer bump.

## 6. Project merge (combining two projects)

Distinct from branch merge: **union two independent projects into one**. This is what "merge two
music projects into one big song" and "merge two edits into one big video" mean.

- **Id namespacing.** Each project's ids are already globally unique (project-id-prefixed), so a
  union never collides. Where two projects were forked from a shared ancestor, matching ids merge
  by §4; otherwise both sides are kept.
- **Timeline composition policy** (chosen at merge time):
  - **concatenate** — B's timeline starts after A's end (one long song / one long video);
  - **overlay** — B's tracks/layers stack onto A's at the same time origin (layer two takes,
    stack two stems);
  - **into a group/track** — B is dropped into a named container in A.
- **Resource pool is shared**, so identical source assets (same hash) are not duplicated on merge.
- The result is a normal Nebula project with its own history; the merge is a commit, so it can be
  branched, rebased, and re-merged like anything else.

## 7. The resource pool

Heavy media never lives in the project text. It lives in a shared, content-addressed pool:

- An asset is keyed by **content hash**; the project references it as `res:<hash>` plus a
  human path hint. Two projects (or two apps) referencing the same file share one pool entry.
- **Moves/renames are transparent** (the hash is the identity); a missing asset is flagged as an
  offline/relinkable reference, never a corrupt project (as Cosmo already does for missing images).
- The pool holds footage, stills, rendered audio/stems, MIDI, LUTs, and presets. Presets/LUTs
  being pool assets is what lets Cosmo and Interstellar share **one look library**, and lets a
  Solaris mixdown be referenced by Interstellar without copying.
- Because projects carry only references + parameters, a project file stays small and textual —
  the precondition for everything in §2–§6.

## 8. What each app plugs in

Nebula is generic; each app registers a **schema** (node types + fields) and **type resolvers**
(merge policies for its types) and delegates rendering to its media engine:

| App | Node types (examples) | Media engine | Renders to |
|-----|-----------------------|--------------|------------|
| **Cosmo** | `group`, `image`, `grade`, `mask`, history `hnode` | ImageProcessing | still frames |
| **Interstellar** | `track`, `clip`, `grade`, `transition`, `keyframe`, `embed` | ImageProcessing (per frame) + timeline | video master |
| **Solaris** | `track`, `clip`, `note`, `rack`, `effect`, `automation`, `bus` | DigitalSignalProcessing | audio mixdown / stems |

All three get the same **text format**, **living branches**, **semantic merge**, **embedding**,
and **resource pool** for free from Nebula. The app-specific details are in
[../interstellar/README.md](../interstellar/README.md) and
[../solaris/README.md](../solaris/README.md); the product story is in [vision.md](vision.md).
