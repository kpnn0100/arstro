/*
 *  interstellar_v1 — interstellar_app_shots: the headless shot harness.
 *
 *  Renders the REAL App, over a FakeService (tests/FakeService.h), through the REAL Cairo adapter
 *  into PNGs with no display — every named state, at two window sizes, at rest AND mid-transition,
 *  so a change can be SEEN before it ships. cosmo_shots' pattern, plus the two flags its skill
 *  says cost cosmo an investigation each: `--size` and `--tree`.
 *
 *   * The clock is a fixed 16 ms tick, never the wall clock: a mid-transition frame is
 *     reproducible only when the frame INDEX decides where it is.
 *   * ONE CairoTarget per rig, re-bound to each frame's context: image ids live in the target,
 *     and a fresh target per frame would blank every registered frame and cover.
 *   * The embedded faces are registered first (cosmo's EmbeddedFonts), so every measurement is
 *     the app's own type — a shot in the host's default sans reports overflow that does not exist.
 *
 *  Usage:
 *     interstellar_app_shots [--outdir DIR] [--only SUBSTR] [--size WxH] [--tree] [--check] [--list]
 *  Without --size, every shot is rendered at 1440x900 AND 1024x640.
 */
#include "App.h"
#include "EmbeddedFonts.h"
#include "adapter/native/CairoTarget.h"
#include "FakeService.h"
#include "Rig.h"
#include <cairo/cairo.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <typeinfo>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using arstro::interstellar_v1::App;
    using arstro::interstellar_v1::EditScreen;
    using artboard::Point;
    using artboard::Rect;
    using artboard::Segment;
    using istest::FakeService;

    struct Options
    {
        std::string outdir = "shots";
        std::string only;
        int w = 0, h = 0;
        bool tree = false, check = false, list = false;
    } gOpt;

    using istest::Rig;
    using istest::world;
    using istest::centre;

    std::string className(const Segment &s)
    {
        int status = 0;
        char *dm = abi::__cxa_demangle(typeid(s).name(), nullptr, nullptr, &status);
        std::string n = (status == 0 && dm) ? dm : typeid(s).name();
        std::free(dm);
        const auto k = n.rfind("::");
        return k == std::string::npos ? n : n.substr(k + 2);
    }

    void dumpTree(std::ostream &o, const Segment &s, int depth)
    {
        const Point a = s.worldTransform().apply(Point{0, 0});
        char buf[256];
        std::snprintf(buf, sizeof buf, "%*s%s  [%.1f,%.1f %.1fx%.1f]  opacity=%.3f%s%s%s\n", depth * 2, "", className(s).c_str(), a.x, a.y,
                      s.width.value(), s.height.value(), s.opacity.value(), s.visible ? "" : "  hidden", s.enabled ? "" : "  disabled",
                      s.isFadedOut() ? "  faded" : "");
        o << buf;
        for (const auto &c : s.children()) dumpTree(o, *c, depth + 1);
    }

    struct Shot
    {
        std::string name;
        std::function<void(FakeService &)> setup;
        std::function<void(Rig &)> drive;
    };

    // ── the shot list ────────────────────────────────────────────────────────────────────

    std::vector<Shot> shots()
    {
        auto home = [](FakeService &s) { s.home(); };
        auto edit = [](FakeService &s) { s.edit(); };
        std::vector<Shot> v;
        // Home
        v.push_back({"home_populated", home, [](Rig &r) { r.settle(); }});
        v.push_back({"home_hover", home, [](Rig &r) {
            r.settle();
            const Rect c = r.app->home().cardLive(0);
            r.move(c.x + c.w * 0.5, c.y + c.h * 0.4);
            r.pump(200);
        }});
        v.push_back({"home_empty", [](FakeService &s) { s.homeEmpty(); }, [](Rig &r) { r.settle(); }});
        v.push_back({"home_loading", [](FakeService &s) { s.homeLoading(); }, [](Rig &r) { r.settle(); }});
        v.push_back({"home_reflow_mid", home, [](Rig &r) {
            const int w = r.w, h = r.h;
            r.resize(w + 300, h);
            r.settle();
            r.resize(w, h);
            r.pump(96);   // ~ a third of the 260 ms reflow
        }});
        v.push_back({"home_settings", home, [](Rig &r) {
            r.settle();
            const Rect s = r.app->home().settingsRect();
            r.click(s.x + 10, s.y + 9);
            r.settle();
        }});
        // Loading
        v.push_back({"loading", [](FakeService &s) { s.loading(); }, [](Rig &r) { r.pump(400); }});
        v.push_back({"loading_to_edit_mid", [](FakeService &s) { s.loading(); }, [](Rig &r) {
            r.pump(400);
            r.svc.edit();
            r.pump(112);   // mid shell cross-fade (260 ms)
        }});
        // Edit — Grade
        v.push_back({"grade_populated", edit, [](Rig &r) { r.settle(); }});
        // cosmo's menu strip, settings dialog and screen scale
        auto openMenu = [](Rig &r, int i, double ms) {
            auto strip = r.app->edit().topBar()->menus();
            const Point t = centre(*strip, strip->titleRect(i));
            r.click(t.x, t.y);
            r.pump(ms);
        };
        v.push_back({"edit_menu_file", edit, [openMenu](Rig &r) { r.settle(); openMenu(r, 0, 400); }});
        v.push_back({"edit_menu_file_mid", edit, [openMenu](Rig &r) { r.settle(); openMenu(r, 0, 64); }});
        v.push_back({"edit_menu_edit", edit, [openMenu](Rig &r) { r.settle(); openMenu(r, 1, 400); }});
        v.push_back({"edit_menu_preset", [](FakeService &s) { s.edit(); s.m.presets = {{"Film/Warm fade", "Film"}, {"Film/Bleach", "Film"}, {"Soft skin", ""}}; ++s.m.revision; },
                     [openMenu](Rig &r) { r.settle(); openMenu(r, 4, 400); }});
        v.push_back({"edit_settings", edit, [](Rig &r) { r.settle(); r.app->openSettings(); r.settle(); }});
        // cosmo's selection + right-click menu on rack items
        v.push_back({"grade_multiselect", [](FakeService &s) { s.edit(); for (int k : {1, 2, 3}) s.m.rack[(size_t)k].selected = true; ++s.m.revision; },
                     [](Rig &r) { r.settle(); }});
        v.push_back({"grade_context_menu", edit, [](Rig &r) {
            r.settle();
            auto rt = r.app->edit().rackTree();
            const Point p = centre(*rt, rt->rowRect(1));
            r.app->pointer(1, p.x - 40, p.y, 0, r.now);
            r.app->pointer(0, p.x - 40, p.y, 2, r.now);
            r.app->pointer(2, p.x - 40, p.y, 2, r.now + 40.0);
            r.settle();
        }});
        // the IMAGE PROCESSING list (R-FX-5): Cosmo selected (its Mix + tabs), an effect selected
        // keyframes (R-ANIM-3/4, amended): authored in the timeline's key lane — a source's colour curve
        // under its clip, a key's handles; and the lane mid-opening
        auto animated = [](FakeService &s) {
            s.edit();
            s.m.selectedClip = "c1";
            s.m.playhead = 2.0;
            s.animate("s_day01.basic.exposure", {{2.0, -1.2}, {4.0, 1.4}, {6.5, 0.3}});
            auto *a = s.animFor("s_day01.basic.exposure", false);
            a->keys[1].in = a->keys[1].out = "bezier";
            a->keys[2].in = "bezier";
            a->keys[2].inflIn = 60.0;
            s.animate("s_day01.basic.contrast", {{3.0, 10.0}, {6.0, -15.0}});
        };
        v.push_back({"cut_key_lane_grade", animated, [](Rig &r) {
            r.app->setTab(1);
            r.settle();
            auto tl = r.app->edit().timeline();
            tl->setKeysShown(true);
            r.settle();
            tl->keyLane()->select("s_day01.basic.exposure");
            r.settle();
            auto g = tl->keyLane()->graph();
            const Point k = world(*g, g->keyPoint(1).x, g->keyPoint(1).y);
            r.press(k.x, k.y);
            r.releaseAt(k.x, k.y);
            r.settle();
        }});
        v.push_back({"cut_key_lane_multi", animated, [](Rig &r) {
            r.app->setTab(1);
            r.settle();
            auto tl = r.app->edit().timeline();
            tl->setKeysShown(true);
            r.settle();
            tl->keyLane()->select("s_day01.basic.contrast");
            tl->keyLane()->select("s_day01.basic.exposure", true);
            r.settle();
            auto g = tl->keyLane()->graph();
            g->selectKey(0, 1, false);
            g->selectKey(1, 1, true);
            r.settle();
        }});
        v.push_back({"cut_key_lane_mid", animated, [](Rig &r) {
            r.app->setTab(1);
            r.settle();
            r.app->edit().timeline()->setKeysShown(true);
            r.pump(100);
        }});
        v.push_back({"cut_key_lane", [](FakeService &s) {
            s.edit();
            for (const auto &c : s.m.clips)
                if (c.id == s.m.selectedClip)
                {
                    const std::string n = c.name.empty() ? c.id : c.name;
                    s.animate(n + ".opacity", {{c.in, 0.0}, {c.in + (c.out - c.in) * 0.4, 1.0}, {c.out, 0.6}});
                }
        }, [](Rig &r) { r.app->setTab(1); r.settle(); r.app->edit().timeline()->setKeysShown(true); r.settle(); }});
        v.push_back({"grade_plugins_effect", edit, [](Rig &r) {
            r.settle();
            r.app->edit().gradeInspector()->plugins()->select("ef_1");
            r.settle();
        }});
        v.push_back({"grade_effects_sections", edit, [](Rig &r) {
            r.settle();
            r.app->edit().gradeInspector()->plugins()->select("ef_1");
            r.settle();
            r.app->edit().gradeInspector()->effectPanel()->setOpen("ef_2", true);
            r.settle();
        }});
        v.push_back({"grade_plugins_swap_mid", edit, [](Rig &r) {
            r.settle();
            r.app->edit().gradeInspector()->plugins()->select("ef_1");
            r.pump(80);
        }});
        // the capture button (R-UI-11) and Grade without a transport (R-UI-3, amended)
        v.push_back({"grade_capture_menu", edit, [](Rig &r) {
            r.settle();
            auto mon = r.app->edit().monitor();
            const Point p = centre(*mon, mon->captureRect());
            r.click(p.x, p.y);
            r.settle();
        }});
        v.push_back({"cut_capture_menu", edit, [](Rig &r) {
            r.app->setTab(1);
            r.settle();
            auto tp = r.app->edit().transport();
            const Point p = centre(*tp, tp->buttonRect(3));
            r.move(p.x, p.y);
            r.click(p.x, p.y);
            r.settle();
        }});
        v.push_back({"grade_frame_copied", edit, [](Rig &r) {
            r.settle();
            auto mon = r.app->edit().monitor();
            const Point p = centre(*mon, mon->captureRect());
            r.click(p.x, p.y);
            r.pump(250);
            auto cm = r.app->edit().contextMenu();
            const Point ip = centre(*cm, cm->itemRect(0));
            r.click(ip.x, ip.y);
            r.pump(400);
        }});
        // the reference-frame slider (R-RACK-3 amended): mid-drag it previews in the monitor
        v.push_back({"grade_ref_seek_drag", edit, [](Rig &r) {
            r.settle();
            auto deck = r.app->edit().gradeDeck();
            const Rect tr = deck->frameTrackRect();
            const Point a = world(*deck, tr.x + tr.w * 0.21, tr.y + tr.h * 0.5), b = world(*deck, tr.x + tr.w * 0.62, tr.y + tr.h * 0.5);
            r.press(a.x, a.y);
            r.frame();
            for (int k = 1; k <= 6; ++k) { r.dragTo(a.x + (b.x - a.x) * k / 6.0, a.y); r.frame(); }
            r.pump(200);
        }});
        v.push_back({"grade_ref_step_hover", edit, [](Rig &r) {
            r.settle();
            auto deck = r.app->edit().gradeDeck();
            const Point p = centre(*deck, deck->stepRect(1));
            r.move(p.x, p.y);
            r.settle();
        }});
        // browsing groups like cosmo (R-UI-12): the top level, mid level-swap, after grouping, gr2 open
        v.push_back({"grade_groups_top", edit, [](Rig &r) { r.settle(); r.app->edit().gradeDeck()->openGroup(""); r.settle(); }});
        v.push_back({"grade_groups_swap_mid", edit, [](Rig &r) { r.settle(); r.app->edit().gradeDeck()->openGroup(""); r.pump(96); }});
        v.push_back({"grade_groups_grouped", edit, [](Rig &r) {
            r.settle();
            std::string err;
            r.svc.dispatch("rack select s_drone01", err);
            r.svc.dispatch("rack group new", err);
            r.settle();
        }});
        v.push_back({"grade_groups_open_gr2", edit, [](Rig &r) {
            r.settle();
            r.app->edit().rackTree()->setOpen("ro4", true);
            r.app->edit().gradeDeck()->openGroup("ro4");
            r.settle();
        }});
        // a variant (R-RACK-5): same file, its own row, "shared" on both
        v.push_back({"grade_variant", edit, [](Rig &r) {
            r.settle();
            std::string err;
            r.svc.dispatch("rack duplicate s_day01", err);
            r.settle();
        }});
        v.push_back({"cut_variant_menu", edit, [](Rig &r) {
            std::string err;
            r.svc.dispatch("rack duplicate s_day01", err);
            r.app->setTab(1);
            r.settle();
            auto bin = r.app->edit().sourceBin();
            const Point p = centre(*bin, bin->rowRect(0));
            r.app->pointer(1, p.x, p.y, 0, r.now);
            r.app->pointer(0, p.x, p.y, 2, r.now);
            r.app->pointer(2, p.x, p.y, 2, r.now + 40.0);
            r.settle();
        }});
        // the monitor zoom (R-UI-13): Ctrl + wheel about a point off-centre, at rest and mid-tween
        auto zoomAt = [](Rig &r) {
            auto mon = r.app->edit().monitor();
            const Rect fr = mon->frameRect();
            const Point p = world(*mon, fr.x + fr.w * 0.62, fr.y + fr.h * 0.42);
            r.app->wheel(p.x, p.y, 8.0, true);
        };
        v.push_back({"grade_monitor_zoom", edit, [zoomAt](Rig &r) { r.settle(); zoomAt(r); r.settle(); }});
        v.push_back({"grade_monitor_zoom_mid", edit, [zoomAt](Rig &r) { r.settle(); zoomAt(r); r.pump(64); }});
        // Deliver's whole spec (R-RENDER-6): H.265 with its rows, and mid-way from ProRes to H.265
        auto clickSegShot = [](Rig &r, std::shared_ptr<arstro::cosmo_v2::SegmentedControl> sc, int i) {
            auto *b = dynamic_cast<arstro::cosmo_v2::PillButton *>(sc->children()[(size_t)i].get());
            const Point q = centre(*b, b->localBounds());
            r.click(q.x, q.y);
        };
        v.push_back({"deliver_spec_h265", edit, [clickSegShot](Rig &r) {
            r.app->setTab(2);
            r.settle();
            auto os = r.app->edit().outputSpec();
            clickSegShot(r, os->formatPicker(), 1);
            r.settle();
            clickSegShot(r, os->depthPicker(), 1);
            clickSegShot(r, os->sizePicker(), 1);
            clickSegShot(r, os->rangePicker(), 1);
            r.settle();
        }});
        // R-COLOR-2..4: a source's input-colour list; Deliver with HDR PQ (the codec moved to H.265 10-bit) and mid-way
        v.push_back({"grade_input_colour_menu", edit, [](Rig &r) {
            r.settle();
            auto rt = r.app->edit().rackTree();
            const Point p = centre(*rt, rt->rowRect(1));
            r.app->pointer(1, p.x - 40, p.y, 0, r.now);
            r.app->pointer(0, p.x - 40, p.y, 2, r.now);
            r.app->pointer(2, p.x - 40, p.y, 2, r.now + 40.0);
            r.settle();
            auto cm = r.app->edit().contextMenu();
            for (int i = 0; i < cm->itemCount(); ++i)
                if (cm->item(i).label.rfind("Input Colour", 0) == 0)
                {
                    const Point q = centre(*cm, cm->itemRect(i));
                    r.click(q.x, q.y);
                }
            r.settle();
        }});
        // R-COLOR-5: a LUT effect selected — its section ends with the file it reads
        v.push_back({"grade_lut_effect", [](FakeService &s) {
            s.edit();
            auto e = FakeService::effect("ef_3", "ro2", "s_day01", "lut.cube", 2, true, 0.85);
            e.file = "/home/editor/luts/Kodak 2383 D65.cube";
            s.m.effects.push_back(e);
            ++s.m.revision;
        }, [](Rig &r) {
            r.settle();
            auto pl = r.app->edit().gradeInspector()->plugins();
            const Point p = centre(*pl, pl->rowRect(3));
            r.click(p.x - 30.0, p.y);
            r.settle();
        }});
        // R-AUD-7/8: the Cut tab with its audio clips' waveforms and the transport's meter mid-song
        v.push_back({"cut_sound", [](FakeService &s) {
            s.edit();
            s.m.playing = true;
            s.m.meterPeakL = 0.62; s.m.meterPeakR = 0.48;
            s.m.meterRmsL = 0.30; s.m.meterRmsR = 0.24;
            ++s.m.revision;
        }, [](Rig &r) {
            r.app->setTab(1);
            r.settle();
        }});
        // R-EDT-1/2: the source viewer with its marks; the timeline's In/Out band, a 2× shuttle, the target track
        v.push_back({"cut_source_viewer", [](FakeService &s) {
            s.edit();
            s.m.sourceView = "s_day02";
            s.m.sourcePlayhead = 3.2;
            s.m.sourceIn = 1.5;
            s.m.sourceOut = 5.0;
            s.m.sourceDuration = 8.0;
            ++s.m.revision;
        }, [](Rig &r) { r.app->setTab(1); r.settle(); }});
        v.push_back({"cut_marks_shuttle", [](FakeService &s) {
            s.edit();
            s.m.markIn = 3.0;
            s.m.markOut = 7.5;
            s.m.playing = true;
            s.m.shuttle = 2.0;
            s.m.targetTrack = "v2";
            ++s.m.revision;
        }, [](Rig &r) { r.app->setTab(1); r.settle(); }});
        v.push_back({"cut_nested", [](FakeService &s) {   // R-EDT-4: a timeline placed as a clip, its menu open
            s.edit();
            auto c = FakeService::clip("n1", "v2", "main", "main", 12.0, 0.0, 3.0, arstro::interstellar::Provenance::Local);
            c.nested = true;
            s.m.clips.push_back(c);
            s.m.timelines[0].placeable = true;
            ++s.m.revision;
        }, [](Rig &r) {
            r.app->setTab(1);
            r.settle();
            auto tl = r.app->edit().timeline();
            const Rect cr = tl->clipRect("n1");
            const Point p = world(*tl, cr.x + cr.w * 0.5, cr.y + cr.h * 0.5);
            r.app->pointer(1, p.x, p.y, 0, r.now);
            r.app->pointer(0, p.x, p.y, 2, r.now);
            r.app->pointer(2, p.x, p.y, 2, r.now + 40.0);
            r.settle();
        }});
        auto multicam = [](FakeService &s) {   // R-EDT-5: a multicam cut twice, the playhead on angle 2
            s.edit();
            s.m.playing = false;
            using arstro::interstellar::Provenance;
            const char *names[] = {"m1", "m2", "m3"};
            const double at[] = {12.0, 13.5, 14.5}, len[] = {1.5, 1.0, 1.5};
            const int ang[] = {1, 2, 3};
            for (int i = 0; i < 3; ++i)
            {
                auto c = FakeService::clip(names[i], "v2", "cams", "cams", at[i], at[i] - 12.0, at[i] - 12.0 + len[i], Provenance::Local);
                c.nested = true;
                c.angle = ang[i];
                s.m.clips.push_back(c);
            }
            s.m.playhead = 13.75;
            s.m.multicamClip = "m2";
            s.m.multicamAngle = 2;
            s.m.multicamAngles = {"A001_C003", "B002_C011", "C003_C007"};
            ++s.m.revision;
        };
        v.push_back({"cut_multicam", multicam, [](Rig &r) { r.app->setTab(1); r.settle(); }});
        v.push_back({"deliver_colour_pq", edit, [clickSegShot](Rig &r) {
            r.app->setTab(2);
            r.settle();
            clickSegShot(r, r.app->edit().outputSpec()->colourPicker(), 4);
            r.settle();
        }});
        v.push_back({"deliver_colour_pq_mid", edit, [clickSegShot](Rig &r) {
            r.app->setTab(2);
            r.settle();
            clickSegShot(r, r.app->edit().outputSpec()->colourPicker(), 4);
            r.pump(80);
        }});
        v.push_back({"deliver_spec_codec_mid", edit, [clickSegShot](Rig &r) {
            r.app->setTab(2);
            r.settle();
            auto os = r.app->edit().outputSpec();
            clickSegShot(r, os->formatPicker(), 2);
            r.settle();
            clickSegShot(r, os->formatPicker(), 1);
            r.pump(80);
        }});
        // the preview cache bar (R-PLAY-1): cached seconds, one building, two stale after an edit
        v.push_back({"cut_cache_bar", [](FakeService &s) {
            s.edit();
            s.m.previewCacheSegmentSeconds = 1.0;
            s.m.previewCacheSegments = {1, 1, 1, 1, 1, 1, 1, 1, 3, 0, 0, 0, 2, 2, 1, 1, 1, 0, 0, 0};
        }, [](Rig &r) { r.app->setTab(1); r.settle(); }});
        // cutting like an editor (R-UI-14): a source dragged over V2, a roll, a slip, the clip menu
        v.push_back({"cut_drop_source", edit, [](Rig &r) {
            r.app->setTab(1);
            r.settle();
            auto tl = r.app->edit().timeline();
            auto bin = r.app->edit().sourceBin();
            const Point f = centre(*bin, bin->rowRect(1));
            const Rect v2 = tl->laneRect("v2");
            const Point to = world(*tl, tl->timeToX(12.5), v2.y + v2.h * 0.5);
            r.press(f.x, f.y);
            r.frame();
            for (int k = 1; k <= 8; ++k) { r.dragTo(f.x + (to.x - f.x) * k / 8.0, f.y + (to.y - f.y) * k / 8.0); r.frame(); }
            r.pump(250);
        }});
        auto altDrag = [](Rig &r, Point p, double dx) {
            r.app->pointer(1, p.x, p.y, 0, r.now, true);
            r.app->pointer(0, p.x, p.y, 0, r.now, true);
            r.frame();
            for (int k = 1; k <= 6; ++k) { r.app->pointer(1, p.x + dx * k / 6.0, p.y, 0, r.now, true); r.frame(); }
            r.pump(120);
        };
        v.push_back({"cut_roll_mid", edit, [altDrag](Rig &r) {
            r.app->setTab(1);
            r.settle();
            auto tl = r.app->edit().timeline();
            const Rect c1 = tl->clipRect("c1");
            altDrag(r, world(*tl, c1.right() - 1.0, c1.y + c1.h * 0.5), 40.0);
        }});
        v.push_back({"cut_slip_mid", edit, [altDrag](Rig &r) {
            r.app->setTab(1);
            r.settle();
            auto tl = r.app->edit().timeline();
            const Rect c5 = tl->clipRect("c5");
            altDrag(r, world(*tl, c5.x + c5.w * 0.5, c5.y + c5.h * 0.5), 48.0);
        }});
        v.push_back({"cut_clip_menu", edit, [](Rig &r) {
            r.app->setTab(1);
            r.settle();
            auto tl = r.app->edit().timeline();
            const Rect c3 = tl->clipRect("c3");
            const Point p = world(*tl, c3.x + c3.w * 0.5, c3.y + c3.h * 0.5);
            r.app->pointer(1, p.x, p.y, 0, r.now);
            r.app->pointer(0, p.x, p.y, 2, r.now);
            r.app->pointer(2, p.x, p.y, 2, r.now + 40.0);
            r.settle();
        }});
        // the scopes (R-UI-15): each mode, and the clip overlay on the monitor
        for (int mode = 1; mode < 4; ++mode)
        {
            static const char *kNames[4] = {"", "grade_scope_waveform", "grade_scope_parade", "grade_scope_vector"};
            v.push_back({kNames[mode], edit, [mode](Rig &r) { r.settle(); r.app->edit().gradeInspector()->scopes()->setMode(mode); r.settle(); }});
        }
        v.push_back({"grade_scope_waveform_rgb", edit, [](Rig &r) {
            r.settle();
            auto sp = r.app->edit().gradeInspector()->scopes();
            sp->setMode(1);   // Waveform
            sp->setWaveRgb(true);
            r.settle();
        }});
        v.push_back({"grade_clip_warning", [](FakeService &s) { s.edit(); s.m.gradeParams.exposure = 2.5f; s.m.gradeOwnParams.exposure = 2.5f; },
                     [](Rig &r) { r.settle(); r.app->edit().gradeInspector()->scopes()->setClipWarning(true); r.settle(); }});
        v.push_back({"edit_scale_125", [](FakeService &s) { s.edit(); s.m.settings.uiScale = 125; }, [](Rig &r) { r.settle(); }});
        v.push_back({"grade_hover", edit, [](Rig &r) {
            r.settle();
            auto rt = r.app->edit().rackTree();
            rt->setOpen("ro4", true);   // gr2 open, so its members show their states
            r.settle();
            const Point p = centre(*rt, rt->rowRect(4));
            r.move(p.x - 40, p.y);
            r.pump(200);
        }});
        v.push_back({"grade_empty", [](FakeService &s) { s.editNewProject(); }, [](Rig &r) { r.settle(); }});
        v.push_back({"grade_no_target", [](FakeService &s) { s.edit(); s.m.selectedRack = -1; s.m.hasGradeTarget = false; }, [](Rig &r) { r.settle(); }});
        v.push_back({"grade_bypassed", [](FakeService &s) { s.edit(); s.selectRack(4); }, [](Rig &r) { r.settle(); }});
        v.push_back({"grade_tab_color", edit, [](Rig &r) {
            r.settle();
            r.app->edit().gradeInspector()->tabs()->setSelectedIndex(1);
            r.settle();
        }});
        v.push_back({"grade_tab_grade", edit, [](Rig &r) {
            r.settle();
            r.app->edit().gradeInspector()->tabs()->setSelectedIndex(2);
            r.settle();
        }});
        v.push_back({"grade_versions_open", edit, [](Rig &r) {
            r.settle();
            auto vs = r.app->edit().topBar()->versions();
            const Point p = centre(*vs, vs->bodyRect());
            r.click(p.x, p.y);
            r.settle();
            const Point q = centre(*vs, vs->versionRowRect(2));
            r.move(q.x, q.y);
            r.pump(200);
        }});
        v.push_back({"grade_versions_open_mid", edit, [](Rig &r) {
            r.settle();
            auto vs = r.app->edit().topBar()->versions();
            const Point p = centre(*vs, vs->bodyRect());
            r.click(p.x, p.y);
            r.pump(64);
        }});
        v.push_back({"grade_version_pinned", [](FakeService &s) { s.edit(); s.m.currentTimeline = "delivery"; }, [](Rig &r) { r.settle(); }});
        v.push_back({"grade_new_version_prompt", edit, [](Rig &r) {
            r.settle();
            auto vs = r.app->edit().topBar()->versions();
            const Point p = centre(*vs, vs->bodyRect());
            r.click(p.x, p.y);
            r.settle();
            const Point q = centre(*vs, vs->actionRect(0));
            r.click(q.x, q.y);
            r.pump(16);
            r.typeText("Festival cut");
            r.settle();
        }});
        v.push_back({"grade_refused", edit, [](Rig &r) {
            r.settle();
            r.svc.refuseNext = true;
            r.app->dispatch("set s_day01.basic.exposure=0.5");
            r.pump(400);
        }});
        v.push_back({"grade_monitor_loading", [](FakeService &s) { s.edit(); s.frameFails = true; }, [](Rig &r) { r.settle(); }});
        v.push_back({"grade_monitor_empty", [](FakeService &s) { s.edit(); s.m.playhead = 15.4; s.m.clips.erase(s.m.clips.begin() + 4); }, [](Rig &r) { r.settle(); }});
        v.push_back({"grade_save_confirm", edit, [](Rig &r) {
            r.settle();
            r.app->requestHome();
            r.settle();
        }});
        // Edit — Cut
        v.push_back({"cut_populated", edit, [](Rig &r) { r.settle(); r.app->setTab(EditScreen::Cut); r.settle(); }});
        v.push_back({"cut_hover", edit, [](Rig &r) {
            r.settle(); r.app->setTab(EditScreen::Cut); r.settle();
            auto tl = r.app->edit().timeline();
            const Point p = centre(*tl, tl->clipRect("c5"));
            r.move(p.x, p.y);
            r.pump(200);
        }});
        v.push_back({"cut_drag_mid", edit, [](Rig &r) {
            r.settle(); r.app->setTab(EditScreen::Cut); r.settle();
            auto tl = r.app->edit().timeline();
            const Rect c = tl->clipRect("c7");
            const Point p = world(*tl, c.x + c.w * 0.6, c.y + c.h * 0.5);
            r.press(p.x, p.y);
            r.pump(16);
            // toward c3's end at 10.0 s: land a few px short so the snap has to catch it
            const double targetX = tl->timeToX(10.0) + 3.0 + c.w * 0.6;
            for (int k = 1; k <= 8; ++k)
            {
                r.dragTo(p.x + (world(*tl, targetX, 0).x - p.x) * k / 8.0, p.y);
                r.pump(16);
            }
            r.pump(120);
        }});
        v.push_back({"cut_zoom_mid", edit, [](Rig &r) {
            r.settle(); r.app->setTab(EditScreen::Cut); r.settle();
            auto tl = r.app->edit().timeline();
            const Point p = centre(*tl, tl->zoomInRect());
            r.click(p.x, p.y);
            r.pump(80);
        }});
        v.push_back({"cut_empty", [](FakeService &s) { s.edit(); s.m.clips.clear(); s.m.transitions.clear(); s.m.selectedClip.clear(); }, [](Rig &r) {
            r.settle(); r.app->setTab(EditScreen::Cut); r.settle();
        }});
        // Edit — Deliver
        v.push_back({"deliver_populated", edit, [](Rig &r) { r.settle(); r.app->setTab(EditScreen::Deliver); r.settle(); }});
        v.push_back({"deliver_empty", [](FakeService &s) { s.editNewProject(); }, [](Rig &r) { r.settle(); r.app->setTab(EditScreen::Deliver); r.settle(); }});
        // transitions
        v.push_back({"tab_grade_to_cut_mid", edit, [](Rig &r) {
            r.settle();
            auto tabs = r.app->edit().topBar()->tabs();
            const Point p = centre(*tabs, tabs->segmentRect(1));
            r.click(p.x, p.y);
            r.pump(80);
        }});
        return v;
    }
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--outdir" && i + 1 < argc) gOpt.outdir = argv[++i];
        else if (a == "--only" && i + 1 < argc) gOpt.only = argv[++i];
        else if (a == "--size" && i + 1 < argc) { std::sscanf(argv[++i], "%dx%d", &gOpt.w, &gOpt.h); }
        else if (a == "--tree") gOpt.tree = true;
        else if (a == "--check") gOpt.check = true;
        else if (a == "--list") gOpt.list = true;
        else
        {
            std::printf("usage: interstellar_app_shots [--outdir DIR] [--only SUBSTR] [--size WxH] [--tree] [--check] [--list]\n");
            return a == "--help" ? 0 : 2;
        }
    }
    arstro::interstellar_v1::installInterstellarAccent();   // before the first frame
    arstro::cosmo_v2::registerEmbeddedFonts();               // the app's own type (R-FONT-1)

    const auto list = shots();
    if (gOpt.list)
    {
        for (const auto &s : list) std::printf("%s\n", s.name.c_str());
        return 0;
    }
    fs::create_directories(gOpt.outdir);
    std::vector<std::pair<int, int>> sizes;
    if (gOpt.w > 0 && gOpt.h > 0) sizes.push_back({gOpt.w, gOpt.h});
    else sizes = {{1440, 900}, {1024, 640}};

    int written = 0, blank = 0;
    for (const auto &s : list)
    {
        if (!gOpt.only.empty() && s.name.find(gOpt.only) == std::string::npos) continue;
        for (const auto &[w, h] : sizes)
        {
            Rig rig(w, h, s.setup);
            s.drive(rig);
            const std::string base = gOpt.outdir + "/" + s.name + "_" + std::to_string(w) + "x" + std::to_string(h);
            if (!rig.write(base + ".png")) { std::printf("FAILED to write %s.png\n", base.c_str()); return 1; }
            ++written;
            const bool u = rig.uniform();
            if (u) ++blank;
            std::printf("wrote %s.png (%dx%d)%s\n", base.c_str(), w, h, u ? "  UNIFORM" : "");
            if (gOpt.tree)
            {
                std::ofstream o(base + ".tree.txt");
                dumpTree(o, *rig.app->activeRoot(), 0);
                if (rig.app->settings().isOpen()) dumpTree(o, rig.app->settings(), 0);
                std::printf("     tree %s.tree.txt\n", base.c_str());
            }
        }
    }
    if (blank) std::printf("%d shot%s came out uniform-colour \xE2\x80\x94 a blank shot is worse than no shot\n", blank, blank == 1 ? "" : "s");
    return (gOpt.check && (blank || written == 0)) ? 1 : 0;
}
