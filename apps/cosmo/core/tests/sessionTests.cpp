// Smoke tests for cosmo_core::EditSession -- sanity-checks the logic extracted
// from cosmo/CosmoApp before any UI is built on top of it. Not exhaustive; the
// full behavioral contract is still verified by the wider cosmo app it was
// extracted from. Plain assert()-based, no external test framework.
#include "../EditSession.h"
#include "../AppSettings.h"
#include "../OrderedParallelLoad.h"
#include "../ProjectLoader.h"
#include "../ThreadBudget.h"
#include "../service/AppModelCodec.h"
#include "../service/CosmoService.h"
#include "../PresetLibrary.h"
#include "TestMain.h"
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

    // ── S2: the service (R-SVC-1/2/3/5) ──────────────────────────────────────────────

    // A decoder that needs no files on disk: every path "decodes" to a small solid image.
    struct FakeDecoder : arstro::cosmo::IImageDecoder
    {
        arstro::cosmo::DecodedImage decodeFile(const std::string &path) override
        {
            arstro::cosmo::DecodedImage d;
            if (path.find("missing") != std::string::npos) return d;   // ok() == false
            d.width = 12; d.height = 8;
            d.rgba.assign((size_t)12 * 8 * 4, 180);
            d.name = path;
            return d;
        }
    };

    std::string writeFakeProject(const std::string &path, int images, bool withGroup, bool withMissing)
    {
        std::ofstream f(path, std::ios::trunc);
        f << "cosmoworkspace=1\n";
        if (withGroup) f << "#group\nparent=-1\nname=Tokyo\n";
        for (int i = 0; i < images; ++i)
            f << "#image\nparent=" << (withGroup ? 0 : -1) << "\npath=/fake/img" << i << ".raf\n";
        if (withMissing) f << "#image\nparent=-1\npath=/fake/missing.raf\n";
        return path;
    }

    // Drive a load to completion the way any front end does: pump, don't block.
    void pumpUntilIdle(arstro::cosmo::CosmoService &svc, int maxMs = 20000)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(maxMs);
        double now = 0;
        while (std::chrono::steady_clock::now() < deadline)
        {
            svc.pump(now);
            now += 16.0;                       // a fixed tick, so a run is reproducible (R-SVC-6)
            if (!svc.model().load.active) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        svc.pump(now);
    }

    // R-SVC-5: the struct is the truth and the text is generated from it, so every command
    // must survive format(parse(text)) unchanged. Two hand-maintained representations would
    // drift, which is the failure this test exists to prevent.
    void test_command_text_roundtrips()
    {
        using arstro::cosmo::Command;
        const char *lines[] = {
            "project open /tmp/a.cmp", "project new /tmp/b.cmp", "project save", "project save /tmp/c.cmp",
            "project close", "import /a.raf /b.raf", "select 3", "select next", "select prev",
            "set exposure=1.2 temp=7000", "bypass 4 on", "bypass 4 off", "group new \"Tokyo Night\"",
            "group ungroup 2", "undo", "redo", "preset apply \"Portrait/Soft Skin\"",
            "preset save MyLook", "settings set cpuPercent=25 previewEdge=1600", "screen home",
            "state print", "state print --json", "quit"};
        for (const char *line : lines)
        {
            std::string err;
            const Command c = arstro::cosmo::parseCommand(line, err);
            assert(err.empty() && c.valid() && "every documented line must parse");
            const std::string back = arstro::cosmo::formatCommand(c);
            std::string err2;
            const Command again = arstro::cosmo::parseCommand(back, err2);
            assert(err2.empty() && again.kind == c.kind && "format(parse(x)) must re-parse to the same kind");
            assert(arstro::cosmo::formatCommand(again) == back && "and be a fixed point");
        }

        // A blank line and a comment are successful no-ops, so a script file reads naturally.
        std::string err;
        assert(!arstro::cosmo::parseCommand("", err).valid() && err.empty());
        assert(!arstro::cosmo::parseCommand("   # just a note", err).valid() && err.empty());
        // Garbage is rejected with a reason rather than silently ignored.
        arstro::cosmo::parseCommand("frobnicate the widget", err);
        assert(!err.empty() && "an unknown command must say so");
        arstro::cosmo::parseCommand("set notakeyvalue", err);
        assert(!err.empty() && "set without key=value must say so");
        arstro::cosmo::parseCommand("export --format jpg", err);
        assert(!err.empty() && "export without --outdir must say so");

        // Quoting survives a name with a space (no escapes anywhere in cosmo's formats).
        const Command g = arstro::cosmo::parseCommand("group new \"Tokyo Night\"", err);
        assert(g.name == "Tokyo Night");
        assert(!arstro::cosmo::commandNames().empty());
        printf("[PASS] command_text_roundtrips\n");
    }

    // R-SVC-1/2/3, and the point of the whole architecture: a project opens, decodes,
    // attaches and lands in the editor with NO window, NO Artboard and NO click. This is
    // what D-6 made impossible and what let D-11/D-12 ship unmeasured.
    void test_service_opens_a_project_with_no_ui()
    {
        using namespace arstro::cosmo;
        const std::string path = "/tmp/cosmo_svc_open.cmp";
        writeFakeProject(path, 5, /*withGroup=*/true, /*withMissing=*/true);

        EditSession session;
        ThreadBudget budget(25, 8);
        CosmoService svc(session, budget);
        svc.setDecoderFactory([] { return std::unique_ptr<IImageDecoder>(new FakeDecoder()); });

        std::vector<std::string> log;
        svc.subscribe([&log](const Event &e) { log.push_back(formatEvent(e)); });

        std::string err;
        assert(svc.dispatchText("project open " + path, err) && err.empty());
        assert(svc.model().screen == Screen::Loading && "opening shows the loading surface");
        pumpUntilIdle(svc);

        const AppModel &m = svc.model();
        assert(m.screen == Screen::Editor && "a finished load lands in the editor");
        assert(m.imageCount == 5 && "the five decodable images attached");
        assert(m.nodes.size() == 7 && "one group + five images + one missing");
        assert(m.projectName == "cosmo_svc_open");
        assert(m.currentSlot >= 0 && "something is on the stage");
        assert(m.budget.total == 2 && "25% of 8 cores");
        assert(m.load.workers >= 1 && m.load.workers <= m.budget.total);

        // The group came first and the images are its children, in entry order.
        assert(m.nodes[0].group && m.nodes[0].name == "Tokyo");
        assert(m.nodes[1].parent == m.nodes[0].node && "images parented to the group");
        // The missing one reads as failed, never as pending — "gone" vs "still coming".
        const NodeModel &last = m.nodes.back();
        assert(!last.group && last.slot < 0 && last.failed && !last.pending);

        // The event stream is the log (R-SVC-5): the milestones must all be in it.
        auto sawPrefix = [&log](const std::string &p) {
            for (const std::string &l : log) if (l.rfind(p, 0) == 0) return true;
            return false;
        };
        assert(sawPrefix("[evt] project.opening name=cosmo_svc_open entries=7"));
        assert(sawPrefix("[evt] entry.decoded"));
        assert(sawPrefix("[evt] entry.failed"));
        assert(sawPrefix("[evt] load.finished decoded=5 total=7"));
        assert(sawPrefix("[evt] project.opened"));
        assert(sawPrefix("[evt] screen.changed editor"));

        std::filesystem::remove(path);
        printf("[PASS] service_opens_a_project_with_no_ui\n");
    }

    // R-SVC-2: editing, selecting, grouping, undo and bypass are all reachable as commands,
    // and each one actually changes the model. A behaviour a front end can reach that no
    // command expresses is a defect in the command set, so this is the coverage check.
    void test_commands_drive_the_session()
    {
        using namespace arstro::cosmo;
        const std::string path = "/tmp/cosmo_svc_cmds.cmp";
        writeFakeProject(path, 3, false, false);

        EditSession session;
        ThreadBudget budget(50, 8);
        CosmoService svc(session, budget);
        svc.setDecoderFactory([] { return std::unique_ptr<IImageDecoder>(new FakeDecoder()); });
        std::string err;
        assert(svc.dispatchText("project open " + path, err));
        pumpUntilIdle(svc);
        assert(svc.model().imageCount == 3);

        const int firstNode = svc.model().nodes.front().node;
        assert(svc.dispatchText("select " + std::to_string(firstNode), err) && err.empty());
        const int slotA = svc.model().currentSlot;
        assert(slotA >= 0);

        // set goes through the SAME text codec the .cosmo/.cmp/.apf formats use, so it can
        // never accept a smaller set of fields than a project file does.
        assert(svc.dispatchText("set exposure=1.25 temp=7200", err) && err.empty());
        assert(std::fabs(svc.model().params.exposure - 1.25f) < 1e-4f);
        assert(std::fabs(svc.model().params.temp - 7200.f) < 1.0f);
        assert(svc.model().history.canUndo && "an edit is undoable");

        assert(svc.dispatchText("undo", err) && err.empty());
        assert(std::fabs(svc.model().params.exposure) < 1e-4f && "undo put it back");
        assert(svc.dispatchText("redo", err) && err.empty());
        assert(std::fabs(svc.model().params.exposure - 1.25f) < 1e-4f);

        assert(svc.dispatchText("select next", err) && err.empty());
        assert(svc.model().currentSlot != slotA && "next moved the stage");
        assert(svc.dispatchText("select prev", err) && err.empty());
        assert(svc.model().currentSlot == slotA && "and back again");

        assert(svc.dispatchText("bypass " + std::to_string(firstNode) + " on", err) && err.empty());
        assert(svc.model().nodes.front().bypass && "R-BYPASS through a command");
        assert(svc.dispatchText("bypass " + std::to_string(firstNode) + " off", err) && err.empty());
        assert(!svc.model().nodes.front().bypass);

        assert(svc.dispatchText("group new \"Set A\"", err) && err.empty());
        bool sawGroup = false;
        for (const NodeModel &n : svc.model().nodes) if (n.group) sawGroup = true;
        assert(sawGroup && "a group exists now");

        assert(svc.dispatchText("settings set cpuPercent=75", err) && err.empty());
        assert(svc.model().budget.percent == 75 && svc.model().settings.cpuPercent == 75);

        assert(svc.dispatchText("screen home", err) && err.empty());
        assert(svc.model().screen == Screen::Home);

        // A rejected command reports why, changes nothing, and leaves it in the model.
        assert(!svc.dispatchText("select 9999", err));
        assert(!svc.model().lastError.empty() && "a rejection is inspectable");
        assert(!svc.dispatchText("settings set nonsense=1", err));

        const std::string save = "/tmp/cosmo_svc_cmds_saved.cmp";
        assert(svc.dispatchText("project save " + save, err) && err.empty());
        std::vector<EditSession::WorkspaceEntry> back;
        assert(EditSession::readWorkspaceFile(save, back) && !back.empty());

        std::filesystem::remove(path);
        std::filesystem::remove(save);
        printf("[PASS] commands_drive_the_session\n");
    }

    // R-SVC-9: two front ends given the same commands must show the same state. The stable
    // dump is the comparison, which is why it excludes revision/frameSeq/peak — a GUI that
    // has animated longer is not a difference in state (R-SVC-4).
    void test_two_services_dump_the_same_state()
    {
        using namespace arstro::cosmo;
        const std::string path = "/tmp/cosmo_svc_dump.cmp";
        writeFakeProject(path, 4, true, false);

        ModelDumpOptions stable;
        stable.stable = true;
        stable.params = true;

        auto runOne = [&](int extraPumps) {
            EditSession session;
            ThreadBudget budget(50, 8);
            CosmoService svc(session, budget);
            svc.setDecoderFactory([] { return std::unique_ptr<IImageDecoder>(new FakeDecoder()); });
            std::string err;
            svc.dispatchText("project open " + path, err);
            pumpUntilIdle(svc);
            svc.dispatchText("select next", err);
            svc.dispatchText("set exposure=0.5 contrast=12", err);
            // The "GUI" pumps many more times than the "CLI" — pure elapsed animation.
            for (int i = 0; i < extraPumps; ++i) svc.pump(1000.0 + i * 16.0);
            return formatModel(svc.model(), stable);
        };

        const std::string cli = runOne(0);
        const std::string gui = runOne(120);
        assert(cli == gui && "same commands -> same state, whatever the frame count");

        // And the unstable dump really does differ, or the exclusion above proves nothing.
        EditSession s3;
        ThreadBudget b3(50, 8);
        CosmoService svc3(s3, b3);
        svc3.setDecoderFactory([] { return std::unique_ptr<IImageDecoder>(new FakeDecoder()); });
        std::string err;
        svc3.dispatchText("project open " + path, err);
        pumpUntilIdle(svc3);
        const std::string a = formatModel(svc3.model(), ModelDumpOptions{});
        svc3.pump(9999.0);
        svc3.dispatchText("select next", err);
        const std::string b = formatModel(svc3.model(), ModelDumpOptions{});
        assert(a != b && "revision moves, so the unstable dump is not accidentally constant");

        // The text form is inspectable, not opaque: these keys are what a debug session greps.
        assert(cli.find("screen=editor") != std::string::npos);
        assert(cli.find("kind=group") != std::string::npos);
        assert(cli.find("budgetTotal=") != std::string::npos);
        assert(cli.find("revision=") == std::string::npos && "stable dumps exclude revision");

        const std::string js = formatModel(svc3.model(), [] { ModelDumpOptions o; o.json = true; return o; }());
        assert(js.front() == '{' && js.find("\"nodes\": [") != std::string::npos && "and there is a --json form");

        std::filesystem::remove(path);
        printf("[PASS] two_services_dump_the_same_state\n");
    }

    // R-SVC-9: every Command::Kind must be expressible in the text grammar. Without this,
    // adding a Kind and forgetting its parser is silent — the struct path works, the CLI and
    // the control socket cannot reach it, and the two front ends diverge exactly the way this
    // architecture exists to prevent. `kKindCount` is asserted too, so ADDING a kind breaks
    // this test until its line is written.
    void test_every_command_kind_has_a_grammar()
    {
        using arstro::cosmo::Command;
        using K = Command::Kind;

        // One documented line per kind. Keep in the enum's order so a gap is obvious.
        const std::pair<K, const char *> cases[] = {
            {K::ProjectOpen, "project open /tmp/a.cmp"},
            {K::ProjectNew, "project new /tmp/a.cmp"},
            {K::ProjectSave, "project save"},
            {K::ProjectClose, "project close"},
            {K::Import, "import /a.raf"},
            {K::Select, "select 2"},
            {K::SelectNext, "select next"},
            {K::SelectPrev, "select prev"},
            {K::Set, "set exposure=1.0"},
            {K::Bypass, "bypass 2 on"},
            {K::GroupNew, "group new Name"},
            {K::GroupUngroup, "group ungroup 2"},
            {K::GroupRename, "group rename 2 Shibuya"},
            {K::Delete, "delete 2"},
            {K::Undo, "undo"},
            {K::Redo, "redo"},
            {K::PresetApply, "preset apply Name"},
            {K::PresetSave, "preset save Name"},
            {K::Export, "export --outdir /tmp/out"},
            {K::SettingsSet, "settings set cpuPercent=50"},
            {K::Screen, "screen home"},
            {K::StatePrint, "state print"},
            {K::Wait, "wait load.finished"},
            {K::Quit, "quit"},
        };
        const int kKindCount = 24;   // Kind::None is not a command
        assert((int)(sizeof(cases) / sizeof(cases[0])) == kKindCount &&
               "a new Command::Kind needs a documented line here and a parser rule");

        bool seen[kKindCount + 1] = {false};
        for (const auto &c : cases)
        {
            std::string err;
            const Command parsed = arstro::cosmo::parseCommand(c.second, err);
            assert(err.empty() && "the documented line must parse");
            assert(parsed.kind == c.first && "and to the kind it documents");
            const int idx = (int)c.first;
            assert(idx >= 1 && idx <= kKindCount && !seen[idx] && "no kind listed twice");
            seen[idx] = true;
        }
        for (int i = 1; i <= kKindCount; ++i) assert(seen[i] && "every kind is covered");

        // commandNames() feeds --help, so it must not lag the grammar either.
        assert((int)arstro::cosmo::commandNames().size() == kKindCount &&
               "commandNames() lists one entry per kind");
        printf("[PASS] every_command_kind_has_a_grammar (%d kinds)\n", kKindCount);
    }

    // D-13: a view is allowed to clear its own state when it hears "a project is opening",
    // and the GTK host does exactly that (App::resetWorkspace, so the transition captures the
    // outgoing editor). If the service has already built the pending tree by then, that
    // handler deletes every node the load is about to attach to — 18 files decoded, none
    // attached. So ProjectOpening must be emitted BEFORE the session is touched.
    //
    // This test stands in for the host by resetting the session from the event handler. It
    // fails on the pre-fix ordering, which is what the headless test could not catch: with no
    // subscriber doing real work, both orderings look identical.
    void test_a_view_may_reset_on_project_opening()
    {
        using namespace arstro::cosmo;
        const std::string path = "/tmp/cosmo_svc_d13.cmp";
        writeFakeProject(path, 6, false, false);

        EditSession session;
        ThreadBudget budget(50, 8);
        CosmoService svc(session, budget);
        svc.setDecoderFactory([] { return std::unique_ptr<IImageDecoder>(new FakeDecoder()); });

        int opening = 0;
        svc.subscribe([&](const Event &e) {
            if (e.kind != Event::Kind::ProjectOpening) return;
            ++opening;
            // What the GTK host does, and what broke it.
            session.resetWorkspace();
        });

        std::string err;
        assert(svc.dispatchText("project open " + path, err) && err.empty());
        assert(opening == 1 && "the view heard about the open exactly once");
        pumpUntilIdle(svc);

        assert(svc.model().imageCount == 6 && "every image attached despite the view's reset");
        assert(svc.model().currentSlot >= 0 && "and one is on the stage");
        int decoded = 0;
        for (const NodeModel &n : svc.model().nodes) if (!n.group && n.slot >= 0) ++decoded;
        assert(decoded == 6 && "the tree agrees");

        std::filesystem::remove(path);
        printf("[PASS] a_view_may_reset_on_project_opening\n");
    }

    // D-14 + D-15, both found by diffing a CLI dump against a GUI dump of the same project —
    // which is exactly what R-SVC-9 exists to do, and neither was visible any other way.
    void test_settings_and_dump_options_reach_the_model()
    {
        using namespace arstro::cosmo;

        // D-15: applySettings must put the preferences in force AND in the model. It used to be
        // done piecemeal by the host — percentage into the budget, edge into the session, model
        // never told — so `settings*` reported defaults while `budget*` reported the truth.
        // Two halves of one answer disagreeing is worse than either being wrong.
        EditSession session;
        ThreadBudget budget(50, 16);
        CosmoService svc(session, budget);

        AppSettings s;
        s.cpuPercent = 25;
        s.previewEdge = 2400;
        s.threads = 4;
        s.useGpu = true;
        svc.applySettings(s);

        const AppModel &m = svc.model();
        assert(m.settings.cpuPercent == 25 && "the model reports the budget the user chose");
        assert(m.settings.previewEdge == 2400 && m.settings.threads == 4 && m.settings.useGpu);
        assert(m.budget.percent == 25 && "and the budget agrees with it");
        assert(m.budget.engineThreads == 4 && "an explicit thread count still wins (R-CPU-2b)");

        // D-14: `state print` carries --json and --stable INDEPENDENTLY. They shared one bool,
        // so --stable parsed and was then dropped — and a dump that cannot be made stable
        // cannot be compared with another front end's, which is the option's only purpose.
        std::string err;
        Command c = parseCommand("state print --stable", err);
        assert(err.empty() && c.kind == Command::Kind::StatePrint);
        assert(!c.flag && c.field("stable") == "1" && "--stable is not --json");
        c = parseCommand("state print --json --stable", err);
        assert(err.empty() && c.flag && c.field("stable") == "1" && "both at once");
        assert(formatCommand(c).find("--stable") != std::string::npos && "and it round-trips");

        // The stable dump really does drop the volatile fields, which is what makes the
        // CLI-vs-GUI comparison meaningful rather than incidentally equal.
        ModelDumpOptions stable;
        stable.stable = true;
        const std::string d = formatModel(m, stable);
        assert(d.find("revision=") == std::string::npos);
        assert(d.find("budgetPeakDecode=") == std::string::npos);
        assert(d.find("settingsCpuPercent=25") != std::string::npos && "but keeps real state");
        printf("[PASS] settings_and_dump_options_reach_the_model\n");
    }
}

int main()
{
    arstro::cosmo::testMainInit();   // D-10: a failing assert must exit, not hang
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
    test_command_text_roundtrips();
    test_every_command_kind_has_a_grammar();
    test_service_opens_a_project_with_no_ui();
    test_commands_drive_the_session();
    test_two_services_dump_the_same_state();
    test_a_view_may_reset_on_project_opening();
    test_settings_and_dump_options_reach_the_model();
    printf("\nAll cosmo_core session tests passed.\n");
    return 0;
}
