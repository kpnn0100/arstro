/*
 *  interstellar_core — the node graph's edits (R-CLR-3).
 *
 *  The rack IS a node graph, read left to right: a source's own grade, then each group it is in (a
 *  group grades its members' results — a SERIAL node). So a serial node after X is a new group around
 *  X, made by Cosmo exactly as `rack group new` makes one. A PARALLEL node is a variant of a source —
 *  a second Cosmo grade on the same media (`rack duplicate`) — that grades the source's INPUT beside it;
 *  its difference from the input is added to the source's result by its mix (Resolve's parallel
 *  mixer: an empty parallel node changes nothing). It starts empty: no grade, no plugins, no curves.
 *  Every node is a Cosmo grade; the graph's wiring is the rack's groups plus `#rackobj parallelOf=`.
 */
#include "ServiceInternal.h"
#include "Project.h"
#include <algorithm>

namespace arstro
{
namespace interstellar
{
    using CK = Command::Kind;

    bool InterstellarService::nodeCommand(const Command &c)
    {
        Project &P = *mProject;
        const std::string verb = specFor(c.kind)->verb;
        RackObj *ro = P.rackObj(P.idForRef(c.arg(0)));
        if (!ro) return fail(verb + ": no rack node named " + c.arg(0));
        if (c.kind == CK::NodeSerial)
        {
            if (!ro->parallelOf.empty()) return fail(verb + ": " + ro->name + " is a parallel node — a serial node goes after its source");
            Command g;
            g.kind = CK::RackGroupNew;
            g.args = {c.has("name") ? c.flag("name") : P.freshName("node")};
            g.flags = {{"nodes", ro->name}};
            return rackCommand(g);
        }
        if (c.kind == CK::NodeParallel)
        {
            if (ro->kind == "group" || ro->media.empty()) return fail(verb + ": " + ro->name + " is a group — a parallel node grades a SOURCE's input");
            if (!ro->parallelOf.empty()) return fail(verb + ": " + ro->name + " is a parallel node itself — add one to its source");
            const NodeId of = ro->id;
            const std::string name = c.has("name") ? c.flag("name") : P.freshName(ro->name + "_par");
            Command d;
            d.kind = CK::RackDuplicate;
            d.args = {ro->name};
            d.flags = {{"name", name}};
            if (!rackCommand(d)) return false;
            RackObj *v = P.rackObj(P.idForRef(name));
            if (!v) return fail(verb + ": the variant was not made");
            v->parallelOf = of;
            v->parallelMix = 1.0;
            // it starts EMPTY, so adding it changes nothing: Cosmo's identity grade, and none of the
            // source's plugins or curves the duplicate copied
            std::string err;
            const int node = cosmoNodeOf(v->id);
            if (node < 0 || !mRack.setParams(node, paramFields(EditParams{}), err)) return fail(verb + ": " + err);
            const NodeId vid = v->id;
            std::vector<NodeId> fx;
            for (const auto &e : P.imageEffects) if (e.node == vid) fx.push_back(e.id);
            P.imageEffects.erase(std::remove_if(P.imageEffects.begin(), P.imageEffects.end(), [&](const Effect &e) { return e.node == vid; }),
                                 P.imageEffects.end());
            fx.push_back(vid);
            for (const auto &owner : fx)
            {
                std::vector<NodeId> anims;
                for (const auto &a : P.anims) if (a.node == owner) anims.push_back(a.id);
                P.animKeys.erase(std::remove_if(P.animKeys.begin(), P.animKeys.end(),
                                                [&](const AnimKey &k) { return std::find(anims.begin(), anims.end(), k.anim) != anims.end(); }),
                                 P.animKeys.end());
                P.anims.erase(std::remove_if(P.anims.begin(), P.anims.end(), [&](const Anim &a) { return a.node == owner; }), P.anims.end());
            }
            markDirty();
            bumpFrame();
            mOutput = vid + " " + name + "\n";
            return true;
        }
        // node remove
        if (!ro->parallelOf.empty())
        {
            Command r;
            r.kind = CK::RackRemove;
            r.args = {ro->name};
            return rackCommand(r);
        }
        if (ro->kind == "group")
        {
            Command u;
            u.kind = CK::RackUngroup;
            u.args = {ro->name};
            return editCommand(u);
        }
        return fail(verb + ": " + ro->name + " is a source — its graph starts there; remove it from the rack instead");
    }
}
}
