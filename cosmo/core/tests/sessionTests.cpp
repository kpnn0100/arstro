// Smoke tests for cosmo_core::EditSession -- sanity-checks the logic extracted
// from cosmo/CosmoApp before any UI is built on top of it. Not exhaustive; the
// full behavioral contract is still verified by the wider cosmo app it was
// extracted from. Plain assert()-based, no external test framework.
#include "../EditSession.h"
#include "../PresetLibrary.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
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
    printf("\nAll cosmo_core session tests passed.\n");
    return 0;
}
