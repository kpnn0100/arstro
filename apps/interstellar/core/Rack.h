/*
 *  interstellar_core — Rack: THE colour authority (R-RACK).
 *
 *  Interstellar does not own a colour model. It owns a running `cosmo::CosmoService` and drives it
 *  with `cosmo::Command`s — so a grade made here enters Cosmo's history, is written into the
 *  `.cmp`, and is there when that file is opened in Cosmo. There is no import, no export and no
 *  synchronisation step, because there is nothing to synchronise.
 *
 *  **This class is the whole of the claim**, and the claim is the one both previous attempts
 *  failed: each shipped with a `FakeRack` and never once wrote a parameter into a real project.
 *  So the surface here is deliberately narrow and every member is something the gate needs.
 *
 *  Three rules it keeps:
 *
 *  1. **Commands only.** No `EditSession` call, no `EditParams` mutation, no reach through the
 *     service. A colour write is exactly the `Select` + `Set` pair `cosmo-cc` sends, so the GUI
 *     path and the CLI path are one path rather than two that must be kept in agreement.
 *  2. **Cosmo's events are re-published, never swallowed.** An edit visible in one channel and
 *     invisible in another is the failure the event contract exists to prevent.
 *  3. **Cosmo keeps no settings of its own here.** The budget, the decoder and the caps are
 *     Interstellar's, injected. Two services each converting the user's CPU percentage is cosmo's
 *     own D-11 with a new name, and this app will eventually have four consumers of that budget.
 */
#pragma once
#include "core/service/CosmoService.h"
#include "core/ThreadBudget.h"
#include "engine/EditParams.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
    class Rack
    {
    public:
        /** One node of the rack, flattened in tree order — what a view draws and what an address
         *  resolves to. A projection of Cosmo's own `NodeModel`, not a copy of its state: nothing
         *  here is writable and nothing is cached across a revision. */
        struct Node
        {
            int id = -1;             // the Cosmo node id — the handle every command uses
            int parent = -1;
            int depth = 0;
            bool group = false;
            bool bypass = false;
            bool pending = false;    // pixels still arriving
            bool failed = false;     // decode failed: reads as missing, never as a stall
            std::string cosmoName;   // Cosmo's own name: a filename, or whatever the user typed.
                                     // NOT an address — it may contain a space or a dot (R-RACK-6).
        };

        /** The host supplies the decoder, because `interstellar_core` carries no codec and because
         *  a VIDEO source reaches Cosmo through exactly this seam: a factory that returns one
         *  extracted frame for a video path is all R-RACK-3 needs, and Cosmo requires no change. */
        using DecoderFactory = cosmo::ProjectLoader::DecoderFactory;

        explicit Rack(cosmo::ThreadBudget &budget);

        void setDecoderFactory(DecoderFactory f);
        void setWorkerInit(std::function<void()> f);
        /** Cosmo's events, re-published with a `rack.` prefix by the caller. */
        void subscribe(std::function<void(const cosmo::Event &)> sink);

        // ── the project (a `.cmp`) ──
        bool openProject(const std::string &path, std::string &err);
        bool newProject(const std::string &path, std::string &err);
        bool addSources(const std::vector<std::string> &paths, std::string &err);
        bool save(std::string &err);
        const std::string &path() const { return mPath; }
        bool isOpen() const { return !mPath.empty(); }

        /** Drain Cosmo's work. Never blocks; the caller drives the clock. */
        void pump(double nowMs);
        /** Pump until the load settles, or `maxMs` of WALL CLOCK passes. Returns false on timeout.
         *
         *  Wall clock and a 1 ms sleep, not a spin over simulated time: the decode runs on a pool,
         *  so a loop that advances a simulated clock as fast as it can iterate finishes 7 500
         *  ticks before a worker has opened the first file — which is exactly the bug the first
         *  run of this had, reporting an empty rack for a project that was loading fine.
         *
         *  The simulated clock is a MEMBER and only ever moves forward, because handing the
         *  service a time in its past leaves coalescing windows that never elapse (cosmo's D-56). */
        bool pumpUntilLoaded(int maxMs = 30000);

        // ── the tree ──
        std::vector<Node> nodes() const;
        /** Nodes whose pixels are resident, in tree order. */
        int imageCount() const;

        // ── colour: the whole point ──
        /** Set one parameter of one node, by Cosmo's OWN key (`exposure`, `temp`, `mixerSpread`),
         *  so a preset, a `.cmp`, a `cosmo-cc set` line and this call all spell it identically.
         *  Becomes `Select` + `Set`, which is what `cosmo-cc` sends. */
        bool setParam(int node, const std::string &key, const std::string &value, std::string &err);
        /** The node's OWN value for `key` — what it contributes, before its ancestors. */
        bool getParam(int node, const std::string &key, double &out) const;
        /** The node's EFFECTIVE parameters: its own composed up its ancestors, which is what a
         *  frame renders with. Whole, never field by field — rebuilding the struct outside Cosmo
         *  would be the second copy of one fact that R-G-3 forbids. */
        bool effectiveParams(int node, EditParams &out) const;
        /** Select a node, so a following read sees it. Public because a front end genuinely wants
         *  to move the selection, not only to read. */
        bool select(int node, std::string &err);
        int selectedNode() const;

        const cosmo::AppModel &model() const { return mCosmo.model(); }
        const std::string &lastError() const { return mCosmo.model().lastError; }

    private:
        bool dispatch(const cosmo::Command &c, std::string &err);

        cosmo::CosmoService mCosmo;
        std::string mPath;
        double mNow = 0.0;   // monotonic across every pump — see pumpUntilLoaded
    };
}
}
