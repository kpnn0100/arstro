// solaris_ui tests — the assembled App over the REAL service: clicks at the geometry the widgets
// publish, asserting the command lines that went out and the model that came back, and that every
// transition is a tween — a LIVE value caught between its ends, never only the end (design rule §1).
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../Rig.h"
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
    r.click(r.app->settings().addFolderRect());
    assert(sentLine(r, "folder add \"/music/One Shots\"") && r.svc->model().settings.folders.size() == 2);
    r.app->onPickFolder = [](std::function<void(const std::string &)> done) { done("/nowhere"); };
    r.settle(); // the list grew: the button moved down a row — aim at where it is NOW
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
    // a chip opens its device's panel, fading in; its rows are the registry's parameters
    r.click(world(d, d.chipRect("ch_2", 0)));
    r.frame();
    r.frame();
    auto &p = d.panel();
    assert(p.shown() && p.appearAmount() > 0.0 && p.appearAmount() < 1.0);
    r.settle();
    const std::string dv = r.svc->model().strips[0].devices[0].id;
    assert(p.device() == dv && p.rowCount() == (int)r.svc->model().strips[0].devices[0].params.size());
    assert(p.slider("filter.cutoff") != nullptr && p.slider("osc1.wave") == nullptr); // a choice is not a slider
    // a slider dragged is `set <dv>.<param>=…` in its unit (a log taper: cutoff moves in ratios)
    int cut = -1;
    for (int i = 0; i < p.rowCount(); ++i)
        if (p.rowParam(i) == "filter.cutoff") cut = i;
    p.reveal("filter.cutoff");                                           // below the fold of the panel: scroll to it
    r.settle();
    const artboard::Rect row = world(p, p.rowRect(cut));
    const double sx = row.x + arstro::cosmo_v2::SliderRow::kLabelWidth + 20.0;
    r.drag(sx, cy(row), sx + 40.0, cy(row), 5);
    r.settle();
    bool cutoff = false;
    for (const auto &l : r.sent) cutoff |= l.rfind("set " + dv + ".filter.cutoff=", 0) == 0;
    assert(cutoff);
    // bypass, eased chip; another device cross-fades the pages
    r.click(world(p, p.bypassRect()));
    r.settle();
    assert(sentLine(r, "set " + dv + ".bypass=true") && r.svc->model().strips[0].devices[0].bypass);
    const std::string fx = r.svc->model().strips[0].devices[1].id;
    r.click(world(d, d.chipRect("ch_2", 1)));
    r.frame();
    r.frame();
    assert(p.device() == fx && p.pageAmount(fx) > 0.0 && p.pageAmount(fx) < 1.0 && p.pageAmount(dv) > 0.0);
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
    pass("Mixer: + Effect is the registry; a chip opens a generated panel (fade), a slider is `set` in its unit, bypass, pages cross-fade; the matrix adds a send");
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
    std::printf("\n%d passed, 0 failed\n", passed);
    return 0;
}
