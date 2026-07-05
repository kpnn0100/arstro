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
}

int main()
{
    test_open_edit_submit_and_poll();
    test_thumbnail_generated();
    test_group_tree_create_navigate_ungroup();
    test_undo_redo();
    test_preset_save_and_apply_roundtrip();
    test_session_save_and_read_roundtrip();
    printf("\nAll cosmo_core session tests passed.\n");
    return 0;
}
