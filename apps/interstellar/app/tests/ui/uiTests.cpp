/*
 *  interstellar_v1 — interstellar_app_ui_tests: assertions over the ASSEMBLED front end.
 *
 *  The real App over a FakeService (tests/FakeService.h) whose `dispatch` records every line, with
 *  real text metrics (Cairo + the embedded faces). Four kinds of assertion:
 *
 *   1. INTENT AS TEXT — a moved control dispatches the exact command line of project-format §8:
 *      the exposure slider → `set s_day01.basic.exposure=…`, a clip drag → `clip move … --at <the
 *      SNAPPED value>`, a version pick → `timeline open …`, and so on for every control.
 *   2. MOTION — nothing changes in one frame. The clock is pumped ONE frame (16 ms) at a time and
 *      the FIRST value that moved is kept: it must lie strictly between where it was and where it
 *      is going. Settling first and then looking would let a 200 ms fade finish and pass a snap.
 *   3. LAYOUT — at 1440x900 and 1024x640, every column inside the window and no two siblings
 *      overlapping; the timeline's one time origin; text measured and ellipsized.
 *   4. REACH — scrollable surfaces clamp at both ends and reach their last row.
 *
 *  Plain assert(), with NDEBUG undefined first: a Release build would otherwise compile every
 *  check away and report green (cosmo D-43; R-TEST-1).
 */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "App.h"
#include "EmbeddedFonts.h"
#include "FakeService.h"
#include "Rig.h"
#include "widgets/TextFit.h"
#include "widgets/CommandLine.h"
#include "../../../cosmo/widgets/SliderRow.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace arstro::interstellar_v1;
using artboard::Point;
using artboard::Rect;
using artboard::Segment;
using istest::centre;
using istest::FakeService;
using istest::Rig;
using istest::world;
using istest::worldRect;

namespace
{
    int gChecks = 0;
    std::string gErr;   // a sink for FakeService::dispatch's error out-param when a test drives the model directly

    bool validUtf8(const std::string &s)
    {
        for (size_t i = 0; i < s.size();)
        {
            const unsigned char c = (unsigned char)s[i];
            const int n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
            if (n == 0 || i + n > s.size()) return false;
            for (int k = 1; k < n; ++k) if (((unsigned char)s[i + k] & 0xC0) != 0x80) return false;
            i += n;
        }
        return true;
    }
#define CHECK(cond, what)                                                                  \
    do                                                                                     \
    {                                                                                      \
        const bool ok_ = (cond);                                                           \
        ++gChecks;                                                                         \
        std::printf("  [%s] %s\n", ok_ ? "ok" : "FAIL", what);                             \
        assert(ok_ && what);                                                               \
    } while (0)

    const int kSizes[2][2] = {{1440, 900}, {1024, 640}};

    bool hasLine(const FakeService &s, const std::string &exact)
    {
        for (const auto &l : s.lines) if (l == exact) return true;
        return false;
    }
    std::string withPrefix(const FakeService &s, const std::string &prefix)
    {
        for (auto it = s.lines.rbegin(); it != s.lines.rend(); ++it)
            if (it->compare(0, prefix.size(), prefix) == 0) return *it;
        return std::string();
    }
    bool inside(const Rect &r, double W, double H, double eps = 0.5)
    {
        return r.x >= -eps && r.y >= -eps && r.right() <= W + eps && r.bottom() <= H + eps;
    }
    bool overlap(const Rect &a, const Rect &b, double eps = 0.5)
    {
        const double ix = std::min(a.right(), b.right()) - std::max(a.x, b.x);
        const double iy = std::min(a.bottom(), b.bottom()) - std::max(a.y, b.y);
        return ix > eps && iy > eps;
    }
    bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

    /** Pump ONE frame at a time; return the first value that differs from `before`. */
    double firstMoved(Rig &r, const std::function<double()> &get, double before, int maxFrames = 40)
    {
        for (int i = 0; i < maxFrames; ++i)
        {
            r.frame();
            const double v = get();
            if (std::fabs(v - before) > 1e-9) return v;
        }
        return before;
    }
    bool strictlyBetween(double v, double a, double b)
    {
        const double lo = std::min(a, b), hi = std::max(a, b);
        return v > lo + 1e-9 && v < hi - 1e-9;
    }
    template <class T>
    T *firstChild(Segment &s)
    {
        for (auto &c : s.children()) if (auto *p = dynamic_cast<T *>(c.get())) return p;
        return nullptr;
    }

    // ── 0. the accent ─────────────────────────────────────────────────────────────────

    void testAccent()
    {
        std::printf("accent\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); });
        r.settle();
        const artboard::Color p = palette::primary();
        CHECK(near(p.r, 0xCF / 255.0, 1e-4) && near(p.g, 0x5A / 255.0, 1e-4) && near(p.b, 0xED / 255.0, 1e-4), "palette::primary() is #CF5AED after App()");
        const artboard::Color fill = sharedTheme().slider.rangeFill.paint.fill;
        CHECK(near(fill.r, p.r) && near(fill.g, p.g) && near(fill.b, p.b), "cosmo's shared slider fill follows the accent");
        CHECK(near(sharedTheme().tab.activeIndicatorColor.b, p.b), "cosmo's tab indicator follows the accent");
        CHECK(near(palette::ring().a, 0.5) && near(palette::ring().r, p.r), "ring() is the accent at 0.5");
        // a reused cosmo widget DRAWS purple-pink: cosmo's SliderRow as Cosmo's Mix (100 %, R-FX-5) has
        // its fill running the whole track — sample inside it, clear of the thumb
        artboard::Slider *sl = firstChild<artboard::Slider>(*r.app->edit().gradeInspector()->cosmoMix());
        CHECK(sl != nullptr, "found the slider inside cosmo's SliderRow (Cosmo's Mix)");
        const Point a = world(*sl, sl->width.value() * 0.4, sl->height.value() * 0.5);
        const uint32_t px = r.pixel((int)a.x, (int)std::floor(a.y));
        const int R = (px >> 16) & 0xFF, G = (px >> 8) & 0xFF, B = px & 0xFF;
        std::printf("      slider fill pixel at (%.0f,%.0f) = #%02X%02X%02X\n", a.x, a.y, R, G, B);
        CHECK(R > 150 && B > 170 && G < 140, "the reused cosmo slider fill is purple-pink on screen");
    }

    // ── 1. intent as text ─────────────────────────────────────────────────────────────

    void testGradeCommands()
    {
        std::printf("grade: cosmo's panels dispatch `set <bind>.<filter>.<key>=…`\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); });
        r.settle();
        auto gi = r.app->edit().gradeInspector();
        auto *row = firstChild<arstro::cosmo_v2::SliderRow>(*gi->basicDetail());
        auto *sl = firstChild<artboard::Slider>(*row);
        const Point c = centre(*sl, sl->localBounds());
        r.drag(c.x, c.y, c.x + 60.0, c.y);
        const std::string ex = withPrefix(r.svc, "set s_day01.basic.exposure=");
        std::printf("      %s\n", ex.c_str());
        CHECK(!ex.empty(), "dragging Exposure dispatched set s_day01.basic.exposure=…");
        const double ev = std::stod(ex.substr(ex.find('=') + 1));
        CHECK(ev > 0.35 && ev <= 5.0, "…in ENGINE units (EV), converted with cosmo's toEv");

        // Grade tab: the shadows hue slider → grade0=h,s,l
        gi->tabs()->setSelectedIndex(GradeInspector::kTabGrade);
        r.settle();
        auto *grow = firstChild<arstro::cosmo_v2::SliderRow>(*gi->gradePanel());
        auto *gsl = grow ? firstChild<artboard::Slider>(*grow) : nullptr;
        CHECK(gsl != nullptr, "found GradePanel's first slider");
        const Point g = centre(*gsl, gsl->localBounds());
        r.drag(g.x, g.y, g.x - 40.0, g.y);
        const std::string gl = withPrefix(r.svc, "set s_day01.grade.grade0=");
        std::printf("      %s\n", gl.c_str());
        CHECK(!gl.empty() && std::count(gl.begin(), gl.end(), ',') == 2, "the grade wheel dispatched set s_day01.grade.grade0=h,s,l");

        // Mixer/Curve tab: drag a point of the tone curve → curve=x,y;…
        gi->tabs()->setSelectedIndex(GradeInspector::kTabColor);
        r.settle();
        auto cp = gi->curve();
        {
            // the plugin list above the tabs (R-FX-5) leaves the curve below the fold: wheel the page
            const Point mid = world(*gi, gi->width.value() * 0.5, gi->height.value() * 0.8);
            r.app->wheel(mid.x, mid.y, -6.0);
            r.settle();
        }
        const Rect pb = cp->plotBox();
        const Point q = world(*cp, pb.x + pb.w * 0.5, pb.y + pb.h * (1.0 - 0.56));
        CHECK(q.y < world(*gi, 0, gi->height.value()).y, "the wheel brings the tone curve into the column");
        r.drag(q.x, q.y, q.x, q.y - 14.0);
        const std::string cl = withPrefix(r.svc, "set s_day01.curve.curve=");
        std::printf("      %s\n", cl.c_str());
        CHECK(!cl.empty() && cl.find(';') != std::string::npos, "dragging the tone curve dispatched set s_day01.curve.curve=x,y;…");
    }

    void testRackCommands()
    {
        std::printf("rack tree, deck and frame selector\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); });
        r.settle();
        auto rt = r.app->edit().rackTree();
        CHECK(!rt->rowVisible(5) && rt->rowVisible(1), "a shut group hides its members; the Grade target's group was opened for it");
        Point p = centre(*rt, rt->chevronRect(3));   // gr2 holds s_still01
        r.click(p.x, p.y);
        r.settle();
        CHECK(rt->isOpen(3) && rt->rowVisible(5) && r.svc.lines.empty(), "its chevron opens the group — presentation, no command");
        p = centre(*rt, rt->rowRect(5));
        r.click(p.x - 40, p.y);
        r.pump(32);
        CHECK(hasLine(r.svc, "rack select s_still01"), "clicking a rack row dispatched rack select s_still01");
        p = centre(*rt, rt->bypassRect(1));
        r.click(p.x, p.y);
        r.pump(32);
        CHECK(hasLine(r.svc, "set s_day01.bypass=1"), "the bypass toggle dispatched set s_day01.bypass=1");
        {
            // ui-brief §3: "revert to base, one click away" — the OVR badge sends `revert <bind>`.
            int ovr = -1;
            for (size_t k = 0; k < r.svc.m.rack.size(); ++k)
                if (r.svc.m.rack[k].overridden && ovr < 0) ovr = (int)k;
            CHECK(ovr >= 0, "the fake model has an overridden rack node");
            if (ovr >= 0)
            {
                const Rect br = rt->overrideBadgeRect(ovr);
                CHECK(br.w > 0, "its OVR badge was painted");
                const Point bp = centre(*rt, br);
                r.click(bp.x, bp.y);
                r.pump(32);
                const std::string want = "revert " + r.svc.m.rack[(size_t)ovr].bindName;
                const std::string what = "clicking the OVR badge dispatched " + want;
                CHECK(hasLine(r.svc, want), what.c_str());
            }
        }
        {
            // R-RACK-4 (amended): no weight bar on a rack row any more — the weight is Cosmo's Mix
            r.svc.lines.clear();
            const Rect row = rt->rowRect(2);
            const Point bar = world(*rt, row.right() - 9.75 - 20.0 - 8.0 - 44.0 - 18.0, row.y + 23.0);   // where the bar used to be
            r.drag(bar.x - 10.0, bar.y, bar.x + 10.0, bar.y);
            CHECK(withPrefix(r.svc, "set s_day02.weight=").empty(), "the rack row has no weight bar to drag (it is Cosmo's Mix now)");
        }
        bool added = false;
        r.app->onPickFootage = [&] { added = true; };
        p = centre(*rt, rt->addRect());
        r.click(p.x, p.y);
        CHECK(added, "the rack's + asks the host for footage");
        r.app->footagePicked({"/f/a.mov", "/f/b c.mov"});
        CHECK(hasLine(r.svc, "rack add /f/a.mov \"/f/b c.mov\""), "picked footage becomes rack add, quoting the path with a space");

        // the deck: cosmo's Filmstrip selects in the rack
        r.svc.lines.clear();
        auto deck = r.app->edit().gradeDeck();
        auto strip = deck->filmstrip();
        r.svc.dispatch("rack select s_day01", gErr);   // the strip follows the target into gr1
        r.settle();
        r.svc.lines.clear();
        const Point s2 = world(*strip, strip->cellXForTest(deck->cellOfRack(2)) + 40.0, 40.0);
        r.click(s2.x, s2.y);
        r.pump(32);
        CHECK(hasLine(r.svc, "rack select s_day02"), "clicking a filmstrip cell dispatched rack select s_day02");
        r.svc.lines.clear();
        r.svc.dispatch("rack select s_day01", gErr);
        r.settle();
        const Rect tr = deck->frameTrackRect();
        CHECK(deck->hasFrameStrip(), "the reference-frame selector is a strip of frames at 1440x900");
        const Point f0 = world(*deck, tr.x + tr.w * 0.3, tr.y + tr.h * 0.5), f1 = world(*deck, tr.x + tr.w * 0.75, tr.y + tr.h * 0.5);
        r.drag(f0.x, f0.y, f1.x, f1.y);
        const std::string fl = withPrefix(r.svc, "rack frame s_day01 --at ");
        std::printf("      %s\n", fl.c_str());
        CHECK(!fl.empty(), "dragging the frame strip dispatched rack frame s_day01 --at <t> on release");
        const double ft = std::stod(fl.substr(fl.rfind(' ') + 1));
        CHECK(std::fabs(ft - 0.75 * deck->sourceDuration()) < 0.2 && near(std::fmod(ft * 24.0 + 1e-6, 1.0), 0.0, 1e-3), "…at 75% of the source, on a frame boundary");
    }

    void testVersionCommands()
    {
        std::printf("version switcher\n");
        {
            Rig r(1440, 900, [](FakeService &s) { s.edit(); });
            r.settle();
            auto vs = r.app->edit().topBar()->versions();
            Point p = centre(*vs, vs->bodyRect());
            r.click(p.x, p.y);
            r.settle();
            CHECK(vs->isOpen(), "clicking the chrome opens the dropdown");
            p = centre(*vs, vs->versionRowRect(2));
            r.click(p.x, p.y);
            r.settle();
            CHECK(hasLine(r.svc, "timeline open delivery"), "choosing a version dispatched timeline open delivery");
            CHECK(!vs->isOpen(), "…and closed the dropdown");
            p = centre(*vs, vs->prevRect());
            r.click(p.x, p.y);
            r.settle();
            CHECK(hasLine(r.svc, "timeline open social30"), "the ‹ arrow steps to the previous version");
        }
        {
            Rig r(1440, 900, [](FakeService &s) { s.edit(); });
            r.settle();
            auto vs = r.app->edit().topBar()->versions();
            for (int a : {VersionSwitcher::Pin, VersionSwitcher::Freeze, VersionSwitcher::Rebase})
            {
                Point p = centre(*vs, vs->bodyRect());
                r.click(p.x, p.y);
                r.settle();
                p = centre(*vs, vs->actionRect(a));
                r.click(p.x, p.y);
                r.settle();
            }
            CHECK(hasLine(r.svc, "timeline pin social30"), "Pin dispatched timeline pin social30");
            CHECK(hasLine(r.svc, "timeline freeze social30"), "Freeze dispatched timeline freeze social30");
            CHECK(hasLine(r.svc, "timeline rebase social30"), "Rebase dispatched timeline rebase social30");
            Point p = centre(*vs, vs->bodyRect());
            r.click(p.x, p.y);
            r.settle();
            p = centre(*vs, vs->actionRect(VersionSwitcher::NewVersion));
            r.click(p.x, p.y);
            r.pump(32);
            CHECK(r.app->edit().namePrompt()->isOpen(), "New version… opens the name prompt");
            r.typeText("Festival cut");
            r.key(13);
            r.pump(32);
            CHECK(hasLine(r.svc, "timeline new Festival_cut --base social30"), "the prompt dispatched timeline new Festival_cut --base social30 (a legal bind name)");
        }
        {
            // the root has no base: pin / freeze / rebase are disabled, a click does nothing
            Rig r(1440, 900, [](FakeService &s) { s.edit(); s.m.currentTimeline = "main"; });
            r.settle();
            auto vs = r.app->edit().topBar()->versions();
            Point p = centre(*vs, vs->bodyRect());
            r.click(p.x, p.y);
            r.settle();
            p = centre(*vs, vs->actionRect(VersionSwitcher::Rebase));
            r.click(p.x, p.y);
            r.settle();
            CHECK(withPrefix(r.svc, "timeline rebase").empty(), "Rebase is disabled on a root version");
        }
    }

    void testCutCommands()
    {
        std::printf("timeline\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); });
        r.settle();
        r.app->setTab(EditScreen::Cut);
        r.settle();
        auto tl = r.app->edit().timeline();
        const double pps = tl->ppsLive();
        const Rect c7 = tl->clipRect("c7");
        CHECK(near(c7.x, tl->timeToX(9.0), 0.01), "a clip is drawn at timeToX(at) — one time origin");
        CHECK(near(tl->rulerRect().x, shell::headerWidth()) && near(tl->lanesRect().x, shell::headerWidth()), "ruler and lanes share headerWidth()");

        // drag c7 so its start lands 3 px past c3's end (10 s): it snaps, keeps its grab, and sends 10
        const Point g0 = world(*tl, c7.x + c7.w * 0.6, c7.y + c7.h * 0.5);
        r.press(g0.x, g0.y);
        r.frame();
        r.dragTo(g0.x + 12.0, g0.y);
        r.frame();
        const Rect mid = tl->clipRect("c7");
        CHECK(std::fabs(mid.x - (c7.x + 12.0)) < 1.0, "a drag keeps the grab offset — the clip moved 12 px, it did not teleport");
        const double target = (tl->timeToX(10.0) + 3.0 + c7.w * 0.6);
        for (int k = 1; k <= 6; ++k) { r.dragTo(g0.x + (world(*tl, target, 0).x - g0.x) * k / 6.0, g0.y); r.frame(); }
        CHECK(tl->dragging() && near(tl->dragValue(), 10.0, 1e-9), "mid-drag the start is SNAPPED to the neighbour's edge (10 s)");
        r.frame();   // the frame that saw the snap STARTED the guide's fade; one more shows it moving
        CHECK(strictlyBetween(tl->snapGuideAmount(), 0.0, 1.0), "…and the snap guide is fading in (eased, not popped)");
        r.releaseAt(world(*tl, target, 0).x, g0.y);
        r.frame();
        CHECK(hasLine(r.svc, "clip move c7 --at 10"), "the drop dispatched the SNAPPED value: clip move c7 --at 10");
        {
            // the other two snap targets: a marker (pickup, 6 s) and the playhead (5.25 s)
            for (double want : {6.0, 5.25})
            {
                r.settle();
                const Rect cr = tl->clipRect("c7");
                const Point s0 = world(*tl, cr.x + cr.w * 0.5, cr.y + cr.h * 0.5);
                const double tx = tl->timeToX(want) + 4.0 + cr.w * 0.5;   // 4 px past it: inside the 8 px pull
                r.drag(s0.x, s0.y, world(*tl, tx, 0).x, s0.y, 10);
                char line[64];
                std::snprintf(line, sizeof line, "clip move c7 --at %g", want);
                char msg[96];
                std::snprintf(msg, sizeof msg, "dropping 4 px off %s snaps: %s", want == 6.0 ? "the marker" : "the playhead", line);
                CHECK(hasLine(r.svc, line), msg);
            }
            // put it back where the rest of this test expects it
            r.svc.dispatch("clip move c7 --at 10", gErr);
        }

        // a vertical drag onto the other video track adds --track; an audio lane is refused
        r.settle();
        const Rect c7b = tl->clipRect("c7");
        const Rect v1 = tl->laneRect("v1");
        const Point h0 = world(*tl, c7b.x + 10.0, c7b.y + c7b.h * 0.5);
        r.drag(h0.x, h0.y, h0.x, world(*tl, 0, v1.y + v1.h * 0.5).y);
        const std::string tm = withPrefix(r.svc, "clip move c7 --at 10 --track");
        std::printf("      %s\n", tm.c_str());
        CHECK(tm == "clip move c7 --at 10 --track v1", "dropping on V1 dispatched clip move c7 --at 10 --track v1");
        r.settle();
        const Rect c7c = tl->clipRect("c7");
        const Rect a2 = tl->laneRect("a2");
        const Point k0 = world(*tl, c7c.x + 10.0, c7c.y + c7c.h * 0.5);
        r.svc.lines.clear();
        r.drag(k0.x, k0.y, k0.x, world(*tl, 0, a2.y + a2.h * 0.5).y);
        CHECK(withPrefix(r.svc, "clip move c7").find("--track a") == std::string::npos, "a video clip cannot be dropped on an audio track");

        // click selects; an edge drag trims; a ruler click moves the playhead
        r.settle();
        Point p = centre(*tl, tl->clipRect("c5"));
        r.click(p.x, p.y);
        r.pump(32);
        CHECK(hasLine(r.svc, "clip select c5"), "clicking a clip dispatched clip select c5");
        r.settle();
        const Rect c5 = tl->clipRect("c5");
        const Point e0 = world(*tl, c5.right() - 2.0, c5.y + c5.h * 0.5);
        r.drag(e0.x, e0.y, e0.x - 40.0, e0.y);
        const std::string tr = withPrefix(r.svc, "clip trim c5 --out ");
        std::printf("      %s\n", tr.c_str());
        CHECK(!tr.empty() && std::stod(tr.substr(tr.rfind(' ') + 1)) < 6.5, "dragging the right edge dispatched clip trim c5 --out <earlier>");
        r.settle();
        p = world(*tl, tl->timeToX(3.0), shell::rulerH() * 0.5);
        r.click(p.x, p.y);
        CHECK(hasLine(r.svc, "playhead 3"), "clicking the ruler at 3 s dispatched playhead 3");
        (void)pps;

        // the inspector's actions, and the keys
        r.svc.dispatch("clip select c2", gErr);
        r.svc.dispatch("playhead 5.25", gErr);
        r.settle();
        auto ci = r.app->edit().clipInspector();
        p = centre(*ci->splitButton(), ci->splitButton()->localBounds());
        r.click(p.x, p.y);
        CHECK(hasLine(r.svc, "clip split c2 --at 5.25"), "Split at playhead dispatched clip split c2 --at 5.25");
        r.key(46);
        CHECK(hasLine(r.svc, "clip delete c2"), "Delete dispatched clip delete c2");
        r.settle();
        CHECK(!ci->hasClip(), "…and the inspector says nothing is selected once the clip is gone");
    }

    void testTransportAndDeliver()
    {
        std::printf("transport, deliver\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); });
        r.app->setTab(1);   // the transport is Cut's and Deliver's; Grade has none (R-UI-3, amended)
        r.settle();
        auto tp = r.app->edit().transport();
        Point p = centre(*tp, tp->buttonRect(1));
        r.click(p.x, p.y);
        r.settle();
        CHECK(hasLine(r.svc, "play"), "▶ dispatched play");
        {
            // (settled above) — replay the switch one frame at a time to see the glyph cross-fade
            r.svc.m.playing = false; ++r.svc.m.revision; r.settle();
            r.svc.m.playing = true; ++r.svc.m.revision;
            const double fp = firstMoved(r, [&] { return tp->playAmount(); }, 0.0);
            CHECK(strictlyBetween(fp, 0.0, 1.0), "the play/pause glyph CROSS-FADES (first frame between)");
            r.svc.m.playing = true; r.settle();
        }
        r.click(p.x, p.y);
        r.settle();
        CHECK(hasLine(r.svc, "pause"), "the same button, now ‖, dispatched pause");
        p = centre(*tp, tp->buttonRect(0));
        r.click(p.x, p.y);
        CHECK(hasLine(r.svc, "playhead prev-cut"), "◀◀ dispatched playhead prev-cut");
        const Rect sr = tp->scrubRect();
        r.svc.lines.clear();
        r.drag(world(*tp, sr.x + sr.w * 0.2, 20).x, world(*tp, 0, sr.h * 0.5).y, world(*tp, sr.x + sr.w * 0.5, 20).x, world(*tp, 0, sr.h * 0.5).y);
        CHECK(!withPrefix(r.svc, "playhead ").empty() && r.svc.lines.size() >= 3, "scrubbing dispatched a run of playhead <t> lines");

        r.app->setTab(EditScreen::Deliver);
        r.settle();
        r.svc.lines.clear();
        auto os = r.app->edit().outputSpec();
        p = centre(*os->renderButton(), os->renderButton()->localBounds());
        r.click(p.x, p.y);
        CHECK(hasLine(r.svc, "render --timeline social30 --out /home/editor/Projects/night-ferry/renders/social30.mp4 --format h264"),
              "Render dispatched render --timeline social30 --out …/social30.mp4 --format h264 — the timeline NAMED");
        auto fmt = os->formatPicker();
        auto *prores = dynamic_cast<arstro::cosmo_v2::PillButton *>(fmt->children()[2].get());   // H.264 · H.265 · ProRes · DNxHR · PNG
        p = centre(*prores, prores->localBounds());
        r.click(p.x, p.y);
        r.pump(32);
        p = centre(*os, os->timelineRowRect(2));
        r.click(p.x, p.y);
        r.pump(32);
        p = centre(*os->renderButton(), os->renderButton()->localBounds());
        r.click(p.x, p.y);
        CHECK(hasLine(r.svc, "render --timeline delivery --out /home/editor/Projects/night-ferry/renders/delivery.mov --format prores"),
              "picking ProRes and the Delivery version re-derives the path: render --timeline delivery … --format prores");
        r.settle();
        CHECK(r.app->edit().renderQueue()->count() == 6, "the queue grew by the two renders the fake accepted");

        // R-RENDER-6: the whole spec. ProRes shows its profile row and hides H.264's quality/speed.
        CHECK(os->rowAmount(OutputSpec::ProresProfile) > 0.999 && os->rowAmount(OutputSpec::Quality) < 0.001,
              "ProRes: its PROFILE row is shown, the H.264/H.265 quality row is collapsed");
        auto clickSeg = [&](std::shared_ptr<arstro::cosmo_v2::SegmentedControl> sc, int i) {
            auto *b = dynamic_cast<arstro::cosmo_v2::PillButton *>(sc->children()[(size_t)i].get());
            const Point q = centre(*b, b->localBounds());
            r.click(q.x, q.y);
            r.pump(32);
        };
        clickSeg(fmt, 1);   // H.265: quality, speed and depth rows open — eased, not popped
        const double qa = firstMoved(r, [&] { return os->rowAmount(OutputSpec::Quality); }, 0.0);
        CHECK(strictlyBetween(qa, 0.0, 1.0), "switching codec, a row EASES open (first frame between)");
        r.settle();
        clickSeg(os->qualityPicker(), 3);   // Master = CRF 12
        clickSeg(os->speedPicker(), 2);     // Slow
        clickSeg(os->depthPicker(), 1);     // 10-bit
        clickSeg(os->sizePicker(), 1);      // half: 3840x2160 → 1920x1080
        const Point up = centre(*os, os->rateStepRect(1));
        r.click(up.x, up.y);                // Project → 23.976, the exact fraction
        r.pump(32);
        clickSeg(os->rangePicker(), 1);     // In–Out, from the playhead
        r.settle();
        r.svc.dispatch("playhead 2.0", gErr);
        r.settle();
        p = centre(*os->setInButton(), os->setInButton()->localBounds());
        r.click(p.x, p.y);
        r.svc.dispatch("playhead 6.5", gErr);
        r.settle();
        p = centre(*os->setOutButton(), os->setOutButton()->localBounds());
        r.click(p.x, p.y);
        r.pump(32);
        r.svc.lines.clear();
        p = centre(*os->renderButton(), os->renderButton()->localBounds());
        r.click(p.x, p.y);
        const std::string rl = withPrefix(r.svc, "render ");
        std::printf("      %s\n      %s\n", rl.c_str(), os->summary().c_str());
        CHECK(rl.find("--format h265") != std::string::npos && rl.find("--quality 12") != std::string::npos &&
                  rl.find("--speed slow") != std::string::npos && rl.find("--bits 10") != std::string::npos &&
                  rl.find("--res 1920x1080") != std::string::npos && rl.find("--fps 24000/1001") != std::string::npos &&
                  rl.find("--range 2:6.5") != std::string::npos,
              "Render sends the whole spec: --format h265 --quality 12 --speed slow --bits 10 --res 1920x1080 --fps 24000/1001 --range 2:6.5");
        {
            // R6 at the small size: the column outgrows the window, scrolls, and Render is reachable
            Rig s(1024, 640, [](FakeService &f) { f.edit(); });
            s.app->setTab(EditScreen::Deliver);
            s.settle();
            auto o = s.app->edit().outputSpec();
            CHECK(o->columnScroll().scrollable(), "1024x640: the Deliver column scrolls");
            const Point mid = centre(*o, Rect{0, o->height.value() * 0.8, o->width.value(), 10.0});
            s.app->wheel(mid.x, mid.y, -40.0);
            s.settle();
            auto rb = o->renderButton();
            CHECK(rb->visible && rb->y.value() + rb->height.value() <= o->height.value() + 0.5 && rb->y.value() >= 0.0,
                  "…and wheeling down brings Render fully into view");
        }
    }

    void testHomeAndShell()
    {
        std::printf("home, open, close\n");
        {
            Rig r(1440, 900, [](FakeService &s) { s.home(); });
            r.settle();
            const Rect c = r.app->home().cardLive(0);
            r.click(c.x + c.w * 0.5, c.y + c.h * 0.4);
            CHECK(hasLine(r.svc, "project open /home/editor/Projects/harbour/drone.isp"), "clicking the newest card dispatched project open <its path>");
            CHECK(r.app->loading().projectName() == "Harbour drone", "…and the loading screen names it");
            bool asked = false;
            r.app->onPickProjectToCreate = [&] { asked = true; };
            const Rect a = r.app->home().actionRect(0);
            r.click(a.x + 20, a.y + 10);
            CHECK(asked, "New project asks the host for a path");
            r.app->newProjectPicked("/tmp/My New Project.isp");
            CHECK(hasLine(r.svc, "project new \"/tmp/My New Project.isp\""), "…which becomes project new \"/tmp/My New Project.isp\"");
        }
        {
            Rig r(1440, 900, [](FakeService &s) { s.edit(); });
            r.settle();
            auto tb = r.app->edit().topBar();
            Point p = centre(*tb, tb->wordmarkRect());
            r.click(p.x, p.y);
            r.settle();
            CHECK(r.app->edit().confirm()->isOpen(), "the wordmark with unsaved edits asks first (cosmo's ConfirmDialog)");
            CHECK(r.svc.lines.empty(), "…and nothing was dispatched yet");
            r.app->edit().confirm()->confirmDefault();
            r.settle();
            CHECK(r.svc.lines.size() == 2 && r.svc.lines[0] == "project save" && r.svc.lines[1] == "project close", "Save dispatched project save, then project close");
            CHECK(r.app->screen() == arstro::interstellar::Screen::Home, "the model's Home screen is now the one shown");
        }
        {
            Rig r(1440, 900, [](FakeService &s) { s.edit(); s.m.dirty = false; });
            r.settle();
            auto tb = r.app->edit().topBar();
            Point p = centre(*tb, tb->wordmarkRect());
            r.click(p.x, p.y);
            CHECK(hasLine(r.svc, "project close") && !r.app->edit().confirm()->isOpen(), "a clean project closes straight away");
        }
        {
            Rig r(1440, 900, [](FakeService &s) { s.edit(); });
            r.settle();
            r.svc.refuseNext = true;
            auto rt = r.app->edit().rackTree();
            const Point p = centre(*rt, rt->bypassRect(1));
            r.click(p.x, p.y);
            const double first = firstMoved(r, [&] { return r.app->edit().toastAmount(); }, 0.0);
            CHECK(strictlyBetween(first, 0.0, 1.0), "a refused line is SAID: the toast eases in (first frame between 0 and 1)");
        }
    }

    // ── 2. motion: the first frame that moved lies strictly between ──────────────────────

    void testMotion()
    {
        std::printf("motion: one frame at a time, first non-zero\n");
        {
            Rig r(1440, 900, [](FakeService &s) { s.edit(); });
            r.settle();
            auto tabs = r.app->edit().topBar()->tabs();
            const Point p = centre(*tabs, tabs->segmentRect(1));
            r.click(p.x, p.y);
            CHECK(r.app->tab() == EditScreen::Cut, "clicking Cut selects the Cut tab");
            double hl = -1, fadeIn = -1, fadeOut = -1;
            for (int i = 0; i < 30 && (hl < 0 || fadeIn < 0 || fadeOut < 0); ++i)
            {
                r.frame();
                if (hl < 0 && r.app->edit().tabHighlight() > 1e-9) hl = r.app->edit().tabHighlight();
                if (fadeIn < 0 && r.app->edit().tabFade(EditScreen::Cut) > 1e-9) fadeIn = r.app->edit().tabFade(EditScreen::Cut);
                if (fadeOut < 0 && r.app->edit().tabFade(EditScreen::Grade) < 1.0 - 1e-9) fadeOut = r.app->edit().tabFade(EditScreen::Grade);
            }
            std::printf("      highlight %.3f  cut fade %.3f  grade fade %.3f\n", hl, fadeIn, fadeOut);
            CHECK(strictlyBetween(hl, 0.0, 1.0), "the tab highlight TRAVELS (first frame between Grade and Cut)");
            CHECK(strictlyBetween(fadeIn, 0.0, 1.0), "the Cut page fades in (first frame between 0 and 1)");
            CHECK(strictlyBetween(fadeOut, 0.0, 1.0), "the Grade page fades out (first frame between 1 and 0)");
            const Rect before = worldRect(*r.app->edit().monitor());
            r.settle();
            const Rect after = worldRect(*r.app->edit().monitor());
            CHECK(near(before.x, after.x) && near(before.y, after.y) && near(before.w, after.w), "the monitor did not move across the tab switch");
            CHECK(after.h < before.h - 1.0, "…only its height eased, giving the transport room in Cut (R-UI-3, amended)");
        }
        {
            Rig r(1024, 640, [](FakeService &s) { s.edit(); });
            r.settle();
            auto vs = r.app->edit().topBar()->versions();
            const Point p = centre(*vs, vs->bodyRect());
            r.click(p.x, p.y);
            const double first = firstMoved(r, [&] { return vs->openAmount(); }, 0.0);
            std::printf("      dropdown open amount %.3f\n", first);
            CHECK(strictlyBetween(first, 0.0, 1.0), "the version dropdown OPENS eased (first frame between 0 and 1)");
        }
        {
            Rig r(1440, 900, [](FakeService &s) { s.edit(); });
            r.settle();
            r.app->setTab(EditScreen::Cut);
            r.settle();
            auto tl = r.app->edit().timeline();
            const double before = tl->ppsLive();
            const Point p = centre(*tl, tl->zoomInRect());
            r.click(p.x, p.y);
            const double target = tl->ppsTarget();
            const double first = firstMoved(r, [&] { return tl->ppsLive(); }, before);
            std::printf("      zoom %.2f -> first %.2f -> target %.2f px/s\n", before, first, target);
            CHECK(target > before && strictlyBetween(first, before, target), "the timeline ZOOM eases (live px/s strictly between)");
            // an agent moves a clip: it travels there
            r.settle();
            const double x0 = tl->clipRect("c5").x;
            for (auto &c : r.svc.m.clips) if (c.id == "c5") c.at = 13.0;
            ++r.svc.m.revision;
            const double x1 = tl->timeToX(13.0);
            const double fx = firstMoved(r, [&] { return tl->clipRect("c5").x; }, x0);
            CHECK(strictlyBetween(fx, x0, x1), "a clip the model moved TRAVELS to its new place");
        }
        {
            Rig r(1440, 900, [](FakeService &s) { s.home(); });
            r.settle();
            r.resize(1100, 900);
            r.frame();
            r.resize(1440, 900);   // the column count changes: 4 across at 1440, 2 at 1100... back
            r.settle();
            r.resize(1100, 900);
            auto &home = r.app->home();
            const Rect from = home.cardLive(2);
            const Rect target = home.cardTarget(2);
            CHECK(!(near(from.x, target.x) && near(from.y, target.y)), "a narrower window moves card 3 to a new slot");
            const double fx = firstMoved(r, [&] { return home.cardLive(2).x; }, from.x);
            std::printf("      card 3 x %.1f -> first %.1f -> target %.1f\n", from.x, fx, target.x);
            CHECK(strictlyBetween(fx, from.x, target.x), "the home grid REFLOW eases (card live x strictly between)");
        }
        {
            Rig r(1440, 900, [](FakeService &s) { s.home(); });
            r.settle();
            r.svc.edit();
            const double first = firstMoved(r, [&] { return r.app->screenOpacity(arstro::interstellar::Screen::Edit); }, 0.0);
            CHECK(strictlyBetween(first, 0.0, 1.0), "Home → Edit CROSS-FADES (Edit's first opacity between 0 and 1)");
        }
        {
            Rig r(1440, 900, [](FakeService &s) { s.edit(); });
            r.settle();
            std::string err;
            r.svc.dispatch("set s_day01.basic.exposure=1.2", err);   // a grade lands: same time, new picture
            r.frame();   // the frame that fetched the new picture starts the dissolve at 0
            CHECK(r.app->edit().monitor()->dissolveAmount() < 1e-9, "the new picture starts fully under the old one");
            const double d = firstMoved(r, [&] { return r.app->edit().monitor()->dissolveAmount(); }, 0.0);
            std::printf("      dissolve first frame %.3f\n", d);
            CHECK(strictlyBetween(d, 0.0, 1.0) && d < 0.2, "a new picture at the same time DISSOLVES (linear: ~16/160 on the first frame)");
            auto tp = r.app->edit().transport();
            r.settle();
            const double t0 = tp->displayedTime();
            r.svc.dispatch("playhead 10", err);
            const double ft = firstMoved(r, [&] { return tp->displayedTime(); }, t0);
            CHECK(strictlyBetween(ft, t0, 10.0), "a playhead jump from the model EASES the transport there");
            auto rt = r.app->edit().rackTree();
            r.settle();
            auto pl = r.app->edit().gradeInspector()->plugins();
            const double k0 = pl->switchAmount(1);   // ef_1, on
            r.svc.dispatch("set ef_1.enabled=0", err);
            const double fk = firstMoved(r, [&] { return pl->switchAmount(1); }, k0);
            CHECK(strictlyBetween(fk, 0.0, k0), "a plugin switch the model changed SLIDES (first frame between)");
        }
        {
            // state that ARRIVES from the model — a toggle, a version switch, a rebase, a finished
            // render — is still a visible change, and still eases (design rule §1)
            Rig r(1440, 900, [](FakeService &s) { s.edit(); });
            r.settle();
            auto rt = r.app->edit().rackTree();
            r.svc.dispatch("set s_day01.bypass=1", gErr);
            const double fb = firstMoved(r, [&] { return rt->bypassAmount(1); }, 0.0);
            CHECK(strictlyBetween(fb, 0.0, 1.0), "a bypass toggled in the model DIMS the rack row eased, not in one frame");

            auto vs = r.app->edit().topBar()->versions();
            r.svc.dispatch("timeline open delivery", gErr);
            r.frame();   // the frame that saw the switch starts the chrome's cross-fade at 0
            const double fs = firstMoved(r, [&] { return vs->swapAmount(); }, 0.0);
            CHECK(strictlyBetween(fs, 0.0, 1.0), "a version switch CROSS-FADES the chrome's label");

            r.app->setTab(EditScreen::Cut);
            r.settle();
            auto tl = r.app->edit().timeline();
            const Rect before = tl->clipRect("c6");
            for (auto &c : r.svc.m.clips) if (c.id == "c6") c.provenance = arstro::interstellar::Provenance::Inherited;   // a rebase reconciled it
            ++r.svc.m.revision;
            r.frame();
            const double fa = firstMoved(r, [&] { return tl->clipAlpha("c6"); }, 0.0);
            CHECK(strictlyBetween(fa, 0.0, 1.0), "a clip whose provenance changed CROSS-FADES to its new look");
            const Rect after = tl->clipRect("c6");
            CHECK(near(before.x, after.x, 0.01) && near(before.w, after.w, 0.01), "…in place (the new look starts from the old one's geometry)");

            for (auto &j : r.svc.m.renders) if (j.id == "r2") { j.done = j.total; j.state = "done"; }
            ++r.svc.m.revision;
            r.frame();
            auto q = r.app->edit().renderQueue();
            const double fd = firstMoved(r, [&] { return q->stateAmount("r2", 2); }, 0.0);
            CHECK(strictlyBetween(fd, 0.0, 1.0), "a render that finishes cross-fades running → done");

            const size_t n = r.svc.m.tracks.size();
            r.svc.m.tracks.erase(r.svc.m.tracks.begin());   // the version switch drops V2: the lanes below slide up
            ++r.svc.m.revision;
            const Rect v1 = tl->laneRect("v1");
            r.frame();
            r.frame();
            const Rect v1mid = tl->laneRect("v1");
            r.settle();
            const Rect v1end = tl->laneRect("v1");
            CHECK(n == 4 && v1end.y < v1.y - 1.0, "dropping a track moves the lanes under it up");
            CHECK(strictlyBetween(v1mid.y, v1.y, v1end.y), "…and they SLIDE there (mid-tween lane between old and new)");
        }
        {
            Rig r(1440, 900, [](FakeService &s) { s.home(); });
            r.settle();
            const Rect s = r.app->home().settingsRect();
            r.click(s.x + 10, s.y + 9);
            const double first = firstMoved(r, [&] { return r.app->settings().appearAmount(); }, 0.0);
            CHECK(strictlyBetween(first, 0.0, 1.0), "the settings modal appears eased");
        }
    }

    // ── 3. layout at two sizes ─────────────────────────────────────────────────────────

    void testLayout()
    {
        std::printf("layout: contained, no sibling overlap, at both sizes\n");
        for (const auto &sz : kSizes)
        {
            const double W = sz[0], H = sz[1];
            Rig r(sz[0], sz[1], [](FakeService &s) { s.edit(); });
            r.settle();
            for (int tab = 0; tab < 3; ++tab)
            {
                r.app->setTab(tab);
                r.settle();
                auto &e = r.app->edit();
                std::vector<Rect> parts = {worldRect(*e.topBar()), worldRect(*e.monitor())};
                if (e.transport()->visible) parts.push_back(worldRect(*e.transport()));   // culled in Grade
                CHECK((tab == 0) != e.transport()->visible, tab == 0 ? "Grade has no transport (culled at rest)" : "Cut and Deliver keep the transport");
                for (auto &c : e.page(tab)->children()) parts.push_back(worldRect(*c));
                bool contained = true, separate = true;
                for (size_t i = 0; i < parts.size(); ++i)
                {
                    contained = contained && inside(parts[i], W, H);
                    for (size_t j = i + 1; j < parts.size(); ++j) separate = separate && !overlap(parts[i], parts[j]);
                }
                char msg[160];
                std::snprintf(msg, sizeof msg, "%dx%d tab %d: top bar, monitor, transport and the tab's three columns are inside the window", sz[0], sz[1], tab);
                CHECK(contained, msg);
                std::snprintf(msg, sizeof msg, "%dx%d tab %d: no two of them overlap", sz[0], sz[1], tab);
                CHECK(separate, msg);
            }
            auto tb = r.app->edit().topBar();
            CHECK(tb->nameRect().right() <= tb->tabs()->x.value() + 0.5, "the project name stops before the tab switcher");
            auto vs = tb->versions();
            CHECK(worldRect(*vs).right() <= W - 9.75 - 20.5 + 0.5, "the version switcher stops before the save button");
            CHECK(r.app->edit().monitor()->width.value() >= shell::minMonitorW() - 0.5, "the monitor keeps its floor");
        }
        {
            Rig r(1024, 640, [](FakeService &s) { s.home(); });
            r.settle();
            auto &home = r.app->home();
            const Rect vp = home.gridViewport();
            bool ok = true;
            for (int i = 0; i < home.cardCount(); ++i)
            {
                const Rect c = home.cardLive(i);
                ok = ok && c.x >= vp.x - 0.5 && c.right() <= vp.right() + 0.5;
                for (int j = i + 1; j < home.cardCount(); ++j) ok = ok && !overlap(c, home.cardLive(j));
            }
            CHECK(ok, "1024x640 home: every card inside the grid's width, none overlapping");
            CHECK(home.wordmarkSize() <= 46.0 && home.wordmarkSize() >= 24.0, "the home wordmark is sized to fit the sidebar");
        }
    }

    void testTextFit()
    {
        std::printf("text fits (measured)\n");
        cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8);
        cairo_t *cr = cairo_create(s);
        artboard::CairoTarget t(cr);
        const std::string longName = "Lisbon interviews (selects, colour pass two, director's notes applied)";
        for (double maxW : {40.0, 120.0, 233.0})
        {
            const std::string e = textfit::ellipsize(t, longName, maxW, 11.0, font::sansMedium());
            char msg[96];
            std::snprintf(msg, sizeof msg, "ellipsized to %.0f px measures within it (%.1f)", maxW, t.measureText(e, 11.0, font::sansMedium()));
            CHECK(t.measureText(e, 11.0, font::sansMedium()) <= maxW + 0.01, msg);
        }
        const std::string dash = textfit::ellipsize(t, "Night Ferry \xE2\x80\x94 Day 3", 70.0, 12.0, font::sansMedium());
        bool cutsOk = validUtf8(dash);
        for (double w = 20.0; w < 140.0; w += 3.0) cutsOk = cutsOk && validUtf8(textfit::ellipsize(t, "Night Ferry \xE2\x80\x94 Day 3", w, 12.0, font::sansMedium()));
        CHECK(cutsOk, "ellipsizing never cuts a multi-byte code point (every width 20..140 px)");
        CHECK(textfit::ellipsize(t, "x", 0.0, 11.0, font::sans()).empty(), "no room → nothing drawn, rather than a glyph over a neighbour");
        cairo_destroy(cr);
        cairo_surface_destroy(s);
    }

    // ── the monitor releases what it replaces ───────────────────────────────────────────

    /** A CairoTarget that counts image registrations, so a test can see ids being RELEASED. */
    class CountingTarget : public artboard::CairoTarget
    {
    public:
        int live = 0;
        int registerImage(const uint8_t *rgba, int w, int h) override { ++live; return CairoTarget::registerImage(rgba, w, h); }
        void releaseImage(int id) override { --live; CairoTarget::releaseImage(id); }
    };

    void testMonitorImages()
    {
        std::printf("monitor: one registered frame, the previous id released\n");
        FakeService svc;
        svc.edit();
        App app(svc.hooks(), 1024, 640);
        app.setTab(1);   // the playhead drives the monitor in Cut; Grade shows the ref frame (R-UI-3)
        cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1024, 640);
        cairo_t *cr = cairo_create(surf);
        CountingTarget t;
        t.setContext(cr);
        double now = 1000.0;
        auto pump = [&](int frames) { for (int i = 0; i < frames; ++i) { app.render(t, now); now += 16.0; } };
        pump(45);
        const int baseline = t.live;
        const int fetched0 = svc.frames;
        for (int k = 0; k < 12; ++k)
        {
            svc.dispatch("playhead " + std::to_string(1.0 + k * 0.25), gErr);
            pump(2);
        }
        pump(45);
        std::printf("      %d frames fetched, live image ids %d -> %d\n", svc.frames - fetched0, baseline, t.live);
        CHECK(svc.frames - fetched0 >= 12, "each playhead step fetched a frame through renderFrame");
        CHECK(t.live - baseline <= 0, "…and released the id it replaced: no image leaks per step");
        svc.dispatch("set s_day01.basic.exposure=1.5", gErr);   // a dissolve holds two ids for 160 ms…
        pump(3);
        const int during = t.live;
        pump(30);
        CHECK(during - baseline <= 1 && t.live - baseline <= 0, "…a dissolve holds ONE extra id while it runs and lets it go after");
        cairo_destroy(cr);
        cairo_surface_destroy(surf);
    }

    // ── 4. reach: scroll clamps both ends and gets to the last row ───────────────────────

    void testScroll()
    {
        std::printf("reach: scroll clamps both ends\n");
        {
            EasedScroll es;
            es.setExtent(0, 100, 80);
            CHECK(!es.scrollBy(40), "an unscrollable list reports false, so the wheel bubbles");
            es.setExtent(0, 100, 300);
            es.scrollBy(1000);
            CHECK(near(es.target(), 200.0), "scrolling past the end clamps at content - viewport");
            es.scrollBy(-5000);
            CHECK(near(es.target(), 0.0), "…and at 0");
        }
        {
            Rig r(1024, 640, [](FakeService &s) {
                s.edit();
                for (int i = 0; i < 40; ++i)
                {
                    auto n = FakeService::node(100 + i, ("rox" + std::to_string(i)).c_str(), ("s_extra" + std::to_string(i)).c_str(),
                                               ("Extra source " + std::to_string(i)).c_str(), -1, 0, false);
                    n.video = true;
                    s.m.rack.push_back(n);
                }
            });
            r.settle();
            auto rt = r.app->edit().rackTree();
            CHECK(rt->scroll().scrollable(), "a 48-node rack outgrows the column at 1024x640");
            const Point p = centre(*rt, rt->viewport());
            r.app->wheel(p.x, p.y, -60.0);
            r.settle();
            const Rect last = rt->rowRect((int)r.svc.m.rack.size() - 1);
            const Rect vp = rt->viewport();
            CHECK(near(rt->scroll().value(), rt->scroll().maxScroll(), 0.5), "wheeling down lands exactly at the end (clamped)");
            CHECK(last.bottom() <= vp.bottom() + 0.5 && last.y >= vp.y - 0.5, "…where the LAST row is fully reachable");
            r.app->wheel(p.x, p.y, 200.0);
            r.settle();
            CHECK(near(rt->scroll().value(), 0.0, 0.5), "wheeling up clamps at the top");
        }
        {
            Rig r(1024, 640, [](FakeService &s) {
                s.edit();
                auto base = s.m.timelines[1];
                for (int i = 0; i < 30; ++i)
                {
                    auto tl = base;
                    tl.id = "v" + std::to_string(i);
                    tl.name = "Variant " + std::to_string(i);
                    s.m.timelines.push_back(tl);
                }
            });
            r.settle();
            auto vs = r.app->edit().topBar()->versions();
            Point p = centre(*vs, vs->bodyRect());
            r.click(p.x, p.y);
            r.settle();
            const Rect m = vs->menuRect();
            const Point bottom = world(*vs, m.x, m.bottom());
            CHECK(bottom.y <= 640.0 - 8.0 + 0.5, "33 versions: the dropdown is CAPPED inside the window (placed against the root)");
            CHECK(vs->listScroll().scrollable(), "…and its list scrolls");
            p = centre(*vs, vs->versionRowRect(1));
            r.app->wheel(p.x, p.y, -100.0);
            r.settle();
            CHECK(near(vs->listScroll().value(), vs->listScroll().maxScroll(), 0.5) && vs->listScroll().maxScroll() > 0, "the wheel scrolls the list to its clamped end");
            CHECK(vs->isOpen(), "…without closing the dropdown");
            r.key(27);
            r.settle();
            CHECK(!vs->isOpen() && vs->openAmount() < 1e-3, "Escape closes it (eased to 0)");
        }
        {
            Rig r(1024, 640, [](FakeService &s) {
                s.edit();
                for (int i = 0; i < 30; ++i)
                {
                    auto j = s.m.renders[2];
                    j.id = "rx" + std::to_string(i);
                    s.m.renders.push_back(j);
                }
            });
            r.settle();
            r.app->setTab(EditScreen::Deliver);
            r.settle();
            auto q = r.app->edit().renderQueue();
            CHECK(q->scroll().scrollable(), "34 renders outgrow the deck");
            const Point p = centre(*q, Rect{0, 40, q->width.value(), 40});
            r.app->wheel(p.x, p.y, -200.0);
            r.settle();
            CHECK(near(q->scroll().value(), q->scroll().maxScroll(), 0.5), "the render queue scrolls to its clamped end");
            const Rect last = q->rowRect(q->count() - 1);
            CHECK(last.bottom() <= q->height.value() + 0.5, "…and its last row is reachable");
        }
        {
            Rig r(1024, 640, [](FakeService &s) { s.home(); });
            r.settle();
            auto &home = r.app->home();
            CHECK(home.scroll().scrollable(), "six projects outgrow the home grid at 1024x640");
            const Rect vp = home.gridViewport();
            r.app->wheel(vp.x + vp.w * 0.5, vp.y + 40.0, -100.0);
            r.settle();
            CHECK(near(home.scroll().value(), home.scroll().maxScroll(), 0.5), "the home grid scrolls to its clamped end");
            CHECK(home.newCardLive().bottom() <= vp.bottom() + 0.5, "…where the trailing New project card is reachable");
            r.app->wheel(vp.x + vp.w * 0.5, vp.y + 40.0, 100.0);
            r.settle();
            CHECK(near(home.scroll().value(), 0.0, 0.5), "…and back to the top");
        }
    }
}

namespace
{
    // ── 8. cosmo's menus, accelerators, settings and screen scale ──────────────────────────

    void ctrlKey(Rig &r, int code, bool shift = false)
    {
        artboard::KeyEvent e;
        e.type = artboard::KeyEvent::Type::Down;
        e.keyCode = code;
        e.ctrl = true;
        e.shift = shift;
        r.app->key(e);
    }

    /** Open menu `title` and click its item whose label starts with `item`. */
    bool clickMenuItem(Rig &r, const std::string &title, const std::string &item)
    {
        auto ms = r.app->edit().topBar()->menus();
        for (int i = 0; i < ms->menuCount(); ++i)
        {
            if (ms->menu(i).title != title) continue;
            const Point t = centre(*ms, ms->titleRect(i));
            r.click(t.x, t.y);
            r.pump(250);
            for (int k = 0; k < (int)ms->menu(i).items.size(); ++k)
                if (ms->menu(i).items[(size_t)k].label.rfind(item, 0) == 0)
                {
                    const Point p = centre(*ms, ms->itemRect(i, k));
                    r.click(p.x, p.y);
                    r.pump(64);
                    return true;
                }
        }
        return false;
    }

    void testMenusSettingsScale()
    {
        std::printf("menus, accelerators, settings and screen scale (cosmo's)\n");
        {
            Rig r(1440, 900, [](FakeService &s) { s.edit(); s.m.presets = {{"Film/Warm", "Film"}, {"Soft", ""}}; ++s.m.revision; });
            r.settle();
            auto ms = r.app->edit().topBar()->menus();
            std::string titles;
            for (int i = 0; i < ms->menuCount(); ++i) titles += (i ? " " : "") + ms->menu(i).title;
            CHECK(titles == "File Edit Settings Workspace Preset Colour", "the top bar carries cosmo's menu strip: File Edit Settings Workspace Preset, and Colour (R-COLOR-3)");
            CHECK(clickMenuItem(r, "File", "Save ") && hasLine(r.svc, "project save"), "File > Save dispatched project save");
            CHECK(clickMenuItem(r, "Edit", "Undo") && hasLine(r.svc, "undo"), "Edit > Undo dispatched undo");
            CHECK(clickMenuItem(r, "Edit", "Paste Grade to All") && hasLine(r.svc, "grade paste --all"), "Edit > Paste Grade to All dispatched grade paste --all");
            const std::string sel = r.svc.m.rack[(size_t)r.svc.m.selectedRack].bindName;
            const std::string apply = "preset apply Film/Warm --node " + sel;
            CHECK(clickMenuItem(r, "Preset", "Apply  Film/Warm") && hasLine(r.svc, apply), "the Preset menu lists the library: Apply Film/Warm dispatched preset apply");
            CHECK(clickMenuItem(r, "Workspace", "Cut") && r.app->tab() == EditScreen::Cut, "Workspace > Cut switched the tab");
            CHECK(clickMenuItem(r, "Settings", "Engine Settings") && r.app->settings().isOpen(), "Settings > Engine Settings opened cosmo's settings dialog");
            r.settle();
            CHECK(r.app->settings().extraRowCount() == 2, "Interstellar adds two rows of its own to cosmo's dialog: Hardware video (R-PLAY-3), Preview cache (R-PLAY-1)");
            const Rect on = r.app->settings().extraChipRect(0, 1);
            CHECK(on.w > 0, "the Hardware video row has an On chip");
            r.click(on.x + on.w * 0.5, on.y + on.h * 0.5);
            CHECK(hasLine(r.svc, "settings set hardwareVideo=1"), "Hardware video > On dispatched settings set hardwareVideo=1");
            const Rect off = r.app->settings().extraChipRect(1, 0);
            r.click(off.x + off.w * 0.5, off.y + off.h * 0.5);
            CHECK(hasLine(r.svc, "settings set previewCache=0"), "Preview cache > Off dispatched settings set previewCache=0");
        }
        {
            // R-PLAY-1: a segment of the cache bar that finishes FADES in — never flips in one frame
            Rig r(1440, 900, [](FakeService &s) { s.edit(); s.m.previewCacheSegments = {0, 0, 0, 0}; });
            r.app->setTab(1);
            r.settle();
            auto tl = r.app->edit().timeline();
            CHECK(tl->cacheAmount(1) == 0.0, "an uncached second draws no bar");
            r.svc.m.previewCacheSegments[1] = 1;
            ++r.svc.m.revision;
            const double first = firstMoved(r, [&] { return tl->cacheAmount(1); }, 0.0);
            CHECK(strictlyBetween(first, 0.0, 1.0), "a second that finished caching fades in on the ruler (live value mid-tween)");
            r.settle();
            CHECK(std::fabs(tl->cacheAmount(1) - 1.0) < 1e-6 && tl->cacheAmount(0) == 0.0, "only that second is drawn cached");
        }
        {
            Rig r(1440, 900, [](FakeService &s) { s.edit(); });
            r.settle();
            ctrlKey(r, 'Z');
            ctrlKey(r, 'Z', true);
            ctrlKey(r, 'Y');
            ctrlKey(r, 'S');
            CHECK(hasLine(r.svc, "undo") && std::count(r.svc.lines.begin(), r.svc.lines.end(), std::string("redo")) == 2 &&
                      hasLine(r.svc, "project save"),
                  "Ctrl+Z undo, Ctrl+Shift+Z and Ctrl+Y redo, Ctrl+S save — cosmo's accelerators");
            const std::string sel = r.svc.m.rack[(size_t)r.svc.m.selectedRack].bindName;
            ctrlKey(r, 'C');
            ctrlKey(r, 'V');
            CHECK(hasLine(r.svc, "grade copy " + sel) && hasLine(r.svc, "grade paste " + sel), "Ctrl+C / Ctrl+V copy and paste the Grade target's grade");
        }
        {
            // Screen scale follows the service's setting, EASED, with input mapped through it.
            Rig r(1440, 900, [](FakeService &s) { s.edit(); });
            r.settle();
            CHECK(std::fabs(r.app->width() - 1440.0) < 1e-6, "at 100% the logical width is the window's");
            r.svc.m.settings.uiScale = 125;
            ++r.svc.m.revision;
            const double first = firstMoved(r, [&] { return r.app->drawnUiScale(); }, 1.0);
            CHECK(strictlyBetween(first, 1.0, 1.25), "a new screen scale ZOOMS (first moved frame strictly between 100% and 125%)");
            r.settle();
            CHECK(std::fabs(r.app->width() - 1440.0 / 1.25) < 1e-6, "at 125% the layout is 1152 logical units wide");
            auto ms = r.app->edit().topBar()->menus();
            const Point t = centre(*ms, ms->titleRect(0));   // logical
            r.click(t.x * 1.25, t.y * 1.25);                 // the host speaks pixels
            r.pump(250);
            CHECK(ms->openIndex() == 0, "a click in pixels lands on the logical File title at 125%");
        }
        {
            // Nothing cut at the playhead + a Grade target: the monitor asks for the reference frame.
            Rig r(1440, 900, [](FakeService &s) { s.edit(); s.m.playhead = 15.4; s.m.clips.erase(s.m.clips.begin() + 4); });
            r.settle();
            CHECK(!r.svc.sourceCalls.empty(), "in Grade the monitor shows the target itself, playhead or not (R-UI-3)");
            r.app->setTab(1);
            r.settle();
            const int before = r.svc.frames;
            r.svc.m.playhead = 15.5;
            ++r.svc.m.revision;
            r.pump(64);
            CHECK(r.svc.frames > before, "with no clip at the playhead the monitor still asks for a frame (the Grade target's reference)");
        }
    }
}

namespace
{
    // ── 9. cosmo's selection and right-click menu on rack items (R-RACK-8, R-UI-9) ───────────

    void clickMod(Rig &r, double x, double y, bool shift, bool ctrl)
    {
        r.app->pointer(1, x, y, 0, r.now);
        r.app->pointer(0, x, y, 0, r.now, false, shift, ctrl);
        r.app->pointer(2, x, y, 0, r.now + 40.0, false, shift, ctrl);
        r.pump(32);
    }

    void testSelectionAndContext()
    {
        std::printf("selection and the right-click menu (cosmo's)\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); });
        r.settle();
        auto rt = r.app->edit().rackTree();
        rt->setOpen("ro4", true);   // gr2's members
        r.settle();
        const Point p5 = centre(*rt, rt->rowRect(5));
        clickMod(r, p5.x - 40, p5.y, true, false);
        CHECK(hasLine(r.svc, "rack select " + r.svc.m.rack[5].bindName + " --range"), "Shift-click on a rack row dispatched rack select … --range");
        const Point p4 = centre(*rt, rt->rowRect(4));
        clickMod(r, p4.x - 40, p4.y, false, true);
        CHECK(hasLine(r.svc, "rack select " + r.svc.m.rack[4].bindName + " --add"), "Ctrl-click dispatched rack select … --add");
        // Right-click: cosmo's context menu opens at the row; Group dispatches rack group new.
        const Point p1 = centre(*rt, rt->rowRect(1));
        r.app->pointer(1, p1.x - 40, p1.y, 0, r.now);
        r.app->pointer(0, p1.x - 40, p1.y, 2, r.now);
        r.app->pointer(2, p1.x - 40, p1.y, 2, r.now + 40.0);
        r.pump(250);
        auto cm = r.app->edit().contextMenu();
        CHECK(cm->isOpen(), "right-click on a rack row opened cosmo's context menu");
        std::string labels;
        for (int i = 0; i < cm->itemCount(); ++i) labels += cm->item(i).label + "|";
        std::printf("      %s\n", labels.c_str());
        CHECK(labels.find("Group") != std::string::npos && labels.find("Rename") != std::string::npos &&
                  labels.find("Copy Grade") != std::string::npos && labels.find("Filter") != std::string::npos,
              "it offers Group, a filter toggle, Rename, Copy Grade (cosmo's items, Interstellar's words)");
        int g = -1;
        for (int i = 0; i < cm->itemCount(); ++i)
            if (cm->item(i).label.rfind("Group", 0) == 0) g = i;
        if (g >= 0)
        {
            const Point gp = centre(*cm, cm->itemRect(g));
            r.click(gp.x, gp.y);
            r.pump(64);
        }
        CHECK(hasLine(r.svc, "rack group new"), "its Group item dispatched rack group new (the selection)");
        // Ctrl+G is Group Selection.
        r.svc.lines.clear();
        ctrlKey(r, 'G');
        CHECK(hasLine(r.svc, "rack group new"), "Ctrl+G dispatched rack group new");
        r.svc.edit();   // the fake grouped for real: back to the standard rack
        ++r.svc.m.revision;
        r.settle();
    }

    /** R-FX-5 / R-RACK-4 (amended): the IMAGE PROCESSING list — Cosmo first with its Mix, effects
     *  after, a switch per row, add from the catalog, select to edit, remove, reorder. */
    /** R-ANIM-3/4 (amended): animation is authored in the timeline's key lane only — the selected
     *  clip's own properties, its source's colour keys and effects, a diamond each, the chosen one's
     *  graph under the clip; a keyframe's menu takes presets and typed numbers. Grade has none. */
    void dblClick(Rig &r, double x, double y);   // defined with the editing tests

    void testKeyframes()
    {
        std::printf("keyframes: authored in the timeline's key lane, not in Grade\n");
        {
            // Grade: no diamond where the old one sat, nothing keyed by a click there
            Rig r(1440, 900, [](FakeService &s) { s.edit(); });
            r.settle();
            auto gi = r.app->edit().gradeInspector();
            auto *row = firstChild<arstro::cosmo_v2::SliderRow>(*gi->basicDetail());
            r.svc.lines.clear();
            const Point p = world(*row, row->width.value() - 6.0, row->height.value() * 0.5);
            r.click(p.x, p.y);
            CHECK(withPrefix(r.svc, "key ").empty(), "Grade keys nothing: its rows carry no diamond (R-ANIM-3, amended)");
        }
        auto withCurve = [](FakeService &s) {
            s.edit();
            s.m.selectedClip = "c1";                 // s_day01 at 0, in 2.0, out 6.5 — its source has two effects
            s.m.playhead = 1.0;                      // → the clip's footage time 3.0
            s.animate("s_day01.basic.exposure", {{2.0, -1.0}, {5.0, 2.0}});
        };
        {
            Rig r(1440, 900, withCurve);
            r.app->setTab(1);
            r.settle();
            auto tl = r.app->edit().timeline();
            // R-ANIM-3 (2026-10-07): the chosen clip's track opens to its properties — the ▸ on its header
            const Rect ex = tl->expandToggleRect();
            CHECK(ex.w > 0 && tl->laneRect(tl->expandedTrack()).contains(Point{tl->laneRect(tl->expandedTrack()).x, ex.y + ex.h * 0.5}),
                  "the chosen clip's track header carries the ▸ that opens it");
            Point p = centre(*tl, ex);
            r.click(p.x, p.y);
            const double ka = firstMoved(r, [&] { return tl->keyLaneAmount(); }, 0.0);
            CHECK(strictlyBetween(ka, 0.0, 1.0), "the track opens to its properties, eased");
            r.settle();
            const Rect trk = tl->laneRect(tl->expandedTrack()), kr = tl->keyLaneRect();
            CHECK(std::fabs(kr.y - trk.bottom()) < 1e-6, "the properties sit directly under the clip's track");
            std::string below;
            for (const auto &t : r.svc.m.tracks) if (t.order == 0 && t.audio) below = t.id;   // the first track drawn under the video
            bool pushed = false;
            for (const auto &t : r.svc.m.tracks)
                if (t.id != tl->expandedTrack() && tl->laneRect(t.id).y > trk.y) pushed = pushed || std::fabs(tl->laneRect(t.id).y - kr.bottom()) < 1e-6;
            CHECK(pushed, "…and the tracks below move down to make room: nothing overlaps");
            auto kl = tl->keyLane();
            CHECK(kl->sectionRow("CLIP") >= 0 && kl->sectionRow("GRADE") >= 0 && kl->sectionRow("EFFECTS") >= 0,
                  "it lists the clip's properties, its source's colour keys and its effects");
            CHECK(std::fabs(kl->now() - 3.0) < 1e-9, "it keys at the playhead, on the clip's footage clock (in 2.0 + 1.0)");
            const int expo = kl->rowOf("s_day01.basic.exposure"), contrast = kl->rowOf("s_day01.basic.contrast"), opac = kl->rowOf("c1.opacity");
            CHECK(expo >= 0 && kl->row(expo).state == 1 && kl->row(contrast).state == 0, "Exposure is animated (outline), Contrast is not");
            CHECK(kl->rowRect(expo).h > 1.0 && kl->rowRect(opac).h < 1.0, "it opens on what moves: GRADE (Exposure is keyed) open, the quiet CLIP folded");
            // every key on its property's row, under the frame it keys (source 2.0 = the clip's first frame)
            const double kx = kl->worldTransform().apply(kl->keyPoint(expo, 0)).x, cx = tl->worldTransform().apply(Point{tl->timeToX(0.0), 0}).x;
            const double kx1 = kl->worldTransform().apply(kl->keyPoint(expo, 1)).x, cx1 = tl->worldTransform().apply(Point{tl->timeToX(3.0), 0}).x;
            CHECK(std::fabs(kx - cx) < 1e-6 && std::fabs(kx1 - cx1) < 1e-6, "Exposure's keys sit on its row under the frames they key (0 s and 3 s on the timeline)");
            // choosing a row opens its curve under it, eased
            p = centre(*kl, kl->rowRect(expo));
            r.click(p.x, p.y);
            const double band = firstMoved(r, [&] { return kl->bandAmount(expo); }, 0.0);
            CHECK(strictlyBetween(band, 0.0, 1.0), "clicking Exposure opens its curve under its row, eased");
            r.settle();
            auto g = kl->graph();
            CHECK(g->curveCount() == 1 && g->selectedAddress() == "s_day01.basic.exposure" && std::fabs(g->worldTransform().apply(Point{0, 0}).y -
                                                                                                        kl->worldTransform().apply(Point{0, kl->bandRect(expo).y}).y) < 1e-6,
                  "…the source's exposure curve, in the band under the row");
            const double x0 = g->worldTransform().apply(Point{g->plotRect().x, 0}).x;
            CHECK(std::fabs(x0 - cx) < 1e-6, "its plot starts where the clip starts on the timeline");
            // a key dragged along its row moves in time
            r.svc.lines.clear();
            const Point k1 = kl->worldTransform().apply(kl->keyPoint(expo, 1));
            r.drag(k1.x, k1.y, k1.x - 24.0, k1.y);
            const std::string mv = withPrefix(r.svc, "key set s_day01.basic.exposure --at 5 --to ");
            CHECK(!mv.empty() && mv.find("--value") == std::string::npos, "dragging a key along its row moves it in time: key set … --at 5 --to <earlier>");
            r.settle();
            // the Contrast diamond keys the SOURCE at the playhead; a double-click on its row keys it there
            r.svc.lines.clear();
            p = centre(*kl, kl->diamondRect(contrast));
            if (kl->diamondRect(contrast).bottom() > kl->height.value())   // the rows scroll inside the lane (R6)
            {
                const Point in = centre(*kl, kl->rowRect(kl->sectionRow("GRADE")));
                for (int k = 0; k < 20 && kl->diamondRect(contrast).bottom() > kl->height.value(); ++k)
                {
                    r.app->wheel(in.x, in.y, -3.0);
                    r.pump(50);
                }
                r.settle();
                p = centre(*kl, kl->diamondRect(contrast));
            }
            r.click(p.x, p.y);
            CHECK(hasLine(r.svc, "key add s_day01.basic.contrast --at 3"), "the Contrast diamond keys the SOURCE's contrast at the playhead (key add … --at 3)");
            const double df = firstMoved(r, [&] { return kl->diamondFill(contrast); }, 0.0);
            CHECK(kl->row(contrast).state == 2 && strictlyBetween(df, 0.0, 1.0), "its diamond fills, eased");
            r.settle();
            r.svc.lines.clear();
            const Rect cr = kl->rowRect(contrast);
            const Point dbl = kl->worldTransform().apply(Point{tl->timeToX(2.0), cr.y + cr.h * 0.5});
            dblClick(r, dbl.x, dbl.y);
            CHECK(hasLine(r.svc, "key add s_day01.basic.contrast --at 4"), "double-clicking Contrast's row at 2 s on the timeline keys it there (source 4.0)");
            // the front row clicked again closes its curve; folding a section is eased presentation
            kl->select("s_day01.basic.exposure");   // the double-click's first click chose Contrast
            r.settle();
            r.svc.lines.clear();
            p = centre(*kl, kl->rowRect(expo));
            r.click(p.x, p.y);
            const double shut = firstMoved(r, [&] { return kl->bandAmount(expo); }, 1.0);
            CHECK(strictlyBetween(shut, 0.0, 1.0) && kl->selected().empty() && r.svc.lines.empty(), "Exposure clicked again closes its curve, eased, dispatching nothing");
            r.settle();
            const int grade = kl->sectionRow("GRADE");
            p = centre(*kl, kl->rowRect(grade));
            if (kl->rowRect(grade).y >= 0 && kl->rowRect(grade).bottom() <= kl->height.value())
            {
                const double h0 = kl->rowRect(expo).h;
                r.click(p.x, p.y);
                const double h1 = firstMoved(r, [&] { return kl->rowRect(expo).h; }, h0);
                CHECK(strictlyBetween(h1, 0.0, h0) && r.svc.lines.empty(), "folding GRADE eases its rows shut and dispatches nothing");
            }
        }
        {
            // the graph under the clip: drag a key, the key menu, the typed sides
            Rig r(1440, 900, withCurve);
            r.app->setTab(1);
            r.settle();
            auto tl = r.app->edit().timeline();
            tl->setKeysShown(true);
            r.settle();
            auto kl = tl->keyLane();
            kl->select("s_day01.basic.exposure");
            r.settle();
            auto g = kl->graph();
            Point k1 = world(*g, g->keyPoint(1).x, g->keyPoint(1).y);
            r.svc.lines.clear();
            r.drag(k1.x, k1.y, k1.x, k1.y - 10.0);
            const std::string set = withPrefix(r.svc, "key set s_day01.basic.exposure --at 5 --value ");
            CHECK(!set.empty() && set.find("--to") == std::string::npos, "dragging a key up dispatched key set … --at 5 --value <higher>");
            r.settle();
            k1 = world(*g, g->keyPoint(0).x, g->keyPoint(0).y);
            r.app->pointer(0, k1.x, k1.y, 2, r.now);
            r.app->pointer(2, k1.x, k1.y, 2, r.now + 40.0);
            r.pump(250);
            auto cm = r.app->edit().contextMenu();
            std::string labels;
            for (int i = 0; i < cm->itemCount(); ++i) labels += cm->item(i).label + "|";
            CHECK(cm->isOpen() && labels.find("Ease In") != std::string::npos && labels.find("Speed & Influence") != std::string::npos &&
                      labels.find("Delete Key") != std::string::npos, "a keyframe's menu: presets, Speed & Influence…, Delete Key");
            auto pick = [&](const std::string &label) {
                for (int i = 0; i < cm->itemCount(); ++i)
                    if (cm->item(i).label.rfind(label, 0) == 0) { const Point ip = centre(*cm, cm->itemRect(i)); r.click(ip.x, ip.y); r.pump(32); return; }
            };
            r.svc.lines.clear();
            pick("Ease Out");
            CHECK(hasLine(r.svc, "key set s_day01.basic.exposure --at 2 --ease ease-out"), "Ease Out dispatched key set … --ease ease-out");
            r.settle();
            r.app->pointer(0, k1.x, k1.y, 2, r.now);
            r.app->pointer(2, k1.x, k1.y, 2, r.now + 40.0);
            r.pump(250);
            pick("Speed & Influence");
            r.settle();
            auto np = r.app->edit().namePrompt();
            CHECK(np->isOpen() && np->fieldCount() == 5, "Speed & Influence… opens a dialog of five numbers");
            np->fieldAt(3)->text = "2.5";
            r.svc.lines.clear();
            np->confirm();
            const std::string typed = withPrefix(r.svc, "key set s_day01.basic.exposure --at 2");
            CHECK(typed.find("--speed-out 2.5") != std::string::npos && typed.find("--speed-in") == std::string::npos,
                  "only the changed number is sent (an untouched side stays linear): --speed-out 2.5");
            // a clip property keys the CLIP
            kl->select("c1.opacity", false);   // into view: the CLIP section has six rows now (R-EDT-3's Speed)
            r.settle();
            r.svc.lines.clear();
            const int op = kl->rowOf("c1.opacity");
            const Point q = centre(*kl, kl->diamondRect(op));
            r.click(q.x, q.y);
            CHECK(hasLine(r.svc, "key add c1.opacity --at 3"), "the Opacity diamond keys the clip at the playhead");
            // R-EDT-3: the clip's speed has a row too — a ramp is keyed like any clip curve
            kl->select("c1.speed", false);
            r.settle();
            r.svc.lines.clear();
            const int sp = kl->rowOf("c1.speed");
            CHECK(sp >= 0, "the CLIP section lists Speed");
            const Point qs = centre(*kl, kl->diamondRect(sp));
            r.click(qs.x, qs.y);
            CHECK(hasLine(r.svc, "key add c1.speed --at 3"), "…and its diamond keys a speed ramp at the playhead");
        }
        {
            // R-ANIM-7: two curves together, a box across both, a group shift, copy and paste
            Rig r(1440, 900, [](FakeService &s) {
                s.edit();
                s.m.selectedClip = "c1";
                s.m.playhead = 1.0;
                s.animate("s_day01.basic.exposure", {{2.0, -1.0}, {5.0, 2.0}});
                s.animate("s_day01.basic.contrast", {{3.0, 10.0}, {6.0, -20.0}});
            });
            r.app->setTab(1);
            r.settle();
            auto tl = r.app->edit().timeline();
            tl->setKeysShown(true);
            r.settle();
            auto kl = tl->keyLane();
            kl->select("s_day01.basic.exposure");
            kl->select("s_day01.basic.contrast", true);   // Ctrl-click: shown together
            r.settle();
            auto g = kl->graph();
            CHECK(g->curveCount() == 2 && g->selectedAddress() == "s_day01.basic.contrast", "two properties drawn together, the last chosen in front");
            // a box from the plot's top-left to past the first key of each curve
            const Rect pr = g->plotRect();
            // dragged from the right to past the plot's left edge (exposure's first key sits ON that edge)
            const double xMid = (g->keyPointOf(0, 0).x + g->keyPointOf(0, 1).x) * 0.5;
            const Point b0 = world(*g, std::min(xMid, g->keyPointOf(1, 1).x - 4.0), pr.bottom() - 2.0);
            const Point b1 = world(*g, pr.x - 6.0, pr.y + 2.0);
            r.drag(b0.x, b0.y, b1.x, b1.y);
            std::printf("      box selected %d: %s\n", g->selectionCount(), g->selectionList().c_str());
            CHECK(g->selectionCount() == 2 && g->isSelected(0, 0) && g->isSelected(1, 0), "a box selects the keys inside it, across both curves");
            // drag one of them: both move in time together — one command
            const Point k = world(*g, g->keyPointOf(1, 0).x, g->keyPointOf(1, 0).y);
            r.svc.lines.clear();
            r.drag(k.x, k.y, k.x + 25.0, k.y);
            const std::string sh = withPrefix(r.svc, "key shift --keys ");
            CHECK(!sh.empty() && sh.find("s_day01.basic.exposure@2") != std::string::npos && sh.find("s_day01.basic.contrast@3") != std::string::npos &&
                      sh.find("--by ") != std::string::npos, "dragging a selected key shifts the whole selection: key shift --keys … --by dt");
            r.settle();
            // Ctrl+C copies the selection; Ctrl+V pastes at the playhead; the empty plot offers paste
            r.svc.lines.clear();
            ctrlKey(r, 'C');
            CHECK(!withPrefix(r.svc, "key copy --keys ").empty(), "Ctrl+C in the key lane copies the selected keys");
            ctrlKey(r, 'V');
            CHECK(hasLine(r.svc, "key paste --at 3"), "Ctrl+V pastes them at the playhead (the clip's footage time 3)");
            const Point e = world(*g, pr.right() - 6.0, pr.y + pr.h * 0.5);
            r.app->pointer(0, e.x, e.y, 2, r.now);
            r.app->pointer(2, e.x, e.y, 2, r.now + 40.0);
            r.pump(250);
            auto cm = r.app->edit().contextMenu();
            std::string labels;
            for (int i = 0; i < cm->itemCount(); ++i) labels += cm->item(i).label + "|";
            CHECK(cm->isOpen() && labels.find("Paste Keys at Playhead") != std::string::npos && labels.find("Paste Keys Here") != std::string::npos,
                  "right-click on empty plot offers paste at the playhead or there");
            cm->close();
        }
        {
            // a clip chosen on another track: the open lane travels there, eased — the tracks between slide
            Rig r(1440, 900, withCurve);
            r.app->setTab(1);
            r.settle();
            auto tl = r.app->edit().timeline();
            tl->setKeysShown(true);
            r.settle();
            const std::string from = tl->expandedTrack();
            std::string other;
            for (const auto &c : r.svc.m.clips)
                if (!c.audio && c.track != from && other.empty()) { other = c.id; r.svc.m.selectedClip = c.id; }
            ++r.svc.m.revision;
            const double l0 = tl->expandedLaneLive();
            const double mid = firstMoved(r, [&] { return tl->expandedLaneLive(); }, l0);
            CHECK(!other.empty() && tl->expandedTrack() != from && std::fabs(mid - l0) > 1e-6 && std::fabs(mid - std::round(mid)) > 1e-6,
                  "choosing a clip on another track moves the open lane there, eased");
            r.settle();
            CHECK(std::fabs(tl->keyLaneRect().y - tl->laneRect(tl->expandedTrack()).bottom()) < 1e-6, "…to sit under that clip's track");
        }
        {
            // R-ANIM-8: the lane is resized at its bottom edge, the height saved, and never hides the clip's track
            Rig r(1440, 900, [](FakeService &s) { s.edit(); s.m.selectedClip = "c1"; });
            r.app->setTab(1);
            r.settle();
            auto tl = r.app->edit().timeline();
            tl->setKeysShown(true);
            r.settle();
            const double h0 = tl->keyLaneH();
            const Rect gb = tl->keyLaneGrabRect();
            CHECK(std::fabs(gb.y - tl->keyLaneRect().bottom()) < 1e-6, "the grip is the lane's bottom edge");
            const Point a0 = centre(*tl, gb);
            r.svc.lines.clear();
            r.drag(a0.x, a0.y, a0.x, a0.y + 40.0);
            CHECK(tl->keyLaneH() > h0 + 30.0, "dragging the lane's bottom edge down makes it taller (direct manipulation)");
            CHECK(!withPrefix(r.svc, "settings set keyLaneHeight=").empty(), "…and the height is saved: settings set keyLaneHeight=<px>");
            r.svc.m.settings.keyLaneHeight = 600;   // more than the window can give
            ++r.svc.m.revision;
            r.settle();
            const Rect trk = tl->laneRect(tl->expandedTrack());
            CHECK(trk.y >= arstro::interstellar_v1::shell::rulerH() - 1e-6 && tl->keyLaneRect().y >= trk.bottom() - 1e-6,
                  "however tall it is asked to be, the clip's track still shows above its properties");
        }
        {
            Rig r(1024, 640, [](FakeService &s) { s.edit(); s.m.selectedClip = "c1"; });
            r.app->setTab(1);
            r.settle();
            auto tl = r.app->edit().timeline();
            tl->setKeysShown(true);
            r.settle();
            const Rect trk = tl->laneRect(tl->expandedTrack());
            std::printf("      1024: track y %.1f h %.1f, lane %.1f..%.1f (h %.1f), deck h %.1f\n", trk.y, trk.h, tl->keyLaneRect().y, tl->keyLaneRect().bottom(), tl->keyLaneH(), tl->height.value());
            CHECK(trk.y >= arstro::interstellar_v1::shell::rulerH() - 1e-6 && tl->keyLaneH() >= 80.0 - 1e-6 &&
                      tl->keyLaneRect().bottom() <= tl->height.value() + 1e-6,
                  "at 1024x640 opening scrolls the clip's track and its properties into view, at a usable height");
        }
        {
            // R-ANIM-6: a shape — the lane lists it; the graph keys it on a row, in time only; Grade edits it
            Rig r(1440, 900, [](FakeService &s) {
                s.edit();
                s.m.selectedClip = "c1";
                s.m.playhead = 1.0;
                s.animateShape("s_day01.grade.grade1", {{2.0, "0,0,0"}, {5.0, "120,60,10"}});
            });
            r.app->setTab(1);
            r.settle();
            auto tl = r.app->edit().timeline();
            tl->setKeysShown(true);
            r.settle();
            auto kl = tl->keyLane();
            CHECK(kl->rowOf("s_day01.curve.curve") >= 0 && kl->rowOf("s_day01.xform.crop") >= 0 && kl->rowOf("s_day01.grade.grade1") >= 0,
                  "the lane lists the source's curves, wheels and crop");
            kl->select("s_day01.grade.grade1");
            r.settle();
            auto g = kl->graph();
            CHECK(g->shapeShown() && std::fabs(g->keyPoint(0).y - g->keyPoint(1).y) < 1e-9, "a shape draws as a row of keys");
            const Point k1 = world(*g, g->keyPoint(1).x, g->keyPoint(1).y);
            r.svc.lines.clear();
            r.drag(k1.x, k1.y, k1.x, k1.y - 20.0);
            CHECK(withPrefix(r.svc, "key set").empty(), "dragging a shape key up and down changes nothing — a shape has no value axis");
            r.settle();
            r.drag(k1.x, k1.y, k1.x - 30.0, k1.y);
            const std::string mv = withPrefix(r.svc, "key set s_day01.grade.grade1 --at 5 --to ");
            CHECK(!mv.empty() && mv.find("--value") == std::string::npos, "dragging it sideways moves it in time only");
            r.settle();
            const Point k0 = world(*g, g->keyPoint(0).x, g->keyPoint(0).y);
            r.app->pointer(0, k0.x, k0.y, 2, r.now);
            r.app->pointer(2, k0.x, k0.y, 2, r.now + 40.0);
            r.pump(250);
            auto cm = r.app->edit().contextMenu();
            int edit = -1;
            bool speeds = false;
            for (int i = 0; i < cm->itemCount(); ++i)
            {
                if (cm->item(i).label == "Edit in Grade at This Key") edit = i;
                speeds = speeds || cm->item(i).label.rfind("Speed", 0) == 0;
            }
            CHECK(edit >= 0 && !speeds, "a shape key's menu: Edit in Grade at This Key, no typed speeds");
            r.svc.lines.clear();
            const Point ip = centre(*cm, cm->itemRect(edit));
            r.click(ip.x, ip.y);
            r.pump(32);
            CHECK(hasLine(r.svc, "rack frame s_day01 --at 2") && r.app->tab() == EditScreen::Grade,
                  "…which stands Grade on the key's source frame (rack frame s_day01 --at 2) and shows Grade");
        }
    }

    void testPluginList()
    {
        std::printf("the image-processing list (plugins)\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); });
        r.settle();
        auto gi = r.app->edit().gradeInspector();
        auto pl = gi->plugins();
        CHECK(pl->rowCount() == 3, "s_day01's stack: Cosmo, then its two effects");
        // Cosmo's switch is the node's bypass; Cosmo's Mix is the node's weight
        r.svc.lines.clear();
        Point p = centre(*pl, pl->switchRect(0));
        r.click(p.x, p.y);
        CHECK(hasLine(r.svc, "set s_day01.bypass=1"), "Cosmo's switch dispatched set s_day01.bypass=1");
        auto mix = gi->cosmoMix();
        const Point m0 = world(*mix, mix->width.value() * 0.55, mix->height.value() * 0.5);
        const Point m1 = world(*mix, mix->width.value() * 0.95, mix->height.value() * 0.5);
        r.svc.lines.clear();
        r.drag(m0.x, m0.y, m1.x, m1.y);
        CHECK(!withPrefix(r.svc, "set s_day01.weight=").empty(), "Cosmo's Mix slider dispatched set s_day01.weight=<0..1>");
        // an effect's switch is its own
        r.svc.lines.clear();
        p = centre(*pl, pl->switchRect(2));
        r.click(p.x, p.y);
        CHECK(hasLine(r.svc, "set ef_2.enabled=1"), "an effect's switch dispatched set ef_2.enabled=1");
        // selecting an effect cross-fades to its parameters
        p = centre(*pl, pl->rowRect(1));
        r.click(p.x - 30.0, p.y);
        CHECK(pl->selected() == "ef_1", "clicking the row selects ef_1 (presentation, no command)");
        const double ff = firstMoved(r, [&] { return gi->pluginFade(); }, 0.0);
        CHECK(strictlyBetween(ff, 0.0, 1.0), "the panel CROSS-FADES from Cosmo's tabs to the effect's parameters");
        r.settle();
        auto ep = gi->effectPanel();
        auto radius = ep->slider("radius");
        CHECK(ep->effect() == "ef_1" && radius && radius->visible, "the effect panel shows ef_1's Radius");
        r.svc.lines.clear();
        const Point q0 = world(*radius, radius->width.value() * 0.6, radius->height.value() * 0.5);
        const Point q1 = world(*radius, radius->width.value() * 0.8, radius->height.value() * 0.5);
        r.drag(q0.x, q0.y, q1.x, q1.y);
        CHECK(!withPrefix(r.svc, "set ef_1.radius=").empty(), "dragging Radius dispatched set ef_1.radius=<px>");
        // R-FX-5 (amended): every effect of the node as a collapsible section — ef_1 open, ef_2 shut
        CHECK(ep->sectionCount() == 2 && ep->sectionId(0) == "ef_1" && ep->sectionId(1) == "ef_2",
              "the panel lists the node's two effects as sections, in stack order");
        CHECK(ep->openAmount("ef_1") > 0.999 && ep->openAmount("ef_2") < 0.001, "the selected effect's section is open, the other shut");
        r.svc.lines.clear();
        Point h = centre(*ep, ep->headerRect(1));
        r.click(h.x, h.y);
        const double o2 = firstMoved(r, [&] { return ep->openAmount("ef_2"); }, 0.0);
        CHECK(strictlyBetween(o2, 0.0, 1.0), "clicking a section's header opens it, eased");
        r.settle();
        CHECK(ep->sliderOf("ef_2", "length") && ep->sliderOf("ef_2", "length")->visible, "…showing its parameters");
        h = centre(*ep, ep->headerRect(0));
        r.click(h.x, h.y);
        const double o1 = firstMoved(r, [&] { return ep->openAmount("ef_1"); }, 1.0);
        CHECK(strictlyBetween(o1, 0.0, 1.0), "clicking an open header collapses it, eased");
        r.settle();
        CHECK(!ep->slider("radius")->visible && r.svc.lines.empty(), "a collapsed section's rows are culled; folding dispatched nothing");
        // + Add: the catalog, then the new effect is selected
        r.svc.lines.clear();
        p = centre(*pl, pl->addRect());
        r.click(p.x, p.y);
        r.pump(250);
        auto cm = r.app->edit().contextMenu();
        std::string labels;
        for (int i = 0; i < cm->itemCount(); ++i) labels += cm->item(i).label + "|";
        std::printf("      %s\n", labels.c_str());
        CHECK(cm->isOpen() && labels.find("Zoom Blur") != std::string::npos && labels.find("Spin Blur") != std::string::npos,
              "+ Add opens the catalog — every blur kind");
        int zi = -1;
        for (int i = 0; i < cm->itemCount(); ++i) if (cm->item(i).label == "Zoom Blur") zi = i;
        if (zi >= 0) { const Point ip = centre(*cm, cm->itemRect(zi)); r.click(ip.x, ip.y); r.pump(32); }
        CHECK(hasLine(r.svc, "effect add s_day01 --type blur.zoom"), "…and picking one dispatched effect add s_day01 --type blur.zoom");
        r.settle();
        CHECK(pl->rowCount() == 4 && ep->effect() == pl->selected() && !pl->selected().empty() && ep->slider("amount"),
              "the new effect is in the list and selected, its Amount shown");
        // the list's height EASED to make room (no jump of the panel under it)
        // remove: the × on hover
        r.svc.lines.clear();
        const Rect rr = pl->removeRect(1);
        p = centre(*pl, rr);
        r.move(p.x, p.y);
        r.pump(64);
        r.click(p.x, p.y);
        CHECK(hasLine(r.svc, "effect remove ef_1"), "the row's × dispatched effect remove ef_1");
        r.settle();
        // right-click: reorder
        p = centre(*pl, pl->rowRect(1));
        r.app->pointer(1, p.x, p.y, 0, r.now);
        r.app->pointer(0, p.x, p.y, 2, r.now);
        r.app->pointer(2, p.x, p.y, 2, r.now + 40.0);
        r.pump(250);
        labels.clear();
        for (int i = 0; i < cm->itemCount(); ++i) labels += cm->item(i).label + "|";
        CHECK(cm->isOpen() && labels.find("Move Down") != std::string::npos && labels.find("Remove") != std::string::npos,
              "right-clicking an effect row offers Move Down and Remove");
    }

    /** R-UI-3 (amended), R-UI-11: Grade drops the transport and its monitor shows the Grade target
     *  alone at its reference frame; the capture button (monitor caption in Grade, beside ▶▶ on the
     *  transport) opens Copy Frame / Save Frame…. */
    void testCaptureAndGradeMonitor()
    {
        std::printf("grade without a transport, the capture button\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); });
        r.settle();
        auto &e = r.app->edit();
        auto mon = e.monitor();
        CHECK(e.transportAmount() < 1e-9 && !e.transport()->visible, "Grade at rest: no transport");
        const double gradeMonH = mon->height.value();
        // the monitor shows the Grade target ALONE, at its reference frame (t < 0 = "its own")
        bool asked = false;
        for (const auto &c : r.svc.sourceCalls) asked = asked || (c.first == "s_day01" && c.second < 0);
        CHECK(asked, "the Grade monitor asked renderSource for the target at its reference frame");
        std::printf("      caption: %s\n", mon->caption().c_str());
        CHECK(mon->caption().find("s_day01") != std::string::npos && mon->caption().find("ref 00:00:02:12") != std::string::npos,
              "its caption names the source and the reference frame's timecode (2.5 s at the source's 24 fps)");
        // Grade → Cut: the transport eases in and the monitor gives up its room through the LIVE value
        r.app->setTab(1);
        const double fa = firstMoved(r, [&] { return e.transportAmount(); }, 0.0);
        CHECK(strictlyBetween(fa, 0.0, 1.0), "the transport EASES in leaving Grade (first frame between)");
        r.frame();   // the app lays out before it advances: geometry reads the amount one frame on
        CHECK(strictlyBetween(mon->height.value(), gradeMonH - shell::transportH(), gradeMonH),
              "…and the monitor's height follows the live amount, not the target");
        r.settle();
        CHECK(std::fabs(mon->height.value() - (gradeMonH - shell::transportH())) < 0.5 && e.transport()->visible,
              "Cut at rest: the transport is back under a shorter monitor");
        CHECK(mon->captureAmount() < 1e-9, "the monitor's own capture button is Grade's only");

        // the transport's capture button, beside ▶▶ → the menu; Copy Frame copies the TIMELINE ("")
        auto tp = e.transport();
        const Rect b2 = tp->buttonRect(2), b3 = tp->buttonRect(3);
        CHECK(b3.x > b2.right() - 0.5 && std::fabs(b3.y - b2.y) < 0.5, "the capture button sits next to ▶▶ (next cut)");
        Point p = centre(*tp, b3);
        r.click(p.x, p.y);
        r.pump(250);
        auto cm = e.contextMenu();
        CHECK(cm->isOpen() && cm->itemCount() == 2 && cm->item(0).label == "Copy Frame" && cm->item(1).label == "Save Frame...",
              "clicking it opens Copy Frame / Save Frame…");
        Point ip = centre(*cm, cm->itemRect(0));
        r.click(ip.x, ip.y);
        r.pump(64);
        CHECK(r.svc.copies.size() == 1 && r.svc.copies[0].empty(), "Copy Frame in Cut copies the timeline at the playhead");
        CHECK(e.toastAmount() > 0.0 && !e.toastIsError(), "…and says so, without the refusal's red");

        // back in Grade: the caption's capture button fades in; Save Frame… names the source
        r.app->setTab(0);
        const double fc = firstMoved(r, [&] { return mon->captureAmount(); }, 0.0);
        CHECK(strictlyBetween(fc, 0.0, 1.0), "the monitor's capture button FADES in on Grade");
        r.settle();
        bool picked = false;
        r.app->onPickFrameToSave = [&] { picked = true; };
        const Rect cr = mon->captureRect();
        CHECK(cr.w > 0 && cr.h > 0, "Grade at rest: the capture button is on the monitor caption");
        p = centre(*mon, cr);
        r.click(p.x, p.y);
        r.pump(250);
        CHECK(cm->isOpen(), "the caption's capture button opens the same menu");
        ip = centre(*cm, cm->itemRect(1));
        r.click(ip.x, ip.y);
        r.pump(64);
        CHECK(picked, "Save Frame… asks the host for a path");
        r.app->frameSavePicked("/shots/frame one.png");
        CHECK(hasLine(r.svc, "capture --out \"/shots/frame one.png\" --source s_day01"),
              "…and the answer dispatches capture --out <path> --source <the Grade target>");
        // Copy fails → the refusal, in red
        r.svc.copyFails = true;
        r.click(p.x, p.y);
        r.pump(250);
        ip = centre(*cm, cm->itemRect(0));
        r.click(ip.x, ip.y);
        r.pump(64);
        CHECK(e.toastIsError(), "a failed copy is SAID, as a refusal");
    }

    /** R-RACK-3 (amended): the reference frame is a fast-seek slider over the whole source that
     *  PREVIEWS in the monitor while dragged and commits on release; ‹ › step one source frame. */
    void testRefFrameSlider()
    {
        std::printf("the reference-frame slider: seek, preview, step\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); });
        r.settle();
        auto deck = r.app->edit().gradeDeck();
        auto mon = r.app->edit().monitor();
        CHECK(near(deck->sourceDuration(), 12.0) && near(deck->sourceFps(), 24.0),
              "the slider spans the WHOLE source (rack[].mediaDuration), at the source's own rate");
        const Rect tr = deck->frameTrackRect();
        const Point f0 = world(*deck, tr.x + tr.w * 0.25, tr.y + tr.h * 0.5), f1 = world(*deck, tr.x + tr.w * 0.75, tr.y + tr.h * 0.5);
        r.svc.lines.clear();
        r.press(f0.x, f0.y);
        r.frame();
        for (int k = 1; k <= 6; ++k) { r.dragTo(f0.x + (f1.x - f0.x) * k / 6.0, f0.y); r.frame(); }
        r.frame();
        CHECK(deck->previewing(), "mid-drag the slider is previewing");
        CHECK(withPrefix(r.svc, "rack frame").empty(), "…and nothing is committed while dragging");
        const auto last = r.svc.sourceCalls.empty() ? std::make_pair(std::string(), -1.0) : r.svc.sourceCalls.back();
        std::printf("      preview asked %s @ %.4f; caption %s\n", last.first.c_str(), last.second, mon->caption().c_str());
        CHECK(last.first == "s_day01" && std::fabs(last.second - 9.0) < 0.05, "the monitor asked renderSource for the source AT the dragged time (75% of 12 s)");
        CHECK(mon->caption().find("seek ") != std::string::npos, "…and its caption says it is seeking, not the committed ref");
        r.releaseAt(f1.x, f1.y);
        r.frame();
        const std::string fl = withPrefix(r.svc, "rack frame s_day01 --at ");
        std::printf("      %s\n", fl.c_str());
        CHECK(!fl.empty() && std::fabs(std::stod(fl.substr(fl.rfind(' ') + 1)) - 9.0) < 0.05, "release commits rack frame s_day01 --at 9");
        CHECK(!deck->previewing(), "…and the preview ends");
        r.settle();
        CHECK(r.svc.sourceCalls.back().second < 0 && mon->caption().find("ref 00:00:09:00") != std::string::npos,
              "at rest the monitor shows the committed reference frame again");
        // ‹ › one source frame, committed at once — the exact frame
        r.svc.lines.clear();
        Point sp = centre(*deck, deck->stepRect(1));
        r.click(sp.x, sp.y);
        r.pump(32);
        CHECK(hasLine(r.svc, "rack frame s_day01 --at 9.041667"), "› steps one frame forward at 24 fps and commits it");
        r.svc.lines.clear();
        r.svc.dispatch("rack select s_day02", gErr);   // a 50 fps source, ref at 1.0 s
        r.settle();
        r.svc.lines.clear();
        sp = centre(*deck, deck->stepRect(-1));
        r.click(sp.x, sp.y);
        r.pump(32);
        CHECK(hasLine(r.svc, "rack frame s_day02 --at 0.98"), "‹ steps one frame back at the SOURCE's 50 fps, not the project's 24");
        // the marker catches up to a committed step, eased
        const double s0 = deck->shownFrame();
        const double fm = firstMoved(r, [&] { return deck->shownFrame(); }, s0);
        CHECK(strictlyBetween(fm, 0.98, s0), "the marker EASES to the stepped frame");
    }

    /** R-RACK-5 (amended): a variant is offered wherever a source is — Ctrl+D, the source bin's menu. */
    void testVariants()
    {
        std::printf("variants: Ctrl+D and the source bin's menu\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); });
        r.settle();
        r.svc.lines.clear();
        ctrlKey(r, 'D');
        CHECK(hasLine(r.svc, "rack duplicate s_day01"), "Ctrl+D duplicates the Grade target as a variant");
        r.svc.lines.clear();
        r.svc.dispatch("rack select gr1", gErr);
        r.settle();
        r.svc.lines.clear();
        ctrlKey(r, 'D');
        CHECK(r.svc.lines.empty(), "…and does nothing on a group (a group is not a source)");
        r.app->setTab(1);
        r.settle();
        auto bin = r.app->edit().sourceBin();
        const Point p = centre(*bin, bin->rowRect(1));
        r.app->pointer(1, p.x, p.y, 0, r.now);
        r.app->pointer(0, p.x, p.y, 2, r.now);
        r.app->pointer(2, p.x, p.y, 2, r.now + 40.0);
        r.pump(250);
        auto cm = r.app->edit().contextMenu();
        bool dup = false;
        for (int i = 0; i < cm->itemCount(); ++i) dup = dup || cm->item(i).label.rfind("Duplicate as Variant", 0) == 0;
        CHECK(cm->isOpen() && dup, "right-clicking a source-bin row opens the rack's menu, Duplicate as Variant in it");
    }

    /** Two clicks a frame apart: the recognizer's double-click. */
    void dblClick(Rig &r, double x, double y)
    {
        r.app->pointer(1, x, y, 0, r.now);
        r.app->pointer(0, x, y, 0, r.now);
        r.app->pointer(2, x, y, 0, r.now + 40.0);
        r.frame();
        r.app->pointer(0, x, y, 0, r.now);
        r.app->pointer(2, x, y, 0, r.now + 40.0);
        r.frame();
    }

    /** R-UI-14: the Cut tab reaches every cut operation — drop a source, roll, slip, ripple delete,
     *  markers, copy/cut/paste, the clip and lane menus. */
    void testCutEditing()
    {
        std::printf("cutting like an editor\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); });
        r.app->setTab(EditScreen::Cut);
        r.settle();
        auto tl = r.app->edit().timeline();
        auto bin = r.app->edit().sourceBin();
        // drag a source from the bin onto V2 — the ghost eases in, snapped, then `clip add` on release
        const Rect v2 = tl->laneRect("v2");
        const Point from = centre(*bin, bin->rowRect(1));
        const Point to = world(*tl, tl->timeToX(13.0), v2.y + v2.h * 0.5);
        r.svc.lines.clear();
        r.press(from.x, from.y);
        r.frame();
        for (int k = 1; k <= 7; ++k) { r.dragTo(from.x + (to.x - from.x) * k / 8.0, from.y + (to.y - from.y) * k / 8.0); r.frame(); }
        CHECK(tl->dropAmount() < 1e-9, "no ghost while the pointer is still over the bin");
        r.dragTo(to.x, to.y);   // into the lanes
        const double ghostFirst = firstMoved(r, [&] { return tl->dropAmount(); }, 0.0);
        CHECK(bin->draggingSource() && tl->dropTrack() == "v2", "dragging a source over V2 targets V2");
        std::printf("      ghost first frame %.3f\n", ghostFirst);
        CHECK(strictlyBetween(ghostFirst, 0.0, 1.0), "the drop ghost EASES in over the lanes (first frame between)");
        r.releaseAt(to.x, to.y);
        r.frame();
        const std::string add = withPrefix(r.svc, "clip add ");
        std::printf("      %s\n", add.c_str());
        CHECK(add.rfind("clip add --track v2 --src s_day02 --in 0 --at ", 0) == 0 && std::fabs(std::stod(add.substr(add.rfind(' ') + 1)) - 13.0) < 0.2,
              "release dispatched clip add --track v2 --src s_day02 --in 0 --at ≈13 (the rest of the source, the service's default)");
        r.settle();
        CHECK(tl->dropAmount() < 1e-9, "…and the ghost leaves");
        // over an audio lane: a video source cannot land there
        const Rect a1 = tl->laneRect("a1");
        r.svc.lines.clear();
        r.press(from.x, from.y);
        r.frame();
        const Point ta = world(*tl, tl->timeToX(3.0), a1.y + a1.h * 0.5);
        for (int k = 1; k <= 6; ++k) { r.dragTo(from.x + (ta.x - from.x) * k / 6.0, from.y + (ta.y - from.y) * k / 6.0); r.frame(); }
        CHECK(tl->dropTrack() == "!", "over an audio lane the ghost says it cannot land");
        r.releaseAt(ta.x, ta.y);
        r.frame();
        CHECK(withPrefix(r.svc, "clip add").empty(), "…and nothing is added");
        r.settle();

        {
            // an EMPTY timeline: the drop makes the video track, then the clip
            Rig e(1440, 900, [](FakeService &f) { f.edit(); f.m.clips.clear(); f.m.transitions.clear(); f.m.tracks.clear(); f.m.selectedClip.clear(); });
            e.app->setTab(EditScreen::Cut);
            e.settle();
            auto etl = e.app->edit().timeline();
            auto ebin = e.app->edit().sourceBin();
            const Point ef = centre(*ebin, ebin->rowRect(0));
            const Rect lr = etl->lanesRect();
            const Point et = world(*etl, etl->timeToX(1.0), lr.y + 20.0);
            e.svc.lines.clear();
            e.press(ef.x, ef.y);
            e.frame();
            for (int k = 1; k <= 6; ++k) { e.dragTo(ef.x + (et.x - ef.x) * k / 6.0, ef.y + (et.y - ef.y) * k / 6.0); e.frame(); }
            CHECK(etl->dropTrack().empty(), "over an empty timeline the ghost offers a new video track");
            e.releaseAt(et.x, et.y);
            e.frame();
            CHECK(e.svc.lines.size() >= 2 && e.svc.lines[e.svc.lines.size() - 2] == "track add --kind video" &&
                      e.svc.lines.back().rfind("clip add --track v11 --src s_day01 --in 0 --at ", 0) == 0,
                  "release made the video track, then added the clip on it");
        }

        // Alt-drag the c1|c2 cut: ROLL
        const Rect c1 = tl->clipRect("c1");
        const Point cut = world(*tl, c1.right() - 1.0, c1.y + c1.h * 0.5);
        r.svc.lines.clear();
        r.app->pointer(1, cut.x, cut.y, 0, r.now, true);
        r.app->pointer(0, cut.x, cut.y, 0, r.now, true);
        r.frame();
        for (int k = 1; k <= 5; ++k) { r.app->pointer(1, cut.x + 6.0 * k, cut.y, 0, r.now, true); r.frame(); }
        CHECK(tl->dragHint() == "roll", "Alt on a cut between touching clips is a ROLL, and says so");
        CHECK(tl->clipRect("c2").x > c1.right() + 10.0, "…both clips move their shared edge live");
        r.app->pointer(2, cut.x + 30.0, cut.y, 0, r.now, true);
        r.frame();
        CHECK(!withPrefix(r.svc, "clip roll c1 --at ").empty(), "release dispatched clip roll c1 --at <t>");
        r.settle();
        // Alt-drag a body: SLIP
        const Rect c5 = tl->clipRect("c5");
        const Point body = world(*tl, c5.x + c5.w * 0.5, c5.y + c5.h * 0.5);
        r.svc.lines.clear();
        r.app->pointer(1, body.x, body.y, 0, r.now, true);
        r.app->pointer(0, body.x, body.y, 0, r.now, true);
        r.frame();
        for (int k = 1; k <= 5; ++k) { r.app->pointer(1, body.x + 8.0 * k, body.y, 0, r.now, true); r.frame(); }
        CHECK(tl->dragHint().rfind("slip -", 0) == 0, "Alt on a clip's body is a SLIP (right = earlier material), and says so");
        CHECK(std::fabs(tl->clipRect("c5").x - c5.x) < 0.5, "…the clip itself does not move");
        r.app->pointer(2, body.x + 40.0, body.y, 0, r.now, true);
        r.frame();
        CHECK(!withPrefix(r.svc, "clip slip c5 --by -").empty(), "release dispatched clip slip c5 --by <negative dt>");
        r.settle();

        // keys: Shift+Delete ripples, M marks the playhead, Ctrl+C/X/V copy, cut, paste
        r.svc.lines.clear();
        {
            artboard::KeyEvent e;
            e.type = artboard::KeyEvent::Type::Down;
            e.keyCode = 46;
            e.shift = true;
            r.app->key(e);
        }
        CHECK(hasLine(r.svc, "clip delete c2 --ripple"), "Shift+Delete dispatched clip delete c2 --ripple");
        r.svc.lines.clear();
        r.key('M');
        CHECK(!withPrefix(r.svc, "marker add m1 --at ").empty(), "M dropped marker m1 at the playhead");
        r.svc.lines.clear();
        ctrlKey(r, 'C');
        CHECK(hasLine(r.svc, "clip copy c2"), "Ctrl+C on the Cut tab copies the selected CLIP");
        r.svc.lines.clear();
        ctrlKey(r, 'X');
        CHECK(hasLine(r.svc, "clip copy c2") && hasLine(r.svc, "clip delete c2"), "Ctrl+X copies then deletes");
        r.svc.lines.clear();
        ctrlKey(r, 'V');
        CHECK(hasLine(r.svc, "clip paste"), "Ctrl+V pastes at the playhead");

        // right-click a clip: the cut menu
        const Rect c3 = tl->clipRect("c3");
        const Point p3 = world(*tl, c3.x + c3.w * 0.5, c3.y + c3.h * 0.5);
        r.app->pointer(1, p3.x, p3.y, 0, r.now);
        r.app->pointer(0, p3.x, p3.y, 2, r.now);
        r.app->pointer(2, p3.x, p3.y, 2, r.now + 40.0);
        r.pump(250);
        auto cm = r.app->edit().contextMenu();
        std::string labels;
        for (int i = 0; i < cm->itemCount(); ++i) labels += cm->item(i).label + "|";
        std::printf("      %s\n", labels.c_str());
        CHECK(cm->isOpen() && labels.find("Ripple Delete") != std::string::npos && labels.find("Copy") != std::string::npos &&
                  labels.find("Speed 200%") != std::string::npos && labels.find("Show Source in Grade") != std::string::npos,
              "right-clicking a clip offers the cut operations (ripple delete, copy, speed, show source…)");
        int rip = -1;
        for (int i = 0; i < cm->itemCount(); ++i) if (cm->item(i).label.rfind("Ripple Delete", 0) == 0) rip = i;
        r.svc.lines.clear();
        if (rip >= 0) { const Point ip = centre(*cm, cm->itemRect(rip)); r.click(ip.x, ip.y); r.pump(32); }
        CHECK(hasLine(r.svc, "clip delete c3 --ripple"), "…its Ripple Delete dispatched clip delete c3 --ripple");
        // right-click empty lane space: tracks, marker, paste
        const Point empty = world(*tl, tl->timeToX(15.0), tl->laneRect("v2").y + 8.0);
        r.app->pointer(1, empty.x, empty.y, 0, r.now);
        r.app->pointer(0, empty.x, empty.y, 2, r.now);
        r.app->pointer(2, empty.x, empty.y, 2, r.now + 40.0);
        r.pump(250);
        labels.clear();
        for (int i = 0; i < cm->itemCount(); ++i) labels += cm->item(i).label + "|";
        CHECK(cm->isOpen() && labels.find("Add Video Track") != std::string::npos && labels.find("Paste Here") != std::string::npos,
              "right-clicking an empty lane offers Paste Here and new tracks");
        CHECK(labels.find("Place Timeline") == std::string::npos, "…and no Place Timeline while no timeline can go inside this one");
        // R-EDT-4: a timeline that can go inside this one (main: social30's base, which holds nothing of it)
        cm->close();
        r.svc.m.timelines[0].placeable = true;
        ++r.svc.m.revision;
        r.pump(300);
        r.app->pointer(1, empty.x, empty.y, 0, r.now);
        r.app->pointer(0, empty.x, empty.y, 2, r.now);
        r.app->pointer(2, empty.x, empty.y, 2, r.now + 40.0);
        r.pump(250);
        auto clickItem = [&](const std::string &prefix) {
            for (int i = 0; i < cm->itemCount(); ++i)
                if (cm->item(i).label.rfind(prefix, 0) == 0)
                {
                    const Point q = centre(*cm, cm->itemRect(i));
                    r.click(q.x, q.y);
                    r.pump(250);
                    return true;
                }
            return false;
        };
        CHECK(clickItem("Place Timeline Here..."), "a video lane offers Place Timeline Here... when one can be placed");
        labels.clear();
        for (int i = 0; i < cm->itemCount(); ++i) labels += cm->item(i).label + "|";
        CHECK(cm->isOpen() && labels == "main|", "…the list opens in its place, naming only what may go inside");
        r.svc.lines.clear();
        CHECK(clickItem("main") && hasLine(r.svc, "clip add --track v2 --src main --in 0 --at 15"),
              "…and picking one places it at the clicked time on that lane");
    }

    /** R-UI-15: the scopes measure what they say — on frames whose answer is known. */
    void testScopes()
    {
        using arstro::interstellar::Raster;
        std::printf("scopes: clipping, levels, waveform, vectorscope, the clip overlay\n");
        {
            Raster f;
            f.allocate(100, 50, 255);
            for (int y = 0; y < 50; ++y)
                for (int x = 50; x < 100; ++x) { uint8_t *p = &f.rgba[((size_t)y * 100 + x) * 4]; p[0] = p[1] = p[2] = 0; }
            const ScopeData d = scopesOf(f);
            CHECK(d.valid && std::fabs(d.clipHi[0] - 50.0) < 0.1 && std::fabs(d.clipLo[2] - 50.0) < 0.1,
                  "half white / half black: 50 % clipped at white, 50 % crushed at black, per channel");
            CHECK(d.levelsUsed() == 2, "…and it uses 2 of 256 levels");
            const Raster m = clipMaskOf(f);
            CHECK(m.rgba[0] == 255 && m.rgba[3] > 0 && m.rgba[((size_t)10 * 100 + 75) * 4 + 2] == 255,
                  "the clip mask is red over the white half, blue over the black half");
        }
        {
            Raster g;
            g.allocate(256, 20, 255);
            for (int y = 0; y < 20; ++y)
                for (int x = 0; x < 256; ++x) { uint8_t *p = &g.rgba[((size_t)y * 256 + x) * 4]; p[0] = p[1] = p[2] = (uint8_t)x; }
            const ScopeData d = scopesOf(g);
            CHECK(d.levelsUsed() == 256, "a full ramp uses all 256 levels");
            auto at = [&](const Raster &r, int x, int y) { return (int)r.rgba[((size_t)y * r.width + x) * 4 + 3]; };
            const int H = ScopeData::kScopeH, W = ScopeData::kScopeW;
            CHECK(at(d.waveform, 0, H - 1) > 0 && at(d.waveform, W - 1, 0) > 0 && at(d.waveform, 0, 0) == 0,
                  "the waveform of a ramp rises left to right: black at the bottom-left, white at the top-right");
        }
        {
            Raster red;
            red.allocate(40, 40, 255);
            for (size_t i = 0; i < red.rgba.size(); i += 4) { red.rgba[i] = 200; red.rgba[i + 1] = 30; red.rgba[i + 2] = 30; }
            const ScopeData d = scopesOf(red);
            int bx = -1, by = -1, best = 0;
            const int N = ScopeData::kVectorN;
            for (int y = 0; y < N; ++y)
                for (int x = 0; x < N; ++x)
                    if (d.vector.rgba[((size_t)y * N + x) * 4 + 3] > best) { best = d.vector.rgba[((size_t)y * N + x) * 4 + 3]; bx = x; by = y; }
            CHECK(bx < N / 2 && by < N / 2, "a red frame lands in the vectorscope's upper-left, where red sits");
            // R-UI-15 (amended): the RGB waveform lights each channel at its own level, in its own colour
            const int W = ScopeData::kScopeW, H = ScopeData::kScopeH;
            auto px = [&](int x, int y, int c) { return (int)d.waveformRgb.rgba[((size_t)y * W + x) * 4 + c]; };
            const int rRow = (255 - 200) * (H - 1) / 255, gRow = (255 - 30) * (H - 1) / 255;
            CHECK(px(W / 2, rRow, 0) > 0 && px(W / 2, rRow, 1) == 0 && px(W / 2, rRow, 2) == 0,
                  "RGB waveform: red's level shows in red only");
            CHECK(px(W / 2, gRow, 1) > 0 && px(W / 2, gRow, 2) > 0 && px(W / 2, gRow, 0) == 0,
                  "…green and blue share their level, drawn cyan (where they agree)");
            Raster grey;
            grey.allocate(40, 40, 255);
            for (size_t i = 0; i < grey.rgba.size(); i += 4) grey.rgba[i] = grey.rgba[i + 1] = grey.rgba[i + 2] = 128;
            const ScopeData dg = scopesOf(grey);
            const int row = (255 - 128) * (H - 1) / 255;
            const uint8_t *q = &dg.waveformRgb.rgba[((size_t)row * W + W / 2) * 4];
            CHECK(q[0] > 0 && q[0] == q[1] && q[1] == q[2], "a neutral grey draws white: the channels agree");
        }
        // the panel: modes cross-fade, the CLIP switch fades the monitor's overlay in
        Rig r(1440, 900, [](FakeService &s) { s.edit(); });
        r.settle();
        auto sp = r.app->edit().gradeInspector()->scopes();
        CHECK(sp->data().valid && sp->readout().find("levels") != std::string::npos, "the readout says the levels used");
        Point p = centre(*sp, sp->modeRect(ScopePanel::Waveform));
        r.click(p.x, p.y);
        const double wf = firstMoved(r, [&] { return sp->modeAmount(ScopePanel::Waveform); }, 0.0);
        CHECK(strictlyBetween(wf, 0.0, 1.0), "Waveform CROSS-FADES in (first frame between)");
        r.settle();
        p = centre(*sp, sp->clipRect());
        r.click(p.x, p.y);
        auto mon = r.app->edit().monitor();
        const double ca = firstMoved(r, [&] { return mon->clipAmount(); }, 0.0);
        CHECK(sp->clipWarning() && strictlyBetween(ca, 0.0, 1.0), "the CLIP switch fades the monitor's clip overlay in");
        // the waveform's Luma | RGB switch cross-fades the two plots
        p = centre(*sp, sp->waveSwitchRect(1));
        r.click(p.x, p.y);
        const double rg = firstMoved(r, [&] { return sp->waveRgbAmount(); }, 0.0);
        CHECK(sp->waveRgb() && strictlyBetween(rg, 0.0, 1.0), "RGB cross-fades the overlaid channel waveform in (first frame between)");
        r.settle();
        p = centre(*sp, sp->waveSwitchRect(0));
        r.click(p.x, p.y);
        r.settle();
        CHECK(!sp->waveRgb() && sp->waveRgbAmount() < 1e-6, "Luma brings the luma waveform back");
    }

    /** R-UI-13: Ctrl + wheel zooms the monitor about the pointer, eased; drag pans; double-click fits. */
    void testMonitorZoom()
    {
        std::printf("monitor zoom: Ctrl + wheel like cosmo\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); });
        r.settle();
        auto mon = r.app->edit().monitor();
        const Rect fr = mon->frameRect();
        const int edge0 = mon->wantedProxyEdge();
        // a point off-centre: the picture point under it must stay under it
        const Point at{fr.x + fr.w * 0.7, fr.y + fr.h * 0.3};
        const Rect ir0 = mon->imageRect();
        const double u = (at.x - ir0.x) / ir0.w, v = (at.y - ir0.y) / ir0.h;
        const Point wp = world(*mon, at.x, at.y);
        r.app->wheel(wp.x, wp.y, 0.0);   // a plain wheel: nothing
        r.app->wheel(wp.x, wp.y, 3.0, false);
        r.pump(64);
        CHECK(near(mon->zoomTarget(), 1.0), "a plain wheel over the monitor does nothing (cosmo)");
        r.app->wheel(wp.x, wp.y, 3.0, true);
        CHECK(std::fabs(mon->zoomTarget() - std::pow(1.15, 3.0)) < 1e-6, "Ctrl + wheel up three notches asks for 1.15^3");
        const double fz = firstMoved(r, [&] { return mon->zoomLive(); }, 1.0);
        CHECK(strictlyBetween(fz, 1.0, mon->zoomTarget()), "the zoom EASES (first frame between 1x and the target)");
        const Rect irMid = mon->imageRect();
        CHECK(std::fabs(irMid.x + u * irMid.w - at.x) < 0.5 && std::fabs(irMid.y + v * irMid.h - at.y) < 0.5,
              "…and mid-tween the picture point under the pointer stays under it (anchored to the live zoom)");
        r.settle();
        CHECK(mon->wantedProxyEdge() > edge0, "zoomed, the monitor asks for a larger proxy");
        // pan: the picture follows the pointer exactly, and stays covering the frame
        const Rect ir1 = mon->imageRect();
        const Point c0 = world(*mon, fr.x + fr.w * 0.5, fr.y + fr.h * 0.5);
        r.press(c0.x, c0.y);
        r.frame();
        r.dragTo(c0.x - 30.0, c0.y - 20.0);
        r.frame();
        r.dragTo(c0.x - 40.0, c0.y - 25.0);
        r.frame();
        const Rect ir2 = mon->imageRect();
        CHECK(mon->panning() && std::fabs((ir2.x - ir1.x) + 40.0) < 0.5 && std::fabs((ir2.y - ir1.y) + 25.0) < 0.5,
              "a drag pans the zoomed picture under the pointer (direct manipulation)");
        r.dragTo(c0.x - 4000.0, c0.y - 4000.0);
        r.frame();
        const Rect ir3 = mon->imageRect();
        CHECK(ir3.right() >= fr.right() - 0.5 && ir3.bottom() >= fr.bottom() - 0.5, "…clamped: the picture always covers the frame");
        r.releaseAt(c0.x - 4000.0, c0.y - 4000.0);
        r.frame();
        // double-click: back to fit, eased
        dblClick(r, c0.x, c0.y);
        CHECK(near(mon->zoomTarget(), 1.0), "a double-click asks for fit");
        const double fb = firstMoved(r, [&] { return mon->zoomLive(); }, mon->zoomLive());
        CHECK(fb > 1.0 + 1e-6, "…and eases back (not a snap)");
        r.settle();
        const Rect irEnd = mon->imageRect();
        CHECK(near(irEnd.x, fr.x, 0.5) && near(irEnd.w, fr.w, 0.5), "at rest the picture fits the frame again");
    }


    /** R-UI-12: groups like cosmo — grouping puts the items INSIDE a shut group; the tree's chevron
     *  opens it (eased); the strip shows one level, a double-click drills in, the breadcrumb goes up. */
    /** R-COLOR-2..4: a source's input colour from its menu, the working space from the Colour menu,
     *  the output colour in Deliver — HDR moving an 8-bit codec to H.265 10-bit. */
    void testColourManagement()
    {
        std::printf("colour management\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); });
        r.settle();
        auto rt = r.app->edit().rackTree();
        auto cm = r.app->edit().contextMenu();
        const std::string bind = r.svc.m.rack[1].bindName;
        auto rightClickRow = [&] {
            const Point p = centre(*rt, rt->rowRect(1));
            r.app->pointer(1, p.x - 40, p.y, 0, r.now);
            r.app->pointer(0, p.x - 40, p.y, 2, r.now);
            r.app->pointer(2, p.x - 40, p.y, 2, r.now + 40.0);
            r.pump(250);
        };
        auto itemStarting = [&](const std::string &prefix) {
            for (int i = 0; i < cm->itemCount(); ++i) if (cm->item(i).label.rfind(prefix, 0) == 0) return i;
            return -1;
        };
        rightClickRow();
        const int ic = itemStarting("Input Colour (Rec.709)");
        CHECK(cm->isOpen() && ic >= 0, "a source's menu names its input colour: Input Colour (Rec.709)...");
        Point q = centre(*cm, cm->itemRect(ic));
        r.click(q.x, q.y);
        r.pump(250);
        CHECK(cm->isOpen() && cm->itemCount() == (int)r.svc.m.colourInputs.size(), "…which opens the list of source spaces in its place");
        CHECK(cm->item(0).label.find("Rec.709") != std::string::npos && cm->item(0).label.rfind("\xE2\x80\xA2", 0) == 0,
              "the current space is marked");
        int slog = -1;
        for (int i = 0; i < cm->itemCount(); ++i) if (cm->item(i).label.find("S-Log3") != std::string::npos) slog = i;
        r.svc.lines.clear();
        q = centre(*cm, cm->itemRect(slog));
        r.click(q.x, q.y);
        r.pump(64);
        CHECK(!r.svc.lines.empty() && r.svc.lines.back() == "set " + bind + ".input=slog3", "choosing S-Log3 dispatches set <bind>.input=slog3");
        rightClickRow();
        CHECK(itemStarting("Input Colour (Sony S-Log3)") >= 0, "the menu now says what the source is");
        r.click(5, 5);
        r.pump(250);

        // the working space, from the Colour menu
        r.svc.lines.clear();
        CHECK(clickMenuItem(r, "Colour", "     ACEScct"), "the Colour menu offers the ACEScct working space");
        CHECK(!r.svc.lines.empty() && r.svc.lines.back() == "colour working acescct", "…and dispatches colour working acescct");
        r.pump(250);
        auto ms = r.app->edit().topBar()->menus();
        bool marked = false;
        for (int i = 0; i < ms->menuCount(); ++i)
            if (ms->menu(i).title == "Colour")
                for (const auto &it : ms->menu(i).items) marked = marked || it.label == "\xE2\x80\xA2  ACEScct Working Space";
        CHECK(marked, "the Colour menu marks the working space the model says");

        // Deliver: HDR needs 10 bits — an 8-bit codec moves to H.265 10-bit, and back again releases it
        r.app->setTab(2);
        r.settle();
        auto os = r.app->edit().outputSpec();
        auto clickSeg = [&](std::shared_ptr<arstro::cosmo_v2::SegmentedControl> sc, int i) {
            auto *b = dynamic_cast<arstro::cosmo_v2::PillButton *>(sc->children()[(size_t)i].get());
            const Point c = centre(*b, b->localBounds());
            r.click(c.x, c.y);
            r.pump(32);
        };
        clickSeg(os->formatPicker(), 0);
        r.settle();
        CHECK(os->renderLine().find("--output") == std::string::npos, "Rec.709 is the default and stays out of the line");
        clickSeg(os->colourPicker(), 4);
        r.settle();
        CHECK(os->formatPicker()->selected() == 1 && os->depthPicker()->selected() == 1, "choosing PQ moves H.264 to H.265 10-bit");
        const std::string line = os->renderLine();
        CHECK(line.find("--format h265") != std::string::npos && line.find("--bits 10") != std::string::npos && line.find("--output pq") != std::string::npos,
              "the line is one the service takes: --format h265 --bits 10 --output pq");
        CHECK(os->summary().find("HDR PQ") != std::string::npos, "the summary says HDR PQ");
        clickSeg(os->depthPicker(), 0);
        r.settle();
        CHECK(os->colourPicker()->selected() == 0, "choosing 8-bit afterwards puts the colour back to Rec.709, never a refused line");
        clickSeg(os->formatPicker(), 2);
        clickSeg(os->colourPicker(), 5);
        r.settle();
        CHECK(os->formatPicker()->selected() == 2 && os->renderLine().find("--output hlg") != std::string::npos,
              "ProRes is 10-bit already: HLG keeps it");
    }

    /** R-COLOR-5/6: a LUT on a source from its menu, a LUT effect's file row, and the export. */
    void testLuts()
    {
        std::printf("LUTs in and out\n");
        Rig r(1440, 900, [](FakeService &s) {
            s.edit();
            s.m.effects.push_back(FakeService::effect("ef_3", "ro2", "s_day01", "lut.cube", 2, true, 1.0));
            ++s.m.revision;
        });
        std::string suggested;
        r.app->onPickLutToOpen = [](std::function<void(const std::string &)> done) { done("/luts/teal.cube"); };
        r.app->onPickLutToSave = [&suggested](const std::string &name, std::function<void(const std::string &)> done) {
            suggested = name;
            done("/out/" + name);
        };
        r.settle();
        auto rt = r.app->edit().rackTree();
        auto cm = r.app->edit().contextMenu();
        auto rightClickRow = [&] {
            const Point p = centre(*rt, rt->rowRect(1));
            r.app->pointer(1, p.x - 40, p.y, 0, r.now);
            r.app->pointer(0, p.x - 40, p.y, 2, r.now);
            r.app->pointer(2, p.x - 40, p.y, 2, r.now + 40.0);
            r.pump(250);
        };
        auto clickItem = [&](const std::string &prefix) {
            for (int i = 0; i < cm->itemCount(); ++i)
                if (cm->item(i).label.rfind(prefix, 0) == 0)
                {
                    const Point q = centre(*cm, cm->itemRect(i));
                    r.click(q.x, q.y);
                    r.pump(64);
                    return true;
                }
            return false;
        };
        rightClickRow();
        r.svc.lines.clear();
        CHECK(clickItem("Input LUT..."), "a source's menu offers Input LUT...");
        CHECK(!r.svc.lines.empty() && r.svc.lines.back() == "set s_day01.lut=/luts/teal.cube", "…the host picks a .cube and the line sets it");
        rightClickRow();
        CHECK(clickItem("Remove Input LUT") && r.svc.lines.back() == "set s_day01.lut=none", "a source with a LUT offers Remove Input LUT");
        rightClickRow();
        CHECK(clickItem("Export LUT...") && suggested == "s_day01.cube" && r.svc.lines.back() == "lut export s_day01 --out /out/s_day01.cube",
              "Export LUT... suggests <bind>.cube and dispatches lut export");
        // the LUT effect: its section ends with a file row
        auto gi = r.app->edit().gradeInspector();
        auto pl = gi->plugins();
        CHECK(pl->rowCount() == 4, "s_day01's stack now ends with its LUT effect");
        Point p = centre(*pl, pl->rowRect(3));
        r.click(p.x - 30.0, p.y);
        r.settle();
        auto ep = gi->effectPanel();
        auto fb = ep->fileButtonOf("ef_3");
        CHECK(fb && fb->visible && fb->label() == "Choose a .cube\xE2\x80\xA6", "the LUT's section offers Choose a .cube…");
        CHECK(!ep->fileButtonOf("ef_1"), "a blur takes no file: no file row");
        r.svc.lines.clear();
        p = centre(*fb, fb->localBounds());
        r.click(p.x, p.y);
        r.settle();
        CHECK(!r.svc.lines.empty() && r.svc.lines.back() == "set ef_3.path=/luts/teal.cube", "clicking it picks the file: set ef_3.path=<file>");
        CHECK(fb->label() == "LUT  \xC2\xB7  teal.cube", "…and the row names the file");

        // D-12: at 1024x640 the panel scrolls to the LUT's section — and paints nothing outside itself
        Rig s(1024, 640, [](FakeService &f) {
            f.edit();
            f.m.effects.push_back(FakeService::effect("ef_3", "ro2", "s_day01", "lut.cube", 2, true, 1.0));
            ++f.m.revision;
        });
        s.settle();
        auto pl2 = s.app->edit().gradeInspector()->plugins();
        auto ep2 = s.app->edit().gradeInspector()->effectPanel();
        p = centre(*pl2, pl2->rowRect(3));
        s.click(p.x - 30.0, p.y);
        s.settle();
        CHECK(ep2->headerRect(0).y < 0.0, "1024x640: the panel scrolled to reveal the LUT's section");
        const Point tl = world(*pl2, 0, 0);
        const int x0 = (int)tl.x, y0 = (int)tl.y, x1 = x0 + (int)pl2->width.value(), y1 = y0 + (int)pl2->height.value();
        std::vector<uint32_t> scrolled;
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) scrolled.push_back(s.pixel(x, y));
        const Point mid = world(*ep2, ep2->width.value() * 0.5, ep2->height.value() * 0.5);
        s.app->wheel(mid.x, mid.y, 40.0);
        s.settle();
        CHECK(ep2->headerRect(0).y >= -0.5, "…the wheel scrolls it back to the top");
        bool same = true;
        size_t k = 0;
        for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) same = same && s.pixel(x, y) == scrolled[k++];
        CHECK(same, "the plugin list above is pixel-identical whether the panel below is scrolled or not (D-12)");
    }

    /** R-AUD-9: Deliver says what sound the render will carry, for the timeline and codec chosen. */
    void testDeliverSound()
    {
        std::printf("deliver: the sound a render carries\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); });
        r.app->setTab(2);
        r.settle();
        auto os = r.app->edit().outputSpec();
        auto clickSeg = [&](std::shared_ptr<arstro::cosmo_v2::SegmentedControl> sc, int i) {
            auto *b = dynamic_cast<arstro::cosmo_v2::PillButton *>(sc->children()[(size_t)i].get());
            const Point c = centre(*b, b->localBounds());
            r.click(c.x, c.y);
            r.settle();
        };
        clickSeg(os->formatPicker(), 0);
        CHECK(os->audioSentence() == "Sound: the master mix, AAC 48 kHz stereo", "H.264 of a timeline with sound: the master, AAC");
        clickSeg(os->formatPicker(), 2);
        CHECK(os->audioSentence().find("24-bit PCM") != std::string::npos, "ProRes: 24-bit PCM");
        clickSeg(os->formatPicker(), 4);
        CHECK(os->audioSentence().find("PNG sequence carries no sound") != std::string::npos, "a PNG sequence says it carries none");
        r.svc.m.timelines[1].hasSound = false;
        ++r.svc.m.revision;
        clickSeg(os->formatPicker(), 0);
        CHECK(os->audioSentence().find("No sound on this timeline") != std::string::npos, "a silent timeline: picture only, said");
    }

    /** R-AUD-7/8: the meter's ballistics and lamp, eased in only where there is sound; the waveform
     *  fading in when its envelope lands. */
    void testSoundUi()
    {
        std::printf("sound: meters and waveforms\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); s.peaksReady = false; });
        r.app->setTab(1);
        r.settle();
        auto tr = r.app->edit().transport();
        CHECK(tr->meterAmount() > 0.999, "the timeline has sound: the transport carries a meter");
        // attack: -6 dB arrives eased (a frame or two), not in one frame
        r.svc.m.meterPeakL = r.svc.m.meterPeakR = 0.5;
        r.svc.m.meterRmsL = r.svc.m.meterRmsR = 0.35;
        ++r.svc.m.revision;
        const double first = firstMoved(r, [&] { return tr->meterDb(0); }, -120.0);
        CHECK(first > -120.0 && first < -6.1, "the peak rises EASED: the first frame is short of -6 dB");
        r.pump(120);
        CHECK(std::fabs(tr->meterDb(0) + 6.02) < 0.2, "…and reaches -6 dB within a few frames");
        // release: the level falls at 24 dB/s, the held peak stays 1.5 s
        r.svc.m.meterPeakL = r.svc.m.meterPeakR = 0.0;
        r.svc.m.meterRmsL = r.svc.m.meterRmsR = 0.0;
        ++r.svc.m.revision;
        r.pump(250);
        CHECK(tr->meterDb(0) < -8.5 && tr->meterDb(0) > -15.0, "the peak FALLS at a meter's rate (about 6 dB in 250 ms), never to silence at once");
        CHECK(std::fabs(tr->meterHoldDb(0) + 6.02) < 0.3, "the held peak stays where the level was");
        r.pump(2000);
        CHECK(tr->meterHoldDb(0) < -12.0, "…and falls after its hold");
        // the clip lamp lights eased
        r.svc.m.meterClip = true;
        ++r.svc.m.revision;
        const double lamp = firstMoved(r, [&] { return tr->clipAmount(); }, 0.0);
        CHECK(strictlyBetween(lamp, 0.0, 1.0), "the clip lamp lights EASED");
        // a timeline with no sound: the meter eases away and the scrubber takes its room back
        const double scrubW = tr->scrubRect().w;
        for (auto &tl : r.svc.m.timelines) tl.hasSound = false;
        ++r.svc.m.revision;
        const double away = firstMoved(r, [&] { return tr->meterAmount(); }, 1.0);
        CHECK(strictlyBetween(away, 0.0, 1.0), "on a silent timeline the meter EASES away");
        r.settle();
        CHECK(tr->meterAmount() < 0.001 && tr->scrubRect().w > scrubW + 60.0, "…and the scrubber takes back its width");
        // R-AUD-7: the envelope lands — the clip's waveform fades in, and pixels change
        auto tl = r.app->edit().timeline();
        const std::string media = "/audio/dialogue_day3.wav";
        CHECK(tl->waveformAmount(media) == 0.0, "no envelope yet: no waveform");
        r.frame();
        const Rect lane = tl->laneRect("a1");
        const Point pw = world(*tl, lane.x + 80.0, lane.y + lane.h * 0.62);
        uint32_t before = 0;
        int changed = 0;
        std::vector<uint32_t> col;
        for (int dx = 0; dx < 120; ++dx) col.push_back(r.pixel((int)pw.x + dx, (int)pw.y));
        r.svc.peaksReady = true;
        r.svc.m.peaksEpoch += 1;
        ++r.svc.m.revision;
        const double wa = firstMoved(r, [&] { return tl->waveformAmount(media); }, 0.0);
        CHECK(strictlyBetween(wa, 0.0, 1.0), "the waveform FADES in when its envelope lands");
        r.settle();
        r.frame();
        for (int dx = 0; dx < 120; ++dx) changed += r.pixel((int)pw.x + dx, (int)pw.y) != col[(size_t)dx];
        (void)before;
        CHECK(changed > 60, "…and the clip shows it: the pixels along its centre line changed");
    }

    /** R-XCH: File › Import / Export Timeline dispatch the grammar; the inspector shows reel and source TC. */
    void testInterchangeUi()
    {
        std::printf("interchange: the File menu and the clip's reel and source timecode\n");
        Rig r(1440, 900, [](FakeService &s) {
            s.edit();
            for (auto &n : s.m.rack)
                if (n.bindName == "s_day02") { n.reel = "A001C007"; n.timecode = "10:00:00:00"; n.mediaFps = 24.0; }
            s.m.selectedClip = "c2";
            ++s.m.revision;
        });
        std::string suggested;
        r.app->onPickTimelineToImport = [](std::function<void(const std::string &)> done) { done("/cuts/from avid.edl"); };
        r.app->onPickTimelineToExport = [&suggested](const std::string &name, std::function<void(const std::string &)> done) {
            suggested = name;
            done("/cuts/social.otio");
        };
        r.app->setTab(1);
        r.settle();
        r.svc.lines.clear();
        CHECK(clickMenuItem(r, "File", "Import Timeline") && !r.svc.lines.empty() && r.svc.lines.back() == "interchange import \"/cuts/from avid.edl\"",
              "File > Import Timeline… dispatches interchange import <file>");
        r.pump(250);
        CHECK(clickMenuItem(r, "File", "Export Timeline") && suggested == "Social 30s.fcpxml" &&
                  r.svc.lines.back() == "interchange export \"Social 30s\" --out /cuts/social.otio",
              "File > Export Timeline… names the open timeline and dispatches interchange export");
        auto ci = r.app->edit().clipInspector();
        std::printf("    clip c2: reel %s, source TC %s\n", ci->reel().c_str(), ci->sourceTimecode().c_str());
        CHECK(ci->reel() == "A001C007" && ci->sourceTimecode() == "10:00:01:00", "the inspector shows the clip's reel and its source timecode at its in");
        // R6: at 1024×640 the taller column scrolls, and its actions come within reach
        Rig s(1024, 640, [](FakeService &f) { f.edit(); f.m.selectedClip = "c2"; ++f.m.revision; });
        s.app->setTab(1);
        s.settle();
        auto c2 = s.app->edit().clipInspector();
        CHECK(c2->scroll().scrollable(), "1024x640: the clip inspector scrolls");
        auto del = c2->deleteButton();
        const Point mid = world(*c2, c2->width.value() * 0.5, c2->height.value() * 0.5);
        s.app->wheel(mid.x, mid.y, -40.0);
        s.settle();
        CHECK(del->visible && del->y.value() + del->height.value() <= c2->height.value() + 0.5, "…to its clamped end, where Delete clip is reachable");
    }

    void key(Rig &r, int code, bool up = false)
    {
        artboard::KeyEvent e;
        e.type = up ? artboard::KeyEvent::Type::Up : artboard::KeyEvent::Type::Down;
        e.keyCode = code;
        r.app->key(e);
    }

    /** R-EDT-5: the multicam angle bar — Cut only, fading; a click and Alt+n switch; the highlight travels. */
    void testMulticamUi()
    {
        std::printf("multicam: the angle bar on the monitor\n");
        Rig r(1440, 900, [](FakeService &s) {
            s.edit();
            s.m.playing = false;
            auto c = FakeService::clip("mc1", "v2", "cams", "cams", 4.0, 0.0, 6.0, arstro::interstellar::Provenance::Local);
            c.nested = true;
            c.angle = 1;
            s.m.clips.push_back(c);
            s.m.multicamClip = "mc1";
            s.m.multicamAngle = 1;
            s.m.multicamAngles = {"A001_C003", "B002_C011", "C003_C007"};
            ++s.m.revision;
        });
        auto mon = r.app->edit().monitor();
        r.app->setTab(0);
        r.settle();
        CHECK(mon->anglesAmount() < 0.001, "Grade shows no angle bar");
        r.app->setTab(1);
        r.pump(16);
        const double in = firstMoved(r, [&] { return mon->anglesAmount(); }, 0.0);
        CHECK(strictlyBetween(in, 0.0, 1.0), "in Cut the angle bar FADES in over a multicam clip");
        r.settle();
        CHECK(mon->anglesAmount() > 0.999 && mon->angleRect(3).w > 0 && std::fabs(mon->angleHighlight()) < 1e-9,
              "…three chips, the first marked");
        r.svc.lines.clear();
        const Rect a2 = mon->angleRect(2);
        const Point p = world(*mon, a2.x + a2.w * 0.5, a2.y + a2.h * 0.5);
        r.click(p.x, p.y);
        CHECK(hasLine(r.svc, "multicam angle 2"), "clicking angle 2 dispatches multicam angle 2");
        artboard::KeyEvent e;
        e.type = artboard::KeyEvent::Type::Down;
        e.keyCode = '3';
        e.alt = true;
        r.svc.lines.clear();
        r.app->key(e);
        CHECK(hasLine(r.svc, "multicam angle 3") && r.app->edit().tab() == 1, "Alt+3 switches to angle 3 — and does not switch tabs");
        e.keyCode = '4';
        r.svc.lines.clear();
        r.app->key(e);
        CHECK(r.svc.lines.empty(), "Alt+4 with three angles does nothing");
        // the service answered: angle 2 here — the highlight travels there
        r.svc.m.multicamAngle = 2;
        ++r.svc.m.revision;
        const double mid = firstMoved(r, [&] { return mon->angleHighlight(); }, 0.0);
        CHECK(strictlyBetween(mid, 0.0, 1.0), "the highlight TRAVELS to the new angle (live value between the chips)");
        r.settle();
        CHECK(std::fabs(mon->angleHighlight() - 1.0) < 1e-6, "…and rests on it");
        // off the multicam: the bar fades out
        r.svc.m.multicamClip.clear();
        ++r.svc.m.revision;
        const double out = firstMoved(r, [&] { return mon->anglesAmount(); }, 1.0);
        CHECK(strictlyBetween(out, 0.0, 1.0), "off the multicam clip the bar fades out");
    }

    /** R-MEDIA-2: proxies — made from a source's menu, the project's switch in Workspace, said on the rows and the picture. */
    void testProxiesUi()
    {
        std::printf("proxies: the source menu, the Workspace switch, the rows, the caption\n");
        Rig r(1440, 900, [](FakeService &s) {
            s.edit();
            for (auto &n : s.m.rack)
                if (n.rackObj == "ro2") n.proxy = "/home/editor/Projects/night-ferry-day3.proxies/s_day01_960.mov";
            s.m.proxyJobs.push_back({"p1", "ro3", "s_day02", "prores", 960, "/x.proxies/s_day02_960.mov", 12, 96, "running", ""});
            ++s.m.revision;
        });
        r.settle();
        auto sb = r.app->edit().sourceBin();
        CHECK(sb->proxyNote("ro2") == "proxy \xC2\xB7 " && sb->proxyNote("ro3") == "proxy 12% \xC2\xB7 " && sb->proxyNote("ro5").empty(),
              "the source bin says which sources have a proxy, and how far one being made is");
        auto rt = r.app->edit().rackTree();
        auto cm = r.app->edit().contextMenu();
        const Point p = centre(*rt, rt->rowRect(1));   // s_day01
        r.app->pointer(1, p.x - 40, p.y, 0, r.now);
        r.app->pointer(0, p.x - 40, p.y, 2, r.now);
        r.app->pointer(2, p.x - 40, p.y, 2, r.now + 40.0);
        r.pump(250);
        std::string labels;
        for (int i = 0; i < cm->itemCount(); ++i) labels += cm->item(i).label + "|";
        CHECK(labels.find("Make Proxy Again|") != std::string::npos && labels.find("Remove Proxy|") != std::string::npos,
              "a source with a proxy offers Make Proxy Again and Remove Proxy");
        r.svc.lines.clear();
        for (int i = 0; i < cm->itemCount(); ++i)
            if (cm->item(i).label == "Remove Proxy") { const Point q = centre(*cm, cm->itemRect(i)); r.click(q.x, q.y); r.pump(64); break; }
        CHECK(hasLine(r.svc, "proxy remove s_day01"), "…Remove Proxy dispatches proxy remove s_day01");
        r.svc.lines.clear();
        CHECK(clickMenuItem(r, "Workspace", "     Use Proxies") && hasLine(r.svc, "proxy use on"), "Workspace › Use Proxies turns the switch on");
        r.svc.m.useProxies = true;
        ++r.svc.m.revision;
        r.settle();
        r.app->setTab(1);
        r.settle();
        CHECK(r.app->edit().monitor()->caption().find("proxies") != std::string::npos, "the monitor's caption says it shows proxies");
        r.app->setTab(0);   // Grade: the selected source, s_day01, through its proxy
        r.settle();
        CHECK(r.app->edit().monitor()->caption().find("proxy") != std::string::npos, "…and in Grade, that this source is its proxy");
        r.svc.lines.clear();
        CHECK(clickMenuItem(r, "Workspace", "\xE2\x80\xA2  Use Proxies") && hasLine(r.svc, "proxy use off"), "…marked while on, and the same item turns it off");
        CHECK(clickMenuItem(r, "Workspace", "     Make Proxies for All Video") && hasLine(r.svc, "proxy make"), "Make Proxies for All Video dispatches proxy make");
    }

    /** R-MEDIA-3: offline media in one list — located by hand or searched for in a folder. */
    void testRelinkUi()
    {
        std::printf("relink: File › Relink Media, the offline row's menu\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); });
        std::string asked;
        r.app->onPickMediaToRelink = [&asked](const std::string &name, std::function<void(const std::string &)> done) { asked = name; done("/cards/B002_C014.mov"); };
        r.app->onPickFolder = [](std::function<void(const std::string &)> done) { done("/cards"); };
        r.settle();
        auto cm = r.app->edit().contextMenu();
        auto clickItem = [&](const std::string &prefix) {
            for (int i = 0; i < cm->itemCount(); ++i)
                if (cm->item(i).label.rfind(prefix, 0) == 0)
                {
                    const Point q = centre(*cm, cm->itemRect(i));
                    r.click(q.x, q.y);
                    r.pump(64);
                    return true;
                }
            return false;
        };
        CHECK(clickMenuItem(r, "File", "Relink Media"), "File offers Relink Media...");
        r.pump(250);
        std::string labels;
        for (int i = 0; i < cm->itemCount(); ++i) labels += cm->item(i).label + "|";
        std::printf("      %s\n", labels.c_str());
        CHECK(cm->isOpen() && labels == "Locate B002_C014 corridor (B002_C014.mov)...|Search a Folder for It...|",
              "…listing every offline source, and a folder search");
        r.svc.lines.clear();
        CHECK(clickItem("Locate ") && asked == "B002_C014.mov" && hasLine(r.svc, "media relink s_off01 /cards/B002_C014.mov"),
              "Locate asks the host for the file by its old name and relinks to it");
        clickMenuItem(r, "File", "Relink Media");
        r.pump(250);
        r.svc.lines.clear();
        CHECK(clickItem("Search a Folder") && hasLine(r.svc, "media relink --search /cards"), "Search a Folder relinks every source found there");
        // the offline row's own menu (the source bin's rows are the rack's sources, groups left out)
        cm->close();
        r.app->setTab(1);
        r.settle();
        int row = -1, k = 0;
        for (const auto &n : r.svc.m.rack)
            if (!n.group) { if (n.bindName == "s_off01") row = k; ++k; }
        auto sb = r.app->edit().sourceBin();
        const Rect rr = sb->rowRect(row);
        const Point p = world(*sb, rr.x + rr.w * 0.5, rr.y + rr.h * 0.5);
        r.app->pointer(1, p.x, p.y, 0, r.now);
        r.app->pointer(0, p.x, p.y, 2, r.now);
        r.app->pointer(2, p.x, p.y, 2, r.now + 40.0);
        r.pump(250);
        r.svc.lines.clear();
        CHECK(clickItem("Relink...") && hasLine(r.svc, "media relink s_off01 /cards/B002_C014.mov"), "an offline source's menu offers Relink...");
    }

    /** R-CLR-1/2: the matte types in the plugin menu; the matte view from the Colour menu and Shift+H, said on the caption. */
    void testMatteUi()
    {
        std::printf("mattes: the plugin menu, Show Matte, Shift+H, the caption\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); });
        r.settle();
        r.svc.lines.clear();
        CHECK(clickMenuItem(r, "Colour", "     Show Matte") && hasLine(r.svc, "view matte on"), "Colour › Show Matte turns the matte view on");
        r.svc.m.matteView = true;
        ++r.svc.m.revision;
        r.settle();
        CHECK(r.app->edit().monitor()->caption().find("matte") != std::string::npos, "Grade's caption says it shows the matte");
        r.svc.lines.clear();
        artboard::KeyEvent e;
        e.type = artboard::KeyEvent::Type::Down;
        e.keyCode = 'H';
        e.shift = true;
        r.app->key(e);
        CHECK(hasLine(r.svc, "view matte off"), "Shift+H in Grade toggles it");
        // the catalog's mattes are in the plugin menu
        auto pl = r.app->edit().gradeInspector()->plugins();
        const Point add = centre(*pl, pl->addRect());
        r.click(add.x, add.y);
        r.pump(250);
        auto cm = r.app->edit().contextMenu();
        std::string labels;
        for (int i = 0; i < cm->itemCount(); ++i) labels += cm->item(i).label + "|";
        CHECK(labels.find("Qualifier (HSL)|") != std::string::npos && labels.find("Window|") != std::string::npos,
              "the plugin menu offers a Qualifier and a Window");
        cm->close();
        // R-CLR-2: a window's row offers tracking; while one runs, its progress is on the caption
        r.svc.m.effects.push_back(FakeService::effect("ef_4", "ro2", "s_day01", "window.shape", 2, true, 1.0));
        ++r.svc.m.revision;
        r.settle();
        auto rightClickRow = [&](int i) {
            const Rect rr = pl->rowRect(i);
            const Point p = world(*pl, rr.x + rr.w * 0.5 - 30.0, rr.y + rr.h * 0.5);
            r.app->pointer(1, p.x, p.y, 0, r.now);
            r.app->pointer(0, p.x, p.y, 2, r.now);
            r.app->pointer(2, p.x, p.y, 2, r.now + 40.0);
            r.pump(250);
        };
        rightClickRow(pl->rowCount() - 1);
        r.svc.lines.clear();
        bool clicked = false;
        for (int i = 0; i < cm->itemCount(); ++i)
            if (cm->item(i).label == "Track Backward") { const Point q = centre(*cm, cm->itemRect(i)); r.click(q.x, q.y); r.pump(64); clicked = true; break; }
        CHECK(clicked && hasLine(r.svc, "track window ef_4 --back"), "a window's row offers Track Backward (and Forward)");
        r.svc.m.trackJobs.push_back({"t1", "ef_4", true, 6, 24, "running", ""});
        ++r.svc.m.revision;
        r.settle();
        CHECK(r.app->edit().monitor()->caption().find("tracking 25%") != std::string::npos, "the caption says how far the track has got");
        rightClickRow(pl->rowCount() - 1);
        labels.clear();
        for (int i = 0; i < cm->itemCount(); ++i) labels += cm->item(i).label + "|";
        CHECK(labels.find("Cancel Tracking|") != std::string::npos && labels.find("Track Forward") == std::string::npos, "…and the row offers Cancel Tracking");
    }

    /** R-CLR-4/5: the stills gallery in the Grade deck, its menu, Grab Still; the monitor's wipe divider. */
    void testStillsUi()
    {
        std::printf("stills: the gallery, its menu, grab; the wipe divider\n");
        Rig r(1440, 900, [](FakeService &s) {
            s.edit();
            s.m.stills = {{"st_1", "look1", "/p.stills/st_1.png", "s_day01", 0.0}, {"st_2", "dusk", "/p.stills/st_2.png", "s_day02", 1.0}};
            ++s.m.revision;
        });
        r.settle();
        auto deck = r.app->edit().gradeDeck();
        CHECK(deck->stillsAmount() < 0.001 && deck->stillsStrip()->cellCount() == 2, "the gallery holds the stills, the sources in front");
        const Rect chip = deck->stillsChipRect();
        const Point c = world(*deck, chip.x + chip.w * 0.5, chip.y + chip.h * 0.5);
        r.click(c.x, c.y);
        const double in = firstMoved(r, [&] { return deck->stillsAmount(); }, 0.0);
        CHECK(chip.w > 0 && strictlyBetween(in, 0.0, 1.0), "the header's STILLS chip brings the gallery forward, cross-faded");
        r.settle();
        // a still's menu: apply, wipe, delete
        auto st = deck->stillsStrip();
        const Point p = world(*st, st->cellXForTest(0) + 30.0, st->height.value() * 0.5);
        r.app->pointer(1, p.x, p.y, 0, r.now);
        r.app->pointer(0, p.x, p.y, 2, r.now);
        r.app->pointer(2, p.x, p.y, 2, r.now + 40.0);
        r.pump(250);
        auto cm = r.app->edit().contextMenu();
        std::string labels;
        for (int i = 0; i < cm->itemCount(); ++i) labels += cm->item(i).label + "|";
        CHECK(labels == "Apply Grade to s_day01|Wipe Against look1|Delete Still|", "a still's menu: apply its grade, wipe against it, delete it");
        r.svc.lines.clear();
        for (int i = 0; i < cm->itemCount(); ++i)
            if (cm->item(i).label == "Wipe Against look1") { const Point q = centre(*cm, cm->itemRect(i)); r.click(q.x, q.y); r.pump(64); break; }
        CHECK(hasLine(r.svc, "view wipe st_1"), "…Wipe Against dispatches view wipe");
        r.svc.lines.clear();
        CHECK(clickMenuItem(r, "Colour", "     Grab Still") && hasLine(r.svc, "still grab s_day01"), "Colour › Grab Still grabs the Grade target");
        // the wipe divider: shown, dragged (direct), moved by the model (eased)
        r.svc.m.wipeRef = "st_1";
        r.svc.m.wipeLabel = "look1";
        r.svc.m.wipeAt = 0.5;
        ++r.svc.m.revision;
        r.settle();
        auto mon = r.app->edit().monitor();
        const Rect g = mon->wipeGripRect();
        CHECK(mon->wipeAmount() > 0.999 && g.w > 0 && std::fabs(mon->wipeLive() - 0.5) < 1e-6, "the divider stands at the split");
        const Rect fr = mon->frameRect();
        const Point g0 = world(*mon, g.x + g.w * 0.5, g.y + g.h * 0.5), g1 = world(*mon, fr.x + fr.w * 0.75, g.y + g.h * 0.5);
        r.svc.lines.clear();
        r.app->pointer(0, g0.x, g0.y, 0, r.now);
        r.pump(16);
        r.app->pointer(1, (g0.x + g1.x) * 0.5, g0.y, 0, r.now);
        r.pump(16);
        r.app->pointer(1, g1.x, g1.y, 0, r.now);
        r.pump(16);
        CHECK(mon->wipeDragging() && std::fabs(mon->wipeLive() - 0.75) < 0.01, "dragging it, the divider is under the pointer (no easing)");
        r.app->pointer(2, g1.x, g1.y, 0, r.now);
        r.pump(16);
        CHECK(withPrefix(r.svc, "view wipe --at 0.7").size() > 0, "…and each new split is asked for");
        r.svc.m.wipeAt = 0.25;
        ++r.svc.m.revision;
        const double mid = firstMoved(r, [&] { return mon->wipeLive(); }, mon->wipeLive());
        CHECK(strictlyBetween(mid, 0.25, 0.76), "a split the model moved EASES there");
        r.svc.m.wipeRef.clear();
        ++r.svc.m.revision;
        const double out = firstMoved(r, [&] { return mon->wipeAmount(); }, 1.0);
        CHECK(strictlyBetween(out, 0.0, 1.0), "the wipe off: the divider fades out");
    }

    /** R-CLR-3: the node graph — the views' tabs, the nodes in order, their menus, a node added sliding the rest along. */
    void testNodeGraphUi()
    {
        std::printf("node graph: the NODES view, its nodes, their menus, motion\n");
        Rig r(1440, 900, [](FakeService &s) {
            s.edit();
            auto v = s.m.rack[1];   // s_day01's parallel node
            v.node = 90; v.rackObj = "ro90"; v.bindName = "s_day01_par"; v.cosmoName = "s_day01_par"; v.parent = -1; v.depth = 0;
            v.parallelOf = "ro2"; v.parallelMix = 0.5;
            s.m.rack.push_back(v);
            ++s.m.revision;
        });
        r.settle();
        auto deck = r.app->edit().gradeDeck();
        const Rect tab = deck->viewTabRect(2);
        const Point tp = world(*deck, tab.x + tab.w * 0.5, tab.y + tab.h * 0.5);
        r.click(tp.x, tp.y);
        const double in = firstMoved(r, [&] { return deck->viewAmount(2); }, 0.0);
        CHECK(tab.w > 0 && strictlyBetween(in, 0.0, 1.0), "the NODES tab cross-fades the graph in");
        r.settle();
        auto g = deck->nodeGraph();
        std::string order;
        for (const auto &n : g->nodes()) order += n.key + ",";
        CHECK(order == "in,ro2,ro90,mix,ro1,out,", "input · source · its parallel node · the mixer · its group · output");
        const Rect ri = g->nodeRect("in"), rs = g->nodeRect("ro2"), rp = g->nodeRect("ro90"), rm = g->nodeRect("mix"), rg = g->nodeRect("ro1"), ro = g->nodeRect("out");
        CHECK(ri.right() < rs.x && rs.right() < rm.x && rm.right() < rg.x && rg.right() < ro.x && std::fabs(rp.x - rs.x) < 1.0 && std::fabs(rp.y - rs.y) > 10.0,
              "left to right, the parallel node beside its source, off its line");
        CHECK(inside(Rect{ro.x + g->worldTransform().apply(artboard::Point{0, 0}).x, 0, ro.w, 1}, 1440, 900), "the output fits at 1440");
        auto cm = r.app->edit().contextMenu();
        auto rightClick = [&](const Rect &rr) {
            const Point p = world(*g, rr.x + rr.w * 0.5, rr.y + rr.h * 0.5);
            r.app->pointer(1, p.x, p.y, 0, r.now);
            r.app->pointer(0, p.x, p.y, 2, r.now);
            r.app->pointer(2, p.x, p.y, 2, r.now + 40.0);
            r.pump(250);
            std::string l;
            for (int i = 0; i < cm->itemCount(); ++i) l += cm->item(i).label + "|";
            return l;
        };
        auto clickItem = [&](const std::string &label) {
            for (int i = 0; i < cm->itemCount(); ++i)
                if (cm->item(i).label == label) { const Point q = centre(*cm, cm->itemRect(i)); r.click(q.x, q.y); r.pump(64); return true; }
            return false;
        };
        CHECK(rightClick(rs) == "Add Serial Node After|Add Parallel Node|", "a source's menu: add a serial or a parallel node");
        r.svc.lines.clear();
        CHECK(clickItem("Add Parallel Node") && hasLine(r.svc, "node parallel s_day01"), "…Add Parallel Node dispatches node parallel");
        const std::string pl = rightClick(rp);
        CHECK(pl.find("Mix 100%|") != std::string::npos && pl.find("Mix 50%") == std::string::npos && pl.find("Remove Parallel Node|") != std::string::npos,
              "a parallel node's menu: its mix (not the one it has) and remove");
        r.svc.lines.clear();
        CHECK(clickItem("Mix 100%") && hasLine(r.svc, "set s_day01_par.parallelMix=1"), "…a mix sets parallelMix");
        CHECK(rightClick(rg) == "Add Serial Node After|Remove Serial Node|", "a group's menu: add after it, or remove it");
        cm->close();
        r.svc.lines.clear();
        const Point gc = world(*g, rg.x + rg.w * 0.5, rg.y + rg.h * 0.5);
        r.click(gc.x, gc.y);
        CHECK(hasLine(r.svc, "rack select gr1"), "clicking a node makes it the Grade target");
        // a serial node inserted between the source and its group: the group's node slides along, eased
        {
            auto n = r.svc.m.rack[0];
            n.node = 91; n.rackObj = "ro91"; n.bindName = "node"; n.cosmoName = "node"; n.parent = 0; n.depth = 1;
            r.svc.m.rack.push_back(n);
            r.svc.m.rack[1].parent = (int)r.svc.m.rack.size() - 1;
            r.svc.m.selectedRack = 1;
            ++r.svc.m.revision;
        }
        const double x0 = g->nodeRect("ro1").x;
        r.pump(16);
        const double mid = firstMoved(r, [&] { return g->nodeRect("ro1").x; }, x0);
        r.settle();
        const double x1 = g->nodeRect("ro1").x;
        CHECK(x1 > x0 + 20.0 && strictlyBetween(mid, x0, x1) && g->nodeAlpha("ro91") > 0.99, "a node added: the ones after it SLIDE along, it fades in");
    }

    /** R-DLV-5/6: the autosave interval in the Settings menu; a recoverable autosave offered once. */
    void testSafetyUi()
    {
        std::printf("safety: autosave in Settings, the recovery offer\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); s.m.settings.autosaveSeconds = 60; ++s.m.revision; });
        r.settle();
        r.svc.lines.clear();
        CHECK(clickMenuItem(r, "Settings", "     Autosave Every 5 Minutes") && hasLine(r.svc, "settings set autosave=300"),
              "Settings › Autosave Every 5 Minutes sets it (the current one is marked)");
        r.svc.lines.clear();
        CHECK(clickMenuItem(r, "Settings", "     Autosave Off") && hasLine(r.svc, "settings set autosave=0"), "…and Off");
        auto dlg = r.app->edit().confirm();
        CHECK(!dlg->isOpen(), "no offer without an autosave");
        r.svc.m.recoveryAvailable = true;
        r.svc.m.recoveryTime = 1790000000;
        ++r.svc.m.revision;
        r.settle();
        CHECK(dlg->isOpen() && dlg->buttonCount() == 2, "a newer autosave is offered: Discard or Recover");
        r.svc.lines.clear();
        CHECK(dlg->confirmDefault() && hasLine(r.svc, "project recover"), "…Recover (the primary answer) dispatches project recover");
        r.settle();
        ++r.svc.m.revision;   // the model still says so (the fake keeps it): asked ONCE per autosave
        r.settle();
        CHECK(!dlg->isOpen(), "the same autosave is not offered twice");
    }

    /** R-DLV-3: render presets on the Deliver tab — chosen, rendered with, left by touching a control, saved. */
    void testRenderPresetsUi()
    {
        std::printf("render presets: the Deliver preset row\n");
        Rig r(1440, 900, [](FakeService &s) {
            s.edit();
            s.m.renderPresets = {{"YouTube 1080p", true, "--format h264 --res 1920x1080", "H.264"}, {"ProRes HQ master", true, "--format prores --profile hq", "ProRes hq"}};
            ++s.m.revision;
        });
        r.app->setTab(2);
        r.settle();
        auto os = r.app->edit().outputSpec();
        auto cm = r.app->edit().contextMenu();
        const Point pb = centre(*os->presetButton(), Rect{0, 0, os->presetButton()->width.value(), os->presetButton()->height.value()});
        r.click(pb.x, pb.y);
        r.pump(250);
        std::string labels;
        for (int i = 0; i < cm->itemCount(); ++i) labels += cm->item(i).label + "|";
        CHECK(cm->isOpen() && labels == "\xE2\x80\xA2  Custom|     YouTube 1080p|     ProRes HQ master|", "the preset row lists Custom and every preset");
        for (int i = 0; i < cm->itemCount(); ++i)
            if (cm->item(i).label == "     YouTube 1080p") { const Point q = centre(*cm, cm->itemRect(i)); r.click(q.x, q.y); r.pump(64); break; }
        CHECK(os->preset() == "YouTube 1080p" && os->presetButton()->label() == "Preset: YouTube 1080p", "choosing one shows it");
        const double dimming = firstMoved(r, [&] { return os->presetAmount(); }, 0.0);
        CHECK(strictlyBetween(dimming, 0.0, 1.0), "…and the controls below dim, eased, while it decides");
        r.settle();
        CHECK(os->renderLine().find("--preset \"YouTube 1080p\"") != std::string::npos && os->renderLine().find("--format") == std::string::npos,
              "…and the render line carries the preset, not the controls");
        os->qualityPicker()->setSelected(3);   // a control touched: the controls are the spec again
        r.pump(32);
        CHECK(os->preset().empty() && os->renderLine().find("--format") != std::string::npos, "touching a control goes back to Custom");
        // Save Preset… names the controls' spec
        const Point sp = centre(*os->savePresetButton(), Rect{0, 0, os->savePresetButton()->width.value(), os->savePresetButton()->height.value()});
        r.click(sp.x, sp.y);
        r.pump(250);
        auto np = r.app->edit().namePrompt();
        CHECK(np->isOpen(), "Save Preset… asks for a name");
        r.svc.lines.clear();
        np->field()->text = "Night review";
        np->confirm();
        r.pump(64);
        CHECK(withPrefix(r.svc, "render preset save \"Night review\" --format h264").size() > 0 && withPrefix(r.svc, "render preset save").find("--quality 12") != std::string::npos,
              "…and saves the controls' flags under it");
    }

    /** R-DLV-2: the burn-in chips on Deliver — toggled (eased), carried by the line, the text asked for. */
    void testBurnInsUi()
    {
        std::printf("burn-ins: the Deliver burn-in menu\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); });
        r.app->setTab(2);
        r.settle();
        auto os = r.app->edit().outputSpec();
        auto cm = r.app->edit().contextMenu();
        auto pick = [&](const std::string &prefix) {
            const auto bb = os->burnButton();
            const Point pb = centre(*bb, Rect{0, 0, bb->width.value(), bb->height.value()});
            r.click(pb.x, pb.y);
            r.pump(250);
            std::string labels;
            for (int i = 0; i < cm->itemCount(); ++i) labels += cm->item(i).label + "|";
            for (int i = 0; i < cm->itemCount(); ++i)
                if (cm->item(i).label.rfind(prefix, 0) == 0) { const Point q = centre(*cm, cm->itemRect(i)); r.click(q.x, q.y); r.pump(64); break; }
            return labels;
        };
        CHECK(os->renderLine().find("--burnin") == std::string::npos && os->burnButton()->label() == "No burn-ins", "no burn-ins until one is chosen");
        const std::string first = pick("     Timecode");
        CHECK(first == "     Timecode, bottom left|     Source timecode, bottom right|     Clip name, top left|     Source name, top right|     Text\xE2\x80\xA6, top centre|",
              "the OUTPUT header's burn-in menu lists the five, each with its place");
        r.settle();
        CHECK(os->burn(OutputSpec::BurnTc) && os->burnButton()->label() == "1 burn-in", "choosing one turns it on, and the button counts it");
        pick("     Clip name");
        r.settle();
        CHECK(os->renderLine().find("--burnin tc@bl,clip@tl") != std::string::npos, "…and the render line burns in the timecode and the clip");
        pick("     Text");
        r.pump(250);
        auto np = r.app->edit().namePrompt();
        CHECK(np->isOpen(), "Text… asks for its words");
        np->field()->text = "DRAFT v3";
        np->confirm();
        r.settle();
        CHECK(os->renderLine().find("--burnin \"tc@bl,clip@tl,text=DRAFT v3@tc\"") != std::string::npos && os->burnButton()->label() == "3 burn-ins",
              "…and burns them in top centre");
        CHECK(os->specFlags().find("--burnin") == std::string::npos, "burn-ins are this render's, not a preset's");
        const std::string marked = pick("\xE2\x80\xA2  Timecode");
        CHECK(marked.find("\xE2\x80\xA2  Text: DRAFT v3|") != std::string::npos, "the menu marks the ones on, the text by its words");
        r.settle();
        CHECK(os->renderLine().find("tc@bl") == std::string::npos, "one chosen again is off");
    }

    /** R-DLV-1: the caption under the playhead on the monitor (cross-faded), the menus, the Deliver pill. */
    void testCaptionsUi()
    {
        std::printf("captions: the monitor, the menus, the Deliver pill\n");
        Rig r(1440, 900, [](FakeService &s) {
            s.edit();
            s.m.captions = {{"cap_1", "cue1", 4.0, 2.0, "Hello\nworld"}, {"cap_2", "cue2", 6.0, 1.0, "Second"}};
            s.m.playhead = 1.0;
            ++s.m.revision;
        });
        r.settle();
        auto mon = r.app->edit().monitor();
        CHECK(mon->subtitleText().empty() && mon->subtitleAmount() < 1e-9, "no caption where none is cut");
        r.svc.m.playhead = 4.5;
        ++r.svc.m.revision;
        const double in = firstMoved(r, [&] { return mon->subtitleAmount(); }, 0.0);
        CHECK(mon->subtitleText() == "Hello\nworld" && strictlyBetween(in, 0.0, 1.0), "the caption under the playhead fades in on the monitor");
        r.settle();
        r.svc.m.playhead = 6.5;
        ++r.svc.m.revision;
        bool cross = false;   // one frame at a time: the first frame with both in flight
        for (int k = 0; k < 10 && !cross; ++k)
        {
            r.pump(16);
            cross = mon->subtitleText() == "Second" && strictlyBetween(mon->subtitleLeavingAmount(), 0.0, 1.0) && strictlyBetween(mon->subtitleAmount(), 0.0, 1.0);
        }
        CHECK(cross, "the next caption cross-fades with the leaving one");
        r.settle();
        r.svc.m.captionsShown = false;
        ++r.svc.m.revision;
        r.settle();
        CHECK(mon->subtitleText().empty() && mon->subtitleLeavingAmount() < 1e-9, "captions off: the monitor shows none");
        r.svc.lines.clear();
        CHECK(clickMenuItem(r, "Workspace", "     Show Captions") && hasLine(r.svc, "view captions on"), "Workspace > Show Captions turns them back on");
        r.app->onPickCaptionsToImport = [](std::function<void(const std::string &)> done) { done("/subs/day 3.srt"); };
        CHECK(clickMenuItem(r, "File", "Import Captions") && hasLine(r.svc, "caption import \"/subs/day 3.srt\""), "File > Import Captions reads an SRT");
        std::string suggested;
        r.app->onPickCaptionsToExport = [&](const std::string &name, std::function<void(const std::string &)> done) { suggested = name; done("/subs/out.srt"); };
        CHECK(clickMenuItem(r, "File", "Export Captions") && suggested == "Social 30s.srt" && hasLine(r.svc, "caption export /subs/out.srt --timeline \"Social 30s\""),
              "File > Export Captions writes the open timeline's");
        // Deliver: the pill dims while the timeline has none, and offers the three ways when it has some
        r.app->setTab(2);
        r.settle();
        auto os = r.app->edit().outputSpec();
        auto cm = r.app->edit().contextMenu();
        CHECK(os->captionsButton()->label() == "No captions" && os->captionsAvailable() < 1e-9 && os->renderLine().find("--captions") == std::string::npos,
              "no captions on the timeline: the pill says so, dimmed, and the line carries none");
        for (auto &tl : r.svc.m.timelines) if (tl.id == "social30") tl.captions = 2;
        ++r.svc.m.revision;
        const double avail = firstMoved(r, [&] { return os->captionsAvailable(); }, 0.0);
        CHECK(strictlyBetween(avail, 0.0, 1.0) && os->captionsButton()->label() == "Captions: off", "captions arriving brighten the pill, eased");
        r.settle();
        auto pick = [&](const std::string &prefix) {
            const auto b = os->captionsButton();
            const Point pb = centre(*b, Rect{0, 0, b->width.value(), b->height.value()});
            r.click(pb.x, pb.y);
            r.pump(250);
            std::string labels;
            for (int i = 0; i < cm->itemCount(); ++i) labels += cm->item(i).label + "|";
            for (int i = 0; i < cm->itemCount(); ++i)
                if (cm->item(i).label.rfind(prefix, 0) == 0) { const Point q = centre(*cm, cm->itemRect(i)); r.click(q.x, q.y); r.pump(64); break; }
            return labels;
        };
        const std::string labels = pick("     Burn into");
        CHECK(labels == "     Burn into the picture|     Subtitle track (MP4, MOV, MKV)|     Sidecar .srt beside the file|", "the RANGE header's pill offers the three ways");
        r.settle();
        CHECK(os->renderLine().find(" --captions burn") != std::string::npos && os->captionsButton()->label() == "Captions: burn", "…and the render line burns them in");
        pick("     Subtitle track");
        r.settle();
        CHECK(os->renderLine().find(" --captions burn,track") != std::string::npos && os->captionsButton()->label() == "Captions: 2 ways", "…and muxes them as a track");
        CHECK(os->specFlags().find("--captions") == std::string::npos, "captions are this render's, not a preset's");
        os->formatPicker()->setSelected(4);   // PNG: no track to carry them
        r.pump(32);
        CHECK(os->renderLine().find(" --captions burn") != std::string::npos && os->renderLine().find("track") == std::string::npos, "a PNG sequence leaves the track out");
        for (auto &tl : r.svc.m.timelines) if (tl.id == "social30") tl.captions = 0;   // the chosen ways stay, the captions go
        ++r.svc.m.revision;
        r.settle();
        CHECK(os->captionWay(OutputSpec::CapBurn) && os->renderLine().find("--captions") == std::string::npos && os->captionsButton()->label() == "No captions",
              "a timeline without captions renders none, whatever was chosen");
    }

    /** R-DLV-4: DCP and IMF on the Deliver tab — the container takes the size's place, the colour becomes words, each says it is unvalidated. */
    void testPackagesUi()
    {
        std::printf("packages: DCP and IMF on the Deliver tab\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); });   // a 3840x2160 project
        r.app->setTab(2);
        r.settle();
        auto os = r.app->edit().outputSpec();
        os->formatPicker()->setSelected(5);
        const double c = firstMoved(r, [&] { return os->rowAmount(OutputSpec::Container); }, 0.0);
        CHECK(strictlyBetween(c, 0.0, 1.0), "DCP: the container cross-fades in where the size was, eased");
        r.settle();
        const std::string dcp = os->renderLine();
        CHECK(dcp.find("--format dcp") != std::string::npos && dcp.find("--container 4k-flat") != std::string::npos && dcp.find("--res") == std::string::npos &&
                  dcp.find("--output") == std::string::npos && dcp.find("renders/social30_DCP --format") != std::string::npos,
              "…the line names the container the project fills (4K Flat), no size, no colour, and a folder");
        CHECK(os->rowAmount(OutputSpec::DcpNote) > 0.99 && os->colourPicker()->opacity.value() < 0.01, "…says it is not validated, and the colour is fixed");
        os->containerPicker()->setSelected(1);
        r.pump(32);
        CHECK(os->renderLine().find("--container 2k-scope") != std::string::npos, "a container chosen is the one rendered");
        os->formatPicker()->setSelected(6);
        r.settle();
        const std::string imf = os->renderLine();
        CHECK(imf.find("--format imf") != std::string::npos && imf.find("--container") == std::string::npos && os->rowAmount(OutputSpec::ImfNote) > 0.99 &&
                  os->rowAmount(OutputSpec::Container) < 0.01 && os->colourPicker()->opacity.value() > 0.99,
              "IMF: the project's size, the colour row back, its own note");
        os->colourPicker()->setSelected(4);
        r.pump(32);
        CHECK(os->formatPicker()->selected() == 6 && os->renderLine().find("--output pq") != std::string::npos, "an IMF carries HDR without changing the codec");
    }

    /** R-EDT-1/2: J/K/L, the marks, Insert/Overwrite keys; the source viewer; the band, the target, the badge — eased. */
    void testEditingUi()
    {
        std::printf("editing: J/K/L, marks, the source viewer, insert and overwrite\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); s.m.playing = false; ++s.m.revision; });
        r.app->setTab(1);
        r.settle();
        r.svc.lines.clear();
        key(r, 'L');
        CHECK(!r.svc.lines.empty() && r.svc.lines.back() == "shuttle forward", "L shuttles forward");
        key(r, 'L');
        r.pump(48);
        auto tr = r.app->edit().transport();
        const double badge = firstMoved(r, [&] { return tr->badgeAmount(); }, 0.0);
        CHECK(strictlyBetween(badge, 0.0, 1.0) && tr->badgeText() == "2\xC3\x97", "at 2× the transport's badge says so, EASED in");
        key(r, 'K');
        CHECK(r.svc.lines.back() == "shuttle stop", "K stops");
        const double ph = r.svc.m.playhead;
        key(r, 'L');
        CHECK(r.svc.lines.back().rfind("playhead ", 0) == 0 && r.svc.lines.back() != "playhead " + cmd::seconds(ph, r.svc.m.fps),
              "K held, L steps a frame instead of shuttling");
        key(r, 'K', true);
        key(r, 'J');
        CHECK(r.svc.lines.back() == "shuttle back", "K released, J shuttles back");
        key(r, 'K');
        key(r, 'K', true);
        // marks and the edit keys
        key(r, 'I');
        CHECK(r.svc.lines.back() == "mark in", "I marks the timeline In");
        key(r, 'O');
        CHECK(r.svc.lines.back() == "mark out", "O marks its Out");
        auto tl = r.app->edit().timeline();
        r.svc.m.markIn = 2.0;
        r.svc.m.markOut = 5.0;
        ++r.svc.m.revision;
        const double band = firstMoved(r, [&] { return tl->markBandAmount(); }, 0.0);
        CHECK(strictlyBetween(band, 0.0, 1.0), "the In/Out band on the ruler EASES in");
        key(r, 188);
        CHECK(r.svc.lines.back() == "edit insert", "comma inserts");
        key(r, 190);
        CHECK(r.svc.lines.back() == "edit overwrite", "period overwrites");
        // the source viewer: a double-click in the bin, the transport on the source's clock, Escape back
        auto bin = r.app->edit().sourceBin();
        const Point p = centre(*bin, bin->rowRect(1));
        dblClick(r, p.x, p.y);
        CHECK(r.svc.lines.back() == "source view s_day02", "double-clicking a source opens it in the viewer");
        r.settle();
        CHECK(tr->sourceMode(), "the transport drives the source viewer");
        CHECK(r.app->edit().monitor()->caption().rfind("SOURCE", 0) == 0, "the monitor says it shows a SOURCE");
        key(r, 'I');
        CHECK(r.svc.lines.back() == "mark in --source", "I marks the SOURCE's In while it is in the viewer");
        const Rect sr = tr->scrubRect();
        const Point sp = world(*tr, sr.x + sr.w * 0.5, sr.h * 0.5);
        r.click(sp.x, sp.y);
        CHECK(r.svc.lines.back().rfind("source playhead ", 0) == 0, "scrubbing moves the source playhead");
        key(r, 27);
        CHECK(r.svc.lines.back() == "source view none", "Escape returns the viewer to the timeline");
        r.settle();
        CHECK(!tr->sourceMode(), "…and the transport to the timeline's clock");
        // the target track: the lane menu sets it, the header's bar moves eased
        const std::string v2 = "v2";
        r.svc.m.targetTrack = "v1";
        ++r.svc.m.revision;
        r.settle();
        r.svc.m.targetTrack = v2;
        ++r.svc.m.revision;
        const double tgt = firstMoved(r, [&] { return tl->targetAmount(v2); }, 0.0);
        CHECK(strictlyBetween(tgt, 0.0, 1.0), "the target bar moves to the new track EASED");
    }

    void testGroupBrowsing()
    {
        std::printf("browsing groups like cosmo\n");
        Rig r(1440, 900, [](FakeService &s) { s.edit(); });
        r.settle();
        auto rt = r.app->edit().rackTree();
        auto deck = r.app->edit().gradeDeck();
        auto strip = deck->filmstrip();
        CHECK(deck->shownLevel() == "ro1" && strip->cellCount() == 2, "the strip shows the Grade target's group: Day exteriors' two sources");
        auto path = deck->crumbPath();
        std::string joined;
        for (const auto &c : path) joined += c + " > ";
        std::printf("      path: %s\n", joined.c_str());
        CHECK(path.size() == 3 && path[0] == "All sources" && path[1] == "Day exteriors" && path[2] == "A001_C003 harbour wide",
              "cosmo's breadcrumb: All sources > Day exteriors > the selected source");
        // a crumb goes back up: the strip fades out, swaps, fades in
        auto bc = deck->breadcrumb();
        Point cp = world(*bc, 9.75 + 20.0, arstro::cosmo_v2::Breadcrumb::kHeight * 0.5);
        r.click(cp.x, cp.y);
        CHECK(deck->wantedLevel().empty(), "clicking \"All sources\" asks for the top level");
        const double lf = firstMoved(r, [&] { return deck->levelAmount(); }, 1.0);
        CHECK(strictlyBetween(lf, 0.0, 1.0), "the strip FADES out before it swaps (first frame between)");
        r.settle();
        CHECK(deck->shownLevel().empty() && strip->cellCount() == 3, "…and shows the top: two group chips and the loose source");
        CHECK(r.svc.lines.empty(), "browsing dispatched nothing (presentation)");
        // grouping puts the item INSIDE a shut group (the user's "the item come inside the group")
        r.svc.dispatch("rack select s_drone01", gErr);
        r.settle();
        r.svc.lines.clear();
        ctrlKey(r, 'G');
        r.settle();
        int g = -1, drone = -1;
        for (int i = 0; i < (int)r.svc.m.rack.size(); ++i)
        {
            if (r.svc.m.rack[(size_t)i].bindName.rfind("group", 0) == 0) g = i;
            if (r.svc.m.rack[(size_t)i].bindName == "s_drone01") drone = i;
        }
        CHECK(hasLine(r.svc, "rack group new") && g >= 0 && drone >= 0, "Ctrl+G grouped the selection");
        CHECK(rt->rowVisible(g) && !rt->isOpen(g) && !rt->rowVisible(drone), "the new group is SHUT in the tree: its member is inside it");
        CHECK(deck->cellOfRack(g) >= 0 && deck->cellOfRack(drone) < 0, "the strip shows the group's chip, not its member");
        // double-click the chip: drill in, and the tree opens the same group, its chevron turning
        const Point gc = world(*strip, strip->cellXForTest(deck->cellOfRack(g)) + 40.0, 40.0);
        dblClick(r, gc.x, gc.y);
        CHECK(deck->wantedLevel() == r.svc.m.rack[(size_t)g].rackObj, "double-clicking the folder chip drills the strip into it");
        CHECK(rt->isOpen(g), "…and opens it in the tree");
        const double oa = firstMoved(r, [&] { return rt->openAmount(g); }, 0.0);
        CHECK(strictlyBetween(oa, 0.0, 1.0), "the chevron TURNS open (first frame between)");
        r.settle();
        CHECK(deck->shownLevel() == r.svc.m.rack[(size_t)g].rackObj && deck->cellOfRack(drone) == 0 && rt->rowVisible(drone),
              "inside: the strip holds the member, the tree shows it");
        path = deck->crumbPath();
        CHECK(path.size() == 2 && path[1] == "Group", "the breadcrumb names the open group");
        // the chevron shuts it again; the member's row leaves as a fading ghost
        Point chev = centre(*rt, rt->chevronRect(g));
        r.click(chev.x, chev.y);
        r.frame();
        CHECK(!rt->isOpen(g) && !rt->rowVisible(drone), "the chevron shuts the group");
        // double-click a group ROW in the tree: the strip opens it
        r.svc.lines.clear();
        const Point g1 = centre(*rt, rt->rowRect(0));
        dblClick(r, g1.x - 40.0, g1.y);
        CHECK(deck->wantedLevel() == "ro1" && rt->isOpen(0), "double-clicking a group row opens it in the strip and the tree");
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // a failing assert must not swallow the log above it
    installInterstellarAccent();
    arstro::cosmo_v2::registerEmbeddedFonts();
    testAccent();
    testGradeCommands();
    testRackCommands();
    testVersionCommands();
    testCutCommands();
    testTransportAndDeliver();
    testHomeAndShell();
    testMotion();
    testLayout();
    testTextFit();
    testMonitorImages();
    testScroll();
    testMenusSettingsScale();
    testSelectionAndContext();
    testCaptureAndGradeMonitor();
    testRefFrameSlider();
    testGroupBrowsing();
    testVariants();
    testMonitorZoom();
    testCutEditing();
    testPluginList();
    testKeyframes();
    testScopes();
    testColourManagement();
    testLuts();
    testDeliverSound();
    testSoundUi();
    testInterchangeUi();
    testEditingUi();
    testMulticamUi();
    testProxiesUi();
    testRelinkUi();
    testMatteUi();
    testStillsUi();
    testNodeGraphUi();
    testSafetyUi();
    testRenderPresetsUi();
    testBurnInsUi();
    testCaptionsUi();
    testPackagesUi();
    std::printf("\ninterstellar_app_ui_tests: %d checks passed\n", gChecks);
    return 0;
}
