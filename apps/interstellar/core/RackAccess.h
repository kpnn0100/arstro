/*
 *  interstellar_core — RackAccess: the seam onto the embedded Cosmo project (R-COSMO).
 *
 *  Interstellar's colour authority is a hosted `cosmo::CosmoService`, and `RackEmbed` is the
 *  class that owns it. This interface is what the rest of the core sees of it, and it exists
 *  for two reasons, both load-bearing:
 *
 *  1. **The evaluator must be testable with no decode, no GTK and no display.** A fake rack in
 *     a unit test is a map of doubles; the real one dispatches `cosmo::Command`s. Both satisfy
 *     this interface, so the frame pipeline is provable at L2 (arstro.rule §4) rather than only
 *     against a real project.
 *  2. **It names the whole surface.** Everything Interstellar can do to the rack is on this
 *     page — read a value, write a value, list the nodes, ask whether it is pinned. If a
 *     behaviour needs more than this, that is a change to the seam and gets a requirement, not
 *     a reach past it.
 *
 *  Leaf keys are COSMO's own `EditParamsIO` names (`exposure`, `mixerSpread`, `sharpenRadius`)
 *  so a preset, a `.cmp`, a `cosmo-cc set` line and an Interstellar expression all spell a
 *  parameter identically (R-PARAM-4). The one extension is a dotted component on a compound
 *  key — `crop.w` — which an implementation turns into a read-modify-write of the `crop`
 *  quadruple, because the address space is per-scalar and Cosmo's codec is per-key.
 */
#pragma once
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
    class RackAccess
    {
    public:
        virtual ~RackAccess() = default;

        /** One node of the rack, flattened in tree order. Mirrors `cosmo::EditSession::TreeRow`
         *  without depending on it, so a fake is three lines. */
        struct Node
        {
            std::string id;        // the Cosmo node id, as text — the handle commands use
            std::string cosmoName; // Cosmo's own name: a filename or a group name. NOT a bind name:
                                   // it may contain spaces, so it cannot be an address (R-PARAM-2).
            int parent = -1;
            int depth = 0;
            bool group = false;
            bool bypass = false;
            bool pending = false;
            bool failed = false;
        };

        virtual std::vector<Node> nodes() const = 0;
        /** A scalar parameter of a rack node. False when the node or the leaf does not exist. */
        virtual bool getParam(const std::string &nodeId, const std::string &leaf, double &out) const = 0;
        /** Write one. False + `err` when the node or leaf is unknown, or when the rack is pinned
         *  — a pin is read-only through every path, and a pin that yields is not a pin
         *  (R-COSMO-5). */
        virtual bool setParam(const std::string &nodeId, const std::string &leaf, double v,
                              std::string &err) = 0;
        virtual bool isPinned() const = 0;
        /** Why it is pinned, for the refusal message. */
        virtual std::string pinCommit() const { return {}; }
    };
}
}
