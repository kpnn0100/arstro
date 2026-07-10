/*
 *  Cosmo by arstro — History: a BRANCHING edit timeline for one image ("time
 *  machine"). Each node is a full EditParams snapshot; edges are parent -> child.
 *  Undo walks to the parent; if the user then edits, a NEW child is added so the
 *  original future (the sibling branch) is left untouched — exactly like a git DAG
 *  of commits. Rapid consecutive edits COALESCE into one node (a settable time
 *  window) and the tree is capped at a settable number of steps (oldest, off-path
 *  nodes pruned first).
 *
 *  This is plain app state (no Artboard/UI here); the HistoryView widget renders a
 *  snapshot of it as a git-style tree.
 */
#pragma once
#include "../../ImageProcessing/src/image_processing.h"
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo
{
    struct HistoryNode
    {
        EditParams params;          // full state snapshot at this node
        int parent = -1;            // parent node index (-1 = root / oldest)
        std::vector<int> kids;      // child node indices (branches), in creation order
        std::string label;          // what changed to reach this node ("Exposure", "Crop", ...)
        int seq = 0;                // creation order (pruning + redo preference)
    };

    class History
    {
    public:
        std::vector<HistoryNode> nodes;
        int current = -1;           // the node whose params are live

        // ── configuration (see SettingsPanel) ──
        int maxSteps = 100;         // cap on stored nodes (>=2)
        double coalesceMs = 450.0;  // consecutive edits within this window merge into one node

        bool empty() const { return nodes.empty(); }
        bool canUndo() const { return current >= 0 && nodes[current].parent >= 0; }
        bool canRedo() const { return current >= 0 && !nodes[current].kids.empty(); }

        /** Seed the tree with a single root node (the opened image's state). */
        void init(const EditParams &p);

        /** Record an edit toward `p` at `nowMs`. Coalesces into the current node when
         *  in the merge window and the current node is a freshly-made leaf; otherwise
         *  adds a new child (branching if the current node already has children).
         *  Returns true if the tree changed (false = `p` equals the current state). */
        bool record(const EditParams &p, double nowMs);

        /** Move to the parent / a child / an arbitrary node and return the params to
         *  apply (nullptr if the move is impossible). All three break coalescing so a
         *  following edit starts a new branch. */
        const EditParams *undo();
        const EditParams *redo();
        const EditParams *jumpTo(int node);

        /** Adopt a deserialized tree (from a saved project): replace nodes + current
         *  + limits, rebuild each node's kids[] from its parent (ascending index =
         *  creation order, matching record()/prune()), and resume seq numbering past
         *  the highest loaded seq so later edits get fresh, non-colliding ids. */
        void restore(std::vector<HistoryNode> loadedNodes, int cur, int steps, double coalMs);

        void breakCoalesce() { mCanCoalesce = false; }
        void setLimits(int steps, double coalMs);

    private:
        void prune();

        int mNextSeq = 0;
        double mLastEditMs = -1e30;
        bool mCanCoalesce = false;
    };

    /** Short human label for the change between two states ("Exposure", "Curve",
     *  "Crop", "Adjustments", ...). Public for tests. */
    std::string describeChange(const EditParams &from, const EditParams &to);
    /** Whether two parameter sets are identical (deep). */
    bool paramsEqual(const EditParams &a, const EditParams &b);
}
}
