/*
 *  arstro-shell — L2 tests: the real service, driven by text lines, no display (arstro.rule §4).
 *  Plain asserts in the suite's existing style; no framework.
 */
#include "../core/AppRegistry.h"
#include "../core/ShellService.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace arstro::shell;

static int gFailed = 0, gChecks = 0;
#define CHECK(cond) do { ++gChecks; if (!(cond)) { ++gFailed; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

struct Rig
{
    ShellService svc;
    std::vector<std::string> events;
    Rig()
    {
        svc.subscribe([this](const Event &e) { events.push_back(formatEvent(e)); });
        svc.setScanner([] {
            std::vector<AppInfo> a(3);
            a[0].id = "arstro.settings"; a[0].name = "Settings"; a[0].exec = "/x/settings"; a[0].system = true;
            a[1].id = "arstro.clock"; a[1].name = "Clock"; a[1].exec = "/x/clock";
            a[2].id = "arstro.notes"; a[2].name = "Notes"; a[2].exec = "/x/notes";
            return a;
        });
        svc.run("screen 1280 800");
        svc.run("apps rescan");
        events.clear();
    }
    bool saw(const std::string &prefix) const
    {
        for (const std::string &e : events)
            if (e.compare(0, prefix.size(), prefix) == 0) return true;
        return false;
    }
    /** A straight swipe: `steps` samples, `ms` long, then up. */
    void swipe(double x0, double y0, double x1, double y1, double ms, int steps = 12, double holdMs = 0)
    {
        static double t = 1000;
        t += 500;
        char b[128];
        std::snprintf(b, sizeof b, "touch down %g %g %g", x0, y0, t);
        svc.run(b);
        for (int i = 1; i <= steps; ++i)
        {
            const double k = (double)i / steps;
            std::snprintf(b, sizeof b, "touch move %g %g %g", x0 + (x1 - x0) * k, y0 + (y1 - y0) * k, t + ms * k);
            svc.run(b);
        }
        if (holdMs > 0)
        {
            std::snprintf(b, sizeof b, "touch move %g %g %g", x1, y1, t + ms + holdMs);
            svc.run(b);
        }
        std::snprintf(b, sizeof b, "touch up %g %g %g", x1, y1, t + ms + holdMs + 1);
        svc.run(b);
    }
};

static void test_launch_focus_and_lifecycle()
{
    Rig r;
    CHECK(r.svc.run("app launch arstro.clock"));
    CHECK(r.saw("task.spawn task=1 app=arstro.clock"));
    CHECK(r.saw("nav.changed state=app task=1"));
    CHECK(r.svc.model().tasks[0].state == TaskState::Starting);
    CHECK(r.svc.run("task connected 1"));
    CHECK(r.saw("task.state task=1 state=running"));
    // A second app pauses the first; launching the first again focuses it, it does not respawn.
    r.svc.run("app launch arstro.notes");
    r.svc.run("task connected 2");
    CHECK(r.svc.model().task(1)->state == TaskState::Paused);
    r.events.clear();
    CHECK(r.svc.run("app launch arstro.clock"));
    CHECK(!r.saw("task.spawn"));
    CHECK(r.svc.model().focusedTask == 1);
    CHECK(r.svc.model().tasks[0].id == 1);  // most recently used first
    CHECK(r.svc.model().task(1)->state == TaskState::Running);
    CHECK(r.svc.model().task(2)->state == TaskState::Paused);
}

static void test_unknown_input_is_rejected_by_name()
{
    Rig r;
    CHECK(!r.svc.run("app launch arstro.nope"));
    CHECK(r.svc.model().lastError.find("arstro.nope") != std::string::npos);
    CHECK(r.saw("error message="));
    CHECK(!r.svc.run("set brightnes=0.5"));
    CHECK(r.svc.model().lastError.find("brightnes") != std::string::npos);
    CHECK(!r.svc.run("frobnicate"));
    CHECK(!r.svc.run("nav home now"));
    CHECK(!r.svc.run("set brightness=7"));
    CHECK(r.svc.model().settings.at("brightness") == "1");
    // All or nothing: a bad second pair leaves the first unapplied.
    r.events.clear();
    CHECK(!r.svc.run("set volume=0.2 wifi=maybe"));
    CHECK(r.svc.model().settings.at("volume") == "0.6");
    CHECK(!r.saw("setting.changed"));
    CHECK(r.svc.run("set volume=0.2 wifi=off"));
    CHECK(r.svc.model().settings.at("wifi") == "false");
    CHECK(r.saw("setting.changed key=volume value=0.2"));
    CHECK(r.svc.model().lastError.empty());
}

static void test_home_gesture_fast_goes_home_slow_goes_recents()
{
    Rig r;
    r.svc.run("app launch arstro.clock");
    r.svc.run("task connected 1");
    r.events.clear();
    // A fast flick from the bottom edge: home.
    r.swipe(640, 795, 640, 560, 110);
    CHECK(r.saw("gesture.begin kind=home"));
    CHECK(r.saw("gesture.end kind=home result=home"));
    CHECK(r.svc.model().nav == NavState::Home);
    CHECK(r.svc.model().task(1)->state == TaskState::Paused);
    // Back into the app, then a slow drag that pauses before lifting: recents.
    r.svc.run("task focus 1");
    r.events.clear();
    r.swipe(640, 795, 640, 600, 500, 12, 200);
    CHECK(r.saw("gesture.end kind=home result=recents"));
    CHECK(r.svc.model().nav == NavState::Recents);
    // From recents a swipe up goes home.
    r.swipe(640, 795, 640, 600, 300);
    CHECK(r.svc.model().nav == NavState::Home);
    // A short nudge is cancelled: nothing changes.
    r.svc.run("task focus 1");
    r.events.clear();
    r.swipe(640, 795, 640, 775, 200, 6, 200);
    CHECK(r.saw("gesture.end kind=home result=cancelled"));
    CHECK(r.svc.model().nav == NavState::App);
}

static void test_a_tap_or_a_sideways_drag_on_the_edge_stays_the_apps()
{
    Rig r;
    r.svc.run("app launch arstro.clock");
    r.svc.run("task connected 1");
    r.events.clear();
    r.svc.run("touch down 640 795 10");
    CHECK(!r.svc.lastTouch().consumed);
    r.svc.run("touch up 640 795 60");
    CHECK(!r.svc.lastTouch().consumed);
    CHECK(!r.saw("gesture.begin"));
    r.swipe(300, 795, 700, 790, 200);  // sideways along the bottom edge
    CHECK(!r.saw("gesture.begin"));
    // The claim arrives on the sample that crosses the slop, and only then is the app cancelled.
    r.svc.run("touch down 640 795 2000");
    r.svc.run("touch move 640 790 2010");
    CHECK(!r.svc.lastTouch().claimedNow && !r.svc.lastTouch().consumed);
    r.svc.run("touch move 640 780 2020");
    CHECK(r.svc.lastTouch().claimedNow && r.svc.lastTouch().consumed);
    CHECK(r.svc.model().gesture.kind == GestureKind::Home);
    CHECK(r.svc.model().gesture.dy == -15);
    r.svc.run("touch cancel 640 780 2030");
    CHECK(r.saw("gesture.end kind=home result=cancelled"));
    CHECK(r.svc.model().gesture.kind == GestureKind::None);
}

static void test_back_gesture_and_back_routing()
{
    Rig r;
    r.svc.run("app launch arstro.clock");
    r.svc.run("task connected 1");
    r.events.clear();
    r.swipe(4, 400, 120, 404, 180);
    CHECK(r.saw("gesture.begin kind=back"));
    CHECK(r.saw("gesture.end kind=back result=back"));
    CHECK(r.saw("task.back task=1"));           // the app is asked first
    CHECK(r.svc.model().nav == NavState::App);  // and nothing moves until it answers
    CHECK(r.svc.run("task back-unhandled 1"));
    CHECK(r.svc.model().nav == NavState::Home);
    // From the right edge too; a short pull is cancelled.
    r.svc.run("task focus 1");
    r.events.clear();
    r.swipe(1276, 400, 1250, 400, 180);
    CHECK(r.saw("gesture.end kind=back result=cancelled"));
    r.swipe(1276, 400, 1150, 400, 180);
    CHECK(r.saw("task.back task=1"));
    // On the home screen the side edges are not a gesture at all.
    r.svc.run("nav home");
    r.events.clear();
    r.swipe(4, 400, 200, 400, 180);
    CHECK(!r.saw("gesture.begin"));
    // Back closes the shade first, and leaves recents.
    r.svc.run("shade open");
    r.svc.run("nav back");
    CHECK(!r.svc.model().shadeOpen);
    r.svc.run("nav recents");
    r.svc.run("nav back");
    CHECK(r.svc.model().nav == NavState::App);
}

static void test_shade_gestures()
{
    Rig r;
    r.swipe(640, 6, 640, 420, 250);
    CHECK(r.saw("gesture.begin kind=shade-open"));
    CHECK(r.svc.model().shadeOpen);
    // While it is open nothing else can be claimed, and a press in the middle is not a gesture.
    r.events.clear();
    r.swipe(640, 400, 640, 200, 200);
    CHECK(!r.saw("gesture.begin"));
    r.swipe(640, 790, 640, 300, 250);
    CHECK(r.saw("gesture.end kind=shade-close result=shade-closed"));
    CHECK(!r.svc.model().shadeOpen);
    // A small pull that is let go slowly springs back.
    r.events.clear();
    r.swipe(640, 6, 640, 90, 400, 12, 200);
    CHECK(r.saw("gesture.end kind=shade-open result=cancelled"));
    CHECK(!r.svc.model().shadeOpen);
}

static void test_close_and_exit()
{
    Rig r;
    r.svc.run("app launch arstro.clock");
    r.svc.run("app launch arstro.notes");
    r.svc.run("nav recents");
    r.events.clear();
    CHECK(r.svc.run("app close 2"));
    CHECK(r.saw("task.kill task=2"));
    CHECK(r.saw("task.closed task=2 app=arstro.notes"));
    CHECK(r.svc.model().nav == NavState::Recents);  // a card is left, so recents stays
    CHECK(r.svc.model().focusedTask == 1);
    CHECK(r.svc.run("app close 1"));
    CHECK(r.svc.model().nav == NavState::Home);
    CHECK(r.svc.model().tasks.empty());
    // A process that dies on its own is removed without a kill.
    r.svc.run("app launch arstro.clock");
    r.events.clear();
    CHECK(r.svc.run("task exited 3"));
    CHECK(!r.saw("task.kill"));
    CHECK(r.svc.model().nav == NavState::Home);
    CHECK(!r.svc.run("app close 99"));
}

static void test_notifications()
{
    Rig r;
    CHECK(r.svc.run("notify post title=\"Build finished\" body=\"cosmo 2.1 is ready\" app=arstro.settings"));
    CHECK(r.svc.run("notify post title=Second"));
    CHECK(r.svc.model().notifications.size() == 2);
    CHECK(r.svc.model().notifications[0].title == "Second");  // newest first
    CHECK(r.svc.model().notifications[1].body == "cosmo 2.1 is ready");
    CHECK(!r.svc.run("notify post body=orphan"));
    CHECK(!r.svc.run("notify post title=x colour=red"));
    CHECK(r.svc.run("notify dismiss 1"));
    CHECK(!r.svc.run("notify dismiss 1"));
    CHECK(r.svc.run("notify clear"));
    CHECK(r.svc.model().notifications.empty());
}

static void test_codec_round_trip_and_dump_is_stable()
{
    for (const char *line : {"app launch arstro.clock", "app close 3", "apps rescan", "nav home", "nav back",
                             "nav recents", "task focus 2", "task connected 2", "task exited 2",
                             "task back-unhandled 2", "shade open", "shade close", "shade toggle",
                             "set brightness=0.5 device-name=\"My Pi\"", "notify post title=\"a b\" body=c",
                             "notify dismiss 4", "notify clear", "touch down 1 2 3", "touch cancel 4.5 6 7",
                             "screen 800 480"})
    {
        Command c;
        std::string err;
        CHECK(parseCommand(line, c, err));
        CHECK(formatCommand(c) == line);
    }
    // Every kind in the table is covered above: the table and the test cannot drift apart.
    CHECK(commandSpecs().size() == 19);
    Rig a, b;
    for (Rig *r : {&a, &b})
    {
        r->svc.run("app launch arstro.clock");
        r->svc.run("set dnd=true");
    }
    // A gesture in flight and extra revisions must not show in the dump.
    b.svc.run("touch down 640 795 10");
    b.svc.run("touch move 640 700 30");
    CHECK(a.svc.dump() == b.svc.dump());
    CHECK(a.svc.dump().find("task 1 app=arstro.clock state=starting") != std::string::npos);
}

static void test_manifest()
{
    AppInfo a;
    std::string err;
    CHECK(parseManifest("# c\nid = arstro.clock\nname = Clock\nexec = bin/clock\nicon = icon.png\naccent = #FF8800\npermissions = system\n",
                        "/apps/clock", a, err));
    CHECK(a.exec == "/apps/clock/bin/clock" && a.icon == "/apps/clock/icon.png" && a.system);
    CHECK(!parseManifest("id = bad id\nname = x\nexec = y\n", "/d", a, err));
    CHECK(!parseManifest("id = a.b\nname = x\n", "/d", a, err) && err == "missing exec");
    CHECK(!parseManifest("id = a.b\nname = x\nexec = y\ncolour = red\n", "/d", a, err));
    CHECK(err.find("colour") != std::string::npos);
}

int main(int argc, char **argv)
{
    // --api-check <dir>: the committed API document must equal what the tables generate.
    if (argc == 3 && std::string(argv[1]) == "--api-check")
    {
        for (const auto &f : {std::make_pair(std::string("api.json"), apiJson()), std::make_pair(std::string("API.md"), apiMarkdown())})
        {
            std::ifstream in(std::string(argv[2]) + "/" + f.first);
            std::stringstream ss;
            ss << in.rdbuf();
            CHECK(in.good() || !ss.str().empty());
            CHECK(ss.str() == f.second);
        }
    }
    else
    {
        test_launch_focus_and_lifecycle();
        test_unknown_input_is_rejected_by_name();
        test_home_gesture_fast_goes_home_slow_goes_recents();
        test_a_tap_or_a_sideways_drag_on_the_edge_stays_the_apps();
        test_back_gesture_and_back_routing();
        test_shade_gestures();
        test_close_and_exit();
        test_notifications();
        test_codec_round_trip_and_dump_is_stable();
        test_manifest();
    }
    std::printf("%d checks, %d failed\n", gChecks, gFailed);
    return gFailed ? 1 : 0;
}
