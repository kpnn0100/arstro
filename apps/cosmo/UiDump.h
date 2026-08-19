/*
 *  Cosmo by arstro — UiDump: the Segment tree as text, for an agent that cannot see.
 *
 *  P0.6. Every UI question so far has been answered by taking a screenshot and looking at it,
 *  which is slow, racy against animation, and useless in a headless run — a whole afternoon
 *  went into chasing a 420x260 splash around a multi-monitor desktop to find out whether a
 *  4 px bar was drawn. A tree dump answers "is it there, where, how big, visible?" exactly,
 *  in microseconds, with no display.
 *
 *  It does NOT replace a rendered frame. A shot tells you it looks wrong; a dump tells you
 *  which node is at fault. They answer different questions and the pair is what makes a
 *  visual defect diagnosable (`arstro.cosmo.design.debug` §2).
 *
 *  Host layer on purpose: it uses RTTI to name a node, and `apps/` may while `cosmo_core` and
 *  `arstro_image` may not. It is read-only — walking the tree changes nothing, so it is safe
 *  to call from a control-socket handler mid-frame.
 */
#pragma once
#include "../../core/Artboard/src/ui/base/Segment.h"
#include <string>

namespace arstro
{
namespace cosmo_v2
{
    /** The first node in `root`'s subtree whose demangled type name equals `type`, or null.
     *  Depth-first, root included. Exists so a test over the ASSEMBLED app can name the widget
     *  it cares about ("CenterStage") instead of indexing into a child list whose order is a
     *  layout detail — an assertion that breaks when a child is inserted is an assertion nobody
     *  keeps. Same RTTI the dump uses to label a node. */
    const artboard::Segment *findSegmentByType(const artboard::Segment &root, const char *type);

    struct UiDumpOptions
    {
        bool json = false;
        int maxDepth = 32;
        /** Skip nodes that are hidden or fully transparent, with their subtrees. Off by
         *  default: "why is it not showing" is the commonest question, and a filter that hides
         *  the answer is worse than a longer dump. */
        bool visibleOnly = false;
    };

    /** One line per node: type, world rect, size, visible/opacity/hover, child count. `label`
     *  names the root, because cosmo has several (the editor tree, the home screen, the
     *  splash) and a dump with no root name cannot be read a week later. */
    std::string dumpSegmentTree(const artboard::Segment &root, const std::string &label,
                                const UiDumpOptions &o = UiDumpOptions{});
}
}
