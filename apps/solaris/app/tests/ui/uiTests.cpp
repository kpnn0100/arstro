// solaris_ui tests — the assembled App over the REAL service: clicks at the geometry the widgets
// publish, asserting the command lines that went out and the model that came back, and that every
// transition is a tween — a LIVE value caught between its ends, never only the end (design rule §1).
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../Rig.h"
#include "../../widgets/DevicePanel.h"
#include "../../widgets/PianoRoll.h"
#include "../../../../cosmo/widgets/SliderRow.h"
using arstro::solaris_ui::Timeline;
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{
    int passed = 0;
    void pass(const char *n)
    {
        std::printf("[PASS] %s\n", n);
        ++passed;
    }
    bool contains(const std::string &s, const std::string &p) { return s.find(p) != std::string::npos; }
    bool sentLine(const sltest::Rig &r, const std::string &line)
    {
        for (const auto &l : r.sent)
            if (l == line) return true;
        return false;
    }
}

static void test_new_song_cross_fades_from_home()
{
    sltest::Rig r("ui-new", 1280, 800);
    r.settle();
    bool asked = false;
    r.app->onPickSongToCreate = [&] { asked = true; r.app->newSongPicked(r.song("First")); };
    r.click(r.app->home().actionRect(0));
    r.frame();
    assert(asked && sentLine(r, "project new " + r.song("First") + ".slp")); // a picked name gains .slp when it has none
    // mid-way through the cross-fade both screens are partly visible — a tween, not a cut
    bool between = false;
    for (int i = 0; i < 20; ++i)
    {
        r.frame();
        const double p = r.app->screenOpacity("project");
        if (p > 0.05 && p < 0.95) between = true;
    }
    assert(between);
    r.settle();
    assert(r.app->screen() == "project" && r.app->screenOpacity("project") == 1.0 && r.app->screenOpacity("home") == 0.0);
    pass("New song: the picker's path becomes `project new`, and Home cross-fades to the song (a tween, caught mid-way)");
}

static void test_settings_chips_send_lines_and_ease()
{
    sltest::Rig r("ui-settings", 1280, 800);
    r.settle();
    r.app->openSettings();
    assert(sentLine(r, "devices list"));
    r.frame();
    r.frame();
    const double mid = r.app->settings().appearAmount();
    assert(mid > 0.0 && mid < 1.0);                                       // the fade, caught
    r.settle();
    assert(r.app->settings().isOpen() && r.app->settings().appearAmount() == 1.0);
    assert(r.app->settings().chipCount(0) == 4);                          // System default + three outputs
    r.click(r.app->settings().chipRect(0, 3));                            // Scarlett 2i2 USB
    assert(sentLine(r, "settings set output=usb_interface") && r.svc->model().settings.output == "usb_interface");
    r.frame();
    r.frame();
    const double chosen = r.app->settings().chosenAmount(0, 3);
    assert(chosen > 0.0 && chosen < 1.0);                                 // the chip's fill eases in
    r.settle();
    assert(r.app->settings().chosenAmount(0, 3) == 1.0 && r.app->settings().chosenAmount(0, 0) == 0.0);
    r.click(r.app->settings().chipRect(3, 1));                            // buffer 128
    assert(r.svc->model().settings.bufferSize == 128);
    // a setting changed from a shell shows here too: the sheet draws the model
    r.cmd("settings set sampleRate=96000");
    r.settle();
    assert(r.app->settings().chosenAmount(2, 3) == 1.0);
    r.key(27);                                                            // Escape closes, eased
    r.frame();
    r.frame();
    assert(r.app->settings().appearAmount() < 1.0 && r.app->settings().appearAmount() > 0.0);
    r.settle();
    assert(!r.app->settings().isOpen());
    pass("Settings: each chip is a `settings set` line, the chosen fill eases, a shell's change shows, Escape fades it shut");
}

static void test_settings_folders()
{
    sltest::Rig r("ui-folders", 1280, 800);
    r.cmd("folder add /music/Samples");
    r.cmd("folder add /music/Loops");
    r.settle();
    r.app->openSettings();
    r.settle();
    r.click(r.app->settings().folderRemoveRect(0));
    assert(sentLine(r, "folder remove /music/Samples") && r.svc->model().settings.folders == std::vector<std::string>{"/music/Loops"});
    r.app->onPickFolder = [](std::function<void(const std::string &)> done) { done("/music/One Shots"); };
    r.settle();
    r.app->settings().revealRect(r.app->settings().addFolderRect()); // the sheet scrolls (R6): bring the button in first
    r.settle();
    r.click(r.app->settings().addFolderRect());
    assert(sentLine(r, "folder add \"/music/One Shots\"") && r.svc->model().settings.folders.size() == 2);
    r.app->onPickFolder = [](std::function<void(const std::string &)> done) { done("/nowhere"); };
    r.settle(); // the list grew: the button moved down a row — aim at where it is NOW
    r.app->settings().revealRect(r.app->settings().addFolderRect());
    r.settle();
    r.click(r.app->settings().addFolderRect());
    r.frame();
    assert(contains(r.app->toastText(), "cannot list /nowhere"));          // a refusal is SAID
    pass("Settings: remove and add sample folders as `folder` lines; a folder that cannot be listed is a toast, not silence");
}

static void test_song_bar_and_keys()
{
    sltest::Rig r("ui-bar", 1280, 800);
    r.cmd("project new " + r.song("Bar") + ".slp --name Bar");
    r.cmd("strip add --kind instrument --instrument drums");
    r.settle();
    assert(r.svc->model().dirty);
    r.click(r.app->project().bar().hitRect(1));                           // Play: this rig has no output
    r.pump(200.0);
    assert(sentLine(r, "transport play") && contains(r.app->toastText(), "cannot play audio"));
    assert(r.app->toastAmount() > 0.5);
    r.click(r.app->project().bar().hitRect(2));                           // Save
    assert(sentLine(r, "project save") && !r.svc->model().dirty);
    r.key(32);                                                            // Space = play/stop
    assert(r.sent.back() == "transport play");
    r.key('S', true);                                                     // Ctrl+S
    assert(r.sent.back() == "project save");
    // undo / redo from the keyboard are the service's lines (R-EDM-1)
    r.key('Z', true);
    r.key('Y', true);
    assert(sentLine(r, "undo") && sentLine(r, "redo"));
    pass("Song bar: Play, Save, Space and Ctrl+S are command lines; a refused play is a toast with the service's reason");
}

static void test_home_unsaved_confirm_and_recents()
{
    sltest::Rig r("ui-home", 1280, 800);
    r.cmd("project new " + r.song("Keep") + ".slp --name Keep");
    r.cmd("project close");
    r.cmd("project new " + r.song("Draft") + ".slp --name Draft");
    r.cmd("strip add --kind bus");
    r.settle();
    r.click(r.app->project().bar().hitRect(0));                           // Home, unsaved → asked
    r.settle();
    assert(r.app->confirm().isOpen() && r.app->screen() == "project");
    r.app->confirm().activate(1);                                         // Discard
    r.settle();
    assert(r.app->screen() == "home" && sentLine(r, "project close"));
    assert(r.app->home().cardCount() == 2);
    r.click(r.app->home().cardLive(1), 2);                                // right-click: forget it
    r.settle();
    assert(r.app->home().cardCount() == 1 && sentLine(r, "recents remove " + r.song("Keep") + ".slp"));
    r.click(r.app->home().cardLive(0));                                   // open the other
    r.settle();
    assert(r.app->screen() == "project" && r.svc->model().projectName == "Draft");
    pass("Home: unsaved changes ask first (Discard closes); a card opens its song, right-click forgets it");
}

using sltest::cx;
using sltest::cy;
using sltest::world;

static void test_browser_tabs_and_sample_drag()
{
    sltest::Rig r("ui-browse", 1280, 800);
    r.cmd("folder add /music/Samples");
    r.cmd("project new " + r.song("Browse") + ".slp --bpm 120");
    r.settle();
    auto &b = r.app->project().browser();
    auto &tl = r.app->project().timeline();
    // the tab highlight SLIDES
    const double x0 = b.tabHighlightX();
    r.click(world(b, b.tabRect(1)));
    r.frame();
    r.frame();
    const double mid = b.tabHighlightX();
    assert(mid > x0 && mid < b.tabRect(1).x);
    r.settle();
    assert(b.tab() == 1 && b.row(1).value == "synth" && b.row(1).label == "Basic Synth");
    // back to Samples: the folder from Settings, browsed by a click
    r.click(world(b, b.tabRect(0)));
    r.settle();
    assert(b.rowCount() == 1 && b.row(0).kind == "folder");
    r.click(world(b, b.rowRect(0)));
    r.settle();
    assert(sentLine(r, "browse /music/Samples"));
    int kick = -1;
    for (int i = 0; i < b.rowCount(); ++i)
        if (b.row(i).label == "808 kick.wav") kick = i;
    assert(kick > 0);
    // drag it onto the lanes, at beat 4 (no lanes yet: below the last = a new one)
    const artboard::Rect from = world(b, b.rowRect(kick));
    const double toX = world(tl, artboard::Rect{tl.beatToX(4.0), 0, 0, 0}).x + 2.0;
    const double toY = world(tl, artboard::Rect{0, Timeline::kRulerH + 20.0, 0, 0}).y;
    r.drag(cx(from), cy(from), toX, toY, 8, false);
    assert(r.app->project().ghostAmount() > 0.0);                          // the ghost follows the pointer
    r.app->pointer(2, toX, toY, 0, r.now);
    r.settle();
    assert(sentLine(r, "clip add --src \"/music/Samples/808 kick.wav\" --at 4"));
    const auto &m = r.svc->model();
    assert(m.clips.size() == 1 && m.clips[0].at == 4.0 && m.strips.size() == 2 && m.lanes.size() == 1); // its own strip, a new lane
    assert(r.app->project().ghostAmount() == 0.0);
    pass("Browser: the tab highlight slides; a sample folder is browsed; a sample dragged to the lanes is `clip add` at the drop beat");
}

static void test_instrument_drop_and_clip_drag()
{
    sltest::Rig r("ui-drop", 1280, 800);
    r.cmd("project new " + r.song("Drop") + ".slp --bpm 120");
    r.cmd("lane add Beats");
    r.cmd("lane add Bass");
    r.settle();
    auto &b = r.app->project().browser();
    auto &tl = r.app->project().timeline();
    r.click(world(b, b.tabRect(1)));
    r.settle();
    int drums = -1;
    for (int i = 0; i < b.rowCount(); ++i)
        if (b.row(i).value == "drums") drums = i;
    const artboard::Rect from = world(b, b.rowRect(drums));
    const artboard::Rect lane0 = world(tl, tl.rowRect(0));
    r.drag(cx(from), cy(from), world(tl, artboard::Rect{tl.beatToX(2.0), 0, 0, 0}).x + 2.0, cy(lane0));
    r.settle();
    assert(sentLine(r, "clip add --instrument drums --at 2 --length 4 --lane ln_1"));   // one drop, one line (R-BROWSE-3)
    assert(!sentLine(r, "strip add --kind instrument --instrument drums"));
    bool made = false;
    for (const auto &st : r.svc->model().strips) made |= st.id == r.svc->model().clips[0].track && st.kind == "instrument";
    assert(made);
    // drag that clip: +2 beats, down a lane — it follows the pointer exactly, then `clip move`
    const artboard::Rect c0 = world(tl, tl.clipRect("ac_1"));
    const artboard::Rect lane1 = world(tl, tl.rowRect(1));
    const double dx = 2.0 * tl.pxPerBeat();
    r.drag(c0.x + 10.0, cy(c0), c0.x + 10.0 + dx, cy(lane1), 8, false);
    const artboard::Rect mid = world(tl, tl.clipRect("ac_1"));
    assert(std::fabs(mid.x - (c0.x + dx)) < 1.0 && std::fabs(cy(mid) - cy(lane1)) < 1.0);
    r.app->pointer(2, c0.x + 10.0 + dx, cy(lane1), 0, r.now);
    r.settle();
    assert(sentLine(r, "clip move ac_1 --at 4 --lane ln_2"));
    assert(r.svc->model().clips[0].at == 4.0 && r.svc->model().clips[0].lane == "ln_2");
    const artboard::Rect after = world(tl, tl.clipRect("ac_1"));
    assert(std::fabs(after.x - mid.x) < 1.0);                           // where it was dropped: nothing jumps
    pass("An instrument dropped on a lane is ONE `clip add --instrument` — a strip and its clip; a clip dragged follows the pointer and lands as `clip move`");
}

static void test_lists_travel_when_the_song_changes_shape()
{
    sltest::Rig r("ui-travel", 1280, 800);
    r.cmd("project new " + r.song("Travel") + ".slp --bpm 120");
    r.cmd("clip add --instrument drums --at 0 --length 4");
    r.settle();
    auto &b = r.app->project().browser();
    auto &tl = r.app->project().timeline();
    assert(tl.clipAlpha("ac_1") == 1.0);                 // there when the song opened: placed, not faded in
    // a clip arriving from a shell fades in: the first frame it shows, it is not yet whole
    r.cmd("clip add --instrument synth --at 4 --length 4");
    double first = 0;
    for (int k = 0; k < 20 && first == 0; ++k) { r.frame(); first = tl.clipAlpha("ac_2"); }
    assert(first > 0.0 && first < 1.0);
    r.settle();
    assert(tl.clipAlpha("ac_2") == 1.0);
    // a clip moved from a shell EASES there
    const double x0 = tl.clipRect("ac_1").x;
    r.cmd("clip move ac_1 --at 8");
    r.frame();
    r.frame();
    const double xm = tl.clipRect("ac_1").x;
    r.settle();
    const double x1 = tl.clipRect("ac_1").x;
    assert(x1 > x0 && xm > x0 && xm < x1);
    // a strip's colour set from a shell: its clips CROSS-FADE to it
    r.cmd("set ch_2.colour=7");
    r.frame();
    r.frame();
    assert(tl.clipHueAmount("ac_1") > 0.0 && tl.clipHueAmount("ac_1") < 1.0);
    r.settle();
    assert(tl.clipHueAmount("ac_1") == 1.0);
    // a lane removed above: the one below SLIDES up; the removed clip fades where it was and takes no input
    const double y0 = tl.rowRect(1).y;
    r.cmd("lane delete ln_1 --with-clips");
    r.frame();
    r.frame();
    const double ym = tl.rowRect(0).y, ghost = tl.clipAlpha("ac_1");
    assert(tl.clipRect("ac_1").w == 0.0 && ghost > 0.0 && ghost < 1.0);
    r.settle();
    const double y1 = tl.rowRect(0).y;
    assert(ym < y0 && ym > y1 && tl.clipAlpha("ac_1") == 0.0);
    // the browser: a tab switched CROSS-FADES its list
    r.click(world(b, b.tabRect(1)));
    double fa = 0;
    for (int k = 0; k < 20 && fa == 0; ++k) { r.frame(); fa = b.rowAlpha(1); }
    assert(fa > 0.0 && fa < 1.0);
    r.settle();
    assert(b.rowAlpha(1) == 1.0);
    pass("Lists travel (§1): a clip from a shell fades in, a `clip move` eases, a lane removed slides the next up, a tab cross-fades");
}

static void test_mixer_dock_strips()
{
    sltest::Rig r("ui-mixer", 1280, 800);
    r.cmd("project new " + r.song("Mix") + ".slp --bpm 120");
    r.cmd("clip add --instrument drums --at 0 --length 4");  // ch_2
    r.cmd("clip add --instrument synth --at 0 --length 4");  // ch_3
    r.settle();
    auto &d = r.app->project().dock();
    using arstro::solaris_ui::MixerDock;
    const auto S = [&r](const std::string &id) -> const arstro::solaris::StripModel & { // by id: the model lists strips in processing order
        for (const auto &s : r.svc->model().strips)
            if (s.id == id) return s;
        assert(false);
        return r.svc->model().strips[0];
    };
    // a fader dragged follows the pointer EXACTLY, and every step is `set ch_2.gain=…`
    const artboard::Rect f = world(d, d.faderRect("ch_2"));
    const auto thumbY = [&](double pos) { return f.bottom() - 6.0 - pos * (f.h - 12.0); };
    r.drag(f.x + f.w * 0.36, thumbY(d.faderLive("ch_2")), f.x + f.w * 0.36, thumbY(d.faderLive("ch_2")) + 30.0, 6, false);
    const double held = d.faderLive("ch_2");
    assert(std::fabs(thumbY(held) - (thumbY(MixerDock::faderPos(0.0)) + 30.0)) < 0.5);   // under the pointer, not easing after it
    bool sent = false;
    for (const auto &l : r.sent) sent |= l.rfind("set ch_2.gain=-", 0) == 0;
    assert(sent && S("ch_2").gain < -1.0);
    r.app->pointer(2, f.x + f.w * 0.36, thumbY(held), 0, r.now);
    r.settle();
    assert(std::fabs(d.faderLive("ch_2") - held) < 0.01);                                   // let go: it stays
    // a gain set from a shell TRAVELS there
    r.cmd("set ch_3.gain=-12");
    r.frame();
    r.frame();
    const double mid = d.faderLive("ch_3");
    assert(mid < MixerDock::faderPos(0.0) - 0.01 && mid > MixerDock::faderPos(-12.0) + 0.01);
    r.settle();
    assert(std::fabs(d.faderLive("ch_3") - MixerDock::faderPos(-12.0)) < 1e-6);
    // mute: a line, and the fill eases
    r.click(world(d, d.muteRect("ch_3")));
    assert(sentLine(r, "set ch_3.mute=true"));
    r.frame();
    r.frame();
    assert(d.muteAmount("ch_3") > 0.0 && d.muteAmount("ch_3") < 1.0);
    r.settle();
    assert(d.muteAmount("ch_3") == 1.0);
    // where it goes: the menu offers exactly the service's targets
    r.click(world(d, d.outRect("ch_3")));
    r.settle();
    auto &menu = r.app->menu();
    assert(menu.isOpen() && menu.itemCount() == (int)S("ch_3").targets.size());
    int master = -1;
    for (int i = 0; i < menu.itemCount(); ++i)
        if (menu.item(i).label.rfind("Master", 0) == 0) master = i;
    r.click(menu.itemRect(master));
    r.settle();
    assert(sentLine(r, "route ch_3 --to master") && S("ch_3").out == "master");
    // tabs: the highlight SLIDES, the pages CROSS-FADE
    const double hx0 = d.tabHighlightX();
    r.click(world(d, d.tabRect(1)));
    r.frame();
    r.frame();
    assert(d.tabHighlightX() > hx0 && d.tabHighlightX() < d.tabRect(1).x);
    assert(d.pageAmount(1) > 0.0 && d.pageAmount(1) < 1.0 && d.pageAmount(0) > 0.0);
    r.settle();
    assert(d.pageAmount(1) == 1.0 && d.pageAmount(0) == 0.0);
    // a fold: two strips feed Main on Sources — folded, their cards narrow away (eased)
    r.click(world(d, d.tabRect(0)));
    r.settle();
    r.cmd("route ch_3 --to ch_1");
    r.settle();
    r.click(world(d, d.foldRect("ch_1")));
    r.frame();
    r.frame();
    assert(d.foldAmount("ch_1") > 0.0 && d.foldAmount("ch_1") < 1.0 && d.cardRect("ch_2").w > 0.0);
    r.settle();
    assert(d.foldAmount("ch_1") == 1.0 && d.cardRect("ch_2").w == 0.0 && d.foldRect("ch_1").w == MixerDock::kStripW);
    // the dock folds down to its tabs, eased, and the lanes take the room
    const double h0 = r.app->project().dockHeight();
    r.click(world(d, d.toggleRect()));
    r.frame();
    r.frame();
    const double hm = r.app->project().dockHeight();
    assert(hm < h0 && hm > MixerDock::kTabsH);
    r.settle();
    assert(r.app->project().dockHeight() == MixerDock::kTabsH);
    pass("Mixer dock: a fader follows the pointer and sends `set`; a shell's gain travels; mute eases; the route menu is the service's targets; tabs slide and cross-fade; a fold and the dock ease");
}

static void test_mixer_matrix_effects_and_device_panel()
{
    sltest::Rig r("ui-matrix", 1280, 800);
    r.cmd("project new " + r.song("Matrix") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");  // ch_2
    r.settle();
    auto &d = r.app->project().dock();
    auto &menu = r.app->menu();
    // + Effect: the registry's effects; one picked is `device add`
    r.click(world(d, d.chipRect("ch_2", d.chipSlots("ch_2") - 1)));
    r.settle();
    int reverb = -1;
    for (int i = 0; i < menu.itemCount(); ++i)
        if (menu.item(i).label == "Reverb") reverb = i;
    assert(menu.isOpen() && reverb >= 0);
    r.click(menu.itemRect(reverb));
    r.settle();
    assert(sentLine(r, "device add ch_2 --type reverb") && r.svc->model().strips[0].devices.size() == 2);
    // a chip opens its device's WINDOW, fading in (R-WIN-1); its rows are the registry's parameters
    r.click(world(d, d.chipRect("ch_2", 0)));
    r.frame();
    r.frame();
    const std::string dv = r.svc->model().strips[0].devices[0].id;
    auto &wl = r.app->project().windows();
    auto *w = wl.window("dev:" + dv);
    assert(w && w->isOpen() && w->appearAmount() > 0.0 && w->appearAmount() < 1.0);
    r.settle();
    auto &p = *wl.devicePanel(dv);
    assert(p.device() == dv && p.rowCount() == (int)r.svc->model().strips[0].devices[0].params.size());
    assert(p.slider("filter.cutoff") != nullptr && p.slider("osc1.wave") == nullptr); // a choice is not a slider
    assert(contains(w->title(), "Basic Synth"));
    // a slider dragged is `set <dv>.<param>=…` in its unit (a log taper: cutoff moves in ratios)
    int cut = -1;
    for (int i = 0; i < p.rowCount(); ++i)
        if (p.rowParam(i) == "filter.cutoff") cut = i;
    p.reveal("filter.cutoff");                                           // below the fold of the window: scroll to it
    r.settle();
    const artboard::Rect row = world(p, p.rowRect(cut));
    const double sx = row.x + arstro::cosmo_v2::SliderRow::kLabelWidth + 20.0;
    r.drag(sx, cy(row), sx + 40.0, cy(row), 5);
    r.settle();
    bool cutoff = false;
    for (const auto &l : r.sent) cutoff |= l.rfind("set " + dv + ".filter.cutoff=", 0) == 0;
    assert(cutoff);
    // bypass, eased chip; another device opens a second window, on top
    r.click(world(p, p.bypassRect()));
    r.settle();
    assert(sentLine(r, "set " + dv + ".bypass=true") && r.svc->model().strips[0].devices[0].bypass);
    const std::string fx = r.svc->model().strips[0].devices[1].id;
    r.click(world(d, d.chipRect("ch_2", 1)));
    r.settle();
    assert(wl.isOpen("dev:" + fx) && wl.isOpen("dev:" + dv) && wl.children().back().get() == wl.window("dev:" + fx));
    wl.close("dev:" + fx);
    wl.close("dev:" + dv);
    r.settle();
    // the matrix: an open cell adds a send; a backward one takes nothing
    r.click(world(d, d.tabRect(d.tabCount() - 1)));
    r.settle();
    assert(d.onMatrix());
    r.click(world(d, d.cellRect("ch_2", "master")));
    r.settle();
    assert(sentLine(r, "send add ch_2 --to master") && r.svc->model().strips[0].sends.size() == 1);
    const size_t before = r.sent.size();
    r.click(world(d, d.cellRect("ch_1", "ch_1")));                       // Main → itself: hatched
    r.settle();
    assert(r.sent.size() == before);
    pass("Mixer: + Effect is the registry; a chip opens the device's generated window (fade), a slider is `set` in its unit, bypass, a second window on top; the matrix adds a send");
}

static void test_song_bar_menus_and_settings_sections()
{
    sltest::Rig r("ui-menus", 1024, 640);
    r.cmd("project new " + r.song("Menus") + ".slp --bpm 120");
    r.cmd("clip add --instrument drums --at 0 --length 4");
    r.settle();
    auto &bar = r.app->project().bar();
    auto &ms = bar.menus();
    using arstro::solaris_ui::SongBar;
    // Settings sits beside Home; the transport keeps clear of the menus and a readable name, even at 1024
    assert(bar.hitRect(SongBar::kSettings).x > bar.hitRect(SongBar::kHome).right());
    assert(bar.hitRect(SongBar::kPlay).x >= ms.x.value() + ms.contentWidth() + SongBar::kNameMin);
    r.click(world(bar, bar.hitRect(SongBar::kSettings)));
    r.settle();
    assert(r.app->settings().isOpen());
    r.key(27);
    r.settle();
    // File › Save is `project save`
    assert(ms.menuCount() == 4 && ms.menu(0).title == "File" && ms.menu(3).title == "View");
    r.click(world(ms, ms.titleRect(0)));
    r.settle();
    assert(ms.openIndex() == 0);
    r.click(world(ms, ms.itemRect(0, 2)));
    r.settle();
    assert(sentLine(r, "project save") && ms.openIndex() == -1);
    // Edit names what undo would take back, and is the `undo` line
    r.cmd("set ch_2.gain=-3");
    r.settle();
    assert(contains(ms.menu(1).items[0].label, "Undo set ch_2.gain"));
    r.click(world(ms, ms.titleRect(1)));
    r.settle();
    r.click(world(ms, ms.itemRect(1, 0)));
    r.settle();
    assert(sentLine(r, "undo") && r.svc->model().strips[0].gain == 0.0);
    // Song › Add Bus
    r.click(world(ms, ms.titleRect(2)));
    r.settle();
    r.click(world(ms, ms.itemRect(2, 1)));
    r.settle();
    assert(sentLine(r, "strip add --kind bus"));
    // a press outside an open menu closes it
    r.click(world(ms, ms.titleRect(0)));
    r.settle();
    r.click(400.0, 400.0);
    r.settle();
    assert(ms.openIndex() == -1);
    // View › Hide Mixer folds the dock, eased; Hide Browser folds the browser, eased; Metronome is a setting
    auto &ps = r.app->project();
    const double dock0 = ps.dockHeight();
    r.click(world(ms, ms.titleRect(3)));
    r.settle();
    assert(ms.menu(3).items[0].label == "Hide Mixer");
    r.click(world(ms, ms.itemRect(3, 0)));
    r.frame();
    r.frame();
    assert(ps.dockHeight() < dock0 && ps.dockHeight() > arstro::solaris_ui::MixerDock::kTabsH);
    r.settle();
    assert(ps.dockHeight() == arstro::solaris_ui::MixerDock::kTabsH && ms.menu(3).items[0].label == "Show Mixer");
    r.click(world(ms, ms.titleRect(3)));
    r.settle();
    r.click(world(ms, ms.itemRect(3, 1)));
    r.frame();
    r.frame();
    assert(ps.browserWidth() > 0.0 && ps.browserWidth() < arstro::solaris_ui::Browser::kWidth);
    r.settle();
    assert(ps.browserWidth() == 0.0);
    r.click(world(ms, ms.titleRect(3)));
    r.settle();
    r.click(world(ms, ms.itemRect(3, 2)));
    r.settle();
    assert(sentLine(r, "settings set metronome=on") && r.svc->model().settings.metronome);
    // the settings sheet's new sections: a chip is a `settings set` line
    r.app->openSettings();
    r.settle();
    using arstro::solaris_ui::SettingsSheet;
    r.app->settings().revealRect(r.app->settings().chipRect(SettingsSheet::kNewBpm, 4));
    r.settle();
    r.click(r.app->settings().chipRect(SettingsSheet::kNewBpm, 4)); // 128
    r.settle();
    assert(sentLine(r, "settings set newBpm=128") && r.svc->model().settings.newBpm == 128.0);
    r.app->settings().revealRect(r.app->settings().chipRect(SettingsSheet::kMotion, 1));
    r.settle();
    r.click(r.app->settings().chipRect(SettingsSheet::kMotion, 1));
    r.settle();
    assert(sentLine(r, "settings set reducedMotion=on") && artboard::reducedMotion()); // the setting reaches every primitive
    r.cmd("settings set reducedMotion=off");
    r.settle();
    assert(!artboard::reducedMotion());
    pass("Song bar: Settings beside Home; File/Edit/Song/View are command lines (Edit names its undo); outside press closes; View folds the dock and browser, eased; new settings rows are lines");
}

static void test_device_window_lists_parameters_and_binds_them()
{
    sltest::Rig r("ui-window", 1280, 800);
    r.cmd("project new " + r.song("Window") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");        // ch_2, dv_1
    r.settle();
    auto &d = r.app->project().dock();
    auto &wl = r.app->project().windows();
    // double-clicking an instrument strip's name opens its synth's window
    const artboard::Rect card = world(d, d.cardRect("ch_2"));
    r.click(card.x + 20.0, card.y + 16.0);
    r.click(card.x + 20.0, card.y + 16.0);
    r.settle();
    assert(wl.isOpen("dev:dv_1"));
    auto &w = *wl.window("dev:dv_1");
    auto &p = *wl.devicePanel("dv_1");
    // dragged by its title, it follows the pointer exactly
    const artboard::Rect t0 = world(w, w.titleRect());
    const double x0 = w.x.value(), y0 = w.y.value();
    r.drag(t0.x + 40.0, cy(t0), t0.x - 60.0, cy(t0) + 30.0, 6);
    assert(std::fabs(w.x.value() - (x0 - 100.0)) < 0.5 && std::fabs(w.y.value() - (y0 + 30.0)) < 0.5);
    // the parameter changed last is lit — whoever changed it — and the light moves, eased
    r.cmd("set dv_1.filter.cutoff=900");
    r.frame();
    r.frame();
    const double mid = p.litAmount("filter.cutoff");
    assert(mid > 0.0 && mid < 1.0);
    r.settle();
    assert(p.litAmount("filter.cutoff") == 1.0 && r.svc->model().strips[0].devices[0].lastChanged == "filter.cutoff");
    r.cmd("set dv_1.filter.res=0.5");
    r.settle();
    assert(p.litAmount("filter.res") == 1.0 && p.litAmount("filter.cutoff") == 0.0);
    // a parameter's menu: Create Automation is ONE line, and the row then says what decides it
    int cut = -1;
    for (int i = 0; i < p.rowCount(); ++i)
        if (p.rowParam(i) == "filter.cutoff") cut = i;
    p.reveal("filter.cutoff");
    r.settle();
    const artboard::Rect row = world(p, p.rowRect(cut));
    auto &menu = r.app->menu();
    r.click(row.x + 20.0, cy(row), 2);
    r.settle();
    assert(menu.isOpen() && menu.item(0).label == "Create Automation");
    r.click(menu.itemRect(0));
    r.settle();
    assert(sentLine(r, "auto create dv_1.filter.cutoff") && p.readout("filter.cutoff") == "auto au_1");
    // Formula… is cosmo's field: what is typed becomes `set <address>=<formula>`
    r.click(row.x + 20.0, cy(row), 2);
    r.settle();
    int formula = -1, clear = -1;
    for (int i = 0; i < menu.itemCount(); ++i)
    {
        if (menu.item(i).label == "Formula\xE2\x80\xA6") formula = i;
        if (menu.item(i).label == "Clear Binding") clear = i;
    }
    assert(formula >= 0 && clear >= 0);
    r.click(menu.itemRect(formula));
    r.settle();
    assert(menu.isRenaming());
    artboard::KeyEvent typed;
    typed.type = artboard::KeyEvent::Type::Text;
    typed.text = "=au_1*2";
    r.app->key(typed);
    r.key(13);
    r.settle();
    assert(sentLine(r, "set dv_1.filter.cutoff==au_1*2") && p.readout("filter.cutoff") == "= au_1*2");
    // Clear Binding: its own number again
    r.click(row.x + 20.0, cy(row), 2);
    r.settle();
    for (int i = 0; i < menu.itemCount(); ++i)
        if (menu.item(i).label == "Clear Binding") clear = i;
    r.click(menu.itemRect(clear));
    r.settle();
    assert(sentLine(r, "bind clear dv_1.filter.cutoff") && p.readout("filter.cutoff") == "900 Hz");
    // × closes it, fading; while closing it takes no input
    r.click(world(w, w.closeRect()));
    r.frame();
    r.frame();
    assert(!w.isOpen() && w.appearAmount() > 0.0 && w.appearAmount() < 1.0);
    r.settle();
    assert(w.appearAmount() == 0.0);
    pass("Device window: an instrument's name opens it; dragged by its title exactly; the last change lit and moving (eased); Create Automation / Formula / Clear Binding are lines and the row says what decides it; × fades it out");
}

static void test_automation_rows_draw_and_edit_curves()
{
    sltest::Rig r("ui-auto", 1280, 800);
    r.cmd("project new " + r.song("Auto") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 8");   // ch_2, dv_1
    r.settle();
    auto &tl = r.app->project().timeline();
    // an automation made from a shell appears as a row under the lanes, growing in
    r.cmd("auto create ch_2.gain");
    r.frame();
    r.frame();
    assert(tl.autoCount() == 1 && tl.autoRowRect("au_1").h > 0.0);
    r.settle();
    const artboard::Rect row = world(tl, tl.autoRowRect("au_1"));
    assert(row.y > world(tl, tl.rowRect(0)).bottom() - 1.0);          // after the lanes
    // a click in its row adds a point at the snapped beat and the value under the pointer
    const double x2 = world(tl, artboard::Rect{tl.beatToX(2.0), 0, 0, 0}).x;
    const double yq = row.y + row.h * 0.25;
    r.click(x2 + 1.0, yq);
    r.settle();
    bool added = false;
    for (const auto &l : r.sent) added |= l.rfind("auto point add au_1 --at 2 --value ", 0) == 0;
    assert(added && r.svc->model().automations[0].points.size() == 3);
    // a point dragged follows the pointer EXACTLY; let go it is ONE `auto point move`, and stays
    const artboard::Point p1 = tl.autoPointAt("au_1", 1);
    const double x3 = tl.beatToX(3.0);
    const artboard::Rect tw = world(tl, artboard::Rect{0, 0, 0, 0});
    r.drag(tw.x + p1.x, tw.y + p1.y, tw.x + x3, tw.y + p1.y - 6.0, 6, false);
    // while held the handle is drawn under the pointer (its beat snapped to a sixteenth), not easing after it
    const artboard::Point held = tl.autoDragPoint();
    assert(std::fabs(held.x - x3) < 0.5 && std::fabs(held.y - (p1.y - 6.0)) < 0.5);
    r.app->pointer(2, tw.x + x3, tw.y + p1.y - 6.0, 0, r.now);
    r.settle();
    bool moved = false;
    for (const auto &l : r.sent) moved |= l.rfind("auto point move au_1 --at 2 --to 3 --value ", 0) == 0;
    assert(moved && r.svc->model().automations[0].points[1].at == 3.0);
    assert(std::fabs(tl.autoPointAt("au_1", 1).x - x3) < 0.5);       // where it was let go: nothing jumps
    // a curve changed from a shell EASES there
    r.cmd("auto point move au_1 --at 3 --value -40");
    r.frame();
    r.frame();
    assert(tl.autoEase("au_1") > 0.0 && tl.autoEase("au_1") < 1.0);
    r.settle();
    assert(tl.autoEase("au_1") == 1.0);
    // right-click a point: its shapes; Hold is one line
    const artboard::Point p0 = tl.autoPointAt("au_1", 0);
    r.click(tw.x + p0.x, tw.y + p0.y, 2);
    r.settle();
    auto &menu = r.app->menu();
    int hold = -1;
    for (int i = 0; i < menu.itemCount(); ++i)
        if (menu.item(i).label.rfind("Hold", 0) == 0) hold = i;
    assert(menu.isOpen() && hold >= 0);
    r.click(menu.itemRect(hold));
    r.settle();
    assert(sentLine(r, "auto point shape au_1 --at 0 --shape hold") && r.svc->model().automations[0].points[0].shape == "hold");
    // a double-click on a point deletes it
    const artboard::Point pm = tl.autoPointAt("au_1", 1);
    r.click(tw.x + pm.x, tw.y + pm.y);
    r.click(tw.x + pm.x, tw.y + pm.y);
    r.settle();
    assert(sentLine(r, "auto point delete au_1 --at 3") && r.svc->model().automations[0].points.size() == 2);
    // right-click the row: Delete Automation (and the formulas that read it)
    r.click(tw.x + tl.beatToX(6.0), cy(world(tl, tl.autoRowRect("au_1"))) + 18.0, 2);
    r.settle();
    r.click(menu.itemRect(menu.itemCount() - 1));
    r.settle();
    assert(sentLine(r, "auto delete au_1 --unbind") && r.svc->model().automations.empty() && r.svc->model().bindings.empty());
    pass("Automation rows: a row per automation under the lanes; click adds, a drag follows exactly and is one move, a shell's edit eases, shapes and deletes from the menu (R-AUTO-6)");
}

static void test_piano_roll_edits_the_pattern()
{
    sltest::Rig r("ui-roll", 1280, 800);
    r.cmd("project new " + r.song("Roll") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");
    const std::string ac = r.svc->model().clips[0].id, pt = r.svc->model().clips[0].pattern;
    r.cmd("note add " + pt + " --pitch 60 --at 0 --length 1");
    r.settle();
    auto &tl = r.app->project().timeline();
    auto &wl = r.app->project().windows();
    // a note clip double-clicked opens its pattern's roll, fading in
    const artboard::Rect clip = world(tl, tl.clipRect(ac));
    r.click(clip.x + 12.0, cy(clip));
    r.click(clip.x + 12.0, cy(clip));
    r.frame();
    r.frame();
    assert(wl.isOpen("roll:" + pt) && wl.window("roll:" + pt)->appearAmount() < 1.0);
    r.settle();
    auto &roll = *wl.roll(pt);
    assert(roll.keyLabel(60) == "C4" && roll.keyLabel(61).empty());
    const artboard::Rect rw = world(roll, artboard::Rect{0, 0, 0, 0});
    auto at = [&](double beat, int pitch) { return artboard::Point{rw.x + roll.beatToX(beat), rw.y + roll.pitchToY(pitch) + roll.kNoteH * 0.5}; };
    // a click on the grid adds a note of the last length in the cell under the pointer; it fades in
    artboard::Point c = at(1.1, 64);
    r.click(c.x, c.y);
    r.frame();
    r.frame();
    assert(sentLine(r, "note add " + pt + " --pitch 64 --at 1 --length 0.25"));
    const double fadeIn = roll.noteAlpha(64, 1.0);
    assert(fadeIn > 0.0 && fadeIn < 1.0);
    r.settle();
    assert(roll.noteAlpha(64, 1.0) == 1.0);
    // a note dragged follows the pointer (its beat snapped); let go it is ONE `note move`, where it was let go
    c = at(0.2, 60);
    const artboard::Point to = at(2.2, 62);
    r.drag(c.x, c.y, to.x, to.y, 6, false);
    const artboard::Rect held = roll.heldRect();
    assert(std::fabs(held.x - roll.beatToX(2.0)) < 0.5 && std::fabs(held.y - (roll.pitchToY(62) + 1.0)) < 0.5);
    r.app->pointer(2, to.x, to.y, 0, r.now);
    r.frame();
    assert(sentLine(r, "note move " + pt + " --pitch 60 --at 0 --to-pitch 62 --to-at 2"));
    assert(roll.noteAlpha(62, 2.0) == 1.0 && roll.noteAlpha(60, 0.0) == 0.0); // nothing re-fades, nothing jumps back
    r.settle();
    // its right edge resizes it — and the next note added takes that length
    const artboard::Rect n64 = world(roll, roll.noteRect(64, 1.0));
    r.drag(n64.right() - 2.0, cy(n64), rw.x + roll.beatToX(2.0) + 1.0, cy(n64), 6);
    r.settle();
    assert(sentLine(r, "note move " + pt + " --pitch 64 --at 1 --length 1"));
    // a velocity stem dragged to the top: one line, 127
    const artboard::Rect vel = world(roll, roll.velRect());
    r.drag(rw.x + roll.beatToX(2.0) + 1.5, vel.bottom() - 10.0, rw.x + roll.beatToX(2.0) + 1.5, vel.y + 2.0, 6);
    r.settle();
    assert(sentLine(r, "note move " + pt + " --pitch 62 --at 2 --vel 127"));
    // a double-click deletes; the note fades out where it was
    c = at(1.5, 64);
    r.click(c.x, c.y);
    r.click(c.x, c.y);
    r.frame();
    r.frame();
    assert(sentLine(r, "note delete " + pt + " --pitch 64 --at 1"));
    const double fadeOut = roll.noteAlpha(64, 1.0);
    assert(fadeOut > 0.0 && fadeOut < 1.0);
    r.settle();
    // snap 1/8, then Quantize… is a menu of lines
    r.click(world(roll, roll.snapRect(1)));
    assert(roll.snap() == 0.5);
    r.click(world(roll, roll.quantizeRect()));
    r.settle();
    auto &menu = r.app->menu();
    assert(menu.isOpen() && menu.itemCount() == 3);
    r.click(menu.itemRect(1));
    r.settle();
    assert(sentLine(r, "pattern quantize " + pt + " --grid 0.5 --swing 0.25"));
    // the pattern's end dragged: `set <pt>.length=`; the handle stays where it was let go
    const artboard::Rect end = world(roll, roll.endRect());
    r.drag(cx(end), cy(end), rw.x + roll.beatToX(8.0), cy(end), 6);
    r.settle();
    assert(sentLine(r, "set " + pt + ".length=8") && std::fabs(roll.endRect().x + 4.875 - roll.beatToX(8.0)) < 0.5);
    assert(r.svc->model().patterns[0].length == 8.0);
    // Steps: the mode cross-fades; a cell toggles a note
    r.click(world(roll, roll.modeRect(1)));
    r.frame();
    r.frame();
    assert(roll.mode() == arstro::solaris_ui::PianoRoll::Steps && roll.modeAmount() > 0.0 && roll.modeAmount() < 1.0);
    r.settle();
    int row60 = -1;
    for (int i = 0; i < roll.stepRows(); ++i)
        if (roll.stepRowPitch(i) == 60) row60 = i;
    assert(row60 >= 0);
    r.click(world(roll, roll.stepCell(row60, 4)));
    r.settle();
    assert(sentLine(r, "note add " + pt + " --pitch 60 --at 1 --length 0.25"));
    r.click(world(roll, roll.stepCell(row60, 4)));
    r.settle();
    assert(sentLine(r, "note delete " + pt + " --pitch 60 --at 1"));
    pass("Piano roll: a note clip opens it; click adds (last length), drag follows the pointer and is one move, the edge resizes, velocity drags, double-click deletes — each note fading; snap, quantize, the end; Steps cross-fades and toggles (R-ROLL-1…5)");
}

static void test_a_kits_roll_names_its_pads()
{
    sltest::Rig r("ui-roll-kit", 1280, 800);
    r.cmd("project new " + r.song("Kit") + ".slp --bpm 120");
    r.cmd("clip add --instrument drums --at 0 --length 4");
    r.settle();
    const std::string strip = r.svc->model().clips[0].track, pt = r.svc->model().clips[0].pattern;
    std::string dv;
    for (const auto &s : r.svc->model().strips)
        if (s.id == strip) dv = s.devices[0].id;
    auto &wl = r.app->project().windows();
    // the instrument's window has "Piano Roll": its strip's pattern
    wl.openDevice(dv);
    r.settle();
    r.click(world(*wl.devicePanel(dv), wl.devicePanel(dv)->rollRect()));
    r.settle();
    assert(wl.isOpen("roll:" + pt));
    auto &roll = *wl.roll(pt);
    // the keys are the kit's pads, from the registry; Steps has a row per pad
    assert(roll.keyLabel(36) == "Kick" && roll.keyLabel(60).empty());
    assert(roll.stepRows() == 10 && roll.stepRowPitch(0) == 36);
    roll.setMode(arstro::solaris_ui::PianoRoll::Steps);
    r.settle();
    r.click(world(roll, roll.stepCell(0, 0)));
    r.settle();
    assert(sentLine(r, "note add " + pt + " --pitch 36 --at 0 --length 0.25"));
    pass("A kit's roll: opened from its window's Piano Roll; keys named by its pads (registry note names); Steps a row per pad");
}

static void test_a_line_added_and_sources_relinked()
{
    sltest::Rig r("ui-lines", 1280, 800);
    r.cmd("project new " + r.song("Lines") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");        // ch_2
    r.settle();
    auto &d = r.app->project().dock();
    auto &menu = r.app->menu();
    const std::string mx = r.svc->model().mixers[0].id, ac = r.svc->model().clips[0].id;
    // "+ Line" after the page's last card: a menu of an audio line, a bus and every instrument — one line each
    const artboard::Rect add0 = world(d, d.addLineRect());
    assert(add0.w > 0.0);
    r.click(add0);
    r.settle();
    assert(menu.isOpen() && menu.item(0).label == "Audio line" && menu.item(1).label == "Bus");
    int synth = -1;
    for (int i = 0; i < menu.itemCount(); ++i)
        if (menu.item(i).label == "Basic Synth") synth = i;
    assert(synth >= 2);
    r.click(menu.itemRect(synth));
    r.frame();
    r.frame();
    assert(sentLine(r, "strip add --kind instrument --instrument synth --mixer " + mx));
    std::string added;
    for (const auto &st : r.svc->model().strips)
        if (st.kind == "instrument" && st.id != "ch_2") added = st.id;
    // the new card grows in, and "+ Line" slides along with it — caught between
    const double w = d.cardRect(added).w, ax = world(d, d.addLineRect()).x;
    assert(w > 0.0 && w < arstro::solaris_ui::MixerDock::kStripW - 0.5);
    assert(ax > add0.x + 0.5 && ax < add0.x + arstro::solaris_ui::MixerDock::kStripW - 0.5);
    r.settle();
    assert(std::fabs(d.addLineRect().x - 6.5 - d.cardRect(added).right()) < 0.5); // right after the last card (two now feed Main: a group)
    // a strip's menu: "Move its clips to ▸" — the lines of its kind — is ONE `strip relink`
    const artboard::Rect card = world(d, d.cardRect("ch_2"));
    r.click(card.x + 20.0, card.y + 16.0, 2);
    r.settle();
    int move = -1;
    for (int i = 0; i < menu.itemCount(); ++i)
        if (menu.item(i).label.rfind("Move its clips to", 0) == 0) move = i;
    assert(menu.isOpen() && move >= 0);
    r.click(menu.itemRect(move));
    r.settle();
    assert(menu.isOpen() && menu.itemCount() == 1);              // only the other instrument line
    r.click(menu.itemRect(0));
    r.settle();
    assert(sentLine(r, "strip relink ch_2 --to " + added) && r.svc->model().clips[0].track == added);
    // a clip's menu on the lanes: "Play through ▸" — the strips of its kind — is ONE `clip move --strip`
    auto &tl = r.app->project().timeline();
    const artboard::Rect clip = world(tl, tl.clipRect(ac));
    r.click(clip.x + 12.0, cy(clip), 2);
    r.settle();
    assert(menu.isOpen() && menu.item(0).label.rfind("Play through", 0) == 0);
    r.click(menu.itemRect(0));
    r.settle();
    int back = -1;
    for (int i = 0; i < menu.itemCount(); ++i)
        if (menu.item(i).label.rfind(r.svc->model().strips[1].name, 0) == 0 && menu.item(i).label.find("now") == std::string::npos) back = i;
    assert(menu.itemCount() == 2 && back >= 0);
    r.click(menu.itemRect(back));
    r.settle();
    assert(sentLine(r, "clip move " + ac + " --strip ch_2") && r.svc->model().clips[0].track == "ch_2");
    pass("Lines: \"+ Line\" adds an audio line, a bus or any instrument to the page in one line, the card growing in and \"+ Line\" sliding along (eased); a strip's clips move to another line in one `strip relink`; a clip plays through another strip from its menu (R-MIX-13/14)");
}

static void test_ruler_seek_keys_and_selection()
{
    sltest::Rig r("ui-ruler", 1280, 800);
    r.cmd("project new " + r.song("Ruler") + ".slp --bpm 120");
    r.cmd("strip add --kind instrument --instrument synth");
    r.cmd("clip add --strip ch_2 --length 4");
    r.settle();
    auto &tl = r.app->project().timeline();
    r.click(world(tl, artboard::Rect{tl.beatToX(8.0), 4.0, 1.0, 10.0}));
    r.frame();
    r.frame();
    assert(sentLine(r, "transport seek 8"));
    assert(tl.playheadBeat() > 0.0 && tl.playheadBeat() < 8.0);        // a seek EASES the playhead
    r.settle();
    assert(tl.playheadBeat() == 8.0);
    r.click(world(tl, tl.clipRect("ac_1")));
    r.frame();
    assert(tl.selectedClip() == "ac_1");
    r.key('D', true);                                                   // Ctrl+D: a linked copy after it
    assert(sentLine(r, "clip duplicate ac_1") && r.svc->model().clips.size() == 2 && r.svc->model().clips[1].linked == 2);
    r.key(46);                                                          // Delete
    assert(sentLine(r, "clip delete ac_1") && r.svc->model().clips.size() == 1);
    r.settle();
    assert(tl.selectedClip().empty());                                  // the selection went with the clip
    pass("Ruler click seeks (the playhead eases); a clip selects; Ctrl+D duplicates linked, Delete removes it");
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    arstro::cosmo_v2::registerEmbeddedFonts();
    test_new_song_cross_fades_from_home();
    test_settings_chips_send_lines_and_ease();
    test_settings_folders();
    test_song_bar_and_keys();
    test_home_unsaved_confirm_and_recents();
    test_browser_tabs_and_sample_drag();
    test_instrument_drop_and_clip_drag();
    test_ruler_seek_keys_and_selection();
    test_lists_travel_when_the_song_changes_shape();
    test_mixer_dock_strips();
    test_mixer_matrix_effects_and_device_panel();
    test_song_bar_menus_and_settings_sections();
    test_device_window_lists_parameters_and_binds_them();
    test_automation_rows_draw_and_edit_curves();
    test_piano_roll_edits_the_pattern();
    test_a_kits_roll_names_its_pads();
    test_a_line_added_and_sources_relinked();
    std::printf("\n%d passed, 0 failed\n", passed);
    return 0;
}
