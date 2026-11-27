/*
 *  arstro-shell — Scene tests: the motion law, MEASURED (arstro.rule.design §1, arstro.rule.animation).
 *
 *  A transition that is correct at rest passes any test that settles and then looks. These step
 *  one frame at a time and assert on the LIVE values: that they differ from their targets
 *  mid-flight, that a finger moves a value exactly, that a card keeps the screen's proportions,
 *  and that a leaving card is still travelling while it fades and is not cut off at partial
 *  opacity.
 */
#include "../core/ShellService.h"
#include "../host/Scene.h"
#include <cmath>
#include <cstdio>

using namespace arstro::shell;
using artboard::Rect;

static int gFailed = 0, gChecks = 0;
#define CHECK(cond) do { ++gChecks; if (!(cond)) { ++gFailed; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const double W = 960, H = 540, DT = 1.0 / 120.0;
static const Rect kIcon{398, 392, 60, 60};

struct Rig
{
    ShellService svc;
    Scene scene;
    double t = 5000;
    Rig()
    {
        svc.setScanner([] {
            std::vector<AppInfo> a(3);
            const char *ids[3] = {"a.clock", "a.notes", "a.settings"};
            for (int i = 0; i < 3; ++i) { a[i].id = ids[i]; a[i].name = ids[i]; a[i].exec = "/x"; }
            return a;
        });
        svc.run("screen 960 540");
        svc.run("apps rescan");
        step(10);
    }
    void step(int frames = 1)
    {
        for (int i = 0; i < frames; ++i)
            scene.advance(svc.model(), DT, [](const std::string &, Rect &out) { out = kIcon; return true; });
    }
    void settle() { step(600); }
    const CardPose *pose(int task) const
    {
        for (const CardPose &c : scene.cards())
            if (c.task == task) return &c;
        return nullptr;
    }
    void touch(const char *phase, double x, double y)
    {
        char b[96];
        t += 8;
        std::snprintf(b, sizeof b, "touch %s %g %g %g", phase, x, y, t);
        svc.run(b);
    }
};

static bool near(double a, double b, double eps = 0.5) { return std::fabs(a - b) <= eps; }

static void test_launch_grows_out_of_the_icon_and_is_eased()
{
    Rig r;
    r.svc.run("app launch a.clock");
    r.step(1);
    const CardPose *c = r.pose(1);
    // One frame in: barely there, and still at the icon — not already full screen.
    CHECK(r.scene.presenceOf(1) > 0.0 && r.scene.presenceOf(1) < 0.2);
    CHECK(!c || c->rect.w < 200.0);
    r.step(12);
    c = r.pose(1);
    CHECK(c && c->rect.w > kIcon.w + 20 && c->rect.w < W - 20);  // mid-flight: neither end
    CHECK(c && c->alpha > 0.0);
    const double midW = c ? c->rect.w : 0;
    r.step(1);
    CHECK(r.pose(1) && r.pose(1)->rect.w > midW);                 // and still moving
    r.settle();
    c = r.pose(1);
    CHECK(c && near(c->rect.x, 0) && near(c->rect.y, 0) && near(c->rect.w, W) && near(c->rect.h, H));
    CHECK(c && near(c->alpha, 1.0, 0.01) && near(c->radius, 0.0, 0.1) && c->iconMix < 0.01);
    CHECK(near(r.scene.home(), 0.0, 0.01));
    CHECK(!r.scene.animating());
}

static void test_going_home_fades_while_travelling_and_never_pops()
{
    Rig r;
    r.svc.run("app launch a.clock");
    r.settle();
    r.svc.run("nav home");
    double lastAlpha = 1.0, lastW = W, maxAlphaStep = 0.0;
    bool sawFadingAndMoving = false, endedAtIcon = false;
    for (int i = 0; i < 600; ++i)
    {
        r.step(1);
        const CardPose *c = r.pose(1);
        const double a = c ? c->alpha : 0.0;
        maxAlphaStep = std::max(maxAlphaStep, std::fabs(a - lastAlpha));
        // Rule 3: partly transparent AND still changing size — not "arrive, stop, then fade".
        if (c && a > 0.15 && a < 0.85 && std::fabs(c->rect.w - lastW) > 0.05) sawFadingAndMoving = true;
        if (c && a < 0.1) endedAtIcon = c->rect.w < kIcon.w + 60.0;  // its last visible frames are at the icon
        lastAlpha = a;
        if (c) lastW = c->rect.w;
    }
    CHECK(sawFadingAndMoving);
    CHECK(endedAtIcon);
    CHECK(maxAlphaStep < 0.06);          // no pop: opacity never jumps between two 8 ms frames
    CHECK(r.pose(1) == nullptr);          // and it ends drawn not at all, rather than left at 1%
    CHECK(near(r.scene.home(), 1.0, 0.01));
}

static void test_a_card_in_the_row_always_has_the_screens_proportions()
{
    Rig r;
    r.svc.run("app launch a.clock");
    r.svc.run("app launch a.notes");
    r.settle();
    r.svc.run("nav recents");
    for (int i = 0; i < 300; ++i)
    {
        r.step(1);
        for (const CardPose &c : r.scene.cards())
            CHECK(std::fabs(c.rect.w / c.rect.h - W / H) < 1e-6);  // Rule 1: a container, uniformly scaled
    }
    const CardPose *front = r.pose(2), *older = r.pose(1);
    CHECK(front && near(front->rect.w, W * Scene::kRecentsScale));
    CHECK(front && near(front->rect.x + front->rect.w * 0.5, W * 0.5));  // most recent, centred
    CHECK(older && older->rect.x < front->rect.x);                        // older to the left
    CHECK(front && front->header > 0.99);
}

static void test_a_gesture_moves_values_exactly_and_release_eases()
{
    Rig r;
    r.svc.run("app launch a.clock");
    r.settle();
    r.touch("down", 480, 536);
    r.touch("move", 480, 500);
    r.touch("move", 480, 455);  // 81 dp up
    r.step(1);
    // The finger is the animation: zoom is a pure function of the travel, with no easing lag.
    CHECK(near(r.scene.zoom(), 81.0 / (H * 0.30), 1e-9));
    r.touch("move", 480, 400);
    r.step(1);
    CHECK(near(r.scene.zoom(), 136.0 / (H * 0.30), 1e-9));
    const double held = r.scene.zoom();
    // Hold, then lift: recents. The zoom carries on FROM where the finger left it.
    r.t += 300;
    r.touch("move", 480, 400);
    r.touch("up", 480, 400);
    CHECK(r.svc.model().nav == NavState::Recents);
    r.step(1);
    CHECK(r.scene.zoom() > held && r.scene.zoom() < 1.0);
    r.settle();
    CHECK(near(r.scene.zoom(), 1.0, 0.01));

    // The shade follows the finger the same way.
    r.svc.run("nav home");
    r.settle();
    r.touch("down", 480, 5);
    r.touch("move", 480, 60);
    r.touch("move", 480, 167);
    r.step(1);
    CHECK(near(r.scene.shade(), 162.0 / (H * 0.6), 1e-9));
    r.touch("cancel", 480, 167);
    r.step(1);
    CHECK(r.scene.shade() > 0.0 && r.scene.shade() < 0.5);  // springing back, not snapped shut
    r.settle();
    CHECK(near(r.scene.shade(), 0.0, 0.01));
}

static void test_recents_scroll_tap_and_flick_to_close()
{
    Rig r;
    r.svc.run("app launch a.clock");
    r.svc.run("app launch a.notes");
    r.svc.run("app launch a.settings");
    r.svc.run("nav recents");
    r.settle();
    std::vector<std::string> cmds;
    const Model &m = r.svc.model();
    const double pitch = W * Scene::kRecentsScale + Scene::kRecentsGap;
    // Drag right by exactly one card: the row follows the finger 1:1, then lands on a card.
    r.scene.recentsTouch({TouchSample::Kind::Down, 480, 300, 100}, m, cmds);
    r.scene.recentsTouch({TouchSample::Kind::Move, 480 + pitch * 0.5, 300, 300}, m, cmds);
    r.step(1);
    CHECK(near(r.scene.scroll(), 0.5, 1e-6));
    r.scene.recentsTouch({TouchSample::Kind::Move, 480 + pitch, 302, 700}, m, cmds);
    r.scene.recentsTouch({TouchSample::Kind::Up, 480 + pitch, 302, 1200}, m, cmds);
    r.settle();
    CHECK(near(r.scene.scroll(), 1.0, 0.01));
    CHECK(cmds.empty());
    // Now the second card is centred: a tap on it focuses it.
    r.scene.recentsTouch({TouchSample::Kind::Down, 480, 300, 2000}, m, cmds);
    r.scene.recentsTouch({TouchSample::Kind::Up, 480, 300, 2050}, m, cmds);
    CHECK(cmds.size() == 1 && cmds[0] == "task focus 2");
    cmds.clear();
    // A flick upward closes it — and the card finishes leaving after its task is gone.
    r.scene.recentsTouch({TouchSample::Kind::Down, 480, 300, 3000}, m, cmds);
    r.scene.recentsTouch({TouchSample::Kind::Move, 480, 240, 3030}, m, cmds);
    r.scene.recentsTouch({TouchSample::Kind::Move, 480, 150, 3060}, m, cmds);
    r.step(1);
    const CardPose *c = r.pose(2);
    CHECK(c && c->alpha < 1.0 && c->alpha > 0.3);  // already fading with the distance thrown
    r.scene.recentsTouch({TouchSample::Kind::Up, 480, 150, 3070}, m, cmds);
    CHECK(cmds.size() == 1 && cmds[0] == "app close 2");
    r.svc.run(cmds[0]);
    r.step(1);
    CHECK(r.pose(2) != nullptr);                    // gone from the model, still on screen
    const double y1 = r.pose(2)->rect.y;
    r.step(4);
    CHECK(r.pose(2) && r.pose(2)->rect.y < y1);     // and still travelling upward while it fades
    r.settle();
    CHECK(r.pose(2) == nullptr);
    // A tap on empty space leaves recents.
    cmds.clear();
    r.scene.recentsTouch({TouchSample::Kind::Down, 20, 500, 9000}, m, cmds);
    r.scene.recentsTouch({TouchSample::Kind::Up, 20, 500, 9040}, m, cmds);
    CHECK(cmds.size() == 1 && cmds[0] == "nav back");
}

static void test_empty_recents_says_so()
{
    Rig r;
    r.svc.run("nav recents");
    r.step(3);
    CHECK(r.scene.emptyRecents() > 0.0 && r.scene.emptyRecents() < 1.0);  // fading in
    r.settle();
    CHECK(near(r.scene.emptyRecents(), 1.0, 0.01));
}

int main()
{
    test_launch_grows_out_of_the_icon_and_is_eased();
    test_going_home_fades_while_travelling_and_never_pops();
    test_a_card_in_the_row_always_has_the_screens_proportions();
    test_a_gesture_moves_values_exactly_and_release_eases();
    test_recents_scroll_tap_and_flick_to_close();
    test_empty_recents_says_so();
    std::printf("%d checks, %d failed\n", gChecks, gFailed);
    return gFailed ? 1 : 0;
}
