/*
 *  solaris_model — Versions: a version of the song is its base resolved, with its deltas on top (R-VER-1…4;
 *  docs/project-format.md §9b; docs/discussion.md Round 15). Interstellar's model, over a whole song.
 *
 *  The deltas are WORKED OUT, never written by a command: when an edit lands on an open version, `recordVersion`
 *  compares the song it now is with its base's resolution, node by node and field by field — so every command is
 *  version-aware with no code of its own, a field edited back to the base's value inherits again, and an inherited
 *  node edited stores a `#vset`, never a copy. The nodes are the `.slp`'s own, read from canonical text: the header
 *  (`project`), every node with an `id`, a rack (`rack:<track>`), a formula (`bind:<address>`). A node's id-less lines
 *  (a pattern's notes, an automation's points, a clip's takes) are ONE field, its body; a rack's devices are its
 *  children, in order. A version's base is main or an earlier version, so a resolution always ends.
 */
#pragma once
#include "Project.h"
#include <string>
#include <vector>

namespace arstro
{
namespace solaris
{
    /** Main's own text: the song when main is open, else what it is under the open version. */
    std::string mainTextOf(const Project &p);

    /** R-VER-1/2: `id`'s resolution as canonical song text — main ("" or "main") is the song itself, a frozen version
     *  its snapshot. `dangling`: the deltas naming a node its base does not have (ignored). False with `err` when
     *  `id` names no version. */
    bool resolveVersion(const Project &p, const std::string &id, std::string &text, std::string &err,
                        std::vector<std::string> *dangling = nullptr);

    /** What `versions[]` says of one: deltas (fields, drops, bodies, orders), nodes of its own, dangling deltas, and
     *  why it does not stand as a song (empty when it does). */
    struct VersionFacts
    {
        int deltas = 0, own = 0, dangling = 0;
        std::string problem;
    };
    VersionFacts versionFacts(const Project &p, const std::string &id);

    /** Open `id` (a version's id or name, or "main"): the song's nodes become its resolution; main's text and the
     *  versions kept. False with `err` (no such version; it does not stand — the base broke it). */
    bool openVersion(Project &p, const std::string &id, std::string &err);

    /** After an edit on the open (unfrozen) version: its deltas worked out again from the song it now is — the
     *  dangling ones kept until a rebase prunes them. Nothing on main. */
    void recordVersion(Project &p);

    /** A version of `base` (an id or name; "" or "main" = main) with no deltas yet: its id, "" with `err`. */
    std::string newVersion(Project &p, const std::string &name, const std::string &base, std::string &err);

    /** R-VER-3: freeze — keep its resolution as a snapshot and stop inheriting; thaw — its deltas live again. */
    bool freezeVersion(Project &p, const std::string &id, std::string &err);
    bool thawVersion(Project &p, const std::string &id, std::string &err);

    /** R-VER-3: a live version's dangling deltas named in `report` and pruned; a frozen one's snapshot advanced to its
     *  base's present state with its deltas on top. `dryRun`: report, change nothing. */
    bool rebaseVersion(Project &p, const std::string &id, bool dryRun, std::vector<std::string> &report, std::string &err);

    /** Refused while another version is based on it; the open one gives way to its base first. */
    bool deleteVersion(Project &p, const std::string &id, std::string &err);

    /** The open version's deltas on `node` — or on one `field` of it — dropped: it inherits there again. Refused on
     *  main, on a frozen version, on a node of its own, and where there is nothing to drop. */
    bool revertVersion(Project &p, const std::string &node, const std::string &field, std::string &err);

    /** Its deltas and its own nodes as stored, one line each (no snapshot), for `version diff`. */
    std::vector<std::string> versionLines(const Project &p, const std::string &id);
}
}
