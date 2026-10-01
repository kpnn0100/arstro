/*
 *  interstellar_core — Colour: the ONE fold from a node's own params to what a frame renders with.
 *
 *  Three places need "the effective grade of rack node N": the live rack, a PINNED version (a
 *  snapshot of the rack at the pin), and a derived version with overrides (`#tlgrade` deltas on
 *  some nodes' own params). They differ only in where each node's OWN params come from, so the
 *  tree is built three ways and folded once — here — by the rule Cosmo's
 *  `EditSession::effectiveParams(slot)` uses: a bypassed node contributes nothing of its own, every
 *  non-bypassed ancestor stacks on through Cosmo's `composeParams`. The fold is held equal to
 *  Cosmo's own answer by `test_render_params_equal_cosmos_own_for_every_node`.
 *
 *  Nothing here is an authority. A tree is always BUILT from Cosmo — its model, or a byte copy of
 *  its `.cmp` read by its own static `readWorkspaceFile` — and thrown away.
 */
#pragma once
#include "engine/EditParams.h"
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace interstellar
{
    struct ColourNode
    {
        int parent = -1;          // index into the tree; -1 = a child of the root
        bool group = false;
        bool bypass = false;
        int cosmoNode = -1;       // the live Cosmo node id; -1 in a snapshot
        std::string name;         // Cosmo's name (group name, or the image's file name)
        std::string imagePath;    // as Cosmo stores it, frame selector included; "" for a group
        EditParams own;
    };
    using ColourTree = std::vector<ColourNode>;

    /** What a frame renders with (EditSession::effectiveParams(slot)). */
    EditParams foldRender(const ColourTree &tree, int index);
    /** What the panels show as the stacked reach (EditSession::effectiveEditParams): the target's
     *  own params always, its ancestors honouring bypass. */
    EditParams foldEditTarget(const ColourTree &tree, int index);

    /** A tree from a `.cmp` on disk, through Cosmo's own reader — for a pin snapshot. */
    bool colourTreeFromCmp(const std::string &cmpPath, ColourTree &out, std::string &err);

    /** One key's value as Cosmo's codec writes it (`0.35`, `0,0;1,1`, `210,18,-4`). */
    bool paramText(const EditParams &p, const std::string &key, std::string &out);
    bool paramScalar(const EditParams &p, const std::string &key, double &out);
    /** Set one key through Cosmo's codec, so the spelling `set` uses is the spelling a .cmp uses. */
    bool setParamText(EditParams &p, const std::string &key, const std::string &value);
    /** A version's overrides: each `key += delta` (scalar keys only — the format carries numbers). */
    void applyDeltas(EditParams &p, const std::vector<std::pair<std::string, double>> &deltas);
}
}
