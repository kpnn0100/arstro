/*
 *  interstellar_ui_tests — assertions over the ASSEMBLED app, with no display (R-UI-7).
 *
 *  The failure these exist for is R-G-1's compliance clause: **"nothing changes in one frame"
 *  cannot be established by reading code.** Three changes in cosmo shipped a snap that read
 *  correctly. So it is established by driving App's clock and requiring the LIVE value to differ
 *  from its TARGET mid-tween — the only assertion that can tell an eased implementation from a
 *  snapping one.
 *
 *  And never by settling first: 400 ms is long enough for a 260 ms fade to finish, so an
 *  assertion after settling reads 1.0 and passes a snap. These pump ONE frame at a time.
 */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "../../App.h"
#include "../../core/service/InterstellarService.h"
#include "../../../../core/Artboard/src/render/RecordingTarget.h"
#include <cmath>
#include <cstdio>
#include <string>

using namespace arstro;
using namespace arstro::interstellar;
using arstro::interstellar_v1::App;

namespace
{
    void seed(InterstellarService &svc)
    {
        // `rack add` first: a clip's `--src` names a SOURCE, and an invented node id is now
        // refused rather than reading as offline three commands later. These tests have no
        // decoder and do not need one — the sources register and read as offline, which is
        // exactly the state a layout test should be able to draw.
        const char *lines[] = {
            "project new /tmp/ui.isp --fps 24 --res 1920x1080",
            "rack add /tmp/ui-a.mov /tmp/ui-b.mov",
            "track add --kind video --name v0",
            "clip add --track v0 --src rack:ui_a --in 12.4 --out 16.6 --at 0 --name clp_a",
            "clip add --track v0 --src rack:ui_b --in 88.0 --out 91.1 --at 4.2 --name clp_b",
            "auto new ac_push --dur 2.0 --points 0=0,1=1 --ease easeInOut",
            "auto link ac_push -> clp_a.geom.scale --at 1.0 --dur 2.0 --from 1.0 --to 1.08",
            "auto link ac_push -> clp_a.opacity --at 1.0 --dur 2.0 --from 1.0 --to 0.4",
            "playhead 2.0"};
        for (const char *l : lines)
        {
            std::string err;
            const bool ok = svc.dispatchText(l, err);
            if (!ok) std::fprintf(stderr, "seed: %s — %s\n", l, svc.model().lastError.c_str());
            assert(ok);
        }
    }

    void test_the_workspace_switch_is_a_tween_not_a_snap()
    {
        InterstellarService::Hooks hooks;
        InterstellarService svc(hooks);
        seed(svc);
        App app(svc, 1440, 900);
        double now = 0;
        app.advance(now);

        app.showWorkspace(Workspace::Mix);
        // ONE frame at a time, keeping the FIRST NON-ZERO value. The distinction matters: the
        // frame that STARTS the tween legitimately reads 0.0 (there is nowhere to travel from
        // yet), so asserting on the very first frame would fail a correct implementation — and
        // settling instead would let a 260 ms fade finish and pass a snapping one.
        double first = 0.0;
        for (int i = 0; i < 8 && first <= 0.0; ++i)
        {
            now += 16.0;
            app.advance(now);
            first = app.workspaceFade();
        }
        assert(first > 0.0 && first < 1.0);
        now += 16.0;
        app.advance(now);
        const double second = app.workspaceFade();
        // Two frames half a tween apart that DIFFER — the compliance check R-G-1 actually asks
        // for, rather than a reading of the code.
        assert(second > first);
        assert(second < 1.0);
        for (int i = 0; i < 40; ++i) { now += 16.0; app.advance(now); }
        assert(std::fabs(app.workspaceFade() - 1.0) < 1e-6);   // and it SETTLES
        std::printf("[PASS] the workspace cross-fade interpolates (%.3f -> %.3f) and settles\n",
                    first, second);
    }

    void test_the_timeline_zoom_eases_and_the_ruler_follows_the_live_value()
    {
        InterstellarService::Hooks hooks;
        InterstellarService svc(hooks);
        seed(svc);
        App app(svc, 1440, 900);
        double now = 0;
        app.advance(now);

        const double before = app.timeline().pixelsPerSecond();
        app.timeline().zoomTo(before * 3.0);
        double live = before;
        for (int i = 0; i < 8 && live <= before; ++i)
        {
            now += 16.0;
            app.advance(now);
            live = app.timeline().pixelsPerSecond();
        }
        // Mid-tween the LIVE value must differ from the TARGET, or the zoom snapped. This is the
        // gotcha-19 failure: deriving geometry once zooms the ruler and leaves the clips behind.
        assert(live > before);
        assert(live < app.timeline().pixelsPerSecondTarget());

        // And every geometric read goes through that live value: a clip's width must match the
        // zoom that is drawn, not the one that was asked for.
        const artboard::Rect r = app.timeline().clipRect("clp_a");
        const double expected = 4.2 * live;   // clp_a is 4.2 s long
        assert(std::fabs(r.w - expected) < 0.5);
        for (int i = 0; i < 40; ++i) { now += 16.0; app.advance(now); }
        assert(std::fabs(app.timeline().pixelsPerSecond() - before * 3.0) < 1e-6);
        std::printf("[PASS] the zoom eases and the clip rect follows the LIVE value\n");
    }

    void test_nothing_overlaps_and_nothing_is_clipped_at_the_edge()
    {
        for (auto size : {std::pair<double, double>{1440, 900}, std::pair<double, double>{1024, 640}})
        {
            InterstellarService::Hooks hooks;
            InterstellarService svc(hooks);
            seed(svc);
            App app(svc, size.first, size.second);
            double now = 0;
            for (int i = 0; i < 30; ++i) { now += 16.0; app.advance(now); }

            struct Box { const char *name; artboard::Rect r; };
            auto rectOf = [](const artboard::Segment &s) {
                return artboard::Rect{s.x.value(), s.y.value(), s.width.value(), s.height.value()};
            };
            const Box boxes[] = {{"bar", rectOf(app.bar())},
                                 {"monitor", rectOf(app.monitor())},
                                 {"transport", rectOf(app.transport())},
                                 {"deck", rectOf(app.timeline())}};
            for (const auto &b : boxes)
            {
                // Contained: nothing may hang off the window edge.
                assert(b.r.x >= -0.001);
                assert(b.r.y >= -0.001);
                assert(b.r.x + b.r.w <= size.first + 0.001);
                assert(b.r.y + b.r.h <= size.second + 0.001);
                assert(b.r.w > 0 && b.r.h > 0);
            }
            // Siblings SNAP, never stack: the four rows tile the height exactly.
            const double total = boxes[0].r.h + boxes[1].r.h + boxes[2].r.h +
                                 boxes[3].r.h;
            assert(std::fabs(total - size.second) < 0.001);
            // And the monitor keeps a usable height even in the small window.
            assert(boxes[1].r.h > 100.0);
            std::printf("[PASS] contained and non-overlapping at %.0fx%.0f\n", size.first, size.second);
        }
    }

    void test_the_monitor_is_one_widget_in_every_workspace()
    {
        // R-UI-2: a frame that looks different in two workspaces is a defect, so the monitor's
        // geometry must not change when the workspace does.
        InterstellarService::Hooks hooks;
        InterstellarService svc(hooks);
        seed(svc);
        App app(svc, 1440, 900);
        double now = 0;
        for (int i = 0; i < 30; ++i) { now += 16.0; app.advance(now); }
        const artboard::Rect cut = app.monitor().frameRect();
        app.showWorkspace(Workspace::Mix);
        for (int i = 0; i < 40; ++i) { now += 16.0; app.advance(now); }
        const artboard::Rect mix = app.monitor().frameRect();
        assert(std::fabs(cut.x - mix.x) < 1e-9 && std::fabs(cut.w - mix.w) < 1e-9);
        // And the playhead did not move: a workspace switch is not a reload.
        assert(std::fabs(svc.model().playhead - 2.0) < 1e-9);
        std::printf("[PASS] the monitor is one widget and the playhead survives a switch\n");
    }

    void test_the_lane_stack_scrolls_and_clamps_both_ends()
    {
        // R6: a clip with no scroll silently deletes content, and a panel the user can see is cut
        // off but cannot reach is worse than one that never showed it.
        InterstellarService::Hooks hooks;
        InterstellarService svc(hooks);
        seed(svc);
        App app(svc, 1440, 900);
        double now = 0;
        for (int i = 0; i < 30; ++i) { now += 16.0; app.advance(now); }

        auto &lanes = app.lanes();
        // Two links were seeded, so there are two lanes.
        assert(lanes.contentHeight() > 0);
        lanes.scrollBy(-500.0);
        for (int i = 0; i < 30; ++i) { now += 16.0; app.advance(now); }
        assert(lanes.scrollOffset() >= -1e-6);      // clamped at the TOP
        lanes.scrollBy(5000.0);
        for (int i = 0; i < 30; ++i) { now += 16.0; app.advance(now); }
        const double maxScroll = std::max(0.0, lanes.contentHeight() - 900.0);
        assert(lanes.scrollOffset() <= maxScroll + 1e-6);   // and at the BOTTOM
        std::printf("[PASS] the lane stack scrolls and clamps both ends\n");
    }

    void test_a_lane_expansion_eases()
    {
        InterstellarService::Hooks hooks;
        InterstellarService svc(hooks);
        seed(svc);
        App app(svc, 1440, 900);
        double now = 0;
        for (int i = 0; i < 30; ++i) { now += 16.0; app.advance(now); }
        const std::string addr = "clp_a.geom.scale";
        assert(app.lanes().expansion(addr) < 0.01);
        app.lanes().toggleExpanded(addr);
        double a = 0.0;
        for (int i = 0; i < 8 && a <= 0.0; ++i)
        {
            now += 16.0;
            app.advance(now);
            a = app.lanes().expansion(addr);
        }
        now += 16.0;
        app.advance(now);
        const double b = app.lanes().expansion(addr);
        // A row that swapped between two heights would read 0 then 1. This must interpolate.
        assert(a > 0.0 && a < 1.0 && b > a && b < 1.0);
        std::printf("[PASS] a lane expansion eases (%.3f -> %.3f), it does not swap\n", a, b);
    }

    void test_a_scrub_goes_through_a_command_and_moves_the_playhead()
    {
        // R-G-4: the GUI has no privileged path. A drag becomes a Command, and the model is what
        // the view then reads back.
        InterstellarService::Hooks hooks;
        InterstellarService svc(hooks);
        seed(svc);
        App app(svc, 1440, 900);
        double now = 0;
        for (int i = 0; i < 30; ++i) { now += 16.0; app.advance(now); }

        const double before = svc.model().playhead;
        // Press in the timeline's ruler, a third of the way across.
        const double deckY = app.timeline().y.value();
        app.pointer(0, 400.0, deckY + 6.0, 0, now);
        const double after = svc.model().playhead;
        assert(std::fabs(after - before) > 1e-6);
        assert(after >= 0.0);
        std::printf("[PASS] a ruler press dispatches a Playhead command (%.3f -> %.3f)\n",
                    before, after);
    }

    void test_the_text_fits_its_box()
    {
        // R5: measured, not estimated. A RecordingTarget gives the real draw-op stream, so this
        // asks the geometric question directly rather than looking at a picture.
        InterstellarService::Hooks hooks;
        InterstellarService svc(hooks);
        seed(svc);
        App app(svc, 1024, 640);
        double now = 0;
        for (int i = 0; i < 30; ++i) { now += 16.0; app.advance(now); }
        artboard::RecordingTarget rec;
        app.render(rec, now);
        // Every text op must start inside the window. A label whose ORIGIN is already off-screen
        // is the overflow this catches; the widgets ellipsize against a measured width, so none
        // should be.
        int texts = 0;
        for (const auto &op : rec.ops())
            if (op.kind == artboard::DrawOp::Kind::DrawText)
            {
                ++texts;
                assert(op.args[0] >= -1.0 && op.args[0] <= 1024.0);
                assert(op.args[1] >= -1.0 && op.args[1] <= 640.0);
            }
        assert(texts > 0);
        std::printf("[PASS] %d text ops, every origin inside a 1024x640 window\n", texts);
    }
}

int main()
{
    test_the_workspace_switch_is_a_tween_not_a_snap();
    test_the_timeline_zoom_eases_and_the_ruler_follows_the_live_value();
    test_nothing_overlaps_and_nothing_is_clipped_at_the_edge();
    test_the_monitor_is_one_widget_in_every_workspace();
    test_the_lane_stack_scrolls_and_clamps_both_ends();
    test_a_lane_expansion_eases();
    test_a_scrub_goes_through_a_command_and_moves_the_playhead();
    test_the_text_fits_its_box();
    std::printf("\nall interstellar ui tests passed\n");
    return 0;
}
