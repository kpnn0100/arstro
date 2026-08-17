// Smoke tests for cosmo_core::EditSession -- sanity-checks the logic extracted
// from cosmo/CosmoApp before any UI is built on top of it. Not exhaustive; the
// full behavioral contract is still verified by the wider cosmo app it was
// extracted from. Plain assert()-based, no external test framework.
#include "../EditSession.h"
#include "../AppSettings.h"
#include "../OrderedParallelLoad.h"
#include "../ProjectLoader.h"
#include "../ThreadBudget.h"
#include "../PresetLibrary.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <thread>

using arstro::cosmo::EditSession;
using arstro::cosmo::History;
using arstro::cosmo::PresetLibrary;

namespace
{
    std::vector<uint8_t> solidImage(int w, int h, uint8_t r, uint8_t g, uint8_t b)
    {
        std::vector<uint8_t> px((size_t)w * h * 4);
        for (int i = 0; i < w * h; ++i)
        {
            px[i * 4 + 0] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = 255;
        }
        return px;
    }

    void test_open_edit_submit_and_poll()
    {
        EditSession s;
        auto px = solidImage(64, 48, 200, 100, 50);
        int slot = s.openImage(px.data(), 64, 48, "a.jpg", "/tmp/a.jpg");
        assert(slot == 0);
        assert(s.imageCount() == 1);
        assert(s.currentSlot() == 0);

        auto *p = s.curParams();
        assert(p != nullptr);
        p->exposure = 1.5f;
        s.submit();

        arstro::RenderService::Frame f;
        bool got = false;
        for (int i = 0; i < 200 && !got; ++i)
        {
            got = s.renderService().tryAcquire(f);
            if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        assert(got && f.width == 64 && f.height == 48);
        printf("[PASS] open_edit_submit_and_poll\n");
    }

    void test_thumbnail_generated()
    {
        EditSession s;
        auto px = solidImage(220, 110, 10, 20, 30);
        s.openImage(px.data(), 220, 110, "b.jpg");
        const auto *thumb = s.thumbForSlot(0);
        assert(thumb != nullptr);
        assert(thumb->w == 110 && thumb->h == 55);  // long edge clamped to 110, aspect kept
        assert(s.thumbForSlot(5) == nullptr);        // out of range
        printf("[PASS] thumbnail_generated\n");
    }

    void test_group_tree_create_navigate_ungroup()
    {
        EditSession s;
        auto px = solidImage(32, 32, 1, 2, 3);
        s.openImage(px.data(), 32, 32, "img1");
        s.openImageInto(s.currentGroup(), px.data(), 32, 32, "img2", "");
        assert((int)s.currentGroupCells().size() == 2);

        // select both, group them
        s.selectNode(0, false, false);
        s.selectNode(1, true, false);  // shift-range -> both selected
        assert(s.selection().size() == 2);
        s.createGroupFromSelection();
        assert(s.currentGroupCells().size() == 1);  // root now has just the new group
        assert(s.currentGroupCells()[0].group);

        int groupNode = s.currentGroupCells()[0].node;
        s.navigateToGroup(groupNode);
        assert(s.currentGroupCells().size() == 2);  // drilled in: the two images
        assert(s.breadcrumbPath().size() == 2);      // "All Photos" -> "Group 1"

        // back at root, select the group itself and ungroup it
        s.navigateToGroup(0);
        assert(s.currentGroupCells().size() == 1);
        s.selectNode(0, false, false);
        s.ungroupSelected();
        assert(s.currentGroupCells().size() == 2);   // the two images are back at root
        assert(!s.currentGroupCells()[0].group && !s.currentGroupCells()[1].group);
        printf("[PASS] group_tree_create_navigate_ungroup\n");
    }

    void test_group_editing_stacks_onto_members()
    {
        EditSession s;
        auto px = solidImage(16, 16, 100, 100, 100);
        const int slotA = s.openImage(px.data(), 16, 16, "a");
        const int slotB = s.openImageInto(s.currentGroup(), px.data(), 16, 16, "b", "");

        // image A gets its own exposure so we can see the group's stack ADD onto it
        s.selectImage(slotA);
        s.curParams()->exposure = 0.5f; s.submit();

        // group both images, then select the GROUP to edit it
        s.navigateToGroup(0);
        s.selectNode(0, false, false);
        s.selectNode(1, true, false);
        s.createGroupFromSelection();
        const int inner = s.currentGroupCells()[0].node;
        s.selectNode(0, false, false);
        assert(s.editGroup() == inner);          // a group is now the edit target
        assert(s.curParams());                    // curParams() returns the GROUP's own params
        s.curParams()->exposure = 1.0f; s.submit();

        // every member's effective exposure = its own + the group's (the headline behaviour)
        assert(std::fabs(s.effectiveParams(slotA).exposure - 1.5f) < 1e-4f);  // 0.5 + 1.0
        assert(std::fabs(s.effectiveParams(slotB).exposure - 1.0f) < 1e-4f);  // 0.0 + 1.0

        // recursion: wrap the group in an OUTER group and edit that too
        s.navigateToGroup(0);
        s.selectNode(0, false, false);            // select the inner group
        s.createGroupFromSelection();             // outer group now contains the inner
        s.selectNode(0, false, false);            // edit the outer group
        s.curParams()->exposure = 0.25f; s.submit();
        assert(std::fabs(s.effectiveParams(slotA).exposure - 1.75f) < 1e-4f);  // 0.5 + 1.0 + 0.25
        assert(std::fabs(s.effectiveParams(slotB).exposure - 1.25f) < 1e-4f);  // 0.0 + 1.0 + 0.25

        // a member's OWN params are never mutated by group edits
        s.selectImage(slotA);
        assert(std::fabs(s.curParams()->exposure - 0.5f) < 1e-4f);

        printf("[PASS] group_editing_stacks_onto_members\n");
    }

    void test_group_params_persist()
    {
        EditSession s;
        auto px = solidImage(8, 8, 50, 50, 50);
        const int slot = s.openImage(px.data(), 8, 8, "a"); (void)slot;
        s.selectNode(0, false, false);
        s.createGroupFromSelection();
        s.selectNode(0, false, false);            // edit the group
        s.curParams()->exposure = 0.75f; s.curParams()->temp = 8000.f; s.submit();

        const std::string path = "/tmp/cosmo_core_group_params.cosmoproj";
        assert(s.saveWorkspaceAs(path));
        std::vector<EditSession::WorkspaceEntry> entries;
        assert(EditSession::readWorkspaceFile(path, entries));

        EditSession s2;
        auto px2 = solidImage(8, 8, 50, 50, 50);
        for (auto &e : entries)
        {
            const int parentNode = e.parent < 0 ? 0 : e.parent + 1;
            if (e.group) { s2.addWorkspaceGroup(parentNode, e.name, e.params, e.history); continue; }
            const int sl = s2.openImageInto(parentNode, px2.data(), 8, 8, "a", e.imagePath);
            s2.applyParamsToSlot(sl, e.params, e.history);
        }
        s2.finishWorkspaceLoad(path);

        // the group's full params (exposure + Kelvin) survived and still stack onto the member
        assert(std::fabs(s2.effectiveParams(0).exposure - 0.75f) < 1e-3f);
        assert(std::fabs(s2.effectiveParams(0).temp - 8000.f) < 1.0f);

        std::filesystem::remove(path);
        printf("[PASS] group_params_persist\n");
    }

    void test_group_edit_renders_brighter_member()
    {
        // End-to-end: editing a group's exposure actually brightens the representative
        // member's rendered pixels (composition -> effectiveParams -> engine render).
        EditSession s;
        auto px = solidImage(24, 24, 90, 90, 90);
        s.openImage(px.data(), 24, 24, "a", "/tmp/a.png");
        auto meanR = [&]() -> double {
            int w = 0, h = 0;
            const uint8_t *out = s.exportFullRes(w, h);  // synchronous full-res render of effectiveParams
            assert(out && w > 0 && h > 0);
            double sum = 0; const size_t n = (size_t)w * h;
            for (size_t i = 0; i < n; ++i) sum += out[i * 4];
            return sum / n;
        };
        const double base = meanR();

        s.selectNode(0, false, false);
        s.createGroupFromSelection();
        s.selectNode(0, false, false);       // edit the group
        s.curParams()->exposure = 1.0f;      // +1 EV on the group
        s.submit();
        const double grouped = meanR();

        assert(grouped > base + 5.0);         // the member renders brighter with the group applied
        printf("[PASS] group_edit_renders_brighter_member\n");
    }

    void test_effective_curve_is_item_plus_group()
    {
        // The green "final" tone curve = effectiveEditParams().curve = the item's curve
        // SUMMED with the group's (item + group - x), and it tracks live edits.
        EditSession s;
        auto px = solidImage(8, 8, 50, 50, 50);
        s.openImage(px.data(), 8, 8, "a");
        s.selectNode(0, false, false);
        s.createGroupFromSelection();
        s.selectNode(0, false, false);   // edit the GROUP: lift mid to 0.7
        s.curParams()->curve = {arstro::CurvePoint{0.f, 0.f}, arstro::CurvePoint{0.5f, 0.7f}, arstro::CurvePoint{1.f, 1.f}};
        s.submit();
        s.selectImage(0);                // the IMAGE: lift mid to 0.6
        s.curParams()->curve = {arstro::CurvePoint{0.f, 0.f}, arstro::CurvePoint{0.5f, 0.6f}, arstro::CurvePoint{1.f, 1.f}};
        s.submit();

        // final mid = item + group - x = 0.6 + 0.7 - 0.5 = 0.8 (curve[16] is x=0.5 of the 33-sample sum)
        auto midY = [&] { return s.effectiveEditParams().curve[16].y; };
        assert(s.effectiveEditParams().curve.size() == 33);
        assert(std::fabs(midY() - 0.8f) < 3e-3f);

        // editing the item curve updates the final: 0.4 + 0.7 - 0.5 = 0.6
        s.curParams()->curve = {arstro::CurvePoint{0.f, 0.f}, arstro::CurvePoint{0.5f, 0.4f}, arstro::CurvePoint{1.f, 1.f}};
        s.submit();
        assert(std::fabs(midY() - 0.6f) < 3e-3f);

        printf("[PASS] effective_curve_is_item_plus_group\n");
    }

    void test_undo_redo()
    {
        EditSession s;
        auto px = solidImage(16, 16, 5, 5, 5);
        s.tick(0.0);
        s.openImage(px.data(), 16, 16, "img");
        s.curParams()->exposure = 1.0f;
        s.submit();
        // Advance past the default 450ms coalesce window so this is a genuinely
        // separate history node, not merged into the previous edit (a slider drag's
        // rapid intermediate values coalesce; two distinct actions should not).
        s.tick(1000.0);
        s.curParams()->exposure = 2.0f;
        s.submit();
        assert(s.canUndo());
        const auto *undone = s.undo();
        assert(undone != nullptr);
        assert(s.curParams()->exposure == 1.0f);
        assert(s.canRedo());
        const auto *redone = s.redo();
        assert(redone != nullptr);
        assert(s.curParams()->exposure == 2.0f);
        printf("[PASS] undo_redo\n");
    }

    void test_preset_save_and_apply_roundtrip()
    {
        EditSession s;
        auto px = solidImage(8, 8, 9, 9, 9);
        s.openImage(px.data(), 8, 8, "img");
        s.curParams()->exposure = 0.75f;
        s.submit();

        const std::string dir = "/tmp/cosmo_core_preset_test";
        std::filesystem::remove_all(dir);
        s.setPresetDir(dir);
        assert(s.savePreset("Warm/Sunset"));  // "/" nests into a subfolder
        assert(std::filesystem::exists(dir + "/Warm/Sunset.apf"));

        auto tree = PresetLibrary::scan(dir);
        assert(tree.size() == 1 && tree[0].folder && tree[0].name == "Warm");
        assert(tree[0].kids.size() == 1 && tree[0].kids[0].name == "Sunset");

        s.curParams()->exposure = 0.0f;
        assert(s.applyPreset("Warm/Sunset"));
        assert(std::fabs(s.curParams()->exposure - 0.75f) < 1e-4f);

        assert(s.deletePresetFile("Warm/Sunset"));
        std::filesystem::remove_all(dir);
        printf("[PASS] preset_save_and_apply_roundtrip\n");
    }

    void test_session_save_and_read_roundtrip()
    {
        EditSession s;
        auto px = solidImage(8, 8, 1, 1, 1);
        s.openImage(px.data(), 8, 8, "img", "/tmp/source.jpg");
        s.curParams()->contrast = 12.5f;
        s.submit();

        const std::string path = "/tmp/cosmo_core_session_test.cosmo";
        assert(s.saveSessionAs(path));

        std::string imagePath; arstro::EditParams params;
        assert(EditSession::readSessionFile(path, imagePath, params));
        assert(imagePath == "/tmp/source.jpg");
        assert(std::fabs(params.contrast - 12.5f) < 1e-3f);
        std::filesystem::remove(path);
        printf("[PASS] session_save_and_read_roundtrip\n");
    }

    // Saving a project must persist the whole branching edit history, not just the
    // live params: after reload the tree (nodes, branches, current, seq order) is
    // intact and undo/redo still walk it.
    void test_workspace_history_roundtrip()
    {
        EditSession s;
        auto px = solidImage(8, 8, 2, 3, 4);
        s.tick(0.0);
        s.openImage(px.data(), 8, 8, "img", "/tmp/hist_src.jpg");
        s.curParams()->exposure = 1.0f; s.submit();          // node 1
        s.tick(1000.0);
        s.curParams()->contrast = 10.0f; s.submit();         // node 2 (child of 1)
        s.tick(2000.0); s.undo();                            // back to node 1
        s.tick(3000.0);
        s.curParams()->saturation = 20.0f; s.submit();       // node 3 (branch off node 1)

        History *h0 = s.currentHistory();
        assert(h0 && h0->nodes.size() == 4);                 // root + 3 edits
        const int cur0 = h0->current;
        assert(h0->nodes[1].kids.size() == 2);               // node 1 branches to 2 and 3

        const std::string path = "/tmp/cosmo_core_ws_hist.cosmoproj";
        assert(s.saveWorkspaceAs(path));

        std::vector<EditSession::WorkspaceEntry> entries;
        assert(EditSession::readWorkspaceFile(path, entries));

        EditSession s2;
        auto px2 = solidImage(8, 8, 0, 0, 0);
        int images = 0;
        for (auto &e : entries)
        {
            const int parentNode = e.parent < 0 ? 0 : e.parent + 1;
            if (e.group) { s2.addWorkspaceGroup(parentNode, e.name, e.params, e.history); continue; }
            const int slot = s2.openImageInto(parentNode, px2.data(), 8, 8, "img", e.imagePath);
            s2.applyParamsToSlot(slot, e.params, e.history);
            ++images;
        }
        s2.finishWorkspaceLoad(path);
        assert(images == 1);

        History *h1 = s2.currentHistory();
        assert(h1 && h1->nodes.size() == 4);                 // full tree restored
        assert(h1->current == cur0);                         // same current node
        assert(h1->nodes[1].kids.size() == 2);               // branch preserved
        assert(std::fabs(s2.curParams()->saturation - 20.0f) < 1e-3f);  // current == branch tip

        const auto *undone = s2.undo();                      // -> node 1
        assert(undone && std::fabs(s2.curParams()->exposure - 1.0f) < 1e-3f);
        assert(std::fabs(s2.curParams()->saturation) < 1e-3f);
        const auto *redone = s2.redo();                      // prefers newest child (node 3)
        assert(redone && std::fabs(s2.curParams()->saturation - 20.0f) < 1e-3f);

        std::filesystem::remove(path);
        printf("[PASS] workspace_history_roundtrip\n");
    }

    // ── R-BYPASS: per-node filter bypass ──────────────────────────────────────

    void test_bypass_skips_only_the_bypassed_node()
    {
        EditSession s;
        auto px = solidImage(16, 16, 100, 100, 100);
        const int slotA = s.openImage(px.data(), 16, 16, "a");
        const int slotB = s.openImageInto(s.currentGroup(), px.data(), 16, 16, "b", "");

        s.selectImage(slotA);
        s.curParams()->exposure = 0.5f; s.submit();
        s.selectImage(slotB);
        s.curParams()->exposure = 0.25f; s.submit();

        // Wrap both in a group carrying its own +1.0 offset.
        s.navigateToGroup(0);
        s.selectNode(0, false, false);
        s.selectNode(1, true, false);
        s.createGroupFromSelection();
        const int group = s.currentGroupCells()[0].node;
        s.selectNode(0, false, false);
        s.curParams()->exposure = 1.0f; s.submit();
        assert(std::fabs(s.effectiveParams(slotA).exposure - 1.5f) < 1e-4f);
        assert(std::fabs(s.effectiveParams(slotB).exposure - 1.25f) < 1e-4f);

        // Bypass image A's OWN leaf: its 0.5 drops out, the group's +1.0 still applies,
        // and image B is untouched (R-BYPASS-2).
        const int nodeA = s.nodeForSlot(slotA);
        s.setBypassed(nodeA, true);
        assert(s.isBypassed(nodeA));
        assert(std::fabs(s.effectiveParams(slotA).exposure - 1.0f) < 1e-4f);
        assert(std::fabs(s.effectiveParams(slotB).exposure - 1.25f) < 1e-4f);

        // Bypass the GROUP as well: now A has nothing at all, B keeps only its own.
        s.setBypassed(group, true);
        assert(std::fabs(s.effectiveParams(slotA).exposure - 0.0f) < 1e-4f);
        assert(std::fabs(s.effectiveParams(slotB).exposure - 0.25f) < 1e-4f);

        // Re-enabling restores the composition exactly -- nothing was destroyed.
        s.setBypassed(nodeA, false);
        s.setBypassed(group, false);
        assert(std::fabs(s.effectiveParams(slotA).exposure - 1.5f) < 1e-4f);
        assert(std::fabs(s.effectiveParams(slotB).exposure - 1.25f) < 1e-4f);

        // toggleBypass flips, and editTargetBypassed() tracks whatever is being edited.
        s.selectImage(slotA);
        assert(!s.editTargetBypassed());
        s.toggleBypass(s.editTargetNode());
        assert(s.editTargetBypassed());
        s.toggleBypass(s.editTargetNode());
        assert(!s.editTargetBypassed());
        printf("[PASS] bypass_skips_only_the_bypassed_node\n");
    }

    void test_bypass_keeps_own_values_in_the_panel_view()
    {
        // effectiveEditParams() drives the panel's green "stacked reach" (effective -
        // own), so it honours bypass on the ANCESTORS but keeps the edit target's own
        // values -- otherwise every slider would show a bogus negative reach while the
        // target is disabled (R-BYPASS-2).
        EditSession s;
        auto px = solidImage(8, 8, 10, 10, 10);
        const int slot = s.openImage(px.data(), 8, 8, "a");
        s.curParams()->exposure = 0.5f; s.submit();
        s.navigateToGroup(0);
        s.selectNode(0, false, false);
        s.createGroupFromSelection();
        const int group = s.currentGroupCells()[0].node;
        s.selectNode(0, false, false);
        s.curParams()->exposure = 1.0f; s.submit();

        s.selectImage(slot);
        assert(std::fabs(s.effectiveEditParams().exposure - 1.5f) < 1e-4f);

        // The image's own bypass must NOT erase its own 0.5 from the panel view...
        s.setBypassed(s.nodeForSlot(slot), true);
        assert(std::fabs(s.effectiveEditParams().exposure - 1.5f) < 1e-4f);
        // ...but what actually renders drops it.
        assert(std::fabs(s.effectiveParams(slot).exposure - 1.0f) < 1e-4f);

        // A bypassed ANCESTOR really does contribute nothing to the reach.
        s.setBypassed(group, true);
        assert(std::fabs(s.effectiveEditParams().exposure - 0.5f) < 1e-4f);
        printf("[PASS] bypass_keeps_own_values_in_the_panel_view\n");
    }

    void test_bypass_workspace_roundtrip()
    {
        EditSession s;
        auto px = solidImage(8, 8, 5, 6, 7);
        s.openImage(px.data(), 8, 8, "img", "/tmp/bypass_src.jpg");
        s.openImageInto(s.currentGroup(), px.data(), 8, 8, "img2", "/tmp/bypass_src2.jpg");
        s.navigateToGroup(0);
        s.selectNode(0, false, false);
        s.selectNode(1, true, false);
        s.createGroupFromSelection();
        const int group = s.currentGroupCells()[0].node;
        s.navigateToGroup(group);
        const int leaf0 = s.currentGroupCells()[0].node;
        s.setBypassed(leaf0, true);      // one image disabled
        s.setBypassed(group, true);      // ...and the group around it

        const std::string path = "/tmp/cosmo_core_ws_bypass.cosmoproj";
        assert(s.saveWorkspaceAs(path));

        std::vector<EditSession::WorkspaceEntry> entries;
        assert(EditSession::readWorkspaceFile(path, entries));
        int bypassedGroups = 0, bypassedImages = 0;
        for (const auto &e : entries)
            if (e.bypass) { if (e.group) ++bypassedGroups; else ++bypassedImages; }
        assert(bypassedGroups == 1);
        assert(bypassedImages == 1);

        EditSession s2;
        auto px2 = solidImage(8, 8, 0, 0, 0);
        for (const auto &e : entries)
        {
            const int parentNode = e.parent < 0 ? 0 : e.parent + 1;
            if (e.group) { s2.addWorkspaceGroup(parentNode, e.name, e.params, e.history, e.bypass); continue; }
            const int slot = s2.openImageInto(parentNode, px2.data(), 8, 8, "img", e.imagePath);
            s2.applyParamsToSlot(slot, e.params, e.history);
            s2.setSlotBypass(slot, e.bypass);
        }
        s2.finishWorkspaceLoad(path);

        // The reloaded tree has exactly the same two nodes disabled.
        int off = 0;
        for (int n = 0; n < (int)s2.nodes().size(); ++n) off += s2.isBypassed(n) ? 1 : 0;
        assert(off == 2);
        assert(s2.isBypassed(1));   // node 1 = the group (root's first child)
        assert(s2.isBypassed(2));   // node 2 = its first image leaf
        printf("[PASS] bypass_workspace_roundtrip\n");
    }

    // ── R-LOADPERF: parallel decode, off-thread apply, progressive reveal ──────

    void test_ordered_parallel_load_is_in_order_and_never_stalls()
    {
        // The project loader decodes on a pool but MUST apply in entry order (a
        // .cosmoproj names a node's parent by entry index), and must not run so far
        // ahead that a catalog is decoded into RAM all at once. Randomised over worker
        // counts, windows and deliberately tiny byte caps, since the interesting failure
        // is a deadlock against the pipeline's own back-pressure.
        for (int trial = 0; trial < 60; ++trial)
        {
            const size_t count = 1 + (size_t)(trial * 7 % 37);
            const int workers = 1 + trial % 8;
            const size_t window = 1 + (size_t)(trial % 4);
            const size_t cap = 1 + (size_t)(trial * 13 % 900);   // often smaller than one item
            arstro::cosmo::OrderedParallelLoad<size_t> pipe;
            std::atomic<int> produced{0};
            pipe.start(count, workers, window, cap,
                       [&produced](size_t i) {
                           ++produced;
                           // Jitter derived from `i`: a shared RNG would itself be a race.
                           std::this_thread::sleep_for(std::chrono::microseconds((i * 2654435761u) % 120));
                           return i;
                       },
                       [](const size_t &v) { return (size_t)(500 + (v * 40503u) % 1500); });

            size_t expect = 0;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (expect < count)
            {
                size_t got = 0;
                if (pipe.tryConsume(got)) { assert(got == expect); ++expect; }
                else std::this_thread::sleep_for(std::chrono::microseconds(100));
                assert(std::chrono::steady_clock::now() < deadline && "pipeline stalled");
            }
            assert(pipe.finished());
            assert(produced.load() == (int)count);
        }
        printf("[PASS] ordered_parallel_load_is_in_order_and_never_stalls\n");
    }

    void test_ordered_parallel_load_stops_cleanly_midway()
    {
        // Abandoning a load (the user goes Home, or another project is opened) must join
        // the pool without waiting for the whole catalog.
        arstro::cosmo::OrderedParallelLoad<size_t> pipe;
        pipe.start(500, 4, 4, 4096,
                   [](size_t i) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); return i; },
                   [](const size_t &) { return (size_t)1024; });
        size_t got = 0;
        while (!pipe.tryConsume(got)) std::this_thread::sleep_for(std::chrono::microseconds(200));
        const auto t0 = std::chrono::steady_clock::now();
        pipe.stop();
        const auto dt = std::chrono::steady_clock::now() - t0;
        assert(dt < std::chrono::seconds(3) && "stop() must not wait for the whole batch");
        assert(!pipe.finished());
        printf("[PASS] ordered_parallel_load_stops_cleanly_midway\n");
    }

    void test_open_image_move_and_prebuilt_thumb_match_the_copying_path()
    {
        // The loader's fast path must land exactly what the copying path did.
        auto px = solidImage(64, 40, 30, 200, 90);
        EditSession a;
        const int slotA = a.openImageInto(0, px.data(), 64, 40, "x.jpg", "/tmp/x.jpg");

        EditSession b;
        auto moved = px;   // the loader owns a decoded buffer it will not touch again
        EditSession::Thumb thumb = EditSession::makeThumb(px.data(), 64, 40, EditSession::kThumbEdge);
        const int slotB = b.openImageInto(0, std::move(moved), 64, 40, "x.jpg", "/tmp/x.jpg", std::move(thumb));

        assert(slotA == slotB);
        assert(a.imageCount() == b.imageCount());
        assert(a.nodes().size() == b.nodes().size());
        assert(a.sourcePathForSlot(slotA) == b.sourcePathForSlot(slotB));
        assert(a.nameForSlot(slotA) == b.nameForSlot(slotB));
        const auto *ta = a.thumbForSlot(slotA);
        const auto *tb = b.thumbForSlot(slotB);
        assert(ta && tb);
        assert(ta->w == tb->w && ta->h == tb->h);
        assert(ta->rgba == tb->rgba);   // identical thumbnails, wherever they were built
        printf("[PASS] open_image_move_and_prebuilt_thumb_match_the_copying_path\n");
    }

    void test_finish_workspace_load_keeps_an_existing_selection()
    {
        // R-LOADPERF-3: with a progressive reveal the photographer can be working on a
        // photo before the last one lands, so finishing the load must not yank them back
        // to image 0 -- but a plain (unselected) load must still land on image 0.
        auto px = solidImage(8, 8, 1, 2, 3);
        EditSession s;
        s.openImageInto(0, px.data(), 8, 8, "a", "/tmp/a.jpg");
        s.openImageInto(0, px.data(), 8, 8, "b", "/tmp/b.jpg");
        s.selectImage(1);                       // the user moved to the second photo
        s.finishWorkspaceLoad("/tmp/p.cosmoproj");
        assert(s.currentSlot() == 1 && "the load must not steal the selection");

        EditSession t;
        t.openImageInto(0, px.data(), 8, 8, "a", "/tmp/a.jpg");
        t.finishWorkspaceLoad("/tmp/q.cosmoproj");
        assert(t.currentSlot() == 0 && "with nothing selected it still lands on image 0");
        printf("[PASS] finish_workspace_load_keeps_an_existing_selection\n");
    }

    // ── R-LOADUX: the rack exists before the pixels do ────────────────────────

    void test_pending_images_appear_then_attach()
    {
        EditSession s;
        auto px = solidImage(20, 12, 9, 40, 80);
        // The whole project is created up front: a group + three not-yet-decoded leaves.
        const int g = s.addWorkspaceGroup(0, "Shoot", arstro::EditParams{});
        const int n0 = s.addPendingImage(g, "a.jpg");
        const int n1 = s.addPendingImage(g, "b.jpg");
        const int n2 = s.addPendingImage(g, "c.jpg");
        s.navigateToGroup(g);

        auto cells = s.currentGroupCells();
        assert(cells.size() == 3);
        for (const auto &c : cells) { assert(c.loading); assert(c.slot < 0); }
        assert(s.imageCount() == 0 && "no engine slots until pixels arrive");

        // Attach OUT of order: the tree already fixed where each photo belongs, so
        // arrival order cannot reparent anything.
        auto thumb = EditSession::makeThumb(px.data(), 20, 12, EditSession::kThumbEdge);
        auto buf = px;
        const int slot1 = s.attachImage(n1, std::move(buf), 20, 12, "/tmp/b.jpg", std::move(thumb));
        assert(slot1 == 0);
        cells = s.currentGroupCells();
        assert(!cells[1].loading && cells[1].slot == slot1);
        assert(cells[0].loading && cells[2].loading && "its siblings are still coming");
        assert(s.nodes()[n1].parent == g && "attaching does not move the node");

        buf = px;
        thumb = EditSession::makeThumb(px.data(), 20, 12, EditSession::kThumbEdge);
        const int slot0 = s.attachImage(n0, std::move(buf), 20, 12, "/tmp/a.jpg", std::move(thumb));
        assert(slot0 == 1);
        assert(s.sourcePathForSlot(slot0) == "/tmp/a.jpg");
        assert(s.nameForSlot(slot0) == "a.jpg");
        assert(s.thumbForSlot(slot0) && s.thumbForSlot(slot0)->w > 0);

        // A failed decode stops spinning and reads as missing, not as still-coming.
        s.markImageFailed(n2);
        cells = s.currentGroupCells();
        assert(!cells[2].loading && cells[2].slot < 0);

        // Attaching twice, or to a group, is refused rather than corrupting the tree.
        buf = px;
        assert(s.attachImage(n1, std::move(buf), 20, 12, "/tmp/x.jpg", EditSession::Thumb{}) == -1);
        buf = px;
        assert(s.attachImage(g, std::move(buf), 20, 12, "/tmp/x.jpg", EditSession::Thumb{}) == -1);
        printf("[PASS] pending_images_appear_then_attach\n");
    }

    void test_selecting_a_pending_image_keeps_the_stage()
    {
        // R-LOADUX-2: clicking (or arrowing onto) a photo that has not arrived moves the
        // selection but must NOT blank the editor; when its pixels land it takes over.
        EditSession s;
        auto px = solidImage(16, 16, 60, 60, 60);
        s.openImageInto(0, px.data(), 16, 16, "ready.jpg", "/tmp/ready.jpg");
        const int pend = s.addPendingImage(0, "later.jpg");
        s.selectImage(0);
        assert(s.currentSlot() == 0);

        // cell 1 is the pending leaf
        s.selectNode(1, false, false);
        assert(s.currentSlot() == 0 && "the stage keeps the photo it was showing");
        assert(std::find(s.selection().begin(), s.selection().end(), pend) != s.selection().end() &&
               "but the selection did move to the pending photo");

        auto thumb = EditSession::makeThumb(px.data(), 16, 16, EditSession::kThumbEdge);
        auto buf = px;
        const int slot = s.attachImage(pend, std::move(buf), 16, 16, "/tmp/later.jpg", std::move(thumb));
        assert(slot >= 0);
        assert(s.currentSlot() == slot && "the photo the user is sitting on takes over when it lands");
        printf("[PASS] selecting_a_pending_image_keeps_the_stage\n");
    }

    // ── R-SETTINGS-4: engine preferences survive a restart ────────────────────

    void test_settings_roundtrip_and_survive_a_bad_file()
    {
        using arstro::cosmo::AppSettings;
        const std::string path = AppSettings::path();
        std::string backup;
        {   // don't clobber a real user's settings while testing
            std::ifstream in(path);
            if (in) backup.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }

        AppSettings s;
        s.previewEdge = 2400; s.threads = 6; s.useGpu = true; s.cpuPercent = 25;
        assert(s.save());
        const AppSettings back = AppSettings::load();
        assert(back.previewEdge == 2400);
        assert(back.threads == 6);
        assert(back.useGpu && "GPU acceleration is still on next launch");
        assert(back.cpuPercent == 25 && "the CPU budget survives a restart (R-CPU-3)");

        // A truncated / garbled file must fall back per field, never stop the app.
        { std::ofstream f(path, std::ios::trunc); f << "cosmosettings=1\nuseGpu=1\npreviewEdge=notanumber\nthre"; }
        const AppSettings partial = AppSettings::load();
        assert(partial.useGpu && "the readable field is still honoured");
        assert(partial.previewEdge == 1600 && "the garbled one falls back to its default");
        assert(partial.threads == 0);
        assert(partial.cpuPercent == 50 && "a settings file that predates the budget gets the default");

        // An out-of-range budget is a corrupt file, not a request for the whole machine.
        { std::ofstream f(path, std::ios::trunc); f << "cosmosettings=1\ncpuPercent=400\n"; }
        assert(AppSettings::load().cpuPercent == 50);
        { std::ofstream f(path, std::ios::trunc); f << "cosmosettings=1\ncpuPercent=0\n"; }
        assert(AppSettings::load().cpuPercent == 50);

        { std::ofstream f(path, std::ios::trunc); f << backup; }
        if (backup.empty()) std::filesystem::remove(path);
        printf("[PASS] settings_roundtrip_and_survive_a_bad_file\n");
    }

    // R-CPU-1: the budget is offered as a percentage and enforced as a worker count.
    // The properties that matter are monotonicity, the never-zero floor and the cap --
    // not an exact number, since the core count differs per machine.
    void test_cpu_budget_scales_with_percent()
    {
        using arstro::cosmo::AppSettings;
        const int full = AppSettings::workersFor(100);
        assert(full >= 1);

        // Never zero, however small the budget or the machine.
        for (int pct : {1, 5, 10, 25, 50, 75, 100})
            assert(AppSettings::workersFor(pct) >= 1 && "a budget always leaves one worker");

        // Monotonic: more budget never means fewer workers.
        int prev = 0;
        for (int pct = 1; pct <= 100; ++pct)
        {
            const int n = AppSettings::workersFor(pct);
            assert(n >= prev && "raising the budget must never shrink the pool");
            assert(n <= full && "no budget exceeds the whole machine");
            prev = n;
        }

        // Half a machine is at most half its cores (the point of the whole feature).
        assert(AppSettings::workersFor(50) <= (full + 1) / 2);

        // The cap wins over the budget; a non-positive cap means uncapped.
        assert(AppSettings::workersFor(100, 2) <= 2);
        assert(AppSettings::workersFor(100, 1) == 1);
        assert(AppSettings::workersFor(100, 0) == full);

        // Garbage percentages are clamped rather than propagated into a thread count.
        assert(AppSettings::workersFor(-10) == AppSettings::workersFor(1));
        assert(AppSettings::workersFor(1000) == full);
        printf("[PASS] cpu_budget_scales_with_percent\n");
    }

    // R-SVC-10 / D-11: the decode pool and the engine are SLICES of one budget, so their
    // sum is what the user chose. This is the test the old code could not pass: two
    // independent workersFor() calls each returned the whole budget, and the sum was ~2x.
    void test_one_budget_is_divided_not_duplicated()
    {
        using arstro::cosmo::ThreadBudget;

        // Checked on the shapes that actually shipped wrong, independent of this machine.
        for (int cores : {2, 4, 8, 12, 16, 24, 32})
            for (int pct : {1, 25, 50, 75, 100})
            {
                ThreadBudget b(pct, cores);
                const int total = b.total();
                assert(total >= 1 && total <= cores && "R-CPU-1: never zero, never the whole machine plus one");

                // Idle: the engine may use the whole budget -- shrinking renders when
                // nothing is loading would be a pointless slowdown.
                assert(b.engineThreads() == total && "an idle engine gets the whole budget");

                // Loading: the pool takes its slice and the engine keeps the remainder.
                const int workers = b.beginLoad();
                assert(workers >= 1 && "R-CPU-1: a load always makes progress");
                assert(workers <= ThreadBudget::kMaxDecodeWorkers && "R-CPU-5: the cap can bind first");
                const int engine = b.engineThreads();
                assert(engine >= 1 && "previews never stop entirely (R-LOADPERF-3 shows the editor)");

                // THE assertion. The two floors are the only slack, and only on a machine
                // so small that the budget is a single thread.
                const int slack = (total <= ThreadBudget::kEngineFloor + 1) ? 1 : 0;
                assert(workers + engine <= total + slack &&
                       "R-SVC-10: decode + engine must not exceed the one budget");

                b.endLoad();
                assert(b.engineThreads() == total && "the engine gets its threads back after a load");
            }

        // R-CPU-2b: an explicit CPU-threads choice outranks the budget for the engine.
        // It is allowed to exceed it -- a deliberate override, logged rather than clamped.
        ThreadBudget ex(25, 16);
        ex.setExplicitEngineThreads(8);
        assert(ex.engineThreads() == 8 && "an explicit thread count wins over Auto");
        ex.setExplicitEngineThreads(0);
        assert(ex.engineThreads() == ex.total() && "0 means Auto, i.e. follow the budget");

        // A corrupt percentage falls back to the default rather than clamping up to 100.
        assert(ThreadBudget(0, 16).percent() == 50 && ThreadBudget(400, 16).percent() == 50);
        printf("[PASS] one_budget_is_divided_not_duplicated\n");
    }

    // R-CPU-4 as amended: the peak is measured. A load of N entries through a fake decoder
    // must never have more producers inside their work at once than the pool it was given
    // -- the number the old log line asserted without ever being run.
    void test_project_load_peak_never_exceeds_its_pool()
    {
        using arstro::cosmo::ProjectLoader;
        using arstro::cosmo::ThreadBudget;

        // A decoder that produces a tiny image and sleeps, so producers genuinely overlap.
        struct SlowFake : arstro::cosmo::IImageDecoder
        {
            arstro::cosmo::DecodedImage decodeFile(const std::string &path) override
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                arstro::cosmo::DecodedImage d;
                d.width = d.height = 8;
                d.rgba.assign((size_t)8 * 8 * 4, 200);
                d.name = path;
                return d;
            }
        };

        std::vector<EditSession::WorkspaceEntry> entries;
        for (int i = 0; i < 18; ++i)   // the reported case: an 18-photo project
        {
            EditSession::WorkspaceEntry e;
            e.imagePath = "/fake/img" + std::to_string(i) + ".raf";
            entries.push_back(e);
        }

        ThreadBudget budget(25, 24);   // the reported settings: 25% of a 24-core machine
        const int engineIdle = budget.engineThreads();
        ProjectLoader loader;
        int workerStarts = 0;
        std::atomic<int> starts{0};
        loader.start(entries, budget, [] { return std::unique_ptr<arstro::cosmo::IImageDecoder>(new SlowFake()); },
                     [&starts] { starts.fetch_add(1); });
        const int pool = loader.workers();
        assert(pool >= 1 && pool <= ThreadBudget::kMaxDecodeWorkers);
        // Captured now, because the invariant is about the concurrent moment: once the load
        // ends the engine takes the whole budget back, which is correct and would make a
        // naive after-the-fact sum look like a violation.
        const int engineDuringLoad = budget.engineThreads();
        assert(engineDuringLoad < engineIdle && "the engine gives up threads for the duration");

        size_t got = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!loader.finished() && std::chrono::steady_clock::now() < deadline)
        {
            ProjectLoader::Result r;
            while (loader.poll(r))
            {
                assert(r.index == got && "results arrive strictly in entry order (parent indices)");
                assert(r.decoded && r.w == 8 && r.h == 8);
                assert(!r.thumb.rgba.empty() && "the worker built the thumbnail, not the UI thread");
                ++got;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(got == entries.size() && "every entry arrived");
        assert(budget.peakDecode() <= pool && "never more producers at once than the pool allotted");
        assert(budget.peakDecode() >= 1 && "the peak was actually measured, not left at zero");
        assert(budget.peakDecode() + engineDuringLoad <= budget.total() + 1 &&
               "R-SVC-10: measured peak plus the engine stays inside the budget");
        assert(budget.engineThreads() == engineIdle && "the engine has its threads back");
        workerStarts = starts.load();
        assert(workerStarts == pool && "the per-thread hook ran once on every worker (D-12's fix)");
        printf("[PASS] project_load_peak_never_exceeds_its_pool (pool %d, peak %d of budget %d)\n",
               pool, budget.peakDecode(), budget.total());
    }
}

int main()
{
    test_open_edit_submit_and_poll();
    test_thumbnail_generated();
    test_group_tree_create_navigate_ungroup();
    test_group_editing_stacks_onto_members();
    test_group_params_persist();
    test_group_edit_renders_brighter_member();
    test_effective_curve_is_item_plus_group();
    test_undo_redo();
    test_preset_save_and_apply_roundtrip();
    test_session_save_and_read_roundtrip();
    test_workspace_history_roundtrip();
    test_bypass_skips_only_the_bypassed_node();
    test_bypass_keeps_own_values_in_the_panel_view();
    test_bypass_workspace_roundtrip();
    test_ordered_parallel_load_is_in_order_and_never_stalls();
    test_ordered_parallel_load_stops_cleanly_midway();
    test_open_image_move_and_prebuilt_thumb_match_the_copying_path();
    test_finish_workspace_load_keeps_an_existing_selection();
    test_pending_images_appear_then_attach();
    test_selecting_a_pending_image_keeps_the_stage();
    test_settings_roundtrip_and_survive_a_bad_file();
    test_cpu_budget_scales_with_percent();
    test_one_budget_is_divided_not_duplicated();
    test_project_load_peak_never_exceeds_its_pool();
    printf("\nAll cosmo_core session tests passed.\n");
    return 0;
}
