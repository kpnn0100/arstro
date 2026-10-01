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
        v.push_back({"grade_hover", edit, [](Rig &r) {
            r.settle();
            auto rt = r.app->edit().rackTree();
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
