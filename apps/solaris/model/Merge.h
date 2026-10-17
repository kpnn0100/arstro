/*
 *  solaris_model — Merge: two people's edits to one song, node by node (R-COLLAB-1; docs/discussion.md Round 19).
 *
 *  Three songs as text: the BASE (what this window last read or wrote), MINE (the song as it is here) and THEIRS (the
 *  file as it is now). Read as the versions read a song (NodeTable.h): the header field by field, every node by its id
 *  (a rack by its strip, a formula by its address), a node's id-less lines its body, one unit. A node changed on one
 *  side only takes that side; the same change on both is taken once; changed on both differently, or changed on one and
 *  removed on the other, is a CONFLICT — kept as mine, and said. A node both sides added under the same id is no
 *  conflict: mine is given the next free id and every reference to it among my nodes is rewritten (a value, an address
 *  in a formula) — `renamed` says which. Order follows theirs, my additions after their neighbours. Pure: text in, text
 *  out.
 */
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace solaris
{
    struct MergeConflict
    {
        std::string node;         // its key: an id, `rack:<track>`, `bind:<address>` — or `project:<field>`, a header field
        std::string what;         // what it is, said ("strip Bass", "pattern Lead", "the song's bpm")
        std::string how;          // "both changed" | "changed here, removed there" | "removed here, changed there" | "both added"
        std::string mine, theirs; // its lines on each side ("" where it is not)
    };

    struct MergeResult
    {
        std::string text;                                         // the merged song — each conflict's node as mine
        std::vector<MergeConflict> conflicts;
        std::vector<std::pair<std::string, std::string>> renamed; // a new id on both sides: mine's, and the one it got
        int fromTheirs = 0, fromMine = 0;                         // the changes each side brought
    };

    MergeResult mergeSongs(const std::string &base, const std::string &mine, const std::string &theirs);

    /** A conflict answered "theirs": `text` with `node` as `theirs` has it — its lines, its children with it — or gone
     *  where theirs has none. False with `err` when it cannot be placed (its parent is in neither). */
    bool takeTheirs(std::string &text, const std::string &theirs, const std::string &node, std::string &err);
}
}
