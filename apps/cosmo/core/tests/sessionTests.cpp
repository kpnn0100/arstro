// Smoke tests for cosmo_core::EditSession -- sanity-checks the logic extracted
// from cosmo/CosmoApp before any UI is built on top of it. Not exhaustive; the
// full behavioral contract is still verified by the wider cosmo app it was
// extracted from. Plain assert()-based, no external test framework.
#include "../EditSession.h"
#include "../AppSettings.h"
#include "../OrderedParallelLoad.h"
#include "../ProjectLoader.h"
#include "../ThreadBudget.h"
#include "../decode/NativeImageDecoder.h"
#include "../service/AppModelCodec.h"
#include "../service/CosmoService.h"
#include "../PresetLibrary.h"
#include "PinnedDecoder.h"   // host layer, compiled into this suite on purpose — see CMakeLists
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
        // the pool without waiting for the whole catalog — and must RETURN (D-42).
        //
        // Two things about the shape of this test are load-bearing, both established by
        // measurement rather than taste:
        //
        // 1. A WATCHDOG, armed before anything else, instead of a deadline checked afterwards.
        //    D-42 was a lost wakeup inside stop(): it never returned at all, so the `dt < 3s`
        //    assertion below never got the chance to run and cosmo_core_tests stopped dead at
        //    test 15 of 36 on Windows, taking the three CPU-budget guards with it. R-TEST-1 — a
        //    suite that cannot report red is not evidence, and a hang is the least legible red.
        // 2. NOTHING may sit between tryConsume() returning and stop() being called, and the
        //    scenario is repeated. The race needs mStop to be set in the narrow gap between a
        //    worker evaluating the wait predicate and actually blocking on it; a std::async, or
        //    even constructing the watchdog thread, in between is enough latency for every
        //    worker to be parked already, and the suite then PASSES on the broken code. Both
        //    were tried and both hid the defect — do not "tidy" this back into either.
        std::atomic<bool> allStopsReturned{false};
        std::thread watchdog([&allStopsReturned] {
            for (int i = 0; i < 3000 && !allStopsReturned.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (allStopsReturned.load()) return;
            std::fputs("[FAIL] ordered_parallel_load_stops_cleanly_midway: stop() did not return "
                       "— mStop must be signalled under mMu (D-42)\n", stderr);
            std::fflush(nullptr);
            std::_Exit(3);
        });

        for (int trial = 0; trial < 20; ++trial)
        {
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
        }

        allStopsReturned.store(true);
        watchdog.join();
        printf("[PASS] ordered_parallel_load_stops_cleanly_midway\n");
    }


    void test_every_decoding_thread_is_pinned()
    {
        // D-41 / R-CPU-2(c) as amended: the nested-team pin belongs to every thread that
        // decodes, not to ProjectLoader's pool. It used to be wired ONLY to the pool's
        // per-worker hook, so the six host call sites that construct a decoder directly —
        // opening one photo, opening a .cosmo, the synchronous workspace load, `cosmo-cc
        // info`, `params --print`, the shot renderer — decoded with a machine-sized OpenMP
        // team the budget knew nothing about. Measured before the fix: 4.6 cores and 20 OS
        // threads at cpuPercent=25, and 4.5 at 100% — identical, so the setting did nothing.
        //
        // What is asserted is the COUNT of distinct threads bound, not a thread-count of the
        // OpenMP team: a team assertion would be inert on any host whose LibRaw has no
        // OpenMP (the vendored Linux libraw.a, for one), and this defect was invisible on
        // exactly such a host for five days. The count behaves identically everywhere, which
        // is the whole reason R-CPU-4 was amended to ask for it.
        using arstro::cosmo_v2::ompPinnedThreadCount;
        const int before = ompPinnedThreadCount();

        // A path that cannot decode is the right probe: the pin happens before the delegate
        // is even called, so this asserts the wrapper's contract without needing a fixture
        // image, a codec or a display.
        arstro::cosmo_v2::PinnedDecoder dec;
        (void)dec.decodeFile("/definitely/not/a/file.arw");
        assert(ompPinnedThreadCount() == before + 1 &&
               "decodeFile must pin the thread it decodes on (D-41)");

        (void)dec.decodeThumb("/definitely/not/a/file.arw", 480);
        assert(ompPinnedThreadCount() == before + 1 &&
               "the same thread counts once, however many decodes it does");

        // Each NEW decoding thread is counted, which is what makes the number readable as
        // "how many threads did the pin actually reach" in the load log and `cosmo-cc info`.
        constexpr int kThreads = 4;
        std::vector<std::thread> pool;
        for (int i = 0; i < kThreads; ++i)
            pool.emplace_back([] {
                arstro::cosmo_v2::PinnedDecoder d;
                (void)d.decodeFile("/definitely/not/a/file.arw");
                (void)d.decodeFile("/definitely/not/a/file.arw");   // still one thread
            });
        for (auto &t : pool) t.join();
        assert(ompPinnedThreadCount() == before + 1 + kThreads &&
               "every decoding thread is counted exactly once (R-CPU-4, measured)");

        // A decoder with a BUDGET attached is the lone-decode case — the user opening one
        // photo — and it must be allowed the whole share rather than one core. Pinning
        // everything to 1 also fixes the budget violation, and measurably over-corrects:
        // opening a 24 MP ARW went from 0.79 s to 1.85 s, at 100% as much as at 25%.
        arstro::cosmo::ThreadBudget budget(50, 16);
        assert(budget.total() == 8);
        arstro::cosmo_v2::PinnedDecoder wide;
        wide.setBudget(&budget);
        (void)wide.decodeFile("/definitely/not/a/file.arw");
        assert(ompPinnedThreadCount() == before + 1 + kThreads &&
               "still this thread, already counted");

        // R-CPU-2(c)'s "an explicit user value still wins": the old pin called
        // omp_set_num_threads(1) unconditionally and overrode a user who had asked for more.
        // 0 means the user said nothing, so the caller's team size decides — never a silent 1.
        const int override_ = arstro::cosmo_v2::ompPinUserOverride();
        const char *env = std::getenv("OMP_NUM_THREADS");
        if (env && *env)
        {
            char *end = nullptr;
            const long n = std::strtol(env, &end, 10);
            if (end && *end == '\0' && n >= 1 && n < 1024)
                assert(override_ == (int)n && "an explicit OMP_NUM_THREADS wins (R-CPU-2c)");
        }
        else
            assert(override_ == 0 && "nothing set means the caller's team size decides");

        printf("[PASS] every_decoding_thread_is_pinned (%d threads; user override %d)\n",
               ompPinnedThreadCount(), override_);
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
        s.previewEdge = 2400; s.threads = 6; s.useGpu = true; s.cpuPercent = 25; s.uiScale = 75;
        s.touchUi = true;
        assert(s.save());
        const AppSettings back = AppSettings::load();
        assert(back.previewEdge == 2400);
        assert(back.threads == 6);
        assert(back.useGpu && "GPU acceleration is still on next launch");
        assert(back.cpuPercent == 25 && "the CPU budget survives a restart (R-CPU-3)");
        assert(back.uiScale == 75 && "the UI scale survives a restart (R-SCALE-1)");
        assert(back.touchUi && "touch mode survives a restart (R-TOUCH-6) -- a UI that reverts to "
                               "the mouse layout on a tablet every launch reads as broken");

        // A truncated / garbled file must fall back per field, never stop the app.
        { std::ofstream f(path, std::ios::trunc); f << "cosmosettings=1\nuseGpu=1\npreviewEdge=notanumber\nthre"; }
        const AppSettings partial = AppSettings::load();
        assert(partial.useGpu && "the readable field is still honoured");
        assert(partial.previewEdge == 1600 && "the garbled one falls back to its default");
        assert(partial.threads == 0);
        assert(partial.cpuPercent == 50 && "a settings file that predates the budget gets the default");
        assert(partial.uiScale == 100 && "a file that predates the UI scale renders at the design size");
        assert(!partial.touchUi && "a file that predates touch mode gets the desktop shell");

        // An out-of-range budget is a corrupt file, not a request for the whole machine.
        { std::ofstream f(path, std::ios::trunc); f << "cosmosettings=1\ncpuPercent=400\n"; }
        assert(AppSettings::load().cpuPercent == 50);
        { std::ofstream f(path, std::ios::trunc); f << "cosmosettings=1\ncpuPercent=0\n"; }
        assert(AppSettings::load().cpuPercent == 50);

        // A scale nothing was laid out at is SNAPPED, not range-checked: every offered scale
        // has a rendered shot and a layout assertion behind it, and an arbitrary 83% has
        // neither (R-SCALE-1).
        { std::ofstream f(path, std::ios::trunc); f << "cosmosettings=1\nuiScale=83\n"; }
        assert(AppSettings::load().uiScale == 90 && "a hand-edited scale snaps to the nearest offered one");
        { std::ofstream f(path, std::ios::trunc); f << "cosmosettings=1\nuiScale=1000\n"; }
        assert(AppSettings::load().uiScale == 200 && "and an absurd one snaps to the largest, not through it");
        for (int s2 : AppSettings::uiScales())
            assert(AppSettings::clampUiScale(s2) == s2 && "every offered scale is a fixed point");

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

                // R-CPU-2d: the UI's one thread comes off the top before anything else
                // divides the rest, floored so a one-thread budget still makes progress.
                const int sched = b.schedulable();
                assert(sched == (total - ThreadBudget::kUiReserve < 1 ? 1 : total - ThreadBudget::kUiReserve));

                // Idle: the engine may use everything that is schedulable -- shrinking
                // renders when nothing is loading would be a pointless slowdown.
                assert(b.engineThreads() == sched && "an idle engine gets the whole schedulable budget");

                // Loading: the pool takes its slice and the engine keeps the remainder.
                const int workers = b.beginLoad();
                assert(workers >= 1 && "R-CPU-1: a load always makes progress");
                assert(workers <= ThreadBudget::kMaxDecodeWorkers && "R-CPU-5: the cap can bind first");
                const int engine = b.engineThreads();
                assert(engine >= 1 && "previews never stop entirely (R-LOADPERF-3 shows the editor)");

                // THE assertion. The two floors are the only slack, and only on a machine
                // so small that the budget is a single thread.
                const int slack = (sched <= ThreadBudget::kEngineFloor + 1) ? 1 : 0;
                assert(workers + engine <= sched + slack &&
                       "R-SVC-10: decode + engine must not exceed the one budget");

                // R-CPU-2d, the point of the reservation: whatever the budget, the thread
                // drawing the window is not competing with every core cosmo scheduled.
                // Before it, 100% of 16 cores meant 8 decode + 8 engine = the machine, and
                // the UI was the 17th thread.
                assert(workers + engine <= total - ThreadBudget::kUiReserve + slack + 1 &&
                       "R-CPU-2d: the UI's thread is inside the budget, not on top of it");
                if (total > ThreadBudget::kEngineFloor + ThreadBudget::kUiReserve)
                    assert(workers + engine < total &&
                           "on any budget with room, a thread is left for the window");

                b.endLoad();
                assert(b.engineThreads() == sched && "the engine gets its threads back after a load");
            }

        // R-CPU-2b: an explicit CPU-threads choice outranks the budget for the engine.
        // It is allowed to exceed it -- a deliberate override, logged rather than clamped.
        ThreadBudget ex(25, 16);
        ex.setExplicitEngineThreads(8);
        assert(ex.engineThreads() == 8 && "an explicit thread count wins over Auto");
        ex.setExplicitEngineThreads(0);
        assert(ex.engineThreads() == ex.schedulable() && "0 means Auto, i.e. follow the budget");

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
            "select 3 add", "select 3 range",   // R-SVC-2: multi-select is IN the grammar now
            "set exposure=1.2 temp=7000", "bypass 4 on", "bypass 4 off", "group new \"Tokyo Night\"",
            "group ungroup 2", "undo", "redo", "preset apply \"Portrait/Soft Skin\"",
            "preset save MyLook", "settings set cpuPercent=25 previewEdge=1600", "screen home",
            "state print", "state print --json",
            "gesture on", "gesture off",     // R-PREVIEW-1
            "quit"};
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
        arstro::cosmo::parseCommand("gesture maybe", err);
        assert(!err.empty() && "gesture takes on|off, nothing else");
        // ...but a bare `gesture` means on, the same way `bypass <n>` does.
        const Command bare = arstro::cosmo::parseCommand("gesture", err);
        assert(err.empty() && bare.kind == Command::Kind::Gesture && bare.flag);

        // Quoting survives a name with a space (no escapes anywhere in cosmo's formats).
        const Command g = arstro::cosmo::parseCommand("group new \"Tokyo Night\"", err);
        assert(g.name == "Tokyo Night");
        assert(!arstro::cosmo::commandNames().empty());
        printf("[PASS] command_text_roundtrips\n");
    }

    // R-SVC-1/2/3, and the point of the whole architecture: a project opens, decodes,
    // attaches and lands in the editor with NO window, NO Artboard and NO click. This is
    // what D-6 made impossible and what let D-11/D-12 ship unmeasured.
    void test_engine_memory_is_capped_and_a_cold_slot_is_re_decoded()
    {
        // R-MEM-1/2/4. The defect: EditEngine held every slot's source as a full-resolution
        // LINEAR FLOAT image — 24 MP x 4ch x 4B = 387 MB — for the life of the project, so
        // resident memory was a function of how many photos were opened rather than of how
        // many were being looked at. Measured on real 24 MP ARWs before the fix: 465 MB per
        // photo, 1.6 GB for four, 3.7 GB for eight, ~54 GB extrapolated for the reported
        // 120-photo catalog on a 27.7 GB machine.
        //
        // Asserted on BYTES the engine reports, not on the process's memory: the pools are
        // what this owns, and a working-set assertion would fail on an allocator's retention
        // rather than on a defect.
        arstro::EditEngine eng;
        constexpr int kW = 256, kH = 256;
        const size_t oneSource = (size_t)kW * kH * 4 * sizeof(Pixel);
        // Room for two sources, so adding a third must evict the first (R-MEM-1).
        eng.setMemoryCaps(oneSource * 2, oneSource * 8);

        std::vector<uint8_t> px((size_t)kW * kH * 4, 128);
        const int a = eng.addImage(px.data(), kW, kH, 4);
        const int b = eng.addImage(px.data(), kW, kH, 4);
        const int c = eng.addImage(px.data(), kW, kH, 4);
        assert(a == 0 && b == 1 && c == 2);
        assert(eng.residentSourceBytes() <= oneSource * 2 &&
               "three sources may not all stay resident under a two-source cap (R-MEM-1)");
        // `a` was auto-selected as the first image, and the slot being rendered is never the
        // one evicted — so the victim is the oldest slot that is NOT current, which is `b`.
        assert(eng.currentSlot() == a);
        assert(eng.slotHasSource(a) && "the current slot survives eviction whatever the cap says");
        assert(!eng.slotHasSource(b) && "the oldest non-current source is the one evicted");
        assert(eng.slotHasSource(c) && "the newest source stays");

        // The evicted slot is COLD, not gone: it has no source and no proxy, and says so.
        assert(eng.slotNeedsSource(b) && "an evicted slot with no proxy reports itself cold");
        // ...and re-supplying it is all it takes to render again (R-MEM-2).
        assert(eng.supplySource(b, px.data(), kW, kH, 4));
        assert(eng.slotHasSource(b));
        assert(eng.rehydrations() == 1 && "the re-decode is counted, not assumed (R-MEM-4)");

        // The slot being rendered is never the one evicted, whatever the cap says — a render
        // reading an image that was dropped to satisfy a number is a crash, not a saving.
        eng.setMemoryCaps(1, 1);
        eng.setPreviewSize(64);
        eng.selectImage(c);
        // Switching to a slot the tiny cap already evicted is the ordinary cold case, and
        // renderPreview says so by returning nothing rather than by drawing an empty image.
        assert(eng.slotNeedsSource(c) && "a slot evicted before it was selected reads as cold");
        arstro::PreviewBuffer none = eng.renderPreview();
        assert(!none.rgba && "a cold slot renders NOTHING rather than garbage (R-MEM-2)");
        // Re-supply is all it takes, and then it renders even though the cap is one byte:
        // the slot being rendered is never the one evicted.
        assert(eng.supplySource(c, px.data(), kW, kH, 4));
        arstro::PreviewBuffer pb = eng.renderPreview();
        assert(pb.rgba && pb.width > 0 && "the current slot renders even under a 1-byte cap");
        assert(eng.residentSourceBytes() <= oneSource &&
               "everything except the slot being rendered is gone at a 1-byte cap");

        // And the whole point, end to end: N images do NOT cost N sources. Twenty of them
        // under a two-source cap must hold what two hold, not what twenty would.
        arstro::EditEngine many;
        many.setMemoryCaps(oneSource * 2, oneSource * 4);
        for (int i = 0; i < 20; ++i) many.addImage(px.data(), kW, kH, 4);
        assert(many.imageCount() == 20);
        assert(many.residentSourceBytes() <= oneSource * 2 &&
               "resident pixels are bounded by the cap, not by the project's size (R-MEM-1)");

        printf("[PASS] engine_memory_is_capped_and_a_cold_slot_is_re_decoded "
               "(20 images resident in %zu KB, cap %zu KB)\n",
               many.residentBytes() / 1024, (oneSource * 6) / 1024);
    }

    void test_a_cold_slot_is_re_decoded_through_the_service()
    {
        using namespace arstro::cosmo;
        // R-MEM-2 through the real service: eviction must be a CACHE decision, never a data
        // decision, so a project bigger than the cap still renders every photo — it just
        // re-reads the ones it dropped. Counted through the decoder, so "it re-decoded" is
        // observed rather than argued.
        const std::string path = "/tmp/cosmo_mem_cold.cmp";
        writeFakeProject(path, 6, /*withGroup=*/false, /*withMissing=*/false);

        static std::atomic<int> sDecodes{0};
        struct CountingDecoder : arstro::cosmo::IImageDecoder
        {
            arstro::cosmo::DecodedImage decodeFile(const std::string &p) override
            {
                sDecodes.fetch_add(1);
                arstro::cosmo::DecodedImage d;
                d.width = 96; d.height = 96;
                d.rgba.assign((size_t)96 * 96 * 4, 200);
                d.name = p;
                return d;
            }
        };

        ThreadBudget budget(50, 8);
        CosmoService svc(budget);
        svc.setDecoderFactory([] { return std::unique_ptr<IImageDecoder>(new CountingDecoder()); });
        // One source's worth of cap, so every slot but the current one is cold.
        svc.session().renderService().setMemoryCaps((size_t)96 * 96 * 4 * sizeof(Pixel), 1);

        std::string err;
        assert(svc.dispatchText("project open " + path, err) && err.empty());
        pumpUntilIdle(svc);
        assert(svc.model().imageCount == 6);
        const int afterLoad = sDecodes.load();
        // Six entries, one decode each — plus at most one more: the load ends by rendering the
        // selected slot, and under a cap this pathologically small (one source, one BYTE of
        // proxy) even that slot can be cold by the time it is drawn. The point of this test is
        // the walk below, not the load's arithmetic, so bound it rather than pin it.
        assert(afterLoad >= 6 && afterLoad <= 7 && "the load decodes each entry once, and settles");

        // Walk the rack. Every hop lands on a slot whose pixels the cap already evicted, so
        // every hop must re-decode and must still produce a frame — the failure this guards
        // against is a photo that silently renders nothing once memory got tight.
        //
        // The re-decode happens on the RENDER WORKER, and `pumpUntilIdle` returns as soon as
        // the LOAD is idle — which it already is here — so it is not the wait this needs.
        // Wait for the work itself, with a deadline, rather than asserting into a race.
        double t = 0;
        auto settle = [&](auto done) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!done() && std::chrono::steady_clock::now() < deadline)
            {
                svc.pump(t);
                t += 16.0;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        };

        for (int i = 0; i < 5; ++i)
        {
            const int before = sDecodes.load();
            assert(svc.dispatchText("select next", err) && err.empty());
            settle([&] { return sDecodes.load() > before; });
        }
        assert(sDecodes.load() > afterLoad &&
               "browsing past the cap re-decodes rather than showing nothing (R-MEM-2)");

        settle([&] { return svc.model().budget.rehydrations > 0; });
        assert(svc.model().budget.rehydrations > 0 && "and the model reports it (R-MEM-4)");
        assert(svc.model().budget.residentBytes > 0 && "resident bytes are measured, not zero");

        printf("[PASS] a_cold_slot_is_re_decoded_through_the_service "
               "(%d decodes for 6 photos, %d re-decodes)\n",
               sDecodes.load(), svc.model().budget.rehydrations);
    }
    void test_walking_a_rack_that_fits_the_cap_never_re_decodes()
    {
        using namespace arstro::cosmo;
        // R-MEM-5 as amended, and the guard D-44 asked for. Reported as "sometime changing photo
        // take too long": one hop took 250 ms and the next 2537 ms, because the proxy pool was
        // filled only by *rendering* a photo — so after a load nothing was cached and the first
        // visit to each photo paid a fresh ~1 s decode. Measured on 120 real ARWs, 8 of 10 hops
        // re-decoded; now 0 of 10.
        //
        // The requirement is stated so it can fail: for a rack that FITS the cap, walking it
        // re-decodes at most once per photo, and a photo already visited never re-decodes again.
        const std::string path = "/tmp/cosmo_mem_walk.cmp";
        writeFakeProject(path, 12, /*withGroup=*/false, /*withMissing=*/false);

        static std::atomic<int> sWalkDecodes{0};
        struct CountingDecoder : IImageDecoder
        {
            DecodedImage decodeFile(const std::string &p) override
            {
                sWalkDecodes.fetch_add(1);
                DecodedImage d;
                d.width = 64; d.height = 64;
                d.rgba.assign((size_t)64 * 64 * 4, 170);
                d.name = p;
                return d;
            }
        };
        sWalkDecodes.store(0);

        ThreadBudget budget(50, 8);
        CosmoService svc(budget);
        svc.setDecoderFactory([] { return std::unique_ptr<IImageDecoder>(new CountingDecoder()); });
        // Room for the whole rack of proxies and almost no sources — the shape the hosts
        // configure (PixelBudget.h sizes both from physical RAM, proxies getting the larger
        // share because they are what browsing touches).
        svc.session().renderService().setMemoryCaps((size_t)64 * 64 * 4 * sizeof(Pixel),
                                                    (size_t)64 * 64 * 4 * sizeof(Pixel) * 64);

        std::string err;
        assert(svc.dispatchText("project open " + path, err) && err.empty());
        pumpUntilIdle(svc);
        assert(svc.model().imageCount == 12);
        const int afterLoad = sWalkDecodes.load();
        assert(afterLoad <= 13 && "the load decodes each entry once (plus at most a settle)");

        double t = 0;
        auto settle = [&](auto done) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!done() && std::chrono::steady_clock::now() < deadline)
            {
                svc.pump(t); t += 16.0;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        };

        // Walk the whole rack twice. The FIRST pass is the one that used to re-decode every
        // photo; the second must be free under any reading of the requirement.
        for (int pass = 0; pass < 2; ++pass)
            for (const NodeModel &n : std::vector<NodeModel>(svc.model().nodes))
            {
                const unsigned seq0 = svc.model().frameSeq;
                if (!svc.dispatchText("select " + std::to_string(n.node), err)) continue;
                settle([&] { return svc.model().frameSeq != seq0; });
            }

        assert(sWalkDecodes.load() == afterLoad &&
               "a rack that fits the cap is walked with NO re-decode at all (R-MEM-5, D-44)");
        assert(svc.model().budget.rehydrations == 0 && "and the model agrees it never rehydrated");

        printf("[PASS] walking_a_rack_that_fits_the_cap_never_re_decodes "
               "(%d decodes for 12 photos across two full walks)\n", sWalkDecodes.load());
    }

    void test_selection_reaches_the_service_during_a_load()
    {
        using namespace arstro::cosmo;
        // Reported as "when photo is loading, select another photo doesn't work (it don't
        // send even to the core)". The core half always worked — selectNode moves onto a
        // pending leaf and keeps the stage (R-LOADUX-2) — but the filmstrip called
        // EditSession directly, so the click emitted no Event, wrote no log line and no
        // other front end could see or script it. R-SVC-2: this asserts the COMMAND path,
        // mid-load, which is the part that did not exist.
        const std::string path = "/tmp/cosmo_select_during_load.cmp";
        writeFakeProject(path, 8, /*withGroup=*/false, /*withMissing=*/false);

        struct SlowDecoder : IImageDecoder
        {
            DecodedImage decodeFile(const std::string &p) override
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                DecodedImage d;
                d.width = 24; d.height = 16;
                d.rgba.assign((size_t)24 * 16 * 4, 140);
                d.name = p;
                return d;
            }
        };

        ThreadBudget budget(50, 8);
        CosmoService svc(budget);
        svc.setDecoderFactory([] { return std::unique_ptr<IImageDecoder>(new SlowDecoder()); });

        std::vector<std::string> log;
        svc.subscribe([&log](const Event &e) { log.push_back(formatEvent(e)); });

        std::string err;
        assert(svc.dispatchText("project open " + path, err) && err.empty());

        // Pump only until the tree exists — the nodes are built before any pixel decodes
        // (R-LOADUX-1), so there are leaves to select while the load is still running.
        double t = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (svc.model().nodes.size() < 8 && std::chrono::steady_clock::now() < deadline)
        {
            svc.pump(t); t += 16.0;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(svc.model().nodes.size() == 8 && "the whole rack exists from the first frame");
        assert(svc.model().load.active && "and the load is genuinely still running");

        // THE assertion: a select dispatched mid-load is accepted, moves the model, and
        // says so on the event stream.
        const int target = svc.model().nodes.back().node;
        const size_t before = log.size();
        assert(svc.dispatchText("select " + std::to_string(target), err) && err.empty() &&
               "selecting during a load must be accepted, not ignored");
        assert(svc.model().selectedNode == target && "the selection actually moved");
        bool sawEvent = false;
        for (size_t i = before; i < log.size(); ++i)
            if (log[i].rfind("[evt] selection.changed", 0) == 0) sawEvent = true;
        assert(sawEvent && "and it reaches every front end as an Event (R-SVC-2/5)");

        // Multi-select is in the grammar too, which it had to be: the filmstrip could always
        // ctrl-click, and a behaviour no Command expresses is a hole in the enum.
        const int other = svc.model().nodes.front().node;
        assert(svc.dispatchText("select " + std::to_string(other) + " add", err) && err.empty());
        assert(svc.session().selection().size() == 2 && "ctrl-click adds rather than replaces");
        assert(!svc.dispatchText("select 1 sideways", err) && !err.empty() &&
               "an unknown modifier is rejected with a reason, not silently ignored");

        pumpUntilIdle(svc, 20000);
        printf("[PASS] selection_reaches_the_service_during_a_load\n");
    }

    void test_service_opens_a_project_with_no_ui()
    {
        using namespace arstro::cosmo;
        const std::string path = "/tmp/cosmo_svc_open.cmp";
        writeFakeProject(path, 5, /*withGroup=*/true, /*withMissing=*/true);
        ThreadBudget budget(25, 8);
        CosmoService svc(budget);
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
    // D-34: an Event and the model it describes must be CONSISTENT when the event is
    // delivered, because a listener reads the model during the callback.
    //
    // `applySetFields` emitted ParamsChanged and left the refresh to its caller, so a
    // subscriber that read the model from the handler saw the PRE-EDIT value. The GUI does
    // exactly that — the host re-seeds the right column from the model on ParamsChanged — so a
    // curve edit was answered by the panel being handed back the curve it had just replaced.
    // The user saw their edit vanish from the curve they were drawing and reappear on the faint
    // "final" readout beside it, because that one is fed from a value read after dispatch
    // returned. Same family as D-13: announce before mutating, refresh before announcing.
    //
    // Asserted from INSIDE the handler, which is the only place the bug exists — every
    // after-the-fact check of the model passes on the broken code.
    void test_an_event_sees_the_model_it_describes()
    {
        using namespace arstro::cosmo;
        const std::string path = "/tmp/cosmo_svc_evt_order.cmp";
        writeFakeProject(path, 2, false, false);
        ThreadBudget budget(50, 8);
        CosmoService svc(budget);
        svc.setDecoderFactory([] { return std::unique_ptr<IImageDecoder>(new FakeDecoder()); });
        std::string err;
        assert(svc.dispatchText("project open " + path, err));
        pumpUntilIdle(svc);
        assert(svc.dispatchText("select " + std::to_string(svc.model().nodes.front().node), err));

        int seen = 0;
        bool ownStale = false, effStale = false;
        svc.subscribe([&](const Event &e) {
            if (e.kind != Event::Kind::ParamsChanged) return;
            ++seen;
            // Both halves of the model, because the view reads own (what it edits) and
            // effective (what it draws behind) and a stale either is a wrong frame.
            if (std::fabs(svc.model().ownParams.exposure - 2.5f) > 1e-4f) ownStale = true;
            if (std::fabs(svc.model().params.exposure - 2.5f) > 1e-4f) effStale = true;
        });

        assert(svc.dispatchText("set exposure=2.5", err) && err.empty());
        assert(seen == 1 && "the edit emitted exactly one ParamsChanged");
        assert(!ownStale && "ownParams already holds the new value when the event is delivered");
        assert(!effStale && "and so does the effective params block");

        // The same for a mask edit, which takes a different path to the same emit.
        ownStale = effStale = false; seen = 0;
        assert(svc.dispatchText("set contrast=30", err) && err.empty());
        assert(seen == 1 && !ownStale && !effStale && "and for any other field");

        std::filesystem::remove(path);
        printf("[PASS] an_event_sees_the_model_it_describes\n");
    }

    // R-PREVIEW-1/2/3: a gesture renders coarse to keep up, and the level walks back to
    // full once the gesture stops. Driven entirely by commands with no display, which is
    // the whole point of the requirement being a LATENCY budget: the behaviour is the same
    // shape on every machine, so it can be asserted on any of them.
    //
    // The one thing that cannot be asserted here is the *number* — a 33 ms budget picks
    // level 0 on this desktop and a coarse level on an A733, and asserting either would be
    // asserting the hardware. So this asserts the MECHANISM: that a budget small enough to
    // rule out level 0 produces a coarse frame, that the walk climbs one step per frame,
    // that it terminates at level 0, and that a refinement adds no history.
    // D-24: a LOAD asks for the cheap demosaic and an EXPORT asks for the quality one.
    //
    // This is a requirement about which of two code paths is taken, and it is invisible in
    // the output — a bilinear-decoded preview and a quality-decoded one look the same at
    // previewEdge, which is exactly why the load is allowed to be cheap. So the only way to
    // guard it is to count the asks, and it is worth guarding: silently exporting from a
    // bilinear decode would lower output quality, which is the one thing a photo editor may
    // never trade for speed.
    void test_a_load_decodes_cheaply_and_an_export_decodes_properly()
    {
        using namespace arstro::cosmo;
        static std::atomic<int> sPreviewAsks{0}, sFullAsks{0};
        sPreviewAsks.store(0);
        sFullAsks.store(0);

        struct CountingDecoder : IImageDecoder
        {
            DecodedImage make(const std::string &path)
            {
                DecodedImage d;
                if (path.find("missing") != std::string::npos) return d;
                d.width = 12; d.height = 8;
                d.rgba.assign((size_t)12 * 8 * 4, 180);
                d.name = path;
                return d;
            }
            DecodedImage decodeFile(const std::string &path) override
            {
                // The un-hinted overload must still mean FULL: a caller that does not know
                // about fidelity may not be silently given the cheap answer.
                sFullAsks.fetch_add(1);
                return make(path);
            }
            DecodedImage decodeFile(const std::string &path, Fidelity f) override
            {
                (f == Fidelity::Preview ? sPreviewAsks : sFullAsks).fetch_add(1);
                return make(path);
            }
        };

        const std::string path = "/tmp/cosmo_svc_fidelity.cmp";
        writeFakeProject(path, 2, false, false);
        ThreadBudget budget(50, 8);
        CosmoService svc(budget);
        svc.setDecoderFactory([] { return std::unique_ptr<IImageDecoder>(new CountingDecoder()); });
        std::string err;
        assert(svc.dispatchText("project open " + path, err));
        pumpUntilIdle(svc);
        assert(svc.model().imageCount == 2);

        // Every decode the load did was a preview decode. Not "mostly" — the load has no
        // reason to want output pixels, and one full decode here is 7x of a photo's cost.
        assert(sPreviewAsks.load() >= 2 && "the load must decode every entry");
        assert(sFullAsks.load() == 0 && "and must not ask for output quality even once");
        const int afterLoad = sPreviewAsks.load();

        // Export re-decodes, and it must ask properly. The load kept no full-resolution
        // source (R-MEM-5), so this genuinely goes back to the file.
        int written = 0;
        svc.setImageWriter([&written](const std::string &, const std::string &, const uint8_t *,
                                      int, int, std::string &) { ++written; return true; });
        assert(svc.dispatchText("export --outdir /tmp", err) && err.empty());
        assert(written == 2 && "both photos exported");
        assert(sFullAsks.load() >= 2 && "export must ask for the quality demosaic");
        assert(sPreviewAsks.load() == afterLoad &&
               "and must not sneak a preview decode into the output path");

        printf("[PASS] a_load_decodes_cheaply_and_an_export_decodes_properly "
               "(load: %d preview / 0 full, export: %d full)\n",
               afterLoad, sFullAsks.load());
    }

    void test_a_gesture_renders_coarse_and_then_refines()
    {
        using namespace arstro::cosmo;
        const std::string path = "/tmp/cosmo_svc_preview.cmp";
        writeFakeProject(path, 2, false, false);
        ThreadBudget budget(50, 8);
        CosmoService svc(budget);
        svc.setDecoderFactory([] { return std::unique_ptr<IImageDecoder>(new FakeDecoder()); });
        std::string err;
        assert(svc.dispatchText("project open " + path, err));
        pumpUntilIdle(svc);
        assert(svc.model().imageCount == 2);
        assert(svc.dispatchText("select " + std::to_string(svc.model().nodes.front().node), err));

        // The contract half is published whatever happens.
        assert(svc.model().previewLevels == arstro::EditEngine::previewLevels());
        assert(svc.model().previewLevels == 4);
        assert(svc.model().interactiveBudgetMs > 0.0 && "a budget must exist to be met");

        auto pumpForFrame = [&](double &now, int maxTicks = 400) {
            const unsigned before = svc.model().frameSeq;
            for (int i = 0; i < maxTicks; ++i)
            {
                svc.pump(now);
                now += 16.0;
                if (svc.model().frameSeq != before) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return false;
        };

        double now = 1000.0;
        // A settled edit renders level 0, always — that is the default and it must not have
        // changed for anyone who never sends `gesture`.
        assert(svc.dispatchText("set exposure=0.2", err) && err.empty());
        assert(pumpForFrame(now) && "a set must produce a frame");
        assert(svc.model().frameLevel == 0 && "a settled edit renders the full level");
        assert(!svc.model().refining);

        // Now force the level chooser's hand: a budget of 1 ms cannot be met by any level,
        // so `levelForBudget` must offer the coarsest rather than giving up. This is the
        // A733 case reproduced on a fast machine by moving the budget instead of the CPU.
        svc.session().renderService().setInteractiveBudgetMs(1.0);
        assert(svc.dispatchText("gesture on", err) && err.empty());
        int coarsest = -1;
        for (int i = 0; i < 6; ++i)   // a few moves, as a drag would send
        {
            assert(svc.dispatchText("set exposure=" + std::to_string(0.3 + i * 0.05), err));
            assert(pumpForFrame(now) && "every move in a gesture must produce a frame");
            coarsest = svc.model().frameLevel;
        }
        assert(coarsest == arstro::EditEngine::previewLevels() - 1 &&
               "an unmeetable budget must still offer the coarsest level, not refuse");
        assert(svc.model().frameLevelEdge > 0);
        assert(svc.model().msPerMegapixel > 0.0 && "the estimate the choice is made from must be readable");
        assert(svc.model().refining && "a coarse frame on screen means a refinement is owed");

        const int historyBeforeWalk = svc.model().history.nodes;

        // Finger up. The walk must climb ONE level per frame — never jumping — and end at 0.
        //
        // The loop tolerates a frame at the SAME level, because one can legitimately arrive:
        // `RenderService` coalesces, so the last `set` of the drag may still have a superseded
        // interactive render queued behind the one we waited for, and it lands at the coarse
        // level after the gesture has already ended. What may never happen is the level going
        // UP, or going down by more than one — that second one is the jump R-PREVIEW-3
        // forbids, and it is what this assertion is here to catch.
        assert(svc.dispatchText("gesture off", err) && err.empty());
        int prevLevel = svc.model().frameLevel;
        const int startLevel = prevLevel;
        int steps = 0, frames = 0;
        while (svc.model().frameLevel > 0 && frames < 20)
        {
            assert(pumpForFrame(now) && "the walk must keep producing frames");
            ++frames;
            const int lvl = svc.model().frameLevel;
            assert(lvl <= prevLevel && "the walk never goes backwards to a coarser level");
            assert(lvl >= prevLevel - 1 && "one step at a time, never a jump (R-PREVIEW-3)");
            if (lvl < prevLevel) ++steps;
            prevLevel = lvl;
        }
        assert(svc.model().frameLevel == 0 && "the walk must reach the full level");
        assert(steps == startLevel && "one step per level, from wherever it started");
        assert(!svc.model().refining && "and then stop claiming it is refining");

        // A refinement is not an edit: the walk must not have added undo entries.
        assert(svc.model().history.nodes == historyBeforeWalk &&
               "refining is not editing — no history for a value the user never touched");

        // A generous budget picks the full level, so the same code is a no-op on a machine
        // that does not need it — no configuration, no per-device branch (R-PREVIEW-2).
        svc.session().renderService().setInteractiveBudgetMs(100000.0);
        assert(svc.dispatchText("gesture on", err));
        assert(svc.dispatchText("set contrast=8", err));
        assert(pumpForFrame(now));
        assert(svc.model().frameLevel == 0 && "a fast machine must never render coarse");
        assert(svc.dispatchText("gesture off", err));

        // The event line carries the level, appended at the END so an older `expect` that
        // quoted "... ms=..." or "... rehydrated" still matches (R-SVC-5 / Event.h).
        std::vector<std::string> lines;
        svc.subscribe([&lines](const arstro::cosmo::Event &e) {
            if (e.kind == arstro::cosmo::Event::Kind::FrameReady)
                lines.push_back(arstro::cosmo::formatEvent(e));
        });
        assert(svc.dispatchText("set exposure=0.9", err));
        assert(pumpForFrame(now));
        assert(!lines.empty() && "a frame must announce itself");
        assert(lines.back().find("[evt] frame.ready") == 0);
        assert(lines.back().find(" level=") != std::string::npos && "and say which level it is");

        printf("[PASS] a_gesture_renders_coarse_and_then_refines (%d steps up)\n", steps);
    }

    void test_commands_drive_the_session()
    {
        using namespace arstro::cosmo;
        const std::string path = "/tmp/cosmo_svc_cmds.cmp";
        writeFakeProject(path, 3, false, false);
        ThreadBudget budget(50, 8);
        CosmoService svc(budget);
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
            ThreadBudget budget(50, 8);
            CosmoService svc(budget);
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
        ThreadBudget b3(50, 8);
        CosmoService svc3(b3);
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
            {K::GroupRename, "group rename 2 Shibuya"},   // bare form covered in commands_drive_the_session
            {K::Delete, "delete 2"},
            {K::MaskSet, "mask set 0 feather=0.4"},
            {K::MaskDelete, "mask delete 0"},
            {K::Undo, "undo"},
            {K::Redo, "redo"},
            {K::PresetApply, "preset apply Name"},
            {K::PresetSave, "preset save Name"},
            {K::Export, "export --outdir /tmp/out"},
            {K::SettingsSet, "settings set cpuPercent=50"},
            {K::Screen, "screen home"},
            {K::StatePrint, "state print"},
            {K::UiDump, "ui dump --root splash"},
            {K::Wait, "wait load.finished"},
            {K::Gesture, "gesture on"},
            {K::Quit, "quit"},
        };
        const int kKindCount = 28;   // Kind::None is not a command
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
        ThreadBudget budget(50, 8);
        CosmoService svc(budget);
        svc.setDecoderFactory([] { return std::unique_ptr<IImageDecoder>(new FakeDecoder()); });

        int opening = 0;
        svc.subscribe([&](const Event &e) {
            if (e.kind != Event::Kind::ProjectOpening) return;
            ++opening;
            // What the GTK host does, and what broke it. Reached through session() because
            // that is precisely the shape of the hazard: a view holding the session and
            // clearing it from inside an event handler (S4c did not remove the hazard, it
            // only changed who owns the object).
            svc.session().resetWorkspace();
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
        ThreadBudget budget(50, 16);
        CosmoService svc(budget);

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

        // R-TOUCH-6: which shell to draw is a setting, so it has to be reachable as a COMMAND —
        // that is what lets a script (or a shot renderer on a desktop host) put the touch shell
        // on screen without a device. The service stores and forwards it; it never acts on it,
        // exactly like uiScale (R-SVC-3).
        assert(!m.settings.touchUi && "the desktop shell is the default");
        {
            std::string err;
            const Command c = parseCommand("settings set touchUi=1", err);
            assert(err.empty() && c.kind == Command::Kind::SettingsSet);
            assert(svc.dispatch(c) && "settings set touchUi=1 is accepted");
        }
        assert(m.settings.touchUi && "and the model reports the touch shell");
        assert(formatModel(m, {}).find("settingsTouchUi=1") != std::string::npos &&
               "so a front end that was not listening can still read it out of a dump");
        {
            std::string err;
            assert(svc.dispatch(parseCommand("settings set touchUi=0", err)));
        }
        assert(!m.settings.touchUi && "and back");

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

    // D-21: `frameSeq` promised "a test can wait for one" and nothing ever incremented it,
    // because the VIEW polled `tryAcquire` — which MOVES the frame out, so the service could
    // not also poll without stealing frames. S4c makes the service the single owner, and this
    // is the test that could not have been written before it.
    void test_a_preview_frame_reaches_the_model()
    {
        using namespace arstro::cosmo;
        const std::string path = "/tmp/cosmo_svc_frame.cmp";
        writeFakeProject(path, 2, false, false);

        ThreadBudget budget(50, 8);
        CosmoService svc(budget);
        svc.setDecoderFactory([] { return std::unique_ptr<IImageDecoder>(new FakeDecoder()); });

        int frameEvents = 0;
        svc.subscribe([&](const Event &e) { if (e.kind == Event::Kind::FrameReady) ++frameEvents; });

        std::string err;
        assert(svc.dispatchText("project open " + path, err));
        pumpUntilIdle(svc);
        assert(svc.model().imageCount == 2);

        // An edit must produce a frame. Pump until one lands rather than sleeping a fixed
        // amount: the render worker's timing is not ours to predict (R-SVC-6).
        const unsigned before = svc.model().frameSeq;
        assert(svc.dispatchText("set exposure=1.5", err) && err.empty());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        double now = 0;
        while (svc.model().frameSeq == before && std::chrono::steady_clock::now() < deadline)
        {
            svc.pump(now);
            now += 16.0;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(svc.model().frameSeq > before && "a preview landed and the model says so");
        assert(frameEvents > 0 && "and an event announced it");
        assert(svc.model().frameWidth > 0 && svc.model().frameHeight > 0);
        assert(svc.model().frameSlot >= 0);

        // The pixels come out exactly once: whoever takes the frame owns it, and a second
        // take finds nothing. That is the invariant that made two pollers impossible.
        arstro::RenderService::Frame f;
        assert(svc.takeFrame(f) && f.width > 0 && (int)f.rgba.size() == f.width * f.height * 4);
        assert(!svc.takeFrame(f) && "a frame is handed over once, not queued");

        // R-SVC-3: the model carries metadata, never pixels — the frame block is ints only.
        assert(svc.model().ownParams.exposure == 1.5f && svc.model().hasEditTarget);

        std::filesystem::remove(path);
        printf("[PASS] a_preview_frame_reaches_the_model (seq %u -> %u, %d events)\n",
               before, svc.model().frameSeq, frameEvents);
    }

    // D-22 / R-LOADUX-4: a load must say something before the first entry FINISHES. With five
    // workers each taking ~9 s on a RAF, `done` alone left the bar at zero for 9.1 s and then
    // leapt by five. The claim is the earliest honest signal there is.
    void test_a_load_reports_work_before_any_result()
    {
        using namespace arstro::cosmo;
        const std::string path = "/tmp/cosmo_svc_started.cmp";
        writeFakeProject(path, 12, false, false);

        ThreadBudget budget(100, 8);
        CosmoService svc(budget);
        // A decoder slow enough that claims MUST arrive before completions, which is the
        // ordering the defect was about.
        struct Slow : IImageDecoder
        {
            DecodedImage decodeFile(const std::string &p) override
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(60));
                DecodedImage d; d.width = d.height = 8;
                d.rgba.assign((size_t)8 * 8 * 4, 120); d.name = p;
                return d;
            }
        };
        svc.setDecoderFactory([] { return std::unique_ptr<IImageDecoder>(new Slow()); });

        int firstStartedAtDone = -1, startedEvents = 0;
        std::vector<std::string> stages;
        svc.subscribe([&](const Event &e) {
            if (e.kind == Event::Kind::LoadStage) stages.push_back(e.text);
            if (e.kind != Event::Kind::EntryStarted) return;
            if (++startedEvents == 1) firstStartedAtDone = svc.model().load.done;
            assert(!e.text.empty() && "a claim names the entry, or the status line has nothing to say");
        });

        std::string err;
        assert(svc.dispatchText("project open " + path, err));
        // The stage is announced before any decode, so the seconds before the first result
        // still say something true.
        assert(!stages.empty() && stages.front() == "reading");
        pumpUntilIdle(svc);

        assert(startedEvents == 12 && "every entry announced its claim exactly once");
        assert(firstStartedAtDone == 0 && "the FIRST claim arrived before ANY entry had finished");
        assert(svc.model().load.started == 12 && svc.model().load.done == 12);
        assert(svc.model().load.inFlight() == 0 && "nothing in flight once it is all applied");
        bool sawDecoding = false;
        for (const std::string &st : stages) if (st == "decoding") sawDecoding = true;
        assert(sawDecoding);

        std::filesystem::remove(path);
        printf("[PASS] a_load_reports_work_before_any_result (%d claims, first at done=%d)\n",
               startedEvents, firstStartedAtDone);
    }

    // D-23: the `.cmp` format stores `path=` for an image and a name only for a group, so an
    // image entry arrived nameless and stayed nameless all the way to the filmstrip, the
    // breadcrumb and the export dialog — where an empty name rendered as "(missing image)".
    // Pre-existing, and silent, which is why it survived: an empty string draws as nothing.
    void test_an_image_entry_gets_its_filename()
    {
        const std::string path = "/tmp/cosmo_core_names.cmp";
        {
            std::ofstream f(path, std::ios::trunc);
            f << "cosmoworkspace=1\n";
            f << "#group\nparent=-1\nname=Tokyo\n";
            f << "#image\nparent=0\npath=/photos/DSCF5186.RAF\n";
            f << "#image\nparent=0\npath=/photos/sub dir/_DSF6151.RAF\n";
        }
        std::vector<EditSession::WorkspaceEntry> entries;
        assert(EditSession::readWorkspaceFile(path, entries));
        assert(entries.size() == 3);
        assert(entries[0].group && entries[0].name == "Tokyo" && "a group keeps its stored name");
        assert(entries[1].name == "DSCF5186.RAF" && "an image is named from its path");
        assert(entries[2].name == "_DSF6151.RAF" && "including one in a directory with a space");

        // And the sink names it too, for any producer that hands over a nameless leaf — the
        // second line of defence, because the original failure was invisible.
        EditSession s;
        const int node = s.addPendingImage(0, "");
        auto px = solidImage(8, 8, 10, 20, 30);
        EditSession::Thumb th = EditSession::makeThumb(px.data(), 8, 8, 110);
        const int slot = s.attachImage(node, std::vector<uint8_t>(px), 8, 8,
                                       "/photos/_DSF7014.RAF", std::move(th));
        assert(slot >= 0);
        assert(s.nameForSlot(slot) == "_DSF7014.RAF" && "attachImage derives a missing name");
        assert(s.nodes()[node].name == "_DSF7014.RAF" && "and the tree agrees, so the filmstrip does");

        std::filesystem::remove(path);
        printf("[PASS] an_image_entry_gets_its_filename\n");
    }
    // R-THUMB-1: the pixel transform a camera's embedded preview needs so a cover is oriented
    // like its photo. Hand-computed against LibRaw's own flip_index, on a 3x2 whose every pixel
    // is identifiable -- a rotation that is 180 degrees out passes any aspect-only check.
    void test_a_thumbnail_is_turned_the_way_the_photo_is()
    {
        using arstro::cosmo::DecodedImage;
        using arstro::cosmo::NativeImageDecoder;
        // A B C
        // D E F   (one grey level per pixel, so a wrong index is a wrong number)
        const uint8_t v[6] = {10, 20, 30, 40, 50, 60};
        auto make = [&] {
            DecodedImage img; img.width = 3; img.height = 2; img.rgba.resize(3 * 2 * 4);
            for (int i = 0; i < 6; ++i)
            { img.rgba[i * 4 + 0] = img.rgba[i * 4 + 1] = img.rgba[i * 4 + 2] = v[i]; img.rgba[i * 4 + 3] = 255; }
            return img;
        };
        auto at = [](const DecodedImage &img, int r, int c) { return img.rgba[((size_t)r * img.width + c) * 4]; };

        DecodedImage none = make();
        NativeImageDecoder::applyFlip(none, 0);          // flip 0 changes nothing at all
        assert(none.width == 3 && none.height == 2 && at(none, 0, 0) == 10 && at(none, 1, 2) == 60);

        // flip 5 (= 4|1) is the quarter turn Fujifilm and Panasonic files carry: the dimensions
        // swap and the result is the source rotated 90 degrees counter-clockwise.
        //   C F
        //   B E
        //   A D
        DecodedImage cw = make();
        NativeImageDecoder::applyFlip(cw, 5);
        assert(cw.width == 2 && cw.height == 3);
        assert(at(cw, 0, 0) == 30 && at(cw, 0, 1) == 60);
        assert(at(cw, 1, 0) == 20 && at(cw, 1, 1) == 50);
        assert(at(cw, 2, 0) == 10 && at(cw, 2, 1) == 40);

        // flip 6 (= 4|2) is the other quarter turn -- 90 degrees clockwise.
        //   D A
        //   E B
        //   F C
        DecodedImage ccw = make();
        NativeImageDecoder::applyFlip(ccw, 6);
        assert(ccw.width == 2 && ccw.height == 3);
        assert(at(ccw, 0, 0) == 40 && at(ccw, 0, 1) == 10);
        assert(at(ccw, 2, 0) == 60 && at(ccw, 2, 1) == 30);

        // flip 3 (= 2|1) is 180 degrees: same shape, both axes mirrored.
        DecodedImage half = make();
        NativeImageDecoder::applyFlip(half, 3);
        assert(half.width == 3 && half.height == 2);
        assert(at(half, 0, 0) == 60 && at(half, 1, 2) == 10);

        // Turning it four quarter-turns comes back to the original, which is the property that
        // would catch a transposed-but-not-mirrored implementation.
        DecodedImage round = make();
        for (int i = 0; i < 4; ++i) NativeImageDecoder::applyFlip(round, 6);
        assert(round.width == 3 && round.height == 2);
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 3; ++c) assert(at(round, r, c) == v[r * 3 + c]);

        printf("[PASS] a_thumbnail_is_turned_the_way_the_photo_is\n");
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
    test_every_decoding_thread_is_pinned();
    test_open_image_move_and_prebuilt_thumb_match_the_copying_path();
    test_finish_workspace_load_keeps_an_existing_selection();
    test_pending_images_appear_then_attach();
    test_selecting_a_pending_image_keeps_the_stage();
    test_an_event_sees_the_model_it_describes();
    test_settings_roundtrip_and_survive_a_bad_file();
    test_cpu_budget_scales_with_percent();
    test_one_budget_is_divided_not_duplicated();
    test_project_load_peak_never_exceeds_its_pool();
    test_command_text_roundtrips();
    test_every_command_kind_has_a_grammar();
    test_engine_memory_is_capped_and_a_cold_slot_is_re_decoded();
    test_a_cold_slot_is_re_decoded_through_the_service();
    test_walking_a_rack_that_fits_the_cap_never_re_decodes();
    test_selection_reaches_the_service_during_a_load();
    test_service_opens_a_project_with_no_ui();
    test_a_load_decodes_cheaply_and_an_export_decodes_properly();
    test_a_gesture_renders_coarse_and_then_refines();
    test_commands_drive_the_session();
    test_two_services_dump_the_same_state();
    test_a_view_may_reset_on_project_opening();
    test_settings_and_dump_options_reach_the_model();
    test_a_preview_frame_reaches_the_model();
    test_a_load_reports_work_before_any_result();
    test_an_image_entry_gets_its_filename();
    test_a_thumbnail_is_turned_the_way_the_photo_is();
    printf("\nAll cosmo_core session tests passed.\n");
    return 0;
}
