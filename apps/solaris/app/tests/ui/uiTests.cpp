// solaris_ui tests — the assembled App over the REAL service: clicks at the geometry the widgets
// publish, asserting the command lines that went out and the model that came back, and that every
// transition is a tween — a LIVE value caught between its ends, never only the end (design rule §1).
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../Rig.h"
#include "../../KeyInput.h"
#include "KeyCombo.h"
#include "../../widgets/AnalogFace.h"
#include "../../widgets/AutomationPanel.h"
#include "../../widgets/SettingsSheet.h"
#include "../../widgets/DevicePanel.h"
#include "../../widgets/LoudnessPanel.h"
#include "../../widgets/VideoPanel.h"
#include "../../widgets/ParamMenu.h"
#include "../../widgets/PianoRoll.h"
#include "../../../../cosmo/widgets/SliderRow.h"
using arstro::solaris_ui::Timeline;
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <string>
#include <thread>

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

static void test_settings_combos_send_lines_and_ease()
{
    using arstro::solaris_ui::SettingsSheet;
    sltest::Rig r("ui-settings", 1280, 800);
    r.settle();
    r.app->openSettings();
    assert(sentLine(r, "devices list"));
    r.frame();
    r.frame();
    auto &s = r.app->settings();
    const double mid = s.appearAmount();
    assert(mid > 0.0 && mid < 1.0);                                       // the fade, caught
    r.settle();
    assert(s.isOpen() && s.appearAmount() == 1.0 && s.section() == SettingsSheet::kAudio);
    assert(s.optionCount(SettingsSheet::kOutput) == 4 && s.shownValue(SettingsSheet::kOutput) == "System default"); // + three outputs
    // a combo clicked drops its list; a row picked is ONE line, and the label cross-fades to it
    r.click(s.controlRect(SettingsSheet::kOutput));
    r.settle();
    assert(s.comboOpen(SettingsSheet::kOutput) && s.optionLabel(SettingsSheet::kOutput, 3) == "Scarlett 2i2 USB");
    r.click(s.optionRect(SettingsSheet::kOutput, 3));
    assert(sentLine(r, "settings set output=usb_interface") && r.svc->model().settings.output == "usb_interface");
    assert(!s.comboOpen(SettingsSheet::kOutput));
    double first = 0.0;
    for (int k = 0; k < 20 && first == 0.0; ++k) { r.frame(); first = s.labelAmount(SettingsSheet::kOutput); }
    assert(first > 0.0 && first < 1.0);                                   // the label cross-fades, caught
    r.settle();
    assert(s.labelAmount(SettingsSheet::kOutput) == 1.0 && s.shownValue(SettingsSheet::kOutput) == "Scarlett 2i2 USB");
    r.click(s.controlRect(SettingsSheet::kBuffer));
    r.settle();
    r.click(s.optionRect(SettingsSheet::kBuffer, 1));                    // 128 samples
    assert(sentLine(r, "settings set bufferSize=128") && r.svc->model().settings.bufferSize == 128);
    r.settle();
    // R-DEV-9: the driver, a combo on the Audio page — ONE line, the label cross-fading to it
    assert(s.shownValue(SettingsSheet::kDriver) == "PulseAudio" && SettingsSheet::sectionOf(SettingsSheet::kDriver) == SettingsSheet::kAudio);
    s.revealRect(s.controlRect(SettingsSheet::kDriver)); // the page may be shorter than its rows
    r.settle();
    r.click(s.controlRect(SettingsSheet::kDriver));
    r.settle();
    assert(s.comboOpen(SettingsSheet::kDriver));
    r.click(s.optionRect(SettingsSheet::kDriver, 1));
    assert(sentLine(r, "settings set driver=pipewire") && r.svc->model().settings.driver == "pipewire");
    first = 0.0;
    for (int k = 0; k < 20 && first == 0.0; ++k) { r.frame(); first = s.labelAmount(SettingsSheet::kDriver); }
    assert(first > 0.0 && first < 1.0);
    r.settle();
    assert(s.shownValue(SettingsSheet::kDriver) == "PipeWire \xE2\x80\x94 low latency");
    // a setting changed from a shell shows here too, eased: the sheet draws the model
    r.cmd("settings set sampleRate=96000");
    first = 0.0;
    for (int k = 0; k < 20 && first == 0.0; ++k) { r.frame(); first = s.labelAmount(SettingsSheet::kRate); }
    assert(first > 0.0 && first < 1.0);
    r.settle();
    assert(s.shownValue(SettingsSheet::kRate) == "96 kHz");
    r.key(27);                                                            // Escape closes, eased
    r.frame();
    r.frame();
    assert(s.appearAmount() < 1.0 && s.appearAmount() > 0.0);
    r.settle();
    assert(!s.isOpen());
    pass("Settings (R-SET-6): a choice is a combo box — its list drops, a row is a `settings set` line, its label cross-fades, a shell's change shows eased; Escape fades the sheet shut");
}

static void test_settings_in_pages()
{
    // R-SET-5: a rail of sections beside ONE page; a section clicked cross-fades its page in, the highlight slides
    using arstro::solaris_ui::SettingsSheet;
    sltest::Rig r("ui-settings-pages", 1280, 800);
    r.settle();
    r.app->openSettings();
    r.settle();
    auto &s = r.app->settings();
    assert(s.pageAmount(SettingsSheet::kAudio) == 1.0 && s.pageAmount(SettingsSheet::kPlayback) == 0.0);
    const double y0 = s.railHighlightY();
    r.click(s.railRect(SettingsSheet::kPlayback));
    assert(s.section() == SettingsSheet::kPlayback);
    double in = 0.0;
    for (int k = 0; k < 20 && in == 0.0; ++k) { r.frame(); in = s.pageAmount(SettingsSheet::kPlayback); }
    assert(in > 0.0 && in < 1.0 && s.pageAmount(SettingsSheet::kAudio) > 0.0 && s.pageAmount(SettingsSheet::kAudio) < 1.0); // a cross-fade, caught
    assert(s.railHighlightY() > y0 && s.railHighlightY() < s.railRect(SettingsSheet::kPlayback).y);                     // the highlight on its way
    r.settle();
    assert(s.pageAmount(SettingsSheet::kAudio) == 0.0 && s.railHighlightY() == s.railRect(SettingsSheet::kPlayback).y);
    // a hidden page's controls take no click: where Audio's Output combo was, nothing opens
    assert(!s.comboOpen(SettingsSheet::kOutput));
    // an on/off is a toggle: clicked, one line; set from a shell, its knob eases there too
    r.click(s.controlRect(SettingsSheet::kMetronome));
    assert(sentLine(r, "settings set metronome=on") && r.svc->model().settings.metronome);
    r.settle();
    r.cmd("settings set metronome=off");
    double knob = 1.0;
    for (int k = 0; k < 20 && knob == 1.0; ++k) { r.frame(); knob = s.toggleAmount(SettingsSheet::kMetronome); }
    assert(knob > 0.0 && knob < 1.0);
    r.settle();
    assert(s.toggleAmount(SettingsSheet::kMetronome) == 0.0);
    // a value its list lacks is still shown, as an entry of its own
    r.cmd("settings set newBpm=137");
    r.click(s.railRect(SettingsSheet::kNewSongs));
    r.settle();
    assert(s.shownValue(SettingsSheet::kNewBpm) == "137 bpm (set elsewhere)");
    // Escape with a list open closes the list, not the sheet
    r.click(s.controlRect(SettingsSheet::kNewSig));
    r.settle();
    assert(s.comboOpen(SettingsSheet::kNewSig));
    r.key(27);
    r.settle();
    assert(!s.comboOpen(SettingsSheet::kNewSig) && s.isOpen());
    // the sheet opens again on the page it was left on
    r.key(27);
    r.settle();
    r.app->openSettings();
    r.settle();
    assert(s.section() == SettingsSheet::kNewSongs && s.pageAmount(SettingsSheet::kNewSongs) == 1.0);
    pass("Settings in pages (R-SET-5): a rail of sections, one page cross-faded in as the highlight slides; a toggle eases set from a shell; a value its list lacks is shown; Escape closes an open list first");
}

static void test_settings_folders()
{
    sltest::Rig r("ui-folders", 1280, 800);
    r.cmd("folder add /music/Samples");
    r.cmd("folder add /music/Loops");
    r.settle();
    r.app->openSettings();
    r.settle();
    r.click(r.app->settings().railRect(arstro::solaris_ui::SettingsSheet::kFolders)); // its page (R-SET-5)
    r.settle();
    r.app->settings().revealRect(r.app->settings().folderRemoveRect(0)); // the page scrolls (R6): bring it in first
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
    sltest::Rig r("ui-bar", 1280, 800, false);                             // no output device: Play is refused
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
    assert(b.rowCount() == 3 && b.row(0).kind == "folder" && b.row(1).kind == "search" && b.row(2).kind == "favourites"); // R-PRESET-3 under it
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
    assert(sentLine(r, "clip add --src \"/music/Samples/808 kick.wav\" --at 4 --lane new"));   // below the last lane (R-BROWSE-3)
    const auto &m = r.svc->model();
    assert(m.clips.size() == 1 && m.clips[0].at == 4.0 && m.strips.size() == 2 && m.lanes.size() == 1); // its own strip, a new lane
    assert(r.app->project().ghostAmount() == 0.0);
    pass("Browser: the tab highlight slides; a sample folder is browsed; a sample dragged to the lanes is `clip add` at the drop beat");
}

static void test_a_lane_is_an_instruments_track()
{
    // R-LANE-3: an instrument's lane is its TRACK — named by it, its instrument under its name; a clip dragged
    // onto another track plays through that one; the lane's menu makes a track (fading in), a plain lane, or copies its id
    sltest::Rig r("ui-tracks", 1280, 800);
    r.cmd("project new " + r.song("Tracks") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");                 // ch_2, ln_1, ac_1
    r.cmd("clip add --instrument drums --at 0 --length 4");                 // ch_3, ln_2, ac_2
    r.cmd("lane add Ideas");                                                // ln_3, plain
    r.cmd("set ch_2.name=Bass");
    r.settle();
    auto &tl = r.app->project().timeline();
    auto &menu = r.app->menu();
    assert(tl.rowLabel(0) == "Bass" && tl.trackLabel("ln_1") == "Basic Synth" && tl.trackAmount("ln_1") == 1.0); // renamed with its strip (D-2)
    assert(tl.trackLabel("ln_2") == "Drum Machine" && tl.trackAmount("ln_3") == 0.0);
    // the bass's clip dragged onto the drums' track: ONE `clip move`, and it plays through the drums now
    const artboard::Rect clip = world(tl, tl.clipRect("ac_1"));
    const artboard::Rect row1 = world(tl, tl.rowRect(1));
    r.drag(clip.x + 12.0, cy(clip), clip.x + 12.0, cy(row1), 6);
    assert(sentLine(r, "clip move ac_1 --lane ln_2"));
    r.frame();
    assert(tl.clipHueAmount("ac_1") < 1.0);                                // its colour eases to the drums' — caught mid-ease
    r.settle();
    const auto *ac1 = &r.svc->model().clips[0];
    for (const auto &c : r.svc->model().clips)
        if (c.id == "ac_1") ac1 = &c;
    assert(ac1->track == "ch_3" && ac1->lane == "ln_2" && tl.clipHueAmount("ac_1") == 1.0);
    // the plain lane's menu: Track of ▸ Bass — and its instrument's line fades in
    const artboard::Rect head = world(tl, artboard::Rect{0, tl.rowRect(2).y, Timeline::kHeaderW, tl.rowRect(2).h});
    r.click(cx(head), cy(head), 2);
    r.settle();
    assert(menu.isOpen() && menu.item(0).label.rfind("Track of", 0) == 0 && menu.item(menu.itemCount() - 1).label == "Copy ID");
    r.click(menu.itemRect(0));
    r.settle();
    int bass = -1;
    for (int i = 0; i < menu.itemCount(); ++i)
        if (menu.item(i).label == "Bass") bass = i;
    assert(bass >= 0);
    r.click(menu.itemRect(bass));
    r.frame();
    r.frame();
    assert(sentLine(r, "set ln_3.strip=ch_2") && tl.trackAmount("ln_3") > 0.0 && tl.trackAmount("ln_3") < 1.0);
    r.settle();
    assert(tl.trackAmount("ln_3") == 1.0 && tl.trackLabel("ln_3") == "Basic Synth" && tl.rowLabel(2) == "Ideas");
    // …and Plain Lane again: it fades out
    r.click(cx(head), cy(head), 2);
    r.settle();
    int plain = -1;
    for (int i = 0; i < menu.itemCount(); ++i)
        if (menu.item(i).label == "Plain Lane") plain = i;
    assert(plain >= 0);
    r.click(menu.itemRect(plain));
    r.frame();
    r.frame();
    assert(sentLine(r, "set ln_3.strip=none") && tl.trackAmount("ln_3") > 0.0 && tl.trackAmount("ln_3") < 1.0);
    r.settle();
    assert(tl.trackAmount("ln_3") == 0.0);
    // an instrument dragged from the browser onto a track gets a lane of its own (it cannot play through another's)
    auto &b = r.app->project().browser();
    r.click(world(b, b.tabRect(1)));
    r.settle();
    const artboard::Rect from = world(b, b.rowRect(1));                     // Basic Synth
    const double toX = world(tl, artboard::Rect{tl.beatToX(8.0), 0, 0, 0}).x + 2.0;
    r.drag(cx(from), cy(from), toX, cy(world(tl, tl.rowRect(0))), 8);
    r.settle();
    assert(sentLine(r, "clip add --instrument synth --at 8 --length 4 --lane new"));
    pass("Tracks: an instrument's lane is named by it and names its instrument; a clip dragged onto another track plays through it (its colour easing); Track of ▸ / Plain Lane fade the line in and out; a browser drop onto a track gets its own lane (R-LANE-3)");
}

static void test_midi_clips_are_made_looped_moved_and_listed()
{
    // R-CLIP-6…8, R-BROWSE-4: a MIDI clip made on its track, looped by its end, moved between tracks; the song's
    // MIDI list dragged in
    sltest::Rig r("ui-midi", 1280, 800);
    r.cmd("project new " + r.song("Midi") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");                 // ch_2, ln_1, pt_1, ac_1
    r.cmd("clip add --instrument drums --at 0 --length 4");                 // ch_3, ln_2, pt_2, ac_2
    r.cmd("set ch_2.name=Bass");
    r.settle();
    auto &tl = r.app->project().timeline();
    auto &menu = r.app->menu();
    auto &wl = r.app->project().windows();
    const artboard::Rect tw = world(tl, artboard::Rect{0, 0, 0, 0});
    auto clipOf = [&](const std::string &id) {
        const auto &cs = r.svc->model().clips;
        for (size_t i = 0; i < cs.size(); ++i)
            if (cs[i].id == id) return &cs[i];
        return static_cast<decltype(&cs[0])>(nullptr);
    };
    const double step = tl.snapStep();
    // a double-click on the Bass track's empty space: a one-bar MIDI clip there, on the snap step — one line
    const double rowY = cy(world(tl, tl.rowRect(0)));
    r.click(tw.x + tl.beatToX(8.1), rowY);
    r.click(tw.x + tl.beatToX(8.1), rowY);
    r.settle();
    assert(sentLine(r, "clip add --lane ln_1 --at " + Timeline::beatText(std::round(8.1 / step) * step) + " --length 4"));
    assert(clipOf("ac_3") && clipOf("ac_3")->track == "ch_2" && clipOf("ac_3")->lane == "ln_1" && clipOf("ac_3")->pattern == "pt_3");
    // …and its menu offers the same
    r.click(tw.x + tl.beatToX(14.1), rowY, 2);
    r.settle();
    assert(menu.isOpen() && menu.itemCount() == 1 && menu.item(0).label == "New MIDI Clip");
    r.click(tw.x + tl.beatToX(30.0), cy(world(tl, tl.rowRect(1))) + 4.0 * Timeline::kRowH); // a press outside closes it
    r.settle();
    assert(!menu.isOpen());
    // ac_1's right edge dragged: its length is the pointer's while held (nothing said), ONE line on release, staying
    const artboard::Rect c1 = world(tl, tl.clipRect("ac_1"));
    const size_t before = r.sent.size();
    r.drag(c1.right() - 2.0, cy(c1), tw.x + tl.beatToX(9.9), cy(c1), 6, false);
    const double want = std::round(9.9 / step) * step;
    assert(tl.resizing() && tl.clipLengthLive("ac_1") == want && r.sent.size() == before);
    r.app->pointer(2, tw.x + tl.beatToX(9.9), cy(c1), 0, r.now);
    r.frame();
    assert(r.sent.size() == before + 1 && r.sent.back() == "set ac_1.length=" + Timeline::beatText(want));
    assert(tl.clipLengthLive("ac_1") == want);                              // where it was let go
    r.settle();
    assert(clipOf("ac_1")->length == want);                                 // the pattern (4 beats) repeats in it
    // a shell's length EASES (nothing snaps)
    r.cmd("set ac_1.length=6");
    r.frame();
    r.frame();
    assert(tl.clipLengthLive("ac_1") < want && tl.clipLengthLive("ac_1") > 6.0);
    r.settle();
    assert(tl.clipLengthLive("ac_1") == 6.0);
    // ac_3 dragged onto the drums' track: it plays through the drums
    const artboard::Rect c3 = world(tl, tl.clipRect("ac_3"));
    r.drag(c3.x + 10.0, cy(c3), c3.x + 10.0, cy(world(tl, tl.rowRect(1))), 6);
    r.settle();
    assert(sentLine(r, "clip move ac_3 --lane ln_2") && clipOf("ac_3")->track == "ch_3");
    // the Song tab: MIDI first — New MIDI, then each pattern with its length, notes and who plays it
    auto &b = r.app->project().browser();
    r.click(world(b, b.tabRect(2)));
    r.settle();
    assert(b.row(0).kind == "header" && b.row(0).label == "MIDI" && b.row(1).kind == "newmidi");
    int p1 = -1;
    for (int i = 0; i < b.rowCount(); ++i)
        if (b.row(i).value == "pt_1") p1 = i;
    assert(p1 > 1 && b.row(p1).kind == "pattern" && contains(b.row(p1).note, "1 bar") && contains(b.row(p1).note, "Bass"));
    // New MIDI: one `pattern new`, its row growing in
    r.click(world(b, b.rowRect(1)));
    r.frame();
    r.frame();
    assert(sentLine(r, "pattern new --name \"MIDI 4\""));
    int p4 = -1;
    for (int i = 0; i < b.rowCount(); ++i)
        if (b.row(i).value == "pt_4") p4 = i;
    assert(p4 > 0 && b.rowAlpha(p4) > 0.0 && b.rowAlpha(p4) < 1.0 && contains(b.row(p4).note, "unplayed"));
    r.settle();
    // dragged onto the Bass track: a clip of it there, playing through the bass
    const artboard::Rect from = world(b, b.rowRect(p4));
    r.drag(cx(from), cy(from), tw.x + tl.beatToX(12.0) + 2.0, rowY, 8);
    r.settle();
    assert(sentLine(r, "clip add --pattern pt_4 --lane ln_1 --at 12"));
    // pt_1 dropped below the last lane: through the instrument its newest clip plays through, on a new track for it
    for (int i = 0; i < b.rowCount(); ++i)
        if (b.row(i).value == "pt_1") p1 = i;
    const artboard::Rect f1 = world(b, b.rowRect(p1));
    const double below = cy(world(tl, tl.rowRect(1))) + 2.0 * Timeline::kRowH;
    r.drag(cx(f1), cy(f1), tw.x + tl.beatToX(16.0) + 2.0, below, 8);
    r.settle();
    assert(sentLine(r, "clip add --pattern pt_1 --strip ch_2 --at 16 --lane new"));
    // a pattern nothing plays, dropped off a track: a notice, nothing sent
    r.cmd("pattern new --name Loose");                                      // pt_5
    r.settle();
    int p5 = -1;
    for (int i = 0; i < b.rowCount(); ++i)
        if (b.row(i).value == "pt_5") p5 = i;
    const size_t n5 = r.sent.size();
    const artboard::Rect f5 = world(b, b.rowRect(p5));
    r.drag(cx(f5), cy(f5), tw.x + tl.beatToX(20.0) + 2.0, below + 2.0 * Timeline::kRowH, 8);
    r.settle();
    assert(r.sent.size() == n5 && contains(r.app->toastText(), "nothing plays it yet"));
    // a double-click: its piano roll; a right-click: its menu
    r.click(world(b, b.rowRect(p1)));
    r.click(world(b, b.rowRect(p1)));
    r.settle();
    assert(wl.isOpen("roll:pt_1"));
    r.click(cx(world(b, b.rowRect(p5))), cy(world(b, b.rowRect(p5))), 2);
    r.settle();
    assert(menu.isOpen() && menu.itemCount() == 5 && menu.item(0).label.rfind("Rename", 0) == 0 && menu.item(4).label == "Delete");
    r.click(menu.itemRect(4));
    r.settle();
    assert(sentLine(r, "pattern delete pt_5"));
    pass("MIDI clips: a double-click on a track makes one there (its menu too); an end dragged is the pointer's, one `set .length` on release, a shell's eases; moved onto another track it plays through it; the Song tab lists the MIDI, New MIDI grows in, a pattern dropped on a track or below plays through the right instrument, unplayed off a track says so, a double-click opens its roll (R-CLIP-6…8, R-BROWSE-4)");
}

static void test_an_instruments_face_is_an_analog_panel()
{
    // R-UI-12 in Solaris's window: modules titled by the registry, knobs and switches, screens; every value eased; the
    // parameter menu on a knob; a bound knob names its binding and takes no drag; the modules wrap; an effect keeps its list
    sltest::Rig r("ui-face", 1440, 900);
    r.cmd("project new " + r.song("Face") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");                 // ch_2, dv_1
    r.cmd("device add ch_2 --type reverb");                                  // dv_2
    r.settle();
    auto &wl = r.app->project().windows();
    wl.openDevice("dv_1");
    r.settle();
    auto &p = *wl.devicePanel("dv_1");
    auto *f = p.face();
    assert(f && f->sectionCount() == 8 && f->sectionTitle(0) == "Oscillator 1" && f->sectionTitle(5) == "Voice" && f->sectionTitle(7) == "Mix & Output");
    assert(wl.window("dev:dv_1")->width.value() >= 800.0);                   // opened wide enough for its modules
    // a shell's change turns the knob there — caught mid-turn — and lights it
    const double res0 = f->turnLive("filter.res");
    r.cmd("set dv_1.filter.res=0.9");
    r.frame();
    r.frame();
    assert(f->turnLive("filter.res") > res0 && f->turnLive("filter.res") < 0.9);
    r.settle();
    assert(std::fabs(f->turnLive("filter.res") - 0.9) < 1e-9 && f->litAmount("filter.res") == 1.0);
    // a key: that choice — one line; the light slides there
    const int wi = f->controlIndex("osc1.wave");
    r.click(world(*f, f->keyRect(wi, 3)));                                      // triangle
    r.frame();
    r.frame();
    assert(sentLine(r, "set dv_1.osc1.wave=triangle") && f->lightLive("osc1.wave") > 1.0 && f->lightLive("osc1.wave") < 3.0);
    r.settle();
    assert(f->lightLive("osc1.wave") == 3.0 && f->valueText("osc1.wave") == "triangle");
    // a knob double-clicked: its default — scrolled into view first, as a hand would (the face is taller than
    // the window since the oscillators gained a Width, REQ-synth2-6)
    r.cmd("set dv_1.volume=-3");
    wl.devicePanel("dv_1")->reveal("volume");
    r.settle();
    const artboard::Rect vol = world(*f, f->dialRect(f->controlIndex("volume")));
    r.click(cx(vol), cy(vol));
    r.click(cx(vol), cy(vol));
    r.settle();
    // (the face's control order is its modules', not the registry's: the parameter is found by name)
    double volume = 0.0;
    for (const auto &pm : r.svc->model().strips[0].devices[0].params)
        if (pm.name == "volume") volume = pm.value;
    assert(sentLine(r, "set dv_1.volume=-12") && volume == -12.0);
    // its menu on a right-click (R-WIN-3): Create Automation binds it
    const artboard::Rect cut = world(*f, f->dialRect(f->controlIndex("filter.cutoff")));
    r.click(cx(cut), cy(cut), 2);
    r.settle();
    auto &menu = r.app->menu();
    int create = -1;
    for (int k = 0; k < menu.itemCount(); ++k)
        if (menu.item(k).label == "Create Automation") create = k;
    assert(menu.isOpen() && create >= 0);
    r.click(menu.itemRect(create));
    r.settle();
    // bound: it names its binding under its knob and takes no drag (its menu clears it)
    assert(contains(f->valueText("filter.cutoff"), "au_1"));
    const size_t n = r.sent.size();
    r.drag(cx(cut), cy(cut), cx(cut), cy(cut) - 40.0, 5);
    r.settle();
    assert(r.sent.size() == n);
    // the modules flow: wrapped in a narrow window — nothing spills, the oscillators alone (two small modules, Voice
    // and Vibrato, may share a row) — the sources together in a wide one
    f->layoutFor(390.0);
    for (int k = 0; k < f->sectionCount(); ++k)
        assert(f->sectionRect(k).x >= arstro::solaris_ui::AnalogFace::kPad && f->sectionRect(k).right() <= 390.0 - arstro::solaris_ui::AnalogFace::kPad + 0.5);
    assert(f->sectionRect(0).x == arstro::solaris_ui::AnalogFace::kPad && f->sectionRect(1).x == arstro::solaris_ui::AnalogFace::kPad);
    f->layoutFor(980.0);
    assert(f->sectionRect(0).y == f->sectionRect(1).y && f->sectionRect(1).y == f->sectionRect(2).y);
    r.settle();
    // an effect keeps its list
    wl.openDevice("dv_2");
    r.settle();
    auto &q = *wl.devicePanel("dv_2");
    assert(q.face() == nullptr && q.rowCount() > 0 && q.slider("size") != nullptr);
    pass("an instrument's window is its analog face: six titled modules; a shell's change turns a knob (mid-turn) and lights it; a key slides its light; a double-click is the default; the menu binds a knob, which then names its binding and takes no drag; the modules flow with the width; an effect keeps its list (R-UI-12)");
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
    // R-LANE-1: a lane moved from a shell (`lane move`) slides to its row with its clips — caught mid-tween
    const double row1 = cy(world(tl, tl.clipRect("ac_1")));
    r.cmd("lane move ln_2 --to 0");
    r.frame();
    r.frame();
    const double moving = cy(world(tl, tl.clipRect("ac_1")));
    r.settle();
    const double row0 = cy(world(tl, tl.clipRect("ac_1")));
    assert(row0 < row1 - 1.0 && moving < row1 - 0.01 && moving > row0 + 0.01);
    assert(r.svc->model().lanes[0].id == "ln_2" && r.svc->model().lanes[0].order == 0);
    pass("An instrument dropped on a lane is ONE `clip add --instrument` — a strip and its clip; a clip dragged follows the pointer and lands as `clip move`; a lane moved slides with its clips");
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

static void test_an_instrument_track_is_armed_from_its_header()
{
    // R-MIDI-2: an instrument track's header has an arm button — ONE `strip arm` line; it lights red, eased; again, disarmed
    sltest::Rig r("ui-arm", 1280, 800);
    r.cmd("project new " + r.song("Arm") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");     // ch_2 on its own track
    r.settle();
    auto &tl = r.app->project().timeline();
    int row = -1;
    for (int i = 0; i < tl.rowCount(); ++i)
        if (tl.rowTrack(i) == "ch_2") row = i;
    assert(row >= 0 && tl.armRect(row).w > 0.0 && tl.armAmount("ch_2") == 0.0);
    r.click(world(tl, tl.armRect(row)));
    assert(r.sent.back() == "strip arm ch_2" && r.svc->model().armed == "ch_2");
    r.frame();
    r.frame();
    assert(tl.armAmount("ch_2") > 0.0 && tl.armAmount("ch_2") < 1.0); // it lights, eased
    r.settle();
    assert(tl.armAmount("ch_2") == 1.0);
    r.click(world(tl, tl.armRect(row)));
    assert(r.sent.back() == "strip arm none" && r.svc->model().armed.empty());
    r.settle();
    assert(tl.armAmount("ch_2") == 0.0);
    // the plain rows have none
    for (int i = 0; i < tl.rowCount(); ++i)
        if (tl.rowTrack(i).empty()) assert(tl.armRect(i).w == 0.0);
    // Record, beside Play: nothing armed — refused, the toast says what to do
    auto &bar = r.app->project().bar();
    using arstro::solaris_ui::SongBar;
    r.click(world(bar, bar.hitRect(SongBar::kRecord)));
    r.settle();
    assert(r.sent.back() == "transport record" && contains(r.app->toastText(), "arm a strip first"));
    // armed: recording, lit red (eased); again, the stop that lands the take
    r.click(world(tl, tl.armRect(row)));
    r.settle();
    r.click(world(bar, bar.hitRect(SongBar::kRecord)));
    assert(r.sent.back() == "transport record" && r.svc->model().transport.recording);
    r.frame();
    r.frame();
    assert(bar.recordAmount() > 0.0 && bar.recordAmount() < 1.0);
    r.settle();
    assert(bar.recordAmount() == 1.0);
    r.click(world(bar, bar.hitRect(SongBar::kRecord)));
    assert(r.sent.back() == "transport stop" && !r.svc->model().transport.recording);
    r.settle();
    assert(bar.recordAmount() == 0.0);
    pass("Arm and Record (R-MIDI-2): an instrument track's header arms its strip in ONE `strip arm` line, lit red (eased); again, "
         "`strip arm none`; plain rows have no button; Record beside Play is `transport record` (refused, toasted, with nothing "
         "armed), lit while recording, and again the `transport stop` that lands the take");
}

static void test_the_export_sheet_is_one_render_line()
{
    // R-EXPORT-1: File › Export… — a combo a choice, the rows that do not apply greyed (eased) and inert, and Export…
    // the host's save dialog, then ONE render line
    sltest::Rig r("ui-export", 1280, 800);
    r.cmd("project new " + r.song("Mixdown") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");
    r.settle();
    std::string asked, suggested;
    r.app->onPickSave = [&](const std::string &title, const std::string &s, std::function<void(const std::string &)> done) {
        asked = title;
        suggested = s;
        done(r.dir + "/Mixdown out" + s.substr(s.rfind('.')));
    };
    auto &ms = r.app->project().bar().menus();
    r.click(world(ms, ms.titleRect(0)));
    r.settle();
    assert(ms.menu(0).items[5].label == "Export\xE2\x80\xA6");
    r.click(world(ms, ms.itemRect(0, 5)));
    r.frame();
    r.frame();
    auto &ex = r.app->exportSheet();
    assert(ex.isOpen() && ex.appearAmount() > 0.0 && ex.appearAmount() < 1.0); // it fades in
    r.settle();
    using ES = arstro::solaris_ui::ExportSheet;
    assert(ex.rowAmount(ES::kBits) == 1.0 && ex.rowAmount(ES::kQuality) == 0.0 && ex.flags() == "--format wav --bits 24");
    auto pick = [&](int row, int i) {
        r.click(ex.controlRect(row));
        r.settle();
        assert(ex.comboOpen(row));
        r.click(ex.optionRect(row, i));
    };
    // MP3: its quality comes up and the bit depth greys — eased, caught mid-way
    pick(ES::kFormat, 2);
    r.frame();
    r.frame();
    assert(ex.rowAmount(ES::kQuality) > 0.0 && ex.rowAmount(ES::kQuality) < 1.0 && ex.rowAmount(ES::kBits) > 0.0);
    r.settle();
    assert(ex.rowAmount(ES::kBits) == 0.0 && ex.flags() == "--format mp3 --kbps 320");
    r.click(ex.controlRect(ES::kBits)); // a greyed row takes no click
    r.settle();
    assert(!ex.comboOpen(ES::kBits));
    // 44.1 kHz, 192 kbps, −14 LUFS (its ceiling comes up with it), a stem per strip
    pick(ES::kRate, 1);
    r.settle();
    pick(ES::kQuality, 1);
    r.settle();
    assert(ex.rowAmount(ES::kCeiling) == 0.0);
    pick(ES::kLoudness, 3);
    r.settle();
    assert(ex.rowAmount(ES::kCeiling) == 1.0);
    r.click(ex.controlRect(ES::kStems));
    r.settle();
    const std::string want = "--format mp3 --rate 44100 --kbps 192 --normalize -14 --ceiling -1 --stems all";
    assert(ex.flags() == want);
    r.click(ex.exportRect());
    r.settle();
    assert(!ex.isOpen() && asked == "Export the mix" && suggested == "Mixdown.mp3");
    assert(r.sent.back() == "render --out \"" + r.dir + "/Mixdown out.mp3\" " + want);
    assert(contains(r.app->toastText(), "no mp3 writer")); // this rig's host has no encoders: the service's own sentence
    // a choice the format cannot take moves: 32-bit float, then FLAC — 24-bit, its label cross-fading there
    r.click(world(ms, ms.titleRect(0)));
    r.settle();
    r.click(world(ms, ms.itemRect(0, 5)));
    r.settle();
    pick(ES::kFormat, 0);
    r.settle();
    pick(ES::kBits, 2);
    r.settle();
    assert(ex.flags() == "--format wav --rate 44100 --bits 32f --normalize -14 --ceiling -1 --stems all");
    pick(ES::kFormat, 1);
    r.frame();
    r.frame();
    assert(ex.shownValue(ES::kBits) == "24-bit");
    r.settle();
    assert(ex.flags() == "--format flac --rate 44100 --bits 24 --normalize -14 --ceiling -1 --stems all");
    r.key(27);
    r.settle();
    assert(!ex.isOpen());
    pass("Export (R-EXPORT-1): File › Export… fades in; MP3 brings its quality up and greys the bit depth (eased, inert); rate, "
         "quality, loudness (its ceiling with it) and stems make ONE render line after the host's save dialog; FLAC moves a float "
         "depth to 24-bit; Escape closes");
}

static void test_meters_and_the_loudness_window()
{
    // R-METER-2: an RMS core, a held line and a clip light on every meter; R-METER-1: the master's LUFS chip opens
    // the Loudness window — every number the model's, eased
    sltest::Rig r("ui-meters", 1280, 800);
    r.cmd("project new " + r.song("Meters") + ".slp --bpm 120");
    for (int k = 0; k < 4; ++k) r.cmd("clip add --src tone.wav --at " + std::to_string(k) + " --length 1"); // its strip: a 382 Hz sine at 0.4
    const std::string ch = r.svc->model().clips.front().track;  // on the first mixer, the page shown
    r.cmd("set " + ch + ".gain=12");                             // ×4: over full scale
    r.settle();
    auto &d = r.app->project().dock();
    const auto S = [&r, ch]() -> const arstro::solaris::StripModel & {
        for (const auto &s : r.svc->model().strips)
            if (s.id == ch) return s;
        assert(false);
        return r.svc->model().strips[0];
    };
    assert(S().id == ch && d.clipAmount(ch) == 0.0 && d.lufsLive() == -70.0);
    r.cmd("transport play");
    bool lit = false;
    for (int k = 0; k < 200 && !lit; ++k) { std::this_thread::sleep_for(std::chrono::milliseconds(16)); r.frame(); lit = S().clip; }
    assert(lit && r.svc->model().transport.masterClip);
    r.frame();
    assert(d.clipAmount(ch) > 0.0 && d.clipAmount(ch) < 1.0);   // the light comes up, eased
    for (int k = 0; k < 110; ++k) { std::this_thread::sleep_for(std::chrono::milliseconds(16)); r.frame(); } // ≈ 0.45 s of audio (the rig plays at a quarter speed)
    assert(d.clipAmount(ch) == 1.0 && d.clipAmount("master") == 1.0);
    assert(d.rmsLive(ch, 0) > 0.5 && d.rmsLive(ch, 0) < d.meterLive(ch, 0));  // the RMS core under the peak
    assert(d.holdLive(ch, 0) >= d.meterLive(ch, 0) - 0.02);
    assert(r.svc->model().loudness.integrated > -143.0);
    // the clip light clicked: ONE line, every light out — eased
    r.cmd("set " + ch + ".gain=0");
    for (int k = 0; k < 10; ++k) { std::this_thread::sleep_for(std::chrono::milliseconds(16)); r.frame(); }
    r.click(world(d, d.clipRect(ch)));
    assert(r.sent.back() == "meters clear" && !S().clip && !r.svc->model().transport.masterClip);
    r.frame();
    r.frame();
    assert(d.clipAmount(ch) > 0.0 && d.clipAmount(ch) < 1.0);
    for (int k = 0; k < 20; ++k) { std::this_thread::sleep_for(std::chrono::milliseconds(16)); r.frame(); }
    assert(d.clipAmount(ch) == 0.0);
    for (int k = 0; k < 110; ++k) { std::this_thread::sleep_for(std::chrono::milliseconds(16)); r.frame(); }
    r.cmd("transport stop");
    r.settle();
    // stopped, the loudness stays to be read: the master's chip shows the integrated, caught up
    const auto &L = r.svc->model().loudness;
    assert(L.integrated > -143.0 && std::fabs(d.lufsLive() - std::max(-70.0, L.integrated)) < 1e-6);
    r.click(world(d, d.loudnessRect()));
    auto &wl = r.app->project().windows();
    r.frame();
    r.frame();
    assert(wl.isOpen("loudness") && wl.window("loudness")->appearAmount() > 0.0 && wl.window("loudness")->appearAmount() < 1.0);
    r.settle();
    auto &lp = *wl.loudnessPanel();
    using LP = arstro::solaris_ui::LoudnessPanel;
    assert(std::fabs(lp.readingLive(LP::Integrated) - L.integrated) < 1e-6 && lp.readingShown(LP::Integrated) == 1.0);
    assert(std::fabs(lp.readingLive(LP::TruePeak) - L.truePeak) < 1e-6 && std::fabs(lp.readingLive(LP::Correlation) - L.correlation) < 1e-6);
    int loudest = 0;
    for (int b = 1; b < LP::kBands; ++b)
        if (lp.bandLive(b) > lp.bandLive(loudest)) loudest = b;
    assert(loudest == 25 || loudest == 26);                      // 382 Hz: between the 354 and 397 Hz bands
    // Reset: ONE line; the integrated fades to "—" (caught mid-fade), the master's chip with it
    r.click(world(lp, lp.resetRect()));
    assert(r.sent.back() == "meters clear" && r.svc->model().loudness.integrated == -144.0);
    r.frame();
    r.frame();
    assert(lp.readingShown(LP::Integrated) > 0.0 && lp.readingShown(LP::Integrated) < 1.0);
    r.settle();
    assert(lp.readingShown(LP::Integrated) == 0.0 && lp.readingShown(LP::Momentary) == 0.0);
    pass("Meters (R-METER-1/2): over full scale the clip lights come up eased and latch; a click on one is `meters clear` and they go out eased; "
         "the RMS core sits under the peak and the held line above it; stopped, the master's chip shows the integrated LUFS, caught up; it "
         "opens the Loudness window (fading in) whose readings and 1/6-octave bands are the model's; Reset is one line and the numbers fade to —");
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
    auto *face = p.face();                                               // an instrument: its analog face (R-UI-12)
    assert(face && face->isKnob(face->controlIndex("filter.cutoff")) && !face->isKnob(face->controlIndex("osc1.wave"))); // a choice is a switch
    assert(contains(w->title(), "Basic Synth"));
    // a knob dragged up is `set <dv>.<param>=…` in its unit (a log taper: cutoff moves in ratios)
    p.reveal("filter.cutoff");                                           // scroll to it when the window is short
    r.settle();
    const artboard::Rect dial = world(*face, face->dialRect(face->controlIndex("filter.cutoff")));
    r.drag(cx(dial), cy(dial), cx(dial), cy(dial) - 40.0, 5);
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
    // every route has a cell (R-MIX-9): the columns are the service's `matrix.columns` — an instrument LATER on
    // Sources is one (forward = processing order, R-MIX-4 amended), where "strips off the first mixer" had none
    r.cmd("clip add --instrument drums --at 0 --length 4");              // ch_3, after ch_2 on Sources
    r.settle();
    const auto &cols = r.svc->model().matrix.columns;
    assert(std::find(cols.begin(), cols.end(), "ch_3") != cols.end() && d.cellRect("ch_2", "ch_3").w > 0.0);
    r.click(world(d, d.cellRect("ch_2", "ch_3")));
    r.settle();
    assert(sentLine(r, "send add ch_2 --to ch_3"));
    const size_t back = r.sent.size();
    r.click(world(d, d.cellRect("ch_3", "ch_3")));                       // itself: hatched, takes nothing
    r.settle();
    assert(r.sent.size() == back);
    pass("Mixer: + Effect is the registry; a chip opens the device's generated window (fade), a slider is `set` in its unit, bypass, a second window on top; the matrix adds a send, and has a column for every strip a route may reach");
}

static void test_the_keys_are_the_machines()
{
    // R-SET-4: a pressed combo is looked up in the model's keymap — re-keyed from anywhere, the window follows
    using arstro::solaris_ui::keyCodeOfKeysym;
    using arstro::solaris_ui::keyTypes;
    // the host's half: the KEY pressed (its unshifted keysym), named as the keymap names it; what it types, apart
    assert(keyCodeOfKeysym('a') == 'A' && keyCodeOfKeysym('1') == '1' && keyCodeOfKeysym(0xffc2) == 116); // F5
    assert(keyCodeOfKeysym(',') == 188 && keyCodeOfKeysym(0xff52) == 38 && keyCodeOfKeysym(0xfe20) == 9 && keyCodeOfKeysym(0xffe1) == 0); // Shift_L: none
    assert(arstro::solaris::comboText(keyCodeOfKeysym(0xffc2), false, false, true) == "Shift+F5");
    assert(keyTypes('a', false, false) && keyTypes(' ', false, false) && !keyTypes('a', true, false) && !keyTypes(0x0d, false, false) && !keyTypes(0x7f, false, false));

    sltest::Rig r("ui-keys", 1280, 800);
    r.cmd("project new " + r.song("Keys") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 8");
    const std::string ac = r.svc->model().clips[0].id, pt = r.svc->model().clips[0].pattern;
    r.cmd("notes add " + pt + " \"C4@0 E4@1\"");
    r.settle();
    const auto &t = r.svc->model().transport;
    auto keyMod = [&](int code, bool shift, bool ctrl) {
        artboard::KeyEvent e;
        e.type = artboard::KeyEvent::Type::Down;
        e.keyCode = code;
        e.shift = shift;
        e.ctrl = ctrl;
        r.app->key(e);
        r.frame();
    };
    // re-keyed by a script, not the window: Space no longer plays; F5 does, and Shift+F5 stops here
    r.cmd("settings set key.transport.play=F5 key.transport.stopHere=Shift+F5");
    r.settle();
    size_t mark = r.sent.size();
    r.key(32);
    r.key(32, true);
    assert(r.sent.size() == mark && !t.playing);
    r.key(116);
    assert(r.sent.back() == "transport play" && t.playing);
    for (int k = 0; k < 20; ++k) { std::this_thread::sleep_for(std::chrono::milliseconds(16)); r.frame(); }
    keyMod(116, true, false);
    assert(r.sent.back() == "transport stop" && !t.playing && t.position > 0.0);
    // the roll's keys, re-keyed: the roll follows
    r.cmd("settings set key.roll.up=K key.roll.selectAll=Ctrl+Shift+A key.roll.delete=Ctrl+S");
    auto &tl = r.app->project().timeline();
    auto &wl = r.app->project().windows();
    const artboard::Rect clip = world(tl, tl.clipRect(ac));
    r.click(clip.x + 12.0, cy(clip));
    r.click(clip.x + 12.0, cy(clip));
    r.settle();
    auto &roll = *wl.roll(pt);
    const artboard::Rect gw = world(roll, roll.gridRect());
    r.app->pointer(0, gw.x + 30.0, gw.y + 30.0, 0, r.now, false, false, true); // a Ctrl-press on its empty grid: the keys are its
    r.app->pointer(2, gw.x + 30.0, gw.y + 30.0, 0, r.now, false, false, true);
    r.settle();
    keyMod('A', false, true);
    assert(roll.selectionCount() == 0);                               // Ctrl+A is no longer select-all
    keyMod('A', true, true);
    assert(roll.selectionCount() == 2);
    mark = r.sent.size();
    keyMod(38, false, false);
    assert(r.sent.size() == mark);                                    // ↑ is no key now
    keyMod('K', false, false);
    assert(r.sent.back() == "notes move " + pt + " \"60@0 64@1\" --by 0 --semi 1");
    // the roll's keys shadow the song's while it has them: Ctrl+S deletes the selection; with none, it saves
    keyMod('S', false, true);
    assert(r.sent.back() == "notes delete " + pt + " \"61@0 65@1\"" && roll.selectionCount() == 0);
    keyMod('S', false, true);
    assert(r.sent.back() == "project save");
    pass("The keys are the machine's (R-SET-4): a combo is looked up in the model's keymap — re-keyed by a script, Space stops playing and F5 plays, the roll's keys follow and shadow the song's; the host names the key, not the character");
}

static void test_the_keyboard_section()
{
    // R-SET-4: the sheet's Keyboard section — a key clicked listens for the next, "+" for one more; Escape cancels,
    // Backspace removes, a key another action has is refused (and said); Reset — each ONE `settings set key.…`
    sltest::Rig r("ui-keysheet", 1280, 800);
    r.settle();
    r.key(188, true);                                                // Ctrl+Comma, from Home
    r.settle();
    auto &s = r.app->settings();
    assert(s.isOpen());
    r.click(s.railRect(arstro::solaris_ui::SettingsSheet::kKeyboard)); // its page (R-SET-5)
    r.settle();
    s.revealRect(s.keyChipRect("song.save", 0));
    r.settle();
    auto keysOf = [&](const std::string &id) {
        for (const auto &k : r.svc->model().settings.keys)
            if (k.id == id) return k.keys;
        return std::vector<std::string>{};
    };
    auto keyMod = [&](int code, bool shift, bool ctrl) {
        artboard::KeyEvent e;
        e.type = artboard::KeyEvent::Type::Down;
        e.keyCode = code;
        e.shift = shift;
        e.ctrl = ctrl;
        r.app->key(e);
        r.frame();
    };
    // a key clicked listens: its look eases in
    r.click(s.keyChipRect("transport.play", 0));
    assert(s.listeningFor() == "transport.play" && s.listeningSlot() == 0);
    double first = 0.0;
    for (int k = 0; k < 20 && first == 0.0; ++k) { r.frame(); first = s.listenAmount("transport.play", 0); }
    assert(first > 0.0 && first < 1.0);
    r.key(116);                                                      // F5
    assert(r.sent.back() == "settings set key.transport.play=F5" && keysOf("transport.play") == std::vector<std::string>{"F5"});
    assert(s.listeningFor().empty());
    double grow = 0.0, shrink = 1.0;
    for (int k = 0; k < 20 && grow == 0.0; ++k)
    {
        r.frame();
        grow = s.keyChipAmount("transport.play", "F5");
        shrink = s.keyChipAmount("transport.play", "Space");
    }
    assert(grow > 0.0 && grow < 1.0 && shrink > 0.0 && shrink < 1.0); // the new key grows in as the old shrinks out
    r.settle();
    assert(s.keyChipAmount("transport.play", "Space") == 0.0 && s.keyChipAmount("transport.play", "F5") == 1.0);
    // "+" listens for one more
    r.click(s.keyAddRect("transport.play"));
    assert(s.listeningFor() == "transport.play" && s.listeningSlot() == 1);
    keyMod(116, true, false);
    assert(r.sent.back() == "settings set \"key.transport.play=F5, Shift+F5\"" && (keysOf("transport.play") == std::vector<std::string>{"F5", "Shift+F5"}));
    r.settle();
    // a key another action of the song's keyboard has: refused by the service, the toast says which; nothing changes
    r.click(s.keyChipRect("song.save", 0));
    keyMod('D', false, true);
    assert(r.sent.back() == "settings set key.song.save=Ctrl+D" && keysOf("song.save") == std::vector<std::string>{"Ctrl+S"});
    assert(r.app->toastText().find("key.lanes.duplicate") != std::string::npos && s.isOpen());
    r.settle();
    // Escape cancels — nothing sent, the sheet stays; Backspace removes the key
    const size_t mark = r.sent.size();
    r.click(s.keyChipRect("transport.play", 1));
    r.key(27);
    assert(r.sent.size() == mark && s.isOpen() && s.listeningFor().empty());
    r.click(s.keyChipRect("transport.play", 1));                   // a second press so soon is a double-click: still a key chip's click
    r.key(8);
    r.key(8);
    assert(r.sent.back() == "settings set key.transport.play=F5" && keysOf("transport.play") == std::vector<std::string>{"F5"});
    r.settle();
    // Reset: ONE line for every changed action
    r.cmd("settings set key.roll.copy=none");
    r.settle();
    r.click(s.keyResetRect());
    assert(r.sent.back() == "settings set key.transport.play=default key.roll.copy=default");
    assert(keysOf("transport.play") == std::vector<std::string>{"Space"} && keysOf("roll.copy") == std::vector<std::string>{"Ctrl+C"});
    // not listening, Escape closes the sheet
    r.key(27);
    r.settle();
    assert(!s.isOpen());
    pass("The sheet's Keyboard section (R-SET-4): a key clicked listens (eased) for the next, \"+\" for one more; Escape cancels, Backspace removes, a key another action has is refused and said; keys grow in and shrink out; Reset is one line");
}

static void test_the_history_tree()
{
    // R-HIST-3: Edit ▸ Show History Tree… — cosmo's own view over the model's tree; a node clicked is ONE line
    sltest::Rig r("ui-history", 1280, 800);
    r.cmd("project new " + r.song("Hist") + ".slp --bpm 120");
    r.cmd("clip add --instrument drums --at 0 --length 4"); // node 1
    r.cmd("set ch_2.gain=-3");                              // node 2
    r.cmd("undo");
    r.cmd("set ch_2.pan=0.5");                              // node 3: a branch beside 2
    r.settle();
    auto &ms = r.app->project().bar().menus();
    r.click(world(ms, ms.titleRect(1)));                    // Edit
    r.settle();
    r.click(world(ms, ms.itemRect(1, 2)));                  // Show History Tree…
    auto &hv = r.app->historyView();
    assert(hv.isOpen() && hv.nodeCount() == 4 && hv.current() == 3);
    r.frame();
    r.frame();
    assert(hv.appearAmount() > 0.0 && hv.appearAmount() < 1.0); // it fades in, caught
    r.settle();
    // a node clicked: `history jump`, the view stays open, the highlight travels from the old node to the new
    const artboard::Point p = hv.nodePoint(2);
    r.click(p.x, p.y);
    assert(r.sent.back() == "history jump 2" && r.svc->model().historyCurrent == 2 && hv.isOpen());
    double lit = 0.0;
    for (int k = 0; k < 20 && lit == 0.0; ++k) { r.frame(); lit = hv.currentAmount(2); }
    assert(lit > 0.0 && lit < 1.0 && hv.currentAmount(3) > 0.0 && hv.currentAmount(3) < 1.0);
    r.settle();
    assert(hv.currentAmount(2) == 1.0 && hv.currentAmount(3) == 0.0);
    // the undo key walks the tree it shows
    r.key('Z', true);
    assert(r.sent.back() == "undo" && r.svc->model().historyCurrent == 1);
    r.settle();
    assert(hv.current() == 1);
    // an edit made elsewhere (a script) while it is open: it shows the new tree
    r.cmd("set ch_2.gain=-6");                              // node 4: a third branch under 1
    r.settle();
    assert(hv.nodeCount() == 5 && hv.current() == 4);
    // Escape closes it, eased; any other key does nothing while it is open
    const size_t mark = r.sent.size();
    r.key(32);
    assert(r.sent.size() == mark);
    r.key(27);
    r.frame();
    r.frame();
    assert(!hv.isOpen() && hv.appearAmount() > 0.0 && hv.appearAmount() < 1.0);
    r.settle();
    assert(hv.appearAmount() == 0.0);
    pass("The history tree (R-HIST-3): Edit ▸ Show History Tree… opens cosmo's view over the model's tree, eased; a node clicked is ONE `history jump` and the highlight travels; the undo key walks it; a script's edit shows; Escape closes it");
}

static void test_clips_are_copied_by_shift_drag_and_ctrl_c_v()
{
    // R-CLIP-9/10: Shift held at the drop duplicates; Ctrl+C / Ctrl+V paste at the time bar, end to end
    sltest::Rig r("ui-clipcopy", 1280, 800);
    r.cmd("project new " + r.song("Copy") + ".slp --bpm 120");
    r.cmd("clip add --instrument drums --at 0 --length 4"); // ac_1 on the drums' track (row 0)
    r.cmd("lane add Extra");                                // a plain lane (row 1)
    r.settle();
    auto &tl = r.app->project().timeline();
    auto clips = [&] { return r.svc->model().clips.size(); };
    struct { sltest::Rig &r; const arstro::solaris::ClipModel *operator()(const std::string &id) const {
        for (const auto &c : r.svc->model().clips) if (c.id == id) return &c;
        return nullptr; } } clipOf{r};
    const artboard::Rect origin = world(tl, artboard::Rect{0, 0, 0, 0});
    artboard::Rect c1 = world(tl, tl.clipRect("ac_1"));
    // Shift held: the copy look eases in while the clip is in hand (the original stays at full strength, a "+")
    const double x1 = origin.x + tl.beatToX(8.0) + 12.0;
    r.drag(c1.x + 12.0, cy(c1), x1, cy(c1), 2, false, true);
    assert(tl.copyAmount() > 0.0 && tl.copyAmount() < 1.0);
    r.settle();
    assert(tl.copyAmount() == 1.0);
    r.app->pointer(2, x1, cy(c1), 0, r.now, false, true);
    r.frame();
    assert(r.sent.back() == "clip duplicate ac_1 --at 8" && clips() == 2);
    assert(clipOf("ac_1")->at == 0.0 && clipOf("ac_2")->at == 8.0 && clipOf("ac_2")->pattern == clipOf("ac_1")->pattern); // linked
    assert(tl.selectedClip() == "ac_2" && tl.clipAlpha("ac_2") == 1.0); // where the pointer left it — not faded in
    r.settle();
    // onto another lane: one line names it
    c1 = world(tl, tl.clipRect("ac_1"));
    const artboard::Rect row1 = world(tl, tl.rowRect(1));
    const double x2 = origin.x + tl.beatToX(4.0) + 12.0;
    r.drag(c1.x + 12.0, cy(c1), x2, cy(row1), 8, true, true);
    assert(r.sent.back() == "clip duplicate ac_1 --at 4 --lane ln_2" && clipOf("ac_3")->lane == "ln_2" && clipOf("ac_1")->lane == "ln_1");
    r.settle();
    // without Shift it still moves
    c1 = world(tl, tl.clipRect("ac_3"));
    r.drag(c1.x + 12.0, cy(c1), c1.x + 12.0 + (tl.beatToX(2.0) - tl.beatToX(0.0)), cy(c1), 8, true, false);
    assert(r.sent.back() == "clip move ac_3 --at 6" && clips() == 3 && tl.copyAmount() < 1.0);
    r.settle();
    // Ctrl+C the selected clip (nothing sent), Ctrl+V at the time bar; again: right after the last copy
    c1 = world(tl, tl.clipRect("ac_1"));
    r.click(c1.x + 12.0, cy(c1));
    r.settle();
    const size_t mark = r.sent.size();
    r.key('C', true);
    assert(r.sent.size() == mark && r.app->toastText().find("Clip copied") != std::string::npos);
    r.cmd("transport seek 12");
    r.settle();
    r.key('V', true);
    assert(r.sent.back() == "clip duplicate ac_1 --at 12" && clipOf("ac_4")->at == 12.0 && clipOf("ac_4")->lane == "ln_1");
    r.settle();
    assert(tl.selectedClip() == "ac_4");
    r.key('V', true);
    assert(r.sent.back() == "clip duplicate ac_1 --at 16");
    r.cmd("transport seek 24");                             // the time bar moved: the next paste goes there
    r.settle();
    r.key('V', true);
    assert(r.sent.back() == "clip duplicate ac_1 --at 24" && clips() == 6);
    r.settle();
    // a piano roll with the keys: its Ctrl+V is the notes', never a clip's
    c1 = world(tl, tl.clipRect("ac_1"));
    r.click(c1.x + 12.0, cy(c1));
    r.click(c1.x + 12.0, cy(c1));
    r.settle();
    auto &roll = *r.app->project().windows().roll(clipOf("ac_1")->pattern);
    const artboard::Rect gw = world(roll, roll.gridRect());
    r.app->pointer(0, gw.x + 30.0, gw.y + 30.0, 0, r.now, false, false, true);
    r.app->pointer(2, gw.x + 30.0, gw.y + 30.0, 0, r.now, false, false, true);
    r.settle();
    const size_t before = r.sent.size();
    r.key('V', true);
    assert(r.sent.size() == before && clips() == 6);
    pass("Copying clips (R-CLIP-9/10): Shift held at the drop is ONE `clip duplicate --at [--lane]` — the copy look eased, the copy selected where it was let go; without Shift it moves; Ctrl+C / Ctrl+V paste at the time bar, end to end; in a roll Ctrl+V stays the notes'");
}

static void test_the_pointer_says_a_drag_resizes()
{
    // R-UI-14: over a grip that changes a size the pointer is a resize cursor — the zone a press takes — and it stays
    // while the resize is in hand; a window covers what is beneath; a modal is the arrow
    using arstro::solaris_ui::Cursor;
    assert(std::string(arstro::solaris_ui::cursorName(Cursor::ResizeH)) == "ew-resize" &&
           std::string(arstro::solaris_ui::cursorName(Cursor::ResizeDiag)) == "nwse-resize");
    sltest::Rig r("ui-cursor", 1280, 800);
    r.cmd("project new " + r.song("Cursor") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 8");
    r.cmd("notes add pt_1 \"C4@0:2\"");
    r.cmd("transport loop 12 16");
    r.settle();
    auto hover = [&](double x, double y) {
        r.app->pointer(1, x, y, 0, r.now);
        r.frame();
        return r.app->cursor();
    };
    auto &tl = r.app->project().timeline();
    // a clip's end ↔, its body the arrow
    const artboard::Rect c = world(tl, tl.clipRect("ac_1"));
    assert(hover(c.right() - 2.0, cy(c)) == Cursor::ResizeH);
    assert(hover(c.x + 20.0, cy(c)) == Cursor::Default);
    // the loop brace's ends ↔ (the lower half of the ruler, as a press takes them); its body moves it: the arrow
    const artboard::Rect br = world(tl, tl.loopRect()), ruler = world(tl, tl.rulerRect());
    const double by = ruler.y + ruler.h * 0.75;
    assert(hover(br.x, by) == Cursor::ResizeH && hover(br.right(), by) == Cursor::ResizeH);
    assert(hover(br.x + br.w * 0.5, by) == Cursor::Default);
    // a clip's end in hand: ↔ wherever the pointer goes, until it is let go
    r.app->pointer(0, c.right() - 2.0, cy(c), 0, r.now);
    r.frame();
    r.app->pointer(1, c.right() + 60.0, cy(c) + 90.0, 0, r.now);
    r.frame();
    assert(r.app->cursor() == Cursor::ResizeH);
    r.app->pointer(2, c.right() + 60.0, cy(c) + 90.0, 0, r.now);
    r.frame();
    assert(r.app->cursor() == Cursor::Default && r.sent.back().rfind("set ac_1.length=", 0) == 0);
    r.settle();
    // the dock's top edge ↕
    auto &dock = r.app->project().dock();
    const artboard::Rect d = world(dock, artboard::Rect{0, 0, dock.width.value(), dock.height.value()});
    assert(hover(d.x + 300.0, d.y + 2.0) == Cursor::ResizeV);
    assert(hover(d.x + 300.0, d.y + 40.0) == Cursor::Default);
    // a roll's window: its corner ⤡; in it a note's end ↔, the pattern's end ↔, a velocity bar ↕ — over the lanes
    const artboard::Rect c2 = world(tl, tl.clipRect("ac_1"));
    r.click(c2.x + 12.0, cy(c2));
    r.click(c2.x + 12.0, cy(c2));
    r.settle();
    auto &wl = r.app->project().windows();
    auto &roll = *wl.roll("pt_1");
    auto *win = wl.window("roll:pt_1");
    const artboard::Rect grip = world(*win, win->gripRect());
    assert(hover(cx(grip), cy(grip)) == Cursor::ResizeDiag);
    const artboard::Rect n = world(roll, roll.noteRect(60, 0.0));
    assert(hover(n.right() - 1.0, cy(n)) == Cursor::ResizeH && hover(n.x + 3.0, cy(n)) == Cursor::Default);
    const artboard::Rect end = world(roll, roll.endRect());
    assert(hover(cx(end), cy(end)) == Cursor::ResizeH);
    const artboard::Rect vel = world(roll, roll.velRect()), rw = world(roll, artboard::Rect{0, 0, 0, 0});
    assert(hover(rw.x + roll.beatToX(0.0) + 1.5, cy(vel)) == Cursor::ResizeV);
    // a window covers what is beneath it: over the window's body, never the lane's clip edge under it
    assert(hover(n.x + 3.0, cy(n)) == Cursor::Default);
    // a modal: the arrow, even over a grip beneath it
    r.app->openSettings();
    r.settle();
    assert(hover(cx(grip), cy(grip)) == Cursor::Default);
    pass("The pointer says a drag resizes (R-UI-14): ↔ a clip's end, the brace's ends, a note's end and the pattern's end; ↕ the dock's top edge and a velocity bar; ⤡ a window's corner — where a press takes them, kept while in hand; a window covers the lanes; a modal is the arrow");
}

static void test_a_looping_automation_is_seen_and_made()
{
    // R-AUTO-12/13 in the window: Create Pump from a fader's menu; the row's span drawn and its grips dragged (one line
    // each); a span set elsewhere eases; a click in a repeat adds the point in the cycle; the window's Loop combo
    using arstro::solaris_ui::Cursor;
    sltest::Rig r("ui-autoloop", 1280, 800);
    r.cmd("project new " + r.song("Loop") + ".slp --bpm 120");
    r.cmd("clip add --src " + r.dir + "/kick.wav --at 0 --length 16");
    r.settle();
    // a fader's menu: Create Pump is ONE line
    auto &dock = r.app->project().dock();
    const std::string ch = r.svc->model().clips[0].track;
    const artboard::Rect fader = world(dock, dock.faderRect(ch));
    r.click(cx(fader), cy(fader), 2);
    r.settle();
    auto &menu = r.app->menu();
    int pump = -1;
    for (int k = 0; k < menu.itemCount(); ++k)
        if (menu.item(k).label == "Create Pump") pump = k;
    assert(menu.isOpen() && pump >= 0);
    r.click(menu.itemRect(pump));
    r.settle();
    assert(sentLine(r, "auto create " + ch + ".gain --pump") && r.svc->model().automations.size() == 1 && r.svc->model().automations[0].loop == 1.0);
    r.cmd("set au_1.at=4 au_1.length=8");
    r.settle();
    auto &tl = r.app->project().timeline();
    const artboard::Rect lanes = world(tl, tl.rowRect(0));
    r.app->wheel(lanes.x + 400.0, lanes.y + 10.0, -4.0); // bring the automation row into view
    r.settle();
    artboard::Rect span = world(tl, tl.autoSpanRect("au_1"));
    const artboard::Rect origin = world(tl, artboard::Rect{0, 0, 0, 0});
    assert(span.w > 0.0 && std::fabs(span.x - (origin.x + tl.beatToX(4.0))) < 0.5 && std::fabs(span.right() - (origin.x + tl.beatToX(12.0))) < 0.5);
    // its grips: the resize cursor; the end dragged two beats on is ONE line
    r.app->pointer(1, span.right(), span.y + 4.0, 0, r.now);
    r.frame();
    assert(r.app->cursor() == Cursor::ResizeH);
    const double twoBeats = tl.beatToX(2.0) - tl.beatToX(0.0);
    r.drag(span.right(), span.y + 4.0, span.right() + twoBeats, span.y + 4.0, 8);
    assert(r.sent.back() == "set au_1.length=10" && r.svc->model().automations[0].length == 10.0);
    r.settle();
    // the start dragged a beat on: ONE line, the end kept
    span = world(tl, tl.autoSpanRect("au_1"));
    r.drag(span.x, span.y + 4.0, span.x + twoBeats * 0.5, span.y + 4.0, 8);
    assert(r.sent.back() == "set au_1.at=5 au_1.length=9" && r.svc->model().automations[0].at == 5.0);
    r.settle();
    // set from a shell: the span eases there
    span = world(tl, tl.autoSpanRect("au_1"));
    r.cmd("set au_1.at=8 au_1.length=4");
    double x = span.x;
    for (int k = 0; k < 20 && x == span.x; ++k) { r.frame(); x = world(tl, tl.autoSpanRect("au_1")).x; }
    assert(x > span.x && x < origin.x + tl.beatToX(8.0) - 0.5);
    r.settle();
    // a click in a REPEAT adds its point in the cycle (beat 10.25, snapped, is a place in the third repeat)
    const artboard::Rect row = world(tl, tl.autoRowRect("au_1"));
    r.click(origin.x + tl.beatToX(10.25), row.y + row.h * 0.5);
    for (int k = 0; k < 40; ++k) r.frame(); // a click waits out a double-click
    {
        // on the grid you see, then folded into the cycle: never a beat past the first one
        const double want = std::fmod(tl.snap(10.25) - 8.0, 1.0);
        assert(r.sent.back().rfind("auto point add au_1 --at " + arstro::solaris_ui::Timeline::beatText(want) + " --value ", 0) == 0);
    }
    r.settle();
    // its window: the Loop combo — a row picked is ONE line
    r.click(row.x + 30.0, row.y + row.h * 0.5);
    r.click(row.x + 30.0, row.y + row.h * 0.5);
    r.settle();
    auto *ap = r.app->project().windows().automationPanel("au_1");
    assert(ap);
    auto &combo = ap->loopCombo();
    assert(combo.shown() == "1 beat");
    const artboard::Rect field = world(*ap, ap->loopRect());
    r.click(cx(field), cy(field));
    r.settle();
    assert(combo.isOpen());
    const auto pop = combo.popup();
    r.click(cx(field), field.y + pop.top + 4 * combo.rowHeight - combo.popupScroll() + combo.rowHeight * 0.5); // "2 beats"
    assert(r.sent.back() == "set au_1.loop=2" && r.svc->model().automations[0].loop == 2.0);
    double lab = 0.0;
    for (int k = 0; k < 20 && lab == 0.0; ++k) { r.frame(); lab = combo.labelAmount(); }
    assert(lab > 0.0 && lab < 1.0);
    r.settle();
    assert(combo.shown() == "2 beats");
    pass("A looping automation, in the window (R-AUTO-12/13): Create Pump from a fader's menu is one line; the row's span drawn, its grips the resize cursor and one line each (the start keeping the end); a span set elsewhere eases; a click in a repeat adds the point in the cycle; the window's Loop combo is one line, its label cross-fading");
}

static void test_a_recovered_song_is_offered()
{
    // R-SAFE-2 in the window: a song opened with an autosave newer than it — asked, Recover or the saved one
    sltest::Rig r("ui-recover", 1280, 800);
    const std::string song = r.song("Kept") + ".slp", as = song + ".autosave";
    r.cmd("project new " + song + " --bpm 120 --name Kept");
    r.cmd("clip add --instrument drums --at 0 --length 4");
    r.cmd("project save");
    r.cmd("project close");
    r.settle();
    // a session that died with edits: the autosave beside the song, newer than it
    auto plant = [&](const std::string &name) {
        std::ifstream in(song);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto line = text.find("\nname ");                    // the header's `name = …`, whatever it says now
        assert(line != std::string::npos);
        const auto eq = text.find("= ", line), end = text.find('\n', line + 1);
        text.replace(eq + 2, end - eq - 2, name);
        std::ofstream(as) << text;
        std::filesystem::last_write_time(as, std::filesystem::last_write_time(song) + std::chrono::seconds(5));
    };
    plant("Rescued");
    r.cmd("project open " + song);
    r.settle();
    auto &cf = r.app->confirm();
    assert(cf.isOpen() && r.svc->model().projectName == "Kept");   // the saved one is open until it is answered
    assert(cf.activate(1));                                          // Recover
    r.settle();
    assert(sentLine(r, "project recover " + song) && r.svc->model().projectName == "Rescued" && r.svc->model().dirty);
    // the saved one instead: the autosave is dropped
    r.cmd("project save");
    r.cmd("project close");
    plant("Lost");
    r.cmd("project open " + song);
    r.settle();
    assert(cf.isOpen());
    assert(cf.activate(0));                                          // Open the Saved One
    r.settle();
    assert(sentLine(r, "project discard-recovery " + song) && !std::filesystem::exists(as) && r.svc->model().projectName == "Rescued");
    pass("A recovered song is offered (R-SAFE-2): opened with a newer autosave, the window asks — Recover is `project recover` (the kept edits, unsaved); Open the Saved One is `project discard-recovery`");
}

static void test_a_midi_file_dropped_on_a_track()
{
    // R-MIDI-1 in the window: a .mid in the browser, dragged onto an instrument's track, is ONE `pattern import`
    sltest::Rig r("ui-midi", 1280, 800);
    r.cmd("folder add " + r.dir);
    r.cmd("project new " + r.song("Midi") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");                  // ch_2's track ln_1, pt_1
    r.cmd("notes add pt_1 \"C4@0:1 E4@1:1 G4@2:2\"");
    r.cmd("pattern export pt_1 " + r.dir + "/groove.mid");                  // a real file where the browser lists it
    r.settle();
    auto &b = r.app->project().browser();
    auto &tl = r.app->project().timeline();
    r.click(world(b, b.tabRect(0)));
    r.settle();
    r.click(world(b, b.rowRect(0)));                                         // the folder
    r.settle();
    int mid = -1;
    for (int i = 0; i < b.rowCount(); ++i)
        if (b.row(i).label == "groove.mid") mid = i;
    assert(mid > 0 && b.row(mid).kind == "midi");
    const artboard::Rect from = world(b, b.rowRect(mid)), track = world(tl, tl.rowRect(0));
    const double toX = world(tl, artboard::Rect{tl.beatToX(4.0), 0, 0, 0}).x + 2.0;
    r.drag(cx(from), cy(from), toX, cy(track), 8, false);
    r.app->pointer(2, toX, cy(track), 0, r.now);
    r.settle();
    assert(sentLine(r, "pattern import " + r.dir + "/groove.mid --lane ln_1 --at 4"));
    const auto &m = r.svc->model();
    bool placed = false;
    for (const auto &c : m.clips)
        if (c.at == 4.0 && c.lane == "ln_1" && c.pattern != "pt_1")
            for (const auto &p : m.patterns)
                placed |= p.id == c.pattern && p.notes.size() == 3 && p.notes[2].pitch == 67 && p.notes[2].length == 2.0;
    assert(placed);
    pass("A .mid dropped on an instrument's track is ONE `pattern import --lane --at` — its notes, a clip there (R-MIDI-1)");
}

static void test_space_returns_and_ctrl_space_stays()
{
    // R-TIME-7/8: Space stops back where the time bar was put; Ctrl+Space stops where it is; the cue is marked
    sltest::Rig r("ui-cue", 1280, 800);
    r.cmd("project new " + r.song("Cue") + ".slp --bpm 120");
    r.cmd("clip add --instrument drums --at 0 --length 16");
    r.settle();
    auto &tl = r.app->project().timeline();
    const auto &t = r.svc->model().transport;
    // the time bar put at beat 6 by a click on the ruler
    const artboard::Rect ruler = world(tl, tl.rulerRect()), origin = world(tl, artboard::Rect{0, 0, 0, 0});
    r.click(origin.x + tl.beatToX(6.0), cy(ruler));                // beatToX is from the timeline's own left edge
    r.settle();
    assert(t.position == 6.0 && t.cue == 6.0 && tl.cueAmount() == 0.0);
    r.key(32);
    assert(r.sent.back() == "transport play" && t.playing);
    double first = 0.0;                                           // the cue's mark fades in with the play
    for (int k = 0; k < 40 && first == 0.0; ++k)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        r.frame();
        first = tl.cueAmount();
    }
    assert(first > 0.0 && first < 1.0 && tl.cueBeat() == 6.0);
    for (int k = 0; k < 30; ++k) { std::this_thread::sleep_for(std::chrono::milliseconds(16)); r.frame(); }
    assert(t.position > 6.0);
    r.key(32);                                                    // Space: back to 6
    assert(r.sent.back() == "transport stop --return" && !t.playing && t.position == 6.0);
    r.frame();
    r.frame();
    assert(tl.cueAmount() > 0.0 && tl.cueAmount() < 1.0);         // and fades out
    // Ctrl+Space: stop where it is — a temporary place: Space plays on from there, and its stop goes back to 6
    r.key(32);
    for (int k = 0; k < 30; ++k) { std::this_thread::sleep_for(std::chrono::milliseconds(16)); r.frame(); }
    r.key(32, true);
    assert(r.sent.back() == "transport stop" && !t.playing && t.position > 6.0 && t.cue == 6.0);
    const double here = t.position;
    r.key(32);
    assert(r.sent.back() == "transport play" && t.playing && t.position > here - 0.1 && t.cue == 6.0); // heard: a latency behind, at first
    for (int k = 0; k < 20; ++k) { std::this_thread::sleep_for(std::chrono::milliseconds(16)); r.frame(); }
    assert(tl.cueBeat() == 6.0);                                  // the mark still stands where the time bar was put
    r.key(32);
    assert(r.sent.back() == "transport stop --return" && t.position == 6.0);
    // the song bar's Stop returns too
    r.key(32, true);                                              // stopped: Ctrl+Space plays as Space does
    for (int k = 0; k < 20; ++k) { std::this_thread::sleep_for(std::chrono::milliseconds(16)); r.frame(); }
    auto &bar = r.app->project().bar();
    r.click(world(bar, bar.hitRect(arstro::solaris_ui::SongBar::kPlay)));
    assert(r.sent.back() == "transport stop --return" && t.position == 6.0);
    pass("Space stops back where the time bar was put, Ctrl+Space stops where it is — a temporary place: the next play starts there and its Space stop still goes back to where the time bar was put; the song bar's Stop returns; the cue is marked on the ruler while playing (R-TIME-7/8)");
}

static void test_the_tempo_is_set_in_the_song_bar()
{
    sltest::Rig r("ui-tempo", 1280, 800);
    r.cmd("project new " + r.song("Tempo") + ".slp --bpm 120");
    r.settle();
    auto &bar = r.app->project().bar();
    const artboard::Rect br = world(bar, bar.hitRect(arstro::solaris_ui::SongBar::kBpm));
    // dragged up ten steps: it is the pointer's, one line per step that moved it
    const size_t mark = r.sent.size();
    r.drag(cx(br), cy(br), cx(br), cy(br) - 10.0 * arstro::solaris_ui::SongBar::kBpmStepPx, 10, false);
    assert(bar.bpmShown() == 130.0);
    std::vector<std::string> lines(r.sent.begin() + (long)mark, r.sent.end());
    // the recognizer reports a drag past its slop, so the first line may cover two steps; after it, each step its own
    assert(lines.size() >= 9 && lines.back() == "set project.bpm=130");
    for (size_t i = 1; i < lines.size(); ++i)
        assert(lines[i] == "set project.bpm=" + std::to_string(130 - (int)(lines.size() - 1 - i)));
    r.app->pointer(2, cx(br), cy(br) - 10.0 * arstro::solaris_ui::SongBar::kBpmStepPx, 0, r.now);
    r.settle();
    assert(r.svc->model().bpm == 130.0 && bar.bpmShown() == 130.0);
    // Shift: a tenth a step
    r.drag(cx(br), cy(br), cx(br), cy(br) + 4.0 * arstro::solaris_ui::SongBar::kBpmStepPx, 4, true, true);
    r.settle();
    assert(r.svc->model().bpm == 129.6 && r.sent.back() == "set project.bpm=129.6");
    // changed from elsewhere: drawn easing there, caught mid-way, never a jump
    r.cmd("set project.bpm=66");
    r.frame();
    r.frame();
    const double mid = bar.bpmShown();
    assert(mid < 129.6 && mid > 66.0);
    r.settle();
    assert(bar.bpmShown() == 66.0);
    // a double-click types one in: ONE line
    r.click(cx(br), cy(br));
    r.click(cx(br), cy(br));
    r.settle();
    auto &menu = r.app->menu();
    assert(menu.isRenaming());
    artboard::KeyEvent typed;
    typed.type = artboard::KeyEvent::Type::Text;
    typed.text = "72";
    r.app->key(typed);
    r.key(13);
    r.settle();
    assert(r.svc->model().bpm == 72.0);
    pass("The tempo is set in the song bar (R-UI-13): dragged, a bpm a step and one `set project.bpm` per step (Shift: a tenth); changed elsewhere it eases there; a double-click types one in");
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
    // Song › Quantize Clip: the selected note clip's pattern, at the lanes' snap step — one line (R-UI-3)
    r.app->project().timeline().selectClip("ac_1");
    r.settle();
    r.click(world(ms, ms.titleRect(2)));
    r.settle();
    assert(ms.menu(2).items[4].label == "Quantize Clip");
    r.click(world(ms, ms.itemRect(2, 4)));
    r.settle();
    {
        char grid[32];
        std::snprintf(grid, sizeof grid, "%.10g", std::min(4.0, std::max(1.0 / 32.0, r.app->project().timeline().snapStep())));
        assert(sentLine(r, std::string("pattern quantize pt_1 --grid ") + grid) && r.svc->model().lastError.empty());
    }
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
    // the settings sheet's pages: a combo's row, a toggle — each a `settings set` line
    r.app->openSettings();
    r.settle();
    using arstro::solaris_ui::SettingsSheet;
    auto &sheet = r.app->settings();
    r.click(sheet.railRect(SettingsSheet::kNewSongs));
    r.settle();
    r.click(sheet.controlRect(SettingsSheet::kNewBpm));
    r.settle();
    r.click(sheet.optionRect(SettingsSheet::kNewBpm, 4)); // 128
    r.settle();
    assert(sentLine(r, "settings set newBpm=128") && r.svc->model().settings.newBpm == 128.0);
    r.click(sheet.railRect(SettingsSheet::kInterface));
    r.settle();
    r.click(sheet.controlRect(SettingsSheet::kMotion));
    r.settle();
    assert(sentLine(r, "settings set reducedMotion=on") && artboard::reducedMotion()); // the setting reaches every primitive
    r.cmd("settings set reducedMotion=off");
    r.settle();
    assert(!artboard::reducedMotion());
    // R-UI-3 (amended): the wordmark IS Home — no Home button; a click on it asks about unsaved changes, then goes Home
    r.key(27); // the settings sheet above is still open: close it
    r.settle();
    {
        const artboard::Rect wm = world(bar, bar.hitRect(SongBar::kHome));
        assert(wm.x < 20.0 && wm.right() < bar.hitRect(SongBar::kSettings).x + 1.0);
        r.cmd("set project.bpm=121"); // an unsaved edit
        r.settle();
        r.click(cx(wm), cy(wm));
        r.settle();
        assert(r.app->confirm().isOpen() && r.app->screen() == "project"); // the song is unsaved: asked first
        r.key(27);
        r.settle();
        r.cmd("project save");
        r.settle();
        r.click(cx(wm), cy(wm));
        r.settle();
        assert(sentLine(r, "project close") && r.app->screen() == "home");
    }
    pass("Song bar: the wordmark is Home (no Home button); Settings beside it; File/Edit/Song/View are command lines (Edit names its undo); outside press closes; View folds the dock and browser, eased; new settings rows are lines");
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
    // while held the handle is drawn under the pointer (its beat on the grid's step, R-UI-10), not easing after it
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

static void test_bezier_handles_and_the_automation_window()
{
    sltest::Rig r("ui-bezier", 1280, 800);
    r.cmd("project new " + r.song("Bezier") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 8");    // ch_2
    r.cmd("auto create ch_2.pan");                             // au_1: −1 … 1, flat at 0 from beat 0 to 16
    r.cmd("auto point add au_1 --at 4 --value 0.5");
    r.cmd("auto point add au_1 --at 8 --value -0.5");
    r.settle();
    auto &tl = r.app->project().timeline();
    const artboard::Rect tw = world(tl, artboard::Rect{0, 0, 0, 0});
    auto lines = [&](const std::string &prefix) {
        int n = 0;
        for (const auto &l : r.sent) n += l.rfind(prefix, 0) == 0;
        return n;
    };
    auto near = [](artboard::Point a, double x, double y) { return std::fabs(a.x - x) < 0.5 && std::fabs(a.y - y) < 0.5; };
    // ── Alt-drag a point pulls out SYMMETRIC handles; the one under the pointer follows it exactly ──
    const artboard::Point p1 = tl.autoPointAt("au_1", 1);
    const double hx = p1.x + 40.0, hy = p1.y - 5.0; // inside the row's range (a pan's −1 … 1 spans ~36 px)
    const size_t before = r.sent.size();
    r.app->pointer(1, tw.x + p1.x, tw.y + p1.y, 0, r.now, true);
    r.app->pointer(0, tw.x + p1.x, tw.y + p1.y, 0, r.now, true);
    r.frame();
    for (int k = 1; k <= 6; ++k)
    {
        r.app->pointer(1, tw.x + p1.x + (hx - p1.x) * k / 6.0, tw.y + p1.y + (hy - p1.y) * k / 6.0, 0, r.now, true);
        r.frame();
    }
    assert(near(tl.autoHandleAt("au_1", 1, 2), hx, hy));                 // under the pointer, not easing after it
    assert(near(tl.autoHandleAt("au_1", 1, 1), 2.0 * p1.x - hx, 2.0 * p1.y - hy)); // the in-handle its mirror
    assert(near(tl.autoPointAt("au_1", 1), p1.x, p1.y));                 // the point itself stays
    assert(r.sent.size() == before);                                      // nothing sent while held
    r.app->pointer(2, tw.x + hx, tw.y + hy, 0, r.now, true);
    r.frame();
    assert(r.sent.size() == before + 1 && lines("auto point shape au_1 --at 4 --shape bezier --speed-in ") == 1); // ONE line
    assert(near(tl.autoHandleAt("au_1", 1, 2), hx, hy) && tl.autoEase("au_1") == 1.0); // where it was let go: nothing jumps
    r.settle();
    assert(near(tl.autoHandleAt("au_1", 1, 2), hx, hy) && tl.autoEase("au_1") == 1.0);
    {
        const auto &pt = r.svc->model().automations[0].points[1];
        assert(pt.shape == "bezier" && pt.speedIn == pt.speedOut && pt.speedOut > 0.0); // pulled up and right: rising through the point
    }
    // ── a handle dragged: the opposite one MIRRORS ──
    const double h2x = hx + 14.0, h2y = hy + 10.0;
    r.drag(tw.x + hx, tw.y + hy, tw.x + h2x, tw.y + h2y, 6, false);
    assert(near(tl.autoHandleAt("au_1", 1, 2), h2x, h2y) && near(tl.autoHandleAt("au_1", 1, 1), 2.0 * p1.x - h2x, 2.0 * p1.y - h2y));
    r.app->pointer(2, tw.x + h2x, tw.y + h2y, 0, r.now);
    r.settle();
    assert(lines("auto point shape au_1 --at 4 --shape bezier") == 2 && near(tl.autoHandleAt("au_1", 1, 2), h2x, h2y));
    // ── Alt on a handle BREAKS the symmetry: only the in-handle moves ──
    const artboard::Point in0 = tl.autoHandleAt("au_1", 1, 1);
    const double i2x = in0.x - 10.0, i2y = in0.y + 12.0;
    r.app->pointer(1, tw.x + in0.x, tw.y + in0.y, 0, r.now, true);
    r.app->pointer(0, tw.x + in0.x, tw.y + in0.y, 0, r.now, true);
    r.frame();
    for (int k = 1; k <= 6; ++k)
    {
        r.app->pointer(1, tw.x + in0.x + (i2x - in0.x) * k / 6.0, tw.y + in0.y + (i2y - in0.y) * k / 6.0, 0, r.now, true);
        r.frame();
    }
    assert(near(tl.autoHandleAt("au_1", 1, 1), i2x, i2y) && near(tl.autoHandleAt("au_1", 1, 2), h2x, h2y));
    r.app->pointer(2, tw.x + i2x, tw.y + i2y, 0, r.now, true);
    r.settle();
    {
        const auto &pt = r.svc->model().automations[0].points[1];
        assert(lines("auto point shape au_1 --at 4 --shape bezier") == 3 && pt.speedIn != pt.speedOut); // a cusp
    }
    assert(near(tl.autoHandleAt("au_1", 1, 1), i2x, i2y));
    // ── a handle a shell moves EASES there ──
    const artboard::Point o0 = tl.autoHandleAt("au_1", 1, 2);
    r.cmd("auto point shape au_1 --at 4 --speed-out 0 --influence-out 80");
    r.frame();
    r.frame();
    const artboard::Point oMid = tl.autoHandleAt("au_1", 1, 2);
    assert(tl.autoEase("au_1") > 0.0 && tl.autoEase("au_1") < 1.0 && std::fabs(oMid.x - o0.x) > 0.5);
    r.settle();
    const artboard::Point o1 = tl.autoHandleAt("au_1", 1, 2);
    assert(std::fabs(o1.y - p1.y) < 0.5 && std::fabs(o1.x - (p1.x + 0.8 * 4.0 * tl.pxPerBeat())) < 0.5 && std::fabs(oMid.x - o1.x) > 0.5);

    // ── double-click the row's HEADER: the automation's window, fading in ──
    auto &wl = r.app->project().windows();
    const artboard::Rect row = world(tl, tl.autoRowRect("au_1"));
    r.click(row.x + 30.0, cy(row));
    r.click(row.x + 30.0, cy(row));
    r.frame();
    r.frame();
    assert(wl.isOpen("auto:au_1") && wl.window("auto:au_1")->appearAmount() > 0.0 && wl.window("auto:au_1")->appearAmount() < 1.0);
    r.settle();
    auto &w = *wl.window("auto:au_1");
    auto &panel = *wl.automationPanel("au_1");
    const auto &am = r.svc->model().automations[0];
    // the facts, all the model's: name, id, range, made from, its points, what reads it (with the formula), now
    const std::string facts = panel.text();
    assert(contains(facts, am.name) && contains(facts, "au_1") && contains(facts, "Made from  ch_2.pan") && contains(facts, "POINTS \xC2\xB7 4"));
    assert(contains(facts, "beat 4  0.50  bezier") && contains(facts, "beat 8  -0.50  linear") && contains(facts, "ch_2.pan  =au_1"));
    assert(w.title() == "Automation \xE2\x80\x94 " + am.name && panel.nowTarget() == am.now);
    // the value at the playhead is the model's, and EASES to a seek
    r.cmd("transport seek 6");
    r.frame();
    r.frame();
    const double nowWant = r.svc->model().automations[0].now;
    assert(panel.nowTarget() == nowWant && std::fabs(panel.nowLive() - nowWant) > 1e-6);
    r.settle();
    assert(panel.nowLive() == nowWant && nowWant != 0.0);
    // its rows update eased: a point added from a shell fades its row in
    r.cmd("auto point add au_1 --at 12 --value 0.25");
    r.frame();
    r.frame();
    std::string key;
    for (const auto &k : panel.rowKeys())
        if (k.rfind("pt:12|", 0) == 0) key = k;
    assert(!key.empty() && panel.rowAlpha(key) > 0.0 && panel.rowAlpha(key) < 1.0);
    r.settle();
    assert(panel.rowAlpha(key) == 1.0);
    // Rename: cosmo's field; what is typed is ONE line, and the window and the row follow
    r.click(world(panel, panel.renameRect()));
    r.settle();
    auto &menu = r.app->menu();
    assert(menu.isRenaming());
    artboard::KeyEvent typed;
    typed.type = artboard::KeyEvent::Type::Text;
    typed.text = "Wobble";
    r.app->key(typed);
    r.key(13);
    r.frame();
    r.frame();
    assert(sentLine(r, "set au_1.name=Wobble") && r.svc->model().automations[0].name == "Wobble");
    assert(w.title() == "Automation \xE2\x80\x94 Wobble" && panel.rowAlpha("name:Wobble") > 0.0 && panel.rowAlpha("name:Wobble") < 1.0);
    r.settle();
    // × closes it; a double-click on the CURVE away from a point opens it again — and adds no point
    r.click(world(w, w.closeRect()));
    r.settle();
    assert(!wl.isOpen("auto:au_1"));
    const int adds = lines("auto point add");
    const double bx = tw.x + tl.beatToX(10.0), by = tw.y + tl.autoRowRect("au_1").y + 8.0;
    r.click(bx, by);
    r.frame();
    assert(tl.autoPendingAmount() > 0.0 && lines("auto point add") == adds);   // a ghost at once; the line waits
    r.click(bx, by);
    r.settle();
    assert(wl.isOpen("auto:au_1") && lines("auto point add") == adds && r.svc->model().automations[0].points.size() == 5);
    assert(tl.autoPendingAmount() == 0.0);
    // a single click still adds — once the double-click has had its chance
    r.click(world(w, w.closeRect()));
    r.settle();
    r.click(bx, by);
    r.pump(160.0);
    assert(lines("auto point add") == adds);
    r.settle();
    assert(lines("auto point add au_1 --at 10 --value ") == 1 && r.svc->model().automations[0].points.size() == 6);
    // a double-click ON a point still deletes it
    const artboard::Point p3 = tl.autoPointAt("au_1", 3);
    r.click(tw.x + p3.x, tw.y + p3.y);
    r.click(tw.x + p3.x, tw.y + p3.y);
    r.settle();
    assert(sentLine(r, "auto point delete au_1 --at 10") && r.svc->model().automations[0].points.size() == 5 && !wl.isOpen("auto:au_1"));
    pass("Bezier and the automation's window: Alt-drag pulls symmetric handles, a handle mirrors, Alt breaks it — under the pointer while held, ONE line, "
         "nothing jumps; a shell's handle eases; header double-click opens the window (fading) with the model's facts, now eased, rows eased, Rename one line; "
         "a curve double-click opens it and adds nothing; a point double-click still deletes (R-AUTO-10, R-AUTO-11)");
}

static void test_the_rolls_lane_draws_the_wheels()
{
    // R-MIDI-3: Vel | Bend | Mod beside the roll's lane (the view's, cross-faded); on a wheel's lane a click adds a
    // point, a point dragged up or down lands its value, a double-click removes it — each ONE line; a shell's change eases
    sltest::Rig r("ui-lanes", 1280, 800);
    r.cmd("project new " + r.song("Lanes") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");
    const std::string pt = r.svc->model().clips[0].pattern;
    r.cmd("note add " + pt + " --pitch 60 --at 0 --length 4");
    r.settle();
    auto &wl = r.app->project().windows();
    wl.openRoll(pt);
    r.settle();
    auto &roll = *wl.roll(pt);
    using PR = arstro::solaris_ui::PianoRoll;
    assert(roll.lane() == PR::LaneVelocity && roll.laneAmount(0) == 1.0);
    r.click(world(roll, roll.laneTabRect(PR::LaneBend)));
    r.frame();
    r.frame();
    assert(roll.lane() == PR::LaneBend && roll.laneAmount(1) > 0.0 && roll.laneAmount(1) < 1.0 && roll.laneAmount(0) > 0.0); // cross-fading
    r.settle();
    assert(roll.laneAmount(1) == 1.0 && roll.laneAmount(0) == 0.0);
    const artboard::Rect rw = world(roll, artboard::Rect{0, 0, 0, 0}), vel = world(roll, roll.velRect());
    const double top = vel.y + 6.0, h = PR::kVelH - 12.0;
    auto yOf = [&](double v) { return top + (1.0 - (v + 1.0) * 0.5) * h; }; // the bend lane: −1 at the bottom, +1 at the top
    // a click: a point at the snapped beat and the height's value
    r.click(rw.x + roll.beatToX(1.0), yOf(0.5));
    assert(sentLine(r, "expr add " + pt + " --lane bend --at 1 --value 0.5"));
    r.settle();
    assert(roll.exprLive("bend", 1.0) == 0.5);
    // dragged down: the pointer's own value while held, ONE line at release
    const artboard::Rect p = world(roll, roll.exprPointRect("bend", 1.0));
    r.drag(cx(p), cy(p), cx(p), yOf(-0.5), 6, false);
    assert(std::fabs(roll.exprLive("bend", 1.0) + 0.5) < 0.011);
    r.app->pointer(2, cx(p), yOf(-0.5), 0, r.now);
    r.settle();
    assert(sentLine(r, "expr add " + pt + " --lane bend --at 1 --value -0.5"));
    // a shell's change eases there
    r.cmd("expr add " + pt + " --lane bend --at 1 --value 0");
    r.frame();
    r.frame();
    const double mid = roll.exprLive("bend", 1.0);
    assert(mid > -0.5 && mid < 0.0);
    r.settle();
    assert(roll.exprLive("bend", 1.0) == 0.0);
    // double-clicked: gone
    const artboard::Rect q = world(roll, roll.exprPointRect("bend", 1.0));
    r.click(cx(q), cy(q));
    r.click(cx(q), cy(q));
    r.settle();
    assert(sentLine(r, "expr delete " + pt + " --lane bend --at 1"));
    // the mod wheel's lane: 0 at the bottom, 1 at the top
    r.click(world(roll, roll.laneTabRect(PR::LaneMod)));
    r.settle();
    r.click(rw.x + roll.beatToX(2.0), top + 0.25 * h);
    assert(sentLine(r, "expr add " + pt + " --lane cc1 --at 2 --value 0.75"));
    pass("the roll's lane (R-MIDI-3): Vel | Bend | Mod cross-fade; a click adds a point, a point dragged up or down lands its value, a double-click removes it — each one line; a shell's change eases");
}

static void test_the_rolls_note_tools()
{
    // R-ROLL-11: Scale ▾ locks the pattern (the rows off it dim, eased; Change key… the key), Chord ▾ makes a click stamp a
    // chord, Quantize… offers the grooves, Tools ▾ acts on the selection (else the pattern), the Chance lane's stem
    // dragged lands ONE `note move --chance`; a velocity changed elsewhere eases in
    sltest::Rig r("ui-tools", 1280, 800);
    r.cmd("project new " + r.song("Tools") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");
    const std::string pt = r.svc->model().clips[0].pattern;
    r.cmd("notes add " + pt + " \"C4@0:1 E4@1:1 G4@2:1\"");
    r.settle();
    auto &wl = r.app->project().windows();
    wl.openRoll(pt);
    r.settle();
    auto &roll = *wl.roll(pt);
    using PR = arstro::solaris_ui::PianoRoll;
    auto &menu = r.app->menu();
    auto pick = [&](const std::string &label) {
        for (int i = 0; i < menu.itemCount(); ++i)
            if (menu.item(i).label == label) { r.click(menu.itemRect(i)); return true; }
        std::printf("    no menu item `%s` among %d:", label.c_str(), menu.itemCount());
        for (int i = 0; i < menu.itemCount(); ++i) std::printf(" [%s]", menu.item(i).label.c_str());
        std::printf("\n");
        return false;
    };
    // Scale ▾: No scale, the scales in C (none chosen yet), Change key… — the rows off it dim, eased
    assert(roll.rowOut(61) == 0.0);
    r.click(world(roll, roll.scaleRect()));
    r.settle();
    assert(menu.isOpen() && menu.itemCount() == 2 + (int)r.svc->model().scales.size());
    assert(pick("C minor") && sentLine(r, "pattern scale " + pt + " C minor"));
    r.frame();
    r.frame();
    const double mid = roll.rowOut(64); // E: off C minor, mid-fade
    assert(mid > 0.0 && mid < 1.0 && roll.rowOut(63) == 0.0);
    r.settle();
    assert(roll.rowOut(64) == 1.0 && roll.rowOut(61) == 1.0 && roll.rowOut(60) == 0.0);
    r.click(world(roll, roll.scaleRect()));
    r.settle();
    assert(pick("Change key\xE2\x80\xA6"));
    r.settle();
    assert(menu.isOpen() && menu.itemCount() == 12);
    assert(pick("D minor") && sentLine(r, "pattern scale " + pt + " D minor"));
    r.settle();
    assert(roll.rowOut(64) == 0.0 && roll.rowOut(60) == 0.0 && roll.rowOut(63) == 1.0); // D E F G A A# C
    // Chord ▾ major: a click on E4 at beat 3 stamps it — in D minor, E's own triad (E G A#)
    r.click(world(roll, roll.chordRect()));
    r.settle();
    assert(pick("maj \xC2\xB7 major") && roll.chordOn() && roll.chord().empty());
    const artboard::Rect rw = world(roll, artboard::Rect{0, 0, 0, 0});
    r.click(rw.x + roll.beatToX(3.0) + 3.0, rw.y + roll.pitchToY(64) + PR::kNoteH * 0.5);
    assert(sentLine(r, "note add " + pt + " --chord E --octave 4 --at 3 --length 0.25"));
    r.settle();
    int at3 = 0;
    for (const auto &n : r.svc->model().patterns[0].notes)
        if (n.at == 3.0) at3 += n.pitch == 64 || n.pitch == 67 || n.pitch == 70;
    assert(at3 == 3);
    r.click(world(roll, roll.chordRect()));
    r.settle();
    assert(pick("Single notes") && !roll.chordOn());
    // a velocity set by a shell eases there
    r.cmd("notes set " + pt + " \"60@0\" --vel 40");
    r.frame();
    r.frame();
    const double v = roll.noteVel(60, 0.0);
    assert(v > 40.0 && v < 100.0);
    r.settle();
    assert(roll.noteVel(60, 0.0) == 40.0);
    // the Chance lane: C4's stem dragged half way down — ONE line at release, where the pointer left it
    r.click(world(roll, roll.laneTabRect(PR::LaneChance)));
    r.settle();
    assert(roll.laneAmount(PR::LaneChance) == 1.0 && roll.laneAmount(PR::LaneVelocity) == 0.0);
    const artboard::Rect vel = world(roll, roll.velRect());
    const double bottom = vel.bottom() - 6.0, h = PR::kVelH - 12.0, x = rw.x + roll.beatToX(0.0) + 1.5;
    r.drag(x, bottom - h, x, bottom - 0.5 * h, 6);
    r.settle();
    assert(sentLine(r, "note move " + pt + " --pitch 60 --at 0 --chance 0.5") && roll.noteChance(60, 0.0) == 0.5);
    assert(r.svc->model().patterns[0].notes[0].chance == 0.5);
    // Quantize… → a groove, on the whole pattern (nothing selected)
    r.click(world(roll, roll.quantizeRect()));
    r.settle();
    assert(pick("Groove: hip-hop") && sentLine(r, "pattern quantize " + pt + " --groove hip-hop"));
    r.settle();
    // a note selected: Tools ▾ acts on it alone
    const artboard::Rect c4 = world(roll, roll.noteRect(60, 0.0));
    r.click(cx(c4), cy(c4));
    r.settle();
    assert(roll.selectionCount() == 1);
    r.click(world(roll, roll.toolsRect()));
    r.settle();
    assert(pick("Chance 100%") && sentLine(r, "notes set " + pt + " \"60@0\" --chance 1"));
    r.frame();
    r.frame();
    const double ch = roll.noteChance(60, 0.0);
    assert(ch > 0.5 && ch < 1.0); // the chance travels too
    r.settle();
    r.click(world(roll, roll.toolsRect()));
    r.settle();
    assert(pick("Legato") && sentLine(r, "pattern legato " + pt + " --notes \"60@0\""));
    r.settle();
    assert(r.svc->model().patterns[0].notes[0].length == 4.0); // alone: to the pattern's end
    r.click(world(roll, roll.toolsRect()));
    r.settle();
    assert(pick("Arpeggiate up-down") && sentLine(r, "pattern arpeggiate " + pt + " --rate 0.25 --mode updown --notes \"60@0\""));
    pass("the roll's note tools (R-ROLL-11): Scale ▾ and Change key… lock the pattern, the rows off it dim eased; Chord ▾ stamps the key's own triad; a groove from Quantize…; the Chance lane's drag one line; Tools ▾ on the selection; velocity and chance changed elsewhere ease in");
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
    assert(menu.isOpen() && menu.itemCount() == 3 + (int)r.svc->model().grooves.size()); // R-ROLL-11: then the grooves
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

static void test_the_roll_plays_its_clip()
{
    sltest::Rig r("ui-rollplay", 1280, 800);
    r.cmd("project new " + r.song("RollPlay") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");
    const std::string ac1 = r.svc->model().clips[0].id, pt = r.svc->model().clips[0].pattern;
    r.cmd("note add " + pt + " --pitch 60 --at 0 --length 1");
    r.cmd("clip duplicate " + ac1);                                   // a linked copy at beat 4
    const std::string ac2 = r.svc->model().clips[1].id;
    r.cmd("transport loop 0 2");                                      // the song's own loop
    r.settle();
    auto &tl = r.app->project().timeline();
    auto &wl = r.app->project().windows();
    // opened from the second clip: that clip is the roll's
    const artboard::Rect c2 = world(tl, tl.clipRect(ac2));
    r.click(c2.x + 12.0, cy(c2));
    r.click(c2.x + 12.0, cy(c2));
    r.settle();
    auto &roll = *wl.roll(pt);
    assert(roll.clip() == ac2 && roll.playheadAlpha() == 0.0);
    // its ▶: ONE line, the clip looped in the song
    r.click(world(roll, roll.playRect()));
    assert(r.sent.back() == "transport play --clip " + ac2);
    const auto &t = r.svc->model().transport;
    assert(t.playing && t.loopClip == ac2 && t.loopFrom == 4.0 && t.loopTo == 8.0);
    // the playhead fades in where the clip is heard — caught mid-fade, never a snap
    double first = 0.0;
    for (int k = 0; k < 40 && first == 0.0; ++k)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        r.frame();
        first = roll.playheadAlpha();
    }
    assert(first > 0.0 && first < 1.0 && roll.playheadBeat() >= 0.0 && roll.playheadBeat() < 4.0);
    assert(roll.looping());
    // Space stops whatever plays — back where the time bar was put (R-TIME-7: here the clip's start); the song's loop comes back
    r.key(32);
    assert(r.sent.back() == "transport stop --return" && t.loopClip.empty() && t.loopFrom == 0.0 && t.loopTo == 2.0);
    assert(t.position == 4.0);
    r.settle();
    assert(roll.playheadAlpha() == 0.0 && !roll.looping());
    // stopped, with the roll in front, Space is the roll's ▶
    r.key(32);
    assert(r.sent.back() == "transport play --clip " + ac2);
    r.key(32);
    assert(r.sent.back() == "transport stop --return");
    // a pattern on no track: nothing to sound through — its ▶ says nothing; Space plays the song
    r.cmd("pattern new --name Loose");
    const std::string pt2 = r.svc->model().patterns.back().id;
    r.frame();                                                        // the app has it, as the MIDI list would
    wl.openRoll(pt2);
    r.settle();
    auto &loose = *wl.roll(pt2);
    assert(loose.clip().empty() && wl.frontRoll() == &loose);
    const size_t before = r.sent.size();
    r.click(world(loose, loose.playRect()));
    assert(r.sent.size() == before);
    r.key(32);
    assert(r.sent.back() == "transport play");
    r.key(32);
    pass("The roll plays its clip (R-ROLL-6): ▶ is ONE `transport play --clip` for the clip it was opened from, looped in the song; the playhead fades in where it is heard; Space stops whatever plays and, stopped, is the front roll's ▶; the song's loop comes back; a pattern on no track has nothing to play");
}

static void test_the_roll_sounds_what_is_made()
{
    sltest::Rig r("ui-rollsound", 1280, 800);
    r.cmd("project new " + r.song("RollSound") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");
    const std::string ac = r.svc->model().clips[0].id, pt = r.svc->model().clips[0].pattern, ch = r.svc->model().clips[0].track;
    r.cmd("note add " + pt + " --pitch 60 --at 0 --length 1 --vel 80");
    r.settle();
    auto &tl = r.app->project().timeline();
    auto &wl = r.app->project().windows();
    const artboard::Rect clip = world(tl, tl.clipRect(ac));
    r.click(clip.x + 12.0, cy(clip));
    r.click(clip.x + 12.0, cy(clip));
    r.settle();
    auto &roll = *wl.roll(pt);
    const artboard::Rect rw = world(roll, artboard::Rect{0, 0, 0, 0});
    auto at = [&](double beat, int pitch) { return artboard::Point{rw.x + roll.beatToX(beat), rw.y + roll.pitchToY(pitch) + roll.kNoteH * 0.5}; };
    auto previews = [&](size_t from) {
        std::vector<std::string> v;
        for (size_t i = from; i < r.sent.size(); ++i)
            if (r.sent[i].rfind("note preview ", 0) == 0) v.push_back(r.sent[i]);
        return v;
    };
    const int depth = r.svc->model().undoDepth;
    // a note made is heard through the clip's track: the edit's line, then the hearing's
    size_t mark = r.sent.size();
    const artboard::Point c = at(2.1, 64);
    r.click(c.x, c.y);
    r.settle();
    assert(r.sent[mark] == "note add " + pt + " --pitch 64 --at 2 --length 0.25");
    assert(previews(mark) == std::vector<std::string>{"note preview " + ch + " 64 --vel 100 --length 0.25"});
    assert(r.svc->model().undoDepth == depth + 1);                    // the hearing is not in undo
    // a note pressed is heard; dragged up two rows, each row it enters
    mark = r.sent.size();
    const artboard::Point n60 = at(0.3, 60), n62 = at(0.3, 62);
    r.drag(n60.x, n60.y, n62.x, n62.y, 8);
    r.settle();
    assert((previews(mark) == std::vector<std::string>{"note preview " + ch + " 60 --vel 80 --length 1", "note preview " + ch + " 61 --vel 80 --length 1",
                                                        "note preview " + ch + " 62 --vel 80 --length 1"}));
    // a key pressed sounds and is lit while held — eased in, eased out; swept, each key it enters
    mark = r.sent.size();
    const artboard::Rect k72 = world(roll, roll.keyRect(72)), k74 = world(roll, roll.keyRect(74));
    r.drag(cx(k72), cy(k72), cx(k74), cy(k74), 4, false);
    r.frame();
    const double lit = roll.keyLit(74);
    assert(lit > 0.0 && lit < 1.0);
    assert((previews(mark) == std::vector<std::string>{"note preview " + ch + " 72 --vel 100 --length 0.5", "note preview " + ch + " 73 --vel 100 --length 0.5",
                                                        "note preview " + ch + " 74 --vel 100 --length 0.5"}));
    r.settle();
    assert(roll.keyLit(74) == 1.0);
    r.app->pointer(2, cx(k74), cy(k74), 0, r.now);
    r.frame();
    r.frame();
    assert(roll.keyLit(74) > 0.0 && roll.keyLit(74) < 1.0);
    r.settle();
    assert(roll.keyLit(74) == 0.0 && r.svc->model().undoDepth == depth + 2); // keys edit nothing
    // a pattern on no track has nothing to sound through: its note is made, not heard
    r.cmd("pattern new --name Loose");
    const std::string pt2 = r.svc->model().patterns.back().id;
    r.frame();
    wl.openRoll(pt2);
    r.settle();
    auto &loose = *wl.roll(pt2);
    const artboard::Rect lw = world(loose, artboard::Rect{0, 0, 0, 0});
    mark = r.sent.size();
    r.click(lw.x + loose.beatToX(1.1), lw.y + loose.pitchToY(60) + loose.kNoteH * 0.5);
    r.settle();
    assert(r.sent.size() == mark + 1 && r.sent[mark].rfind("note add " + pt2, 0) == 0);
    pass("The roll sounds what is made (R-ROLL-7): a note added, a note pressed, each row a drag enters and each key pressed or swept is a `note preview` through the clip's track, never in undo; the key is lit while held, eased; a pattern on no track is made, not heard");
}

static void test_the_rolls_selection_and_what_is_done_to_it()
{
    sltest::Rig r("ui-rollsel", 1280, 800);
    r.cmd("project new " + r.song("Sel") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 8");
    const std::string ac = r.svc->model().clips[0].id, pt = r.svc->model().clips[0].pattern;
    r.cmd("set " + pt + ".length=8");
    r.cmd("notes add " + pt + " \"C4@0:2 E4@0.25:1.75 G4@0.5 C5@0.75 E5@1\"");
    r.settle();
    auto &tl = r.app->project().timeline();
    auto &wl = r.app->project().windows();
    const artboard::Rect clip = world(tl, tl.clipRect(ac));
    r.click(clip.x + 12.0, cy(clip));
    r.click(clip.x + 12.0, cy(clip));
    r.settle();
    auto &roll = *wl.roll(pt);
    const artboard::Rect rw = world(roll, artboard::Rect{0, 0, 0, 0});
    auto noteAt = [&](int pitch, double at) { return world(roll, roll.noteRect(pitch, at)); };
    auto press = [&](double x, double y, bool shift, bool ctrl) {
        r.app->pointer(0, x, y, 0, r.now, false, shift, ctrl);
        r.app->pointer(2, x, y, 0, r.now, false, shift, ctrl);
        r.frame();
    };
    auto keyMod = [&](int code, bool shift, bool ctrl) {
        artboard::KeyEvent e;
        e.type = artboard::KeyEvent::Type::Down;
        e.keyCode = code;
        e.shift = shift;
        e.ctrl = ctrl;
        r.app->key(e);
        r.frame();
    };
    auto lastEdit = [&]() {
        for (auto it = r.sent.rbegin(); it != r.sent.rend(); ++it)
            if (it->rfind("note preview ", 0) != 0) return *it;
        return std::string();
    };
    auto has = [&](const std::string &p, int pitch, double at) {
        for (const auto &x : r.svc->model().patterns)
            if (x.id == p)
                for (const auto &n : x.notes)
                    if (n.pitch == pitch && std::fabs(n.at - at) < 1e-9) return true;
        return false;
    };
    // a click selects — lit, eased in; Shift+click adds
    artboard::Rect g4 = noteAt(67, 0.5);
    press(cx(g4), cy(g4), false, false);
    r.frame();
    assert(roll.selectionCount() == 1 && roll.selected(67, 0.5));
    const double lit = roll.selectAmount(67, 0.5);
    assert(lit > 0.0 && lit < 1.0);
    press(cx(noteAt(72, 0.75)), cy(noteAt(72, 0.75)), true, false);
    press(cx(noteAt(76, 1.0)), cy(noteAt(76, 1.0)), true, false);
    r.settle();
    assert(roll.selectionCount() == 3 && roll.selectAmount(67, 0.5) == 1.0);
    // a selected note dragged carries the selection with the pointer: ONE `notes move` when let go
    const size_t mark = r.sent.size();
    g4 = noteAt(67, 0.5);
    const double dx = roll.beatToX(1.5) - roll.beatToX(0.5), dy = roll.pitchToY(69) - roll.pitchToY(67);
    r.drag(cx(g4), cy(g4), cx(g4) + dx, cy(g4) + dy, 8, false);
    for (size_t i = mark; i < r.sent.size(); ++i) assert(r.sent[i].rfind("note preview ", 0) == 0); // heard (R-ROLL-7), no edit until it lands
    r.app->pointer(2, cx(g4) + dx, cy(g4) + dy, 0, r.now);
    r.frame();
    assert(lastEdit() == "notes move " + pt + " \"67@0.5 72@0.75 76@1\" --by 1 --semi 2");
    assert(has(pt, 69, 1.5) && has(pt, 74, 1.75) && has(pt, 78, 2) && roll.selected(69, 1.5) && roll.selectionCount() == 3);
    r.settle();
    // the arrows: ↑ a semitone, Shift+↓ an octave, ← a snap step — each ONE line
    keyMod(38, false, false);
    assert(lastEdit() == "notes move " + pt + " \"69@1.5 74@1.75 78@2\" --by 0 --semi 1");
    keyMod(40, true, false);
    assert(lastEdit() == "notes move " + pt + " \"70@1.5 75@1.75 79@2\" --by 0 --semi -12");
    keyMod(37, false, false);
    assert(lastEdit() == "notes move " + pt + " \"58@1.5 63@1.75 67@2\" --by -0.25 --semi 0");
    assert(has(pt, 58, 1.25) && has(pt, 63, 1.5) && has(pt, 55, 1.75) == false && has(pt, 67, 1.75));
    // Ctrl+D: a copy right after it — its span, 1.25 to 2.0, is 0.75 (a whole number of sixteenths) — the copy selected
    keyMod('D', false, true);
    assert(lastEdit() == "notes add " + pt + " \"58@2:0.25:100 63@2.25:0.25:100 67@2.5:0.25:100\"");
    assert(has(pt, 58, 2) && roll.selected(67, 2.5) && !roll.selected(67, 1.75) && roll.selectionCount() == 3);
    // Ctrl+C here, Ctrl+V in another roll: at the same beats, selected there
    keyMod('C', false, true);
    r.cmd("pattern new --name Other --length 4");
    const std::string pt2 = r.svc->model().patterns.back().id;
    r.frame();
    wl.openRoll(pt2);
    r.settle();
    auto &other = *wl.roll(pt2);
    const artboard::Rect ow = world(other, other.gridRect());
    press(ow.x + 30.0, ow.y + 30.0, false, true);                     // a Ctrl-press on its empty grid: the keys are its
    keyMod('V', false, true);
    assert(lastEdit() == "notes add " + pt2 + " \"58@2:0.25:100 63@2.25:0.25:100 67@2.5:0.25:100\"");
    assert(has(pt2, 63, 2.25) && other.selectionCount() == 3);
    wl.close("roll:" + pt2);
    r.settle();
    // back in the first: Ctrl+A, then Delete — ONE `notes delete`
    const artboard::Rect rg = world(roll, roll.gridRect());
    press(rg.right() - 20.0, rg.bottom() - 10.0, false, true);        // a Ctrl-press on its empty grid: the keys are its again
    keyMod('A', false, true);
    const int all = roll.selectionCount();
    assert(all == 8);
    keyMod(46, false, false);
    assert(lastEdit().rfind("notes delete " + pt + " \"", 0) == 0 && roll.selectionCount() == 0);
    for (const auto &x : r.svc->model().patterns)
        if (x.id == pt) assert(x.notes.empty());
    // a Ctrl+drag draws a box selecting what it touches; Escape lets go
    r.cmd("notes add " + pt + " \"C4@0 D4@0.5 E4@1 F4@3\"");
    r.settle();
    const artboard::Rect a0 = noteAt(60, 0), a1 = noteAt(64, 1.0);
    // from just inside the grid, a row above E4 (empty there), to below C4 past E4's end
    const double bx0 = a0.x + 2.0, by0 = a1.y - 4.0, bx1 = a1.right() + 4.0, by1 = a0.bottom() + 4.0;
    r.app->pointer(0, bx0, by0, 0, r.now, false, false, true);
    for (int k = 1; k <= 6; ++k) { r.app->pointer(1, bx0 + k * (bx1 - bx0) / 6.0, by0 + k * (by1 - by0) / 6.0, 0, r.now, false, false, true); r.frame(); }
    assert(roll.boxRect().w > 0.0);
    r.app->pointer(2, a1.right() + 4.0, a0.bottom() + 4.0, 0, r.now, false, false, true);
    r.frame();
    assert(roll.selectionCount() == 3 && roll.selected(62, 0.5) && !roll.selected(65, 3));
    keyMod(27, false, false);
    assert(roll.selectionCount() == 0);
    // U-10: a Ctrl+drag that starts ON a selected note is still a box — Ctrl never moves a note
    keyMod('A', false, true);
    const size_t m2 = r.sent.size();
    const artboard::Rect c4 = noteAt(60, 0);
    r.app->pointer(0, c4.x + 3.0, cy(c4), 0, r.now, false, false, true);
    for (int k = 1; k <= 6; ++k) { r.app->pointer(1, c4.x + 3.0 + k * 10.0, cy(c4) - k * 2.0, 0, r.now, false, false, true); r.frame(); }
    r.app->pointer(2, c4.x + 63.0, cy(c4) - 12.0, 0, r.now, false, false, true);
    r.settle();
    for (size_t i = m2; i < r.sent.size(); ++i) assert(r.sent[i].rfind("notes move", 0) != 0 && r.sent[i].rfind("note move", 0) != 0);
    assert(has(pt, 60, 0) && roll.selected(60, 0) && roll.selectionCount() < 4);
    keyMod(27, false, false);
    // the keys follow the pointer: a press on the lanes, and Delete is not the roll's
    const artboard::Rect ruler = world(tl, tl.rulerRect());
    press(ruler.x + 40.0, ruler.bottom() + 200.0, false, false);     // the lanes, left of the roll's window
    keyMod('A', false, true);
    assert(roll.selectionCount() == 0);
    pass("The roll's selection (R-ROLL-8): click and Shift+click select (lit, eased); a selected note drags the selection as ONE `notes move`; ↑ ↓ (Shift: octave) ← → move it; Ctrl+D lays a copy after it; Ctrl+C / Ctrl+V paste into another roll; Ctrl+A and Delete; a Ctrl box; Escape; the keys go to the roll last pressed");
}

static void test_the_length_a_click_makes_and_a_window_resized()
{
    sltest::Rig r("ui-rolllen", 1280, 800);
    r.cmd("project new " + r.song("Len") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 8");
    const std::string ac = r.svc->model().clips[0].id, pt = r.svc->model().clips[0].pattern;
    r.cmd("note add " + pt + " --pitch 60 --at 0 --length 0.25");
    r.settle();
    auto &tl = r.app->project().timeline();
    auto &wl = r.app->project().windows();
    const artboard::Rect clip = world(tl, tl.clipRect(ac));
    r.click(clip.x + 12.0, cy(clip));
    r.click(clip.x + 12.0, cy(clip));
    r.settle();
    auto &roll = *wl.roll(pt);
    auto lastEdit = [&]() {
        for (auto it = r.sent.rbegin(); it != r.sent.rend(); ++it)
            if (it->rfind("note preview ", 0) != 0) return *it;
        return std::string();
    };
    // R-ROLL-10: the chip names the length a click makes; its menu chooses one — the view's, no line
    assert(roll.clickLength() == 0.25);
    const size_t mark = r.sent.size();
    r.click(world(roll, roll.lengthRect()));
    r.settle();
    auto &menu = r.app->menu();
    assert(menu.isOpen() && menu.itemCount() == 7 && r.sent.size() == mark);
    r.click(menu.itemRect(4));                                           // 1/4
    r.settle();
    assert(roll.clickLength() == 1.0);
    const artboard::Rect rw = world(roll, artboard::Rect{0, 0, 0, 0});
    r.click(rw.x + roll.beatToX(2.1), rw.y + roll.pitchToY(64) + roll.kNoteH * 0.5);
    r.settle();
    assert(lastEdit() == "note add " + pt + " --pitch 64 --at 2 --length 1");
    // "Last used": the length of the note last made or resized
    r.click(world(roll, roll.lengthRect()));
    r.settle();
    r.click(menu.itemRect(0));
    r.settle();
    assert(roll.clickLength() == 0.25);
    const artboard::Rect n60 = world(roll, roll.noteRect(60, 0.0));
    r.drag(n60.right() - 2.0, cy(n60), rw.x + roll.beatToX(2.0) + 1.0, cy(n60), 6);
    r.settle();
    assert(roll.clickLength() == 2.0);
    // R-WIN-5: the corner resizes the window — the pointer's, the roll laid out at the new size
    auto &w = *wl.window("roll:" + pt);
    const double w0 = w.width.value(), h0 = w.height.value(), grid0 = roll.gridRect().h;
    const artboard::Rect g = world(w, w.gripRect());
    r.drag(cx(g), cy(g), cx(g) - 150.0, cy(g) + 120.0, 6, false);
    assert(std::fabs(w.width.value() - (w0 - 150.0)) < 0.5 && std::fabs(w.height.value() - (h0 + 120.0)) < 0.5);
    r.app->pointer(2, cx(g) - 150.0, cy(g) + 120.0, 0, r.now);
    r.settle();
    assert(std::fabs(roll.gridRect().h - (grid0 + 120.0)) < 0.5);           // more rows to see
    // never smaller than the floor, never out of the layer
    const artboard::Rect g2 = world(w, w.gripRect());
    r.drag(cx(g2), cy(g2), cx(g2) - 2000.0, cy(g2) - 2000.0, 6);
    r.settle();
    assert(w.width.value() == arstro::solaris_ui::FloatWindow::kMinW && w.height.value() == arstro::solaris_ui::FloatWindow::kMinH);
    const artboard::Rect g3 = world(w, w.gripRect());
    r.drag(cx(g3), cy(g3), cx(g3) + 4000.0, cy(g3) + 4000.0, 6);
    r.settle();
    assert(w.x.value() + w.width.value() <= wl.width.value() + 0.5 && w.y.value() + w.height.value() <= wl.height.value() + 0.5);
    pass("The length a click makes is named and chosen (R-ROLL-10): the chip's menu, 'Last used' following a resize; a window resized by its corner (R-WIN-5): the pointer's, its content laid out anew, never below the floor nor out of the layer");
}

static void test_the_wheel_scrolls_sideways_with_shift()
{
    // D-5: Shift goes with the wheel through App::wheel (the window passes it, and a sideways swipe arrives as it)
    sltest::Rig r("ui-sideways", 1280, 800);
    r.cmd("project new " + r.song("Side") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 64");
    const std::string ac = r.svc->model().clips[0].id, pt = r.svc->model().clips[0].pattern;
    r.cmd("set " + pt + ".length=64");
    r.settle();
    auto &tl = r.app->project().timeline();
    auto &wl = r.app->project().windows();
    // the lanes: Shift+wheel moves time, not the rows
    const artboard::Rect ruler = world(tl, tl.rulerRect());
    const double t0 = tl.beatToX(0.0), row0 = tl.rowRect(0).y;
    r.app->wheel(ruler.x + 200.0, ruler.bottom() + 30.0, -3.0, false, true);
    r.settle();
    assert(tl.beatToX(0.0) < t0 - 1.0 && tl.rowRect(0).y == row0);
    // the roll: the same
    wl.openRoll(pt, ac);
    r.settle();
    auto &roll = *wl.roll(pt);
    const artboard::Rect g = world(roll, roll.gridRect());
    const double x0 = roll.beatToX(0.0), y60 = roll.pitchToY(60);
    r.app->wheel(cx(g), cy(g), -3.0, false, true);
    r.settle();
    assert(roll.beatToX(0.0) < x0 - 1.0 && roll.pitchToY(60) == y60);
    // and without Shift the wheel still scrolls the rows
    r.app->wheel(cx(g), cy(g), -2.0);
    r.settle();
    assert(roll.pitchToY(60) != y60);
    pass("D-5: Shift+wheel scrolls sideways — the lanes and the roll move time, not rows; the plain wheel still scrolls rows");
}

static void test_a_short_drag_lands_where_it_was_let_go()
{
    // D-6: zoomed out, a sixteenth is ~6 px — a pull shorter than the recognizer's 5 px threshold was a click
    sltest::Rig r("ui-shortdrag", 1280, 800);
    r.cmd("project new " + r.song("Short") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 8");
    const std::string ac = r.svc->model().clips[0].id, pt = r.svc->model().clips[0].pattern;
    r.cmd("notes add " + pt + " \"E4@0.25:2 G4@1.1:0.25\"");             // G4 off the grid
    r.settle();
    auto &wl = r.app->project().windows();
    wl.openRoll(pt, ac);
    r.settle();
    auto &roll = *wl.roll(pt);
    const artboard::Rect g = world(roll, roll.gridRect());
    for (int k = 0; k < 3; ++k) r.app->wheel(cx(g), cy(g), -1.0, true); // zoomed out: 48 → ~25 px a beat
    r.settle();
    const artboard::Rect rw = world(roll, artboard::Rect{0, 0, 0, 0});
    auto lastEdit = [&]() {
        for (auto it = r.sent.rbegin(); it != r.sent.rend(); ++it)
            if (it->rfind("note preview ", 0) != 0) return *it;
        return std::string();
    };
    // E4's end pulled back one sixteenth: 4 px, under the threshold — it lands
    const artboard::Rect e4 = world(roll, roll.noteRect(64, 0.25));
    const double to = rw.x + roll.beatToX(2.0) - 1.0;
    assert(e4.right() - 2.0 - to < 5.0);
    const size_t m0 = r.sent.size();
    r.drag(e4.right() - 2.0, cy(e4), to, cy(e4), 4);
    r.settle();
    assert(lastEdit() == "note move " + pt + " --pitch 64 --at 0.25 --length 1.75");
    for (size_t i = m0; i < r.sent.size(); ++i) assert(r.sent[i].rfind("note add", 0) != 0); // and nothing else: not a click too
    // a click on the off-grid G4 moves nothing (a release where it was pressed is a click)
    const size_t mark = r.sent.size();
    const artboard::Rect g4 = world(roll, roll.noteRect(67, 1.1));
    r.click(g4.x + 2.0, cy(g4));
    r.settle();
    for (size_t i = mark; i < r.sent.size(); ++i) assert(r.sent[i].rfind("note move", 0) != 0);
    assert(roll.selected(67, 1.1));
    // D-7: a sixteenth zoomed out (~5 px) is grabbed by its body and moved, not resized: its right third is the edge
    r.cmd("note add " + pt + " --pitch 72 --at 3 --length 0.25");
    r.settle();
    const artboard::Rect c5 = world(roll, roll.noteRect(72, 3.0));
    assert(c5.w < 8.0);
    const size_t m1 = r.sent.size();
    r.drag(c5.x + 1.5, cy(c5), c5.x + 1.5, cy(c5) - 2.0 * roll.kNoteH, 6);
    r.settle();
    assert(lastEdit() == "note move " + pt + " --pitch 72 --at 3 --to-pitch 74 --to-at 3");
    for (size_t i = m1; i < r.sent.size(); ++i) assert(r.sent[i].find("--length") == std::string::npos || r.sent[i].rfind("note preview", 0) == 0);
    pass("D-6: a drag shorter than the recognizer's threshold lands where it was let go (a sixteenth pulled at ~6 px); a click moves nothing. D-7: a ~5 px note is grabbed by its body and moved");
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
    assert(roll.stepRows() == 12 && roll.stepRowPitch(0) == 36); // twelve pads: the crash and ride too (REQ-drum-6)
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

static void test_a_strip_ducks_under_another_from_its_menu()
{
    // R-MIX-18: right-click the bass › Duck Under ▸ the kick — ONE `strip duck` line; the submenu offers exactly the
    // strips whose keys may reach it (an earlier strip's keyTargets)
    sltest::Rig r("ui-duck", 1280, 800);
    r.cmd("project new " + r.song("Duck") + ".slp --bpm 120");
    r.cmd("clip add --instrument drums --at 0 --length 4");       // ch_2, the kick
    r.cmd("clip add --instrument synth --at 0 --length 4");       // ch_3, the bass — later in order
    r.settle();
    auto &d = r.app->project().dock();
    auto &menu = r.app->menu();
    auto openDuck = [&](const std::string &ch) {
        const artboard::Rect card = world(d, d.cardRect(ch));
        r.click(card.x + 20.0, card.y + 16.0, 2);
        r.settle();
        assert(menu.isOpen());
        int at = -1;
        for (int i = 0; i < menu.itemCount(); ++i)
            if (menu.item(i).label.rfind("Duck Under", 0) == 0) at = i;
        return at;
    };
    const int duck = openDuck("ch_3");
    assert(menu.isOpen() && duck >= 0);
    r.click(menu.itemRect(duck));
    r.settle();
    int kick = -1;
    for (int i = 0; i < menu.itemCount(); ++i)
        if (menu.item(i).label == "Drum Machine") kick = i;
    assert(menu.isOpen() && kick >= 0);
    r.click(menu.itemRect(kick));
    r.settle();
    assert(r.sent.back() == "strip duck ch_3 --under ch_2");
    bool comp = false, keyed = false;
    for (const auto &st : r.svc->model().strips)
    {
        if (st.id == "ch_3")
            for (const auto &dv : st.devices) comp |= dv.type == "compressor";
        if (st.id == "ch_2")
            for (const auto &sd : st.sends) keyed |= sd.to == "ch_3" && sd.sidechain && sd.pre;
    }
    assert(comp && keyed);
    // the kick, first in order, has nothing to duck under: no such item
    assert(openDuck("ch_2") < 0);
    r.click(5.0, 790.0); // away: the menu closes
    r.settle();
    assert(!menu.isOpen());
    pass("Duck Under (R-MIX-18): a strip's menu ducks it under an earlier strip in ONE `strip duck` line — the compressor and its pre-fader key — offering only the strips that may key it");
}

static void test_a_strip_keys_a_later_compressor()
{
    sltest::Rig r("ui-key", 1280, 800);
    r.cmd("project new " + r.song("Key") + ".slp --bpm 120");
    r.cmd("clip add --instrument drums --at 0 --length 4");       // ch_2, the kick
    r.cmd("clip add --instrument synth --at 0 --length 4");       // ch_3, the bass — later in order
    r.cmd("device add ch_3 --type compressor");
    r.settle();
    auto &d = r.app->project().dock();
    auto &menu = r.app->menu();
    // the kick's menu: "Sidechain to ▸" offers exactly the service's keyTargets; one is ONE line
    const artboard::Rect card = world(d, d.cardRect("ch_2"));
    r.click(card.x + 20.0, card.y + 16.0, 2);
    r.settle();
    int key = -1;
    for (int i = 0; i < menu.itemCount(); ++i)
        if (menu.item(i).label.rfind("Sidechain to", 0) == 0) key = i;
    assert(menu.isOpen() && key >= 0);
    r.click(menu.itemRect(key));
    r.settle();
    auto keyTargetsOf = [&](const std::string &id) {
        for (const auto &st : r.svc->model().strips)
            if (st.id == id) return st.keyTargets;
        return std::vector<std::string>{};
    };
    const auto targets = keyTargetsOf("ch_2");
    assert(menu.isOpen() && menu.itemCount() == (int)targets.size());
    int bassItem = -1;
    for (size_t i = 0; i < targets.size(); ++i)
        if (targets[i] == "ch_3") bassItem = (int)i;
    assert(bassItem >= 0);
    r.click(menu.itemRect(bassItem));
    r.settle();
    assert(sentLine(r, "send add ch_2 --to ch_3 --sidechain"));
    bool keyed = false;
    for (const auto &st : r.svc->model().strips)
        for (const auto &sd : st.sends) keyed |= st.id == "ch_2" && sd.sidechain;
    assert(keyed);
    // and no key back from the bass to the kick: keys only go forward in processing order
    const auto back = keyTargetsOf("ch_3");
    assert(std::find(back.begin(), back.end(), "ch_2") == back.end());
    pass("Sidechain: a strip's menu keys a later strip in one `send add --sidechain`, offering exactly the service's keyTargets (R-MIX-15)");
}

static void test_the_loop_region_on_the_ruler()
{
    sltest::Rig r("ui-loop", 1280, 800);
    r.cmd("project new " + r.song("Loop") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 16");
    r.settle();
    auto &tl = r.app->project().timeline();
    assert(tl.loopRect().w == 0.0);
    // a shell's loop: the brace fades in where it is, then MOVES eased when changed
    r.cmd("transport loop 4 8");
    r.frame();
    r.frame();
    assert(tl.loopAmount() > 0.0 && tl.loopAmount() < 1.0);
    r.settle();
    assert(tl.loopAmount() == 1.0 && std::fabs(tl.loopRect().x - tl.beatToX(4.0)) < 0.5 && std::fabs(tl.loopRect().right() - tl.beatToX(8.0)) < 0.5);
    r.cmd("transport loop 2 6");
    r.frame();
    r.frame();
    assert(tl.loopFromLive() < 4.0 && tl.loopFromLive() > 2.0);
    r.settle();
    assert(std::fabs(tl.loopFromLive() - 2.0) < 1e-9);
    // Shift-drag on the ruler: the brace is the pointer's while held; ONE line on release; it stays
    const artboard::Rect ruler = world(tl, tl.rulerRect());
    const artboard::Rect tw = world(tl, artboard::Rect{0, 0, 0, 0});
    const double y = cy(ruler);
    r.drag(tw.x + tl.beatToX(9.1), y, tw.x + tl.beatToX(12.9), y, 6, false, true);
    assert(std::fabs(tl.loopRect().x - tl.beatToX(9.0)) < 0.5 && std::fabs(tl.loopRect().right() - tl.beatToX(13.0)) < 0.5);
    r.app->pointer(2, tw.x + tl.beatToX(12.9), y, 0, r.now, false, true);
    r.frame();
    assert(sentLine(r, "transport loop 9 13"));
    assert(std::fabs(tl.loopRect().x - tl.beatToX(9.0)) < 0.5); // where it was let go: nothing jumps
    r.settle();
    assert(r.svc->model().transport.loopFrom == 9.0 && r.svc->model().transport.loopTo == 13.0);
    // a click inside the brace clears it — and it fades out
    const artboard::Rect br = world(tl, tl.loopRect());
    r.click(cx(br), cy(br));
    r.frame();
    r.frame();
    assert(sentLine(r, "transport loop off") && tl.loopAmount() > 0.0 && tl.loopAmount() < 1.0);
    r.settle();
    assert(tl.loopAmount() == 0.0 && r.svc->model().transport.loopTo <= r.svc->model().transport.loopFrom);
    // a plain click on the ruler still seeks
    r.click(tw.x + tl.beatToX(3.0), y);
    r.settle();
    assert(sentLine(r, "transport seek 3"));
    pass("Loop region: a shell's loop fades in and moves eased; Shift-drag on the ruler is the pointer's, one `transport loop` on release, staying; a click inside clears it, fading; a plain click seeks (R-EDM-7)");
}

static void test_the_ruler_is_dragged()
{
    // R-TIME-6: a drag on the ruler — the playhead is the pointer's, a seek at each grid line; the loop's
    // brace moved by its body and resized by an end, ONE `transport loop` on release
    sltest::Rig r("ui-ruler-drag", 1280, 800);
    r.cmd("project new " + r.song("Scrub") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 16");
    r.settle();
    auto &tl = r.app->project().timeline();
    const artboard::Rect ruler = world(tl, tl.rulerRect());
    const artboard::Rect tw = world(tl, artboard::Rect{0, 0, 0, 0});
    const double top = ruler.y + Timeline::kRulerH * 0.25, low = ruler.y + Timeline::kRulerH * 0.75;
    auto seeks = [&] {
        std::vector<std::string> out;
        for (const auto &l : r.sent)
            if (l.rfind("transport seek ", 0) == 0) out.push_back(l);
        return out;
    };
    const double step = tl.snapStep();
    assert(step > 0.0);
    // held: the playhead is EXACTLY the pointer's on the grid (not eased toward it), and the song was told
    // at every line it crossed — not once at the end
    r.drag(tw.x + tl.beatToX(1.1), top, tw.x + tl.beatToX(6.9), top, 6, false);
    assert(tl.scrubbing());
    const double want = std::round(6.9 / step) * step;
    assert(tl.playheadBeat() == want);
    const auto held = seeks();
    assert(held.size() >= 4 && held.back() == "transport seek " + Timeline::beatText(want));
    r.app->pointer(2, tw.x + tl.beatToX(6.9), top, 0, r.now);
    r.frame();
    assert(!tl.scrubbing() && seeks().size() == held.size());    // let go on the line it was on: nothing more to say
    r.frame();
    assert(tl.playheadBeat() == want && r.svc->model().transport.position == want); // nothing eases back
    // while PLAYING the playhead is still the pointer's, not the audio's
    r.cmd("transport play");
    r.pump(100.0);
    r.drag(tw.x + tl.beatToX(2.0), top, tw.x + tl.beatToX(10.2), top, 4, false);
    const double held2 = std::round(10.2 / step) * step;
    for (int k = 0; k < 6; ++k)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        r.frame();
        assert(tl.playheadBeat() == held2);
    }
    r.app->pointer(2, tw.x + tl.beatToX(10.2), top, 0, r.now);
    r.frame();
    r.cmd("transport stop");
    r.settle();
    assert(r.svc->model().transport.position >= held2);            // it played on from where it was let go
    // the loop's brace: its body moves it, its length kept — the pointer's while held, one line on release
    r.cmd("transport loop 4 8");
    r.settle();
    const size_t before = r.sent.size();
    r.drag(tw.x + tl.beatToX(5.0), low, tw.x + tl.beatToX(7.0), low, 6, false);
    assert(tl.braceDragging() && !tl.scrubbing());
    assert(std::fabs(tl.loopRect().x - tl.beatToX(6.0)) < 0.5 && std::fabs(tl.loopRect().right() - tl.beatToX(10.0)) < 0.5);
    assert(r.sent.size() == before);                                // nothing said while held
    r.app->pointer(2, tw.x + tl.beatToX(7.0), low, 0, r.now);
    r.frame();
    assert(r.sent.size() == before + 1 && r.sent.back() == "transport loop 6 10");
    assert(std::fabs(tl.loopRect().x - tl.beatToX(6.0)) < 0.5);     // where it was let go
    r.settle();
    assert(r.svc->model().transport.loopFrom == 6.0 && r.svc->model().transport.loopTo == 10.0);
    // an end resizes it; it never crosses the other end
    r.drag(tw.x + tl.beatToX(10.0), low, tw.x + tl.beatToX(12.0), low, 6);
    assert(r.sent.back() == "transport loop 6 12");
    r.settle();
    r.drag(tw.x + tl.beatToX(6.0), low, tw.x + tl.beatToX(20.0), low, 6);
    assert(r.sent.back() == "transport loop " + Timeline::beatText(12.0 - step) + " 12");
    r.settle();
    // a click is still a seek (R-TIME-5), and the playhead eases there
    r.click(tw.x + tl.beatToX(3.0), top);
    r.frame();
    assert(r.sent.back() == "transport seek 3");
    r.settle();
    assert(tl.playheadBeat() == 3.0);
    pass("The ruler dragged: the playhead is the pointer's on the grid, a seek at each line, playing or not, staying where let go; the loop's brace moved by its body and resized by an end, one `transport loop` on release; a click still seeks (R-TIME-6)");
}

static void test_a_sample_dropped_on_a_sampler_loads_it()
{
    sltest::Rig r("ui-sampler", 1280, 800);
    r.cmd("folder add /music/Samples");
    r.cmd("project new " + r.song("Sampler") + ".slp --bpm 120");
    r.cmd("strip add --kind instrument --instrument sampler");     // ch_2, dv_1 — no sound yet
    r.settle();
    auto &wl = r.app->project().windows();
    wl.openDevice("dv_1");
    r.settle();
    auto &panel = *wl.devicePanel("dv_1");
    assert(panel.takesSample() && panel.sampleText() == "no sample \xE2\x80\x94 drop one here");
    // a sample from the browser, dragged onto the sampler's window: it lights (eased), and the drop is ONE line
    auto &b = r.app->project().browser();
    r.click(world(b, b.rowRect(0)));
    r.settle();
    int kick = -1;
    for (int i = 0; i < b.rowCount(); ++i)
        if (b.row(i).label == "808 kick.wav") kick = i;
    assert(kick > 0);
    const artboard::Rect from = world(b, b.rowRect(kick));
    const artboard::Rect win = world(*wl.window("dev:dv_1"), artboard::Rect{0, 0, wl.window("dev:dv_1")->width.value(), wl.window("dev:dv_1")->height.value()});
    r.drag(cx(from), cy(from), cx(win), cy(win), 4, false);
    assert(wl.samplerAt(artboard::Point{cx(win), cy(win)}) == "dv_1");
    r.frame();                                                       // the tween began on the last move's frame
    assert(panel.dropAmount() > 0.0 && panel.dropAmount() < 1.0);
    r.settle();
    assert(panel.dropAmount() == 1.0);
    r.app->pointer(2, cx(win), cy(win), 0, r.now);
    r.settle();
    assert(sentLine(r, "set dv_1.sample=\"/music/Samples/808 kick.wav\""));
    assert(panel.sampleText() == "sample 808 kick.wav" && panel.dropAmount() == 0.0);
    assert(r.svc->model().clips.empty()); // it loaded the sampler; it did not place a clip
    pass("Sampler: its window says it has no sound; a sample dragged onto it lights it (eased) and is ONE `set <dv>.sample=`; then it names the sound (R-EDM-8)");
}

static void test_a_sample_is_heard_from_the_browser()
{
    sltest::Rig r("ui-audition", 1280, 800);
    r.cmd("folder add /music/Samples");
    r.cmd("project new " + r.song("Audition") + ".slp --bpm 120");
    r.settle();
    auto &b = r.app->project().browser();
    r.click(world(b, b.rowRect(0)));
    r.settle();
    int kick = -1;
    for (int i = 0; i < b.rowCount(); ++i)
        if (b.row(i).label == "808 kick.wav") kick = i;
    assert(kick > 0);
    const std::string file = "/music/Samples/808 kick.wav";
    // a click: heard now (ONE line, not an edit); the row's fill eases in
    r.click(world(b, b.rowRect(kick)));
    r.frame();
    r.frame();
    assert(sentLine(r, "audition \"" + file + "\"") && r.svc->model().audition.playing);
    assert(b.auditionAmount(file) > 0.0 && b.auditionAmount(file) < 1.0);
    assert(!r.svc->model().dirty);
    r.pump(600.0);                                                    // past the double-click time: a double-click PLACES it
    assert(b.auditionAmount(file) == 1.0);
    // a click on the one being heard stops it; the fill fades out
    r.click(world(b, b.rowRect(kick)));
    r.frame();
    r.frame();
    assert(sentLine(r, "audition stop") && !r.svc->model().audition.playing);
    assert(b.auditionAmount(file) > 0.0 && b.auditionAmount(file) < 1.0);
    r.settle();
    assert(b.auditionAmount(file) == 0.0);
    pass("Audition: a click on a sample hears it now (one line, not an edit), its row filling as it plays, eased in; a second click stops it, eased out (R-EDM-9)");
}

namespace
{
    int menuItem(sltest::Rig &r, const std::string &label)
    {
        auto &menu = r.app->menu();
        for (int i = 0; i < menu.itemCount(); ++i)
            if (menu.item(i).label == label) return i;
        return -1;
    }
    /** Open the right-click menu at `at` and click `label` in it. */
    void menuPick(sltest::Rig &r, const artboard::Rect &at, const std::string &label)
    {
        r.click(at, 2);
        r.settle();
        const int i = menuItem(r, label);
        if (i < 0) std::printf("    no `%s` in the menu\n", label.c_str());
        assert(r.app->menu().isOpen() && i >= 0);
        r.click(r.app->menu().itemRect(i));
    }
    double liveOf(const sltest::Rig &r, const std::string &address)
    {
        for (const auto &b : r.svc->model().bindings)
            if (b.address == address) return b.live;
        assert(false);
        return 0.0;
    }
}

static void test_the_mixers_numbers_bind_from_the_dock()
{
    // R-MIX-16: a fader, a pan, a send's level, the master — the device parameter's menu; a bound control says
    // what drives it and is DRAWN at the model's live value: eased onto a new driver, following the engine while playing
    sltest::Rig r("ui-mixbind", 1280, 800);
    r.cmd("project new " + r.song("Bind") + ".slp --bpm 120");
    r.cmd("clip add --instrument drums --at 0 --length 16");     // ch_2
    r.cmd("clip add --instrument synth --at 0 --length 16");     // ch_3
    r.cmd("strip add --kind bus --name Verb");                   // ch_4
    r.cmd("send add ch_3 --to ch_4 --gain -8");                  // sd_1
    r.settle();
    using arstro::solaris_ui::MixerDock;
    auto &d = r.app->project().dock();
    auto &menu = r.app->menu();
    // a fader's menu is the parameter menu: Create Automation is ONE line, and the fader then says what drives it
    r.click(world(d, d.faderRect("ch_2")), 2);
    r.settle();
    assert(menu.isOpen() && menu.item(0).label == "Create Automation");
    for (const char *l : {"Formula\xE2\x80\xA6", "Reset to Default", "Copy Address", "Copy Value", "Copy as Formula"}) assert(menuItem(r, l) >= 0);
    assert(menuItem(r, "Clear Binding") < 0 && menuItem(r, "Rename\xE2\x80\xA6") < 0);    // not the strip's menu
    r.click(menu.itemRect(0));
    r.frame();
    r.frame();
    assert(sentLine(r, "auto create ch_2.gain"));
    const double tagMid = d.tagAmount("ch_2.gain");
    assert(tagMid > 0.0 && tagMid < 1.0);                                                  // the tag fades in
    r.settle();
    assert(d.tagText("ch_2.gain") == "auto au_1" && d.tagAmount("ch_2.gain") == 1.0);
    // Copy Address: the host's clipboard, and the app says so
    menuPick(r, world(d, d.faderRect("ch_2")), "Copy Address");
    r.settle();
    assert(!r.copied.empty() && r.copied.back() == "ch_2.gain");
    assert(r.app->toastText() == "Copied ch_2.gain" && !r.app->toastIsRefusal() && r.app->toastAmount() > 0.0);
    // Formula… on a pan: cosmo's field; what is typed is one quoted `set`, and the knob EASES to what it evaluates to
    menuPick(r, world(d, d.panRect("ch_3")), "Formula\xE2\x80\xA6");
    r.settle();
    assert(menu.isRenaming());
    artboard::KeyEvent typed;
    typed.type = artboard::KeyEvent::Type::Text;
    typed.text = "=0.25 - 0.75";
    r.app->key(typed);
    r.key(13);
    r.frame();
    r.frame();
    assert(sentLine(r, "set ch_3.pan=\"=0.25 - 0.75\"") && liveOf(r, "ch_3.pan") == -0.5);
    const double panMid = d.panLive("ch_3");
    assert(panMid < -0.01 && panMid > -0.49);                                              // a new driver: eased onto
    r.settle();
    assert(std::fabs(d.panLive("ch_3") + 0.5) < 1e-9 && d.tagAmount("ch_3.pan") == 1.0 && r.svc->model().strips.size() > 0);
    // a send's level: its menu too — Copy Value is what it shows, Copy as Formula what a formula reads it by
    menuPick(r, world(d, d.sendRect("ch_3", 0)), "Copy Value");
    r.settle();
    assert(r.copied.back() == "-8.0 dB");
    menuPick(r, world(d, d.sendRect("ch_3", 0)), "Copy as Formula");
    r.settle();
    assert(r.copied.back() == "=sd_1.gain");
    menuPick(r, world(d, d.sendRect("ch_3", 0)), "Formula\xE2\x80\xA6");
    r.settle();
    typed.text = "=au_1-4";
    r.app->key(typed);
    r.key(13);
    r.settle();
    assert(sentLine(r, "set sd_1.gain==au_1-4") && d.tagAmount("sd_1.gain") == 1.0 && d.shownValue("sd_1.gain") == -4.0);
    // the master fader
    menuPick(r, world(d, d.faderRect("master")), "Copy Address");
    r.settle();
    assert(r.copied.back() == "project.masterGain");
    // Clear Binding: its own value again, the tag fading out
    menuPick(r, world(d, d.panRect("ch_3")), "Clear Binding");
    r.frame();
    r.frame();
    assert(sentLine(r, "bind clear ch_3.pan") && d.tagAmount("ch_3.pan") > 0.0 && d.tagAmount("ch_3.pan") < 1.0);
    r.settle();
    assert(d.tagAmount("ch_3.pan") == 0.0 && d.tagText("ch_3.pan").empty());
    // playing: the bound fader FOLLOWS the engine's value, continuously (the playhead's idiom), and moves with it
    r.cmd("auto point move au_1 --at 16 --value -32");                                      // ch_2.gain ramps −2 dB a beat
    r.settle();
    assert(std::fabs(d.faderLive("ch_2") - MixerDock::faderPos(0.0)) < 1e-9);
    r.cmd("transport play");
    r.pump(300.0);                                                                           // past the catch-up
    const double before = d.faderLive("ch_2");
    int followed = 0;
    for (int k = 0; k < 12; ++k)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        r.frame();
        const double want = MixerDock::faderPos(liveOf(r, "ch_2.gain"));
        if (std::fabs(d.faderLive("ch_2") - want) > 1e-9) std::printf("    frame %d: fader %.6f, live %.6f\n", k, d.faderLive("ch_2"), want);
        assert(std::fabs(d.faderLive("ch_2") - want) < 1e-9);
        ++followed;
    }
    assert(followed == 12 && liveOf(r, "ch_2.gain") < -0.1 && d.faderLive("ch_2") < before);
    r.cmd("transport stop");
    r.settle();
    assert(std::fabs(d.faderLive("ch_2") - MixerDock::faderPos(liveOf(r, "ch_2.gain"))) < 1e-9);
    pass("Mixer numbers bind (R-MIX-16): a fader/pan/send/master right-click is the parameter menu — Create Automation, Formula… (one quoted line), Clear Binding, Copy Address/Value/as Formula to the host's clipboard (toasted); the tag fades in and out; a new driver eases; playing, the fader follows the engine's live value");
}

static void test_ids_shown_and_copied()
{
    // R-UI-11: View › Show IDs is a machine setting the ids FADE in by; Copy items on parameter rows, clips, automations
    sltest::Rig r("ui-ids", 1280, 800);
    r.cmd("project new " + r.song("Ids") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 8");      // ch_2, dv_1, ac_1, pt_1
    r.cmd("set dv_1.filter.cutoff=900");
    r.settle();
    auto &ps = r.app->project();
    auto &ms = ps.bar().menus();
    // View › Show IDs → `settings set showIds=on`; every id fades in together, eased (caught mid-tween)
    assert(ps.idsAmount() == 0.0);
    r.click(world(ms, ms.titleRect(3)));
    r.settle();
    int show = -1;
    for (int i = 0; i < (int)ms.menu(3).items.size(); ++i)
        if (ms.menu(3).items[(size_t)i].label == "Show IDs") show = i;
    assert(show >= 0);
    r.click(world(ms, ms.itemRect(3, show)));
    r.frame();
    r.frame();
    assert(sentLine(r, "settings set showIds=on") && r.svc->model().settings.showIds);
    const double mid = ps.idsAmount();
    assert(mid > 0.0 && mid < 1.0);
    r.settle();
    assert(ps.idsAmount() == 1.0 && ms.menu(3).items[(size_t)show].label == "Hide IDs");
    // an agent turns it off: they fade out the same way
    r.cmd("settings set showIds=off");
    r.frame();
    r.frame();
    assert(ps.idsAmount() > 0.0 && ps.idsAmount() < 1.0);
    r.settle();
    assert(ps.idsAmount() == 0.0);
    // a device parameter row: Copy Address / Value / as Formula
    auto &wl = ps.windows();
    wl.openDevice("dv_1");
    r.settle();
    auto &p = *wl.devicePanel("dv_1");
    int cut = -1;
    for (int i = 0; i < p.rowCount(); ++i)
        if (p.rowParam(i) == "filter.cutoff") cut = i;
    p.reveal("filter.cutoff");
    r.settle();
    const artboard::Rect row = world(p, p.rowRect(cut));
    menuPick(r, artboard::Rect{row.x + 10.0, row.y, 20.0, row.h}, "Copy Address");
    r.settle();
    assert(r.copied.back() == "dv_1.filter.cutoff" && r.app->toastText() == "Copied dv_1.filter.cutoff");
    menuPick(r, artboard::Rect{row.x + 10.0, row.y, 20.0, row.h}, "Copy Value");
    r.settle();
    assert(r.copied.back() == "900 Hz");
    menuPick(r, artboard::Rect{row.x + 10.0, row.y, 20.0, row.h}, "Copy as Formula");
    r.settle();
    assert(r.copied.back() == "=dv_1.filter.cutoff");
    // a choice row: no formula reads it, so no Copy as Formula; its value is its name
    int wave = -1;
    for (int i = 0; i < p.rowCount(); ++i)
        if (p.rowParam(i) == "osc1.wave") wave = i;
    p.reveal("osc1.wave");
    r.settle();
    const artboard::Rect wr = world(p, p.rowRect(wave));
    r.click(artboard::Rect{wr.x + 10.0, wr.y, 20.0, wr.h}, 2);
    r.settle();
    assert(menuItem(r, "Copy Value") >= 0 && menuItem(r, "Copy as Formula") < 0 && menuItem(r, "Create Automation") < 0);
    r.click(r.app->menu().itemRect(menuItem(r, "Copy Value")));
    r.settle();
    std::string waveText;
    for (const auto &st : r.svc->model().strips)
        for (const auto &dv : st.devices)
            for (const auto &pm : dv.params)
                if (dv.id == "dv_1" && pm.name == "osc1.wave") waveText = pm.text;
    assert(!waveText.empty() && r.copied.back() == waveText);
    wl.close("dev:dv_1");
    r.settle();
    // a clip: Copy ID
    auto &tl = ps.timeline();
    menuPick(r, world(tl, tl.clipRect("ac_1")), "Copy ID");
    r.settle();
    assert(r.copied.back() == "ac_1");
    // an automation's row: Copy ID, Copy as Formula
    r.cmd("auto create dv_1.filter.cutoff");
    r.settle();
    const artboard::Rect ar = world(tl, tl.autoRowRect("au_1"));
    const double ax = world(tl, artboard::Rect{tl.beatToX(3.0), 0, 0, 0}).x;
    menuPick(r, artboard::Rect{ax, ar.y + ar.h * 0.5 - 2.0, 4.0, 4.0}, "Copy as Formula");
    r.settle();
    assert(r.copied.back() == "=au_1");
    menuPick(r, artboard::Rect{ax, ar.y + ar.h * 0.5 - 2.0, 4.0, 4.0}, "Copy ID");
    r.settle();
    assert(r.copied.back() == "au_1");
    pass("IDs (R-UI-11): View › Show IDs is `settings set showIds=…` and the ids fade in and out together (caught mid-tween); a parameter row copies its address, value and formula; a choice no formula; a clip and an automation their ids — to the host's clipboard");
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

static void test_the_grid_follows_the_zoom()
{
    sltest::Rig r("ui-grid", 1280, 800);
    r.cmd("project new " + r.song("Grid") + ".slp --bpm 120");
    r.cmd("lane add Beats");
    r.cmd("clip add --instrument synth --at 2 --length 4 --lane ln_1");   // ac_1
    r.settle();
    auto &tl = r.app->project().timeline();
    const artboard::Rect tw = world(tl, artboard::Rect{0, 0, 0, 0});
    const double ry = cy(world(tl, tl.rulerRect()));
    auto rulerClick = [&](double beat) {
        r.click(tw.x + tl.beatToX(beat), ry);
        r.settle();
    };
    auto zoom = [&](int notches) { // Ctrl+wheel about beat 2 — the clip's start stays where it is
        for (int i = 0; i < std::abs(notches); ++i) r.app->wheel(tw.x + tl.beatToX(2.0), ry + 60.0, notches > 0 ? 1.0 : -1.0, true);
    };
    // the default zoom (28 px a beat): bars, beats and halves drawn, quarters barely; the step is the half you see
    assert(tl.pxPerBeat() == Timeline::kZoomPpb);
    assert(tl.gridAlpha(0) == 1.0 && tl.gridAlpha(1) == 1.0 && tl.gridAlpha(2) > 0.5 && tl.gridAlpha(3) < 0.1 && tl.gridAlpha(4) == 0.0);
    assert(tl.snapStep() == 0.5 && tl.snapLabel() == "1/8");
    rulerClick(3.3);
    assert(sentLine(r, "transport seek 3.5") && r.svc->model().transport.position == 3.5); // not 3.25: a quarter you cannot see

    // Ctrl+wheel EASES the zoom, and a level FADES with it — caught mid-fade, between its two ends (§1)
    const double q0 = tl.gridAlpha(3);
    zoom(2);                                                            // 43.75 px a beat, eased
    r.frame();
    r.frame();
    r.frame();
    const double qMid = tl.gridAlpha(3), ppbMid = tl.pxPerBeat();
    r.settle();
    const double q1 = tl.gridAlpha(3);
    assert(ppbMid > Timeline::kZoomPpb && ppbMid < tl.pxPerBeat());
    assert(q0 < qMid - 0.02 && qMid < q1 - 0.05);                      // neither popped at the end nor stuck at the start

    // four notches more (107 px a beat): the step is 1/8 of a beat, its name cross-fading on the ruler as it changes
    zoom(4);
    bool nameMid = false;
    for (int i = 0; i < 40; ++i)
    {
        r.frame();
        nameMid |= tl.snapLabelAmount() > 0.0 && tl.snapLabelAmount() < 1.0;
    }
    assert(nameMid && tl.snapLabelAmount() == 1.0);
    assert(tl.snapStep() == 0.125 && tl.snapLabel() == "1/32" && tl.gridAlpha(4) > 0.5 && tl.gridAlpha(5) < 0.5); // the step: drawn more than half
    rulerClick(5.17);
    assert(sentLine(r, "transport seek 5.125"));                      // a fixed quarter would say 5.25
    // a clip dragged lands on the same step, following the pointer exactly (on the grid) while held
    const artboard::Rect c0 = world(tl, tl.clipRect("ac_1"));
    const double dx = 1.1 * tl.pxPerBeat();
    r.drag(c0.x + 10.0, cy(c0), c0.x + 10.0 + dx, cy(c0), 8, false);
    assert(std::fabs(world(tl, tl.clipRect("ac_1")).x - (tw.x + tl.beatToX(3.125))) < 0.5);
    r.app->pointer(2, c0.x + 10.0 + dx, cy(c0), 0, r.now);
    r.settle();
    assert(sentLine(r, "clip move ac_1 --at 3.125") && r.svc->model().clips[0].at == 3.125);
    // the browser's drop too: the hint and the line on the same step
    auto &b = r.app->project().browser();
    r.click(world(b, b.tabRect(1)));
    r.settle();
    int drums = -1;
    for (int i = 0; i < b.rowCount(); ++i)
        if (b.row(i).value == "drums") drums = i;
    const artboard::Rect from = world(b, b.rowRect(drums));
    r.drag(cx(from), cy(from), tw.x + tl.beatToX(4.1), cy(world(tl, tl.rowRect(0))));
    r.settle();
    assert(sentLine(r, "clip add --instrument drums --at 4.125 --length 4 --lane ln_1"));

    // the deepest zoom (14 notches; one more changes nothing): NOTHING snaps — the exact tick under the pointer
    zoom(9);
    r.settle();
    assert(tl.atDeepestZoom() && tl.snapStep() == 0.0 && tl.snapLabel() == "Off" && tl.gridAlpha(6) == 1.0);
    rulerClick(2.0 + 7.0 / 960.0);                                     // seven ticks past beat 2: on no grid line
    assert(sentLine(r, "transport seek 2.007") && r.svc->model().transport.position == 1927.0 / 960.0);

    // all the way out (8 notches past the default): bars only, and a click goes to the nearest bar
    zoom(-30);
    r.settle();
    assert(std::fabs(tl.pxPerBeat() - Timeline::kZoomPpb * std::pow(1.25, -Timeline::kZoomOutSteps)) < 1e-9);
    assert(tl.gridAlpha(0) == 1.0 && tl.gridAlpha(1) == 0.0 && tl.snapStep() == 4.0 && tl.snapLabel() == "Bar");
    rulerClick(9.9);
    assert(sentLine(r, "transport seek 8") && r.svc->model().transport.position == 8.0);
    pass("The grid follows the zoom: levels fade in as they get room (caught mid-fade in an eased zoom); a ruler click, a clip drag and a drop "
         "land on the finest level you see — a bar far out, the tick at the deepest zoom (R-UI-10, R-TIME-5)");
}

static void test_markers_on_the_ruler()
{
    // R-ARR-1: the ruler's band — a marker's flag clicked seeks to it, double-clicked loops its section, dragged moves
    // it (the pointer's while held, ONE line, staying where it was let go), right-clicked renames or deletes it; a
    // right-click elsewhere on the ruler adds one; a shell's move eases there, a new one fades in
    sltest::Rig r("ui-markers", 1280, 800);
    r.cmd("project new " + r.song("Markers") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 32");
    r.cmd("marker add Intro --at 0");
    r.cmd("marker add Chorus --at 8 --length 4");
    r.settle();
    auto &tl = r.app->project().timeline();
    assert(tl.markerAlpha("mk_1") == 1.0 && tl.markerEnd("mk_1") == 8.0 && tl.markerEnd("mk_2") == 12.0);
    const artboard::Rect f = world(tl, tl.markerRect("mk_2"));
    assert(f.w > 20.0 && f.h == arstro::solaris_ui::Timeline::kMarkH);
    r.click(cx(f), cy(f));
    r.settle();
    assert(sentLine(r, "transport seek mk_2") && r.svc->model().transport.position == 8.0);
    r.click(cx(f), cy(f));
    r.click(cx(f), cy(f));
    r.settle();
    assert(sentLine(r, "transport loop mk_2") && r.svc->model().transport.loopFrom == 8.0 && r.svc->model().transport.loopTo == 12.0);
    // dragged to beat 16: the pointer's while held, then ONE line, and it stays there
    const double to = world(tl, artboard::Rect{tl.beatToX(16.0), 0, 0, 0}).x;
    r.drag(f.x + 4.0, cy(f), to + 4.0, cy(f), 6, false);
    assert(std::fabs(tl.markerBeat("mk_2") - 16.0) < 1e-9 && std::fabs(tl.markerEnd("mk_2") - 20.0) < 1e-9); // its section travels with it
    r.app->pointer(2, to + 4.0, cy(f), 0, r.now);
    r.frame();
    assert(sentLine(r, "marker move mk_2 --at 16") && tl.markerBeat("mk_2") == 16.0);
    r.settle();
    assert(r.svc->model().markers[1].at == 16.0 && tl.markerBeat("mk_2") == 16.0);
    // a shell's move eases there; a new marker fades in
    r.cmd("marker move mk_1 --at 4");
    r.frame();
    r.frame();
    const double mid = tl.markerBeat("mk_1");
    assert(mid > 0.0 && mid < 4.0);
    r.settle();
    assert(tl.markerBeat("mk_1") == 4.0);
    r.cmd("marker add Outro --at 24");
    r.frame();
    r.frame();
    const double in = tl.markerAlpha("mk_3");
    assert(in > 0.0 && in < 1.0);
    r.settle();
    // a right-click on a flag: Rename… (cosmo's field — ONE line) and Delete
    auto &menu = r.app->menu();
    const artboard::Rect f3 = world(tl, tl.markerRect("mk_3"));
    r.click(cx(f3), cy(f3), 2);
    r.settle();
    assert(menu.isOpen() && menuItem(r, "Rename\xE2\x80\xA6") >= 0 && menuItem(r, "Delete marker") >= 0);
    r.click(menu.itemRect(menuItem(r, "Rename\xE2\x80\xA6")));
    r.settle();
    assert(menu.isRenaming());
    artboard::KeyEvent typed;
    typed.type = artboard::KeyEvent::Type::Text;
    typed.text = "Big Outro";
    r.app->key(typed);
    r.key(13);
    r.settle();
    assert(sentLine(r, "marker rename mk_3 \"Big Outro\"") && r.svc->model().markers[2].name == "Big Outro");
    r.click(cx(f3), cy(f3), 2);
    r.settle();
    r.click(menu.itemRect(menuItem(r, "Delete marker")));
    r.frame();
    r.frame();
    assert(sentLine(r, "marker delete mk_3"));
    const double out = tl.markerAlpha("mk_3");
    assert(out > 0.0 && out < 1.0); // fading out where it was
    r.settle();
    assert(tl.markerAlpha("mk_3") == 0.0);
    // a right-click on the ruler away from any flag: a marker there, on the grid you see
    const artboard::Rect band = world(tl, artboard::Rect{tl.beatToX(28.0), 0.0, 1.0, arstro::solaris_ui::Timeline::kMarkH});
    r.click(cx(band), cy(band), 2);
    r.settle();
    assert(menuItem(r, "Add marker here") >= 0);
    r.click(menu.itemRect(menuItem(r, "Add marker here")));
    r.settle();
    assert(sentLine(r, "marker add \"Marker 3\" --at 28") && r.svc->model().markers.size() == 3);
    pass("markers on the ruler (R-ARR-1): a flag clicked seeks, double-clicked loops its section, dragged moves it (one line, its section with it), right-clicked renames or deletes it; elsewhere adds one; a shell's move eases, a new one fades in, a deleted one fades out");
}

static void test_time_edits_from_the_ruler()
{
    // R-ARR-2: the ruler's menu — a bar of time opened where it was right-clicked, that bar taken out, the loop's span
    // taken out — each ONE line; what moves eases there
    sltest::Rig r("ui-time", 1280, 800);
    r.cmd("project new " + r.song("Time") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");
    r.cmd("clip add --strip " + r.svc->model().clips[0].track + " --pattern " + r.svc->model().clips[0].pattern + " --at 8 --length 4"); // ac_2
    r.settle();
    auto &tl = r.app->project().timeline();
    auto &menu = r.app->menu();
    const artboard::Rect band = world(tl, artboard::Rect{tl.beatToX(5.0), 0.0, 1.0, arstro::solaris_ui::Timeline::kMarkH});
    r.click(cx(band), cy(band), 2);
    r.settle();
    assert(menuItem(r, "Insert a bar here") >= 0 && menuItem(r, "Delete this bar") >= 0 && menuItem(r, "Delete the loop's time") < 0);
    const double x8 = tl.clipRect("ac_2").x;
    r.click(menu.itemRect(menuItem(r, "Insert a bar here")));
    r.frame();
    r.frame();
    assert(sentLine(r, "time insert --at 4 --length 4") && r.svc->model().clips[1].at == 12.0);
    const double mid = tl.clipRect("ac_2").x;
    assert(mid > x8 + 0.5 && mid < x8 + tl.beatToX(4.0) - tl.beatToX(0.0) - 0.5); // on its way
    r.settle();
    r.click(cx(band), cy(band), 2);
    r.settle();
    r.click(menu.itemRect(menuItem(r, "Delete this bar")));
    r.settle();
    assert(sentLine(r, "time delete --from 4 --to 8") && r.svc->model().clips[1].at == 8.0 && std::fabs(tl.clipRect("ac_2").x - x8) < 0.01);
    r.cmd("transport loop 8 12");
    r.settle();
    r.click(cx(band), cy(band), 2);
    r.settle();
    assert(menuItem(r, "Delete the loop's time") >= 0);
    r.click(menu.itemRect(menuItem(r, "Delete the loop's time")));
    r.settle();
    assert(sentLine(r, "time delete --from 8 --to 12") && r.svc->model().clips.size() == 1);
    pass("time edits from the ruler (R-ARR-2): Insert a bar here, Delete this bar, Delete the loop's time — each one line; the clips eased there");
}

static void test_lane_groups_on_the_timeline()
{
    // R-ARR-3: a group's chevron folds it (eased: the chevron turns, its clips travel into ONE row); a clip dragged on the
    // folded row carries the group's clips — ONE `clips move`; the folded row's menu opens it; a lane's menu puts it
    // into a group and out again
    sltest::Rig r("ui-groups", 1280, 800);
    r.cmd("project new " + r.song("Groups") + ".slp --bpm 120");
    r.cmd("lane add A");
    r.cmd("lane add B");
    r.cmd("lane add C");
    std::string a, b, c;
    for (const auto &l : r.svc->model().lanes) { if (l.name == "A") a = l.id; if (l.name == "B") b = l.id; if (l.name == "C") c = l.id; }
    r.cmd("clip add --instrument synth --at 0 --length 4 --lane " + a);
    const std::string c1 = r.svc->model().clips.back().id, track = r.svc->model().clips.back().track;
    r.cmd("clip add --strip " + track + " --at 4 --length 4 --lane " + b);
    const std::string c2 = r.svc->model().clips.back().id;
    r.cmd("lane group " + a + " " + b + " --name Drums");
    r.settle();
    auto &tl = r.app->project().timeline();
    assert(tl.groupToggleRect("lg_1").w > 0.0 && tl.groupTurn("lg_1") == 0.0 && tl.groupRow("lg_1") < 0);
    const double y2 = tl.clipRect(c2).y;
    // folded by its chevron: ONE line; the chevron turns and c2 travels up into the group's row, eased
    r.click(world(tl, tl.groupToggleRect("lg_1")));
    r.frame();
    r.frame();
    assert(sentLine(r, "set lg_1.folded=on") && tl.groupRow("lg_1") >= 0);
    const double turn = tl.groupTurn("lg_1"), mid = tl.clipRect(c2).y;
    assert(turn > 0.0 && turn < 1.0 && mid < y2 - 0.5 && mid > tl.clipRect(c1).y + 0.5);
    r.settle();
    {
        // one row: each lane's clips in its own band of it, so they never lie on one another
        const artboard::Rect row = tl.rowRect(tl.groupRow("lg_1")), k1 = tl.clipRect(c1), k2 = tl.clipRect(c2);
        assert(tl.groupTurn("lg_1") == 1.0 && k1.y >= row.y && k2.bottom() <= row.bottom() && k2.y >= k1.bottom() - 0.01 && k1.h < row.h * 0.5);
    }
    // a clip dragged on the folded row carries the other: the pointer's while held, then ONE line
    const artboard::Rect k1 = world(tl, tl.clipRect(c1));
    const double x0 = tl.clipRect(c2).x, dx = tl.beatToX(2.0) - tl.beatToX(0.0);
    r.drag(k1.x + 10.0, cy(k1), k1.x + 10.0 + dx, cy(k1), 6, false);
    assert(std::fabs(tl.clipRect(c2).x - (x0 + dx)) < 0.5); // carried
    r.app->pointer(2, k1.x + 10.0 + dx, cy(k1), 0, r.now);
    r.frame();
    assert(sentLine(r, "clips move \"" + c1 + " " + c2 + "\" --by 2") || sentLine(r, "clips move \"" + c2 + " " + c1 + "\" --by 2"));
    assert(std::fabs(tl.clipRect(c2).x - (x0 + dx)) < 0.5); // where it was let go
    r.settle();
    assert(r.svc->model().clips[0].at == 2.0 && r.svc->model().clips[1].at == 6.0);
    // the folded row's menu: Open group
    auto &menu = r.app->menu();
    const artboard::Rect head = world(tl, artboard::Rect{20.0, tl.rowRect(tl.groupRow("lg_1")).y + 10.0, 1.0, 1.0});
    r.click(head.x, head.y, 2);
    r.settle();
    assert(menu.isOpen() && menuItem(r, "Open group") >= 0 && menuItem(r, "Ungroup") >= 0);
    r.click(menu.itemRect(menuItem(r, "Open group")));
    r.settle();
    assert(sentLine(r, "set lg_1.folded=off") && tl.groupRow("lg_1") < 0 && tl.groupTurn("lg_1") == 0.0);
    // a lane's menu: into the group, and out again
    int rc = -1;
    for (int i = 0; i < tl.rowCount(); ++i)
        if (tl.rowLane(i) == c) rc = i;
    const artboard::Rect lc = world(tl, artboard::Rect{20.0, tl.rowRect(rc).y + 10.0, 1.0, 1.0});
    r.click(lc.x, lc.y, 2);
    r.settle();
    assert(menuItem(r, "New group") >= 0 && menuItem(r, "Into group\xE2\x80\xA6") >= 0);
    r.click(menu.itemRect(menuItem(r, "Into group\xE2\x80\xA6")));
    r.settle();
    assert(menuItem(r, "Drums") >= 0);
    r.click(menu.itemRect(menuItem(r, "Drums")));
    r.settle();
    assert(sentLine(r, "set " + c + ".group=lg_1") && r.svc->model().groups[0].lanes.size() == 3);
    for (int i = 0; i < tl.rowCount(); ++i)
        if (tl.rowLane(i) == c) rc = i;
    const artboard::Rect lc2 = world(tl, artboard::Rect{20.0, tl.rowRect(rc).y + 10.0, 1.0, 1.0});
    r.click(lc2.x, lc2.y, 2);
    r.settle();
    r.click(menu.itemRect(menuItem(r, "Leave group")));
    r.settle();
    assert(sentLine(r, "set " + c + ".group=none") && r.svc->model().groups[0].lanes.size() == 2);
    pass("lane groups on the timeline (R-ARR-3): the chevron folds and opens a group, one line, eased — the chevron turns, the clips travel into one row; a clip dragged on the folded row carries the group's clips, one `clips move`; the folded row's menu opens it; a lane's menu puts it into a group and out");
}

static void test_recording_audio_from_the_window()
{
    // R-REC-1: an audio line's R in the mixer — with no input, a menu of the song's input ports ("Arm with Mic": ONE line);
    // lit red, eased; R again disarms; the strip menu's Monitor…; the ruler punches the loop's span — a red line, eased
    sltest::Rig r("ui-recaudio", 1280, 800);
    r.cmd("project new " + r.song("RecAudio") + ".slp --bpm 120");
    r.cmd("port add Mic --dir in");
    r.cmd("strip add --kind audio --name Vox");
    std::string vox, bus;
    for (const auto &s : r.svc->model().strips) { if (s.name == "Vox") vox = s.id; if (s.kind == "bus" && bus.empty()) bus = s.id; }
    r.settle();
    auto &d = r.app->project().dock();
    auto &menu = r.app->menu();
    assert(d.armRect(vox).w > 0 && d.armRect(bus).w == 0.0 && d.muteRect(vox).right() <= d.soloRect(vox).x && d.soloRect(vox).right() <= d.armRect(vox).x);
    r.click(world(d, d.armRect(vox)));
    r.settle();
    assert(menu.isOpen() && menuItem(r, "Arm with Mic") >= 0);
    r.click(menu.itemRect(menuItem(r, "Arm with Mic")));
    r.frame();
    r.frame();
    assert(sentLine(r, "strip arm " + vox + " --input prt_2") && r.svc->model().armed == vox);
    const double mid = d.armAmount(vox);
    assert(mid > 0.0 && mid < 1.0);
    r.settle();
    assert(d.armAmount(vox) == 1.0);
    r.click(world(d, d.armRect(vox)));
    r.settle();
    assert(sentLine(r, "strip arm none") && d.armAmount(vox) == 0.0);
    // its menu: Monitor… → On
    const artboard::Rect card = world(d, d.cardRect(vox));
    r.click(card.x + 20.0, card.y + 16.0, 2);
    r.settle();
    assert(menuItem(r, "Monitor\xE2\x80\xA6") >= 0 && menuItem(r, "Input\xE2\x80\xA6") >= 0);
    r.click(menu.itemRect(menuItem(r, "Monitor\xE2\x80\xA6")));
    r.settle();
    assert(menuItem(r, "On") >= 0);
    r.click(menu.itemRect(menuItem(r, "On")));
    r.settle();
    assert(sentLine(r, "set " + vox + ".monitor=on"));
    for (const auto &s : r.svc->model().strips) if (s.id == vox) assert(s.monitor == "on");
    // the ruler punches the loop's span: a red line, eased in
    r.cmd("transport loop 4 8");
    r.settle();
    auto &tl = r.app->project().timeline();
    const artboard::Rect band = world(tl, artboard::Rect{tl.beatToX(5.0), 0.0, 1.0, arstro::solaris_ui::Timeline::kMarkH});
    r.click(cx(band), cy(band), 2);
    r.settle();
    assert(menuItem(r, "Punch the loop's span") >= 0 && menuItem(r, "Punch off") < 0);
    r.click(menu.itemRect(menuItem(r, "Punch the loop's span")));
    r.frame();
    r.frame();
    assert(sentLine(r, "transport punch 4 8"));
    const double pin = tl.punchAmount();
    assert(pin > 0.0 && pin < 1.0);
    r.settle();
    assert(tl.punchAmount() == 1.0 && std::fabs(tl.punchRect().x - tl.beatToX(4.0)) < 0.01);
    pass("recording audio from the window (R-REC-1): R on an audio line — no input: the input ports offered, one line — lit red eased, again disarms; Monitor… On; the ruler punches the loop's span, a red line eased in");
}

static void test_takes_under_their_clip()
{
    // R-REC-2: a clip with three takes says so; its menu shows them — a row per take under its lane, inserted eased — the
    // last lit (the default); a drag across take 1 is ONE `clip comp`, its light crossing over eased; a double-click on
    // take 2 is that take for the whole clip; Hide takes
    sltest::Rig r("ui-takes", 1280, 800);
    const std::string path = r.song("Takes") + ".slp";
    r.cmd("project new " + path + " --bpm 120");
    r.cmd("clip add --src vox.wav --at 0");                         // half a second: a beat long
    r.cmd("set " + r.svc->model().clips[0].id + ".length=4");
    r.cmd("set " + r.svc->model().clips[0].id + ".loop=true");
    r.cmd("project save");
    std::string text;
    {
        std::ifstream f(path);
        std::stringstream ss;
        ss << f.rdbuf();
        text = ss.str();
    }
    const auto at = text.find("#aclip");
    const auto eol = text.find('\n', at);
    text.insert(eol + 1, "  #take src=a.wav\n  #take src=b.wav\n  #take src=c.wav\n");
    {
        std::ofstream f(path);
        f << text;
    }
    r.cmd("project open " + path);
    r.settle();
    auto &tl = r.app->project().timeline();
    const std::string ac = r.svc->model().clips[0].id;
    assert(r.svc->model().clips[0].takes.size() == 3 && !tl.takesShown(ac) && tl.takeRowRect(ac, 1).h == 0.0);
    const int rows = tl.rowCount();
    auto &menu = r.app->menu();
    const artboard::Rect cr = world(tl, tl.clipRect(ac));
    r.click(cr.x + 20.0, cy(cr), 2);
    r.settle();
    assert(menuItem(r, "Show takes") >= 0);
    r.click(menu.itemRect(menuItem(r, "Show takes")));
    r.settle();
    assert(tl.takesShown(ac) && tl.rowCount() == rows + 3 && tl.takeRowRect(ac, 3).h > 0.0);
    assert(tl.takeLit(ac, 3, 1.5) == 1.0 && tl.takeLit(ac, 1, 1.5) == 0.0); // the last plays where the comp says nothing
    // a drag across take 1, beats 1 … 2: ONE line; the light crosses over, eased
    const artboard::Rect t1 = world(tl, tl.takeRowRect(ac, 1));
    const double x1 = world(tl, artboard::Rect{tl.beatToX(1.0), 0, 0, 0}).x, x2 = world(tl, artboard::Rect{tl.beatToX(2.0), 0, 0, 0}).x;
    r.drag(x1 + 1.0, cy(t1), x2 + 1.0, cy(t1), 6);
    r.frame();
    r.frame();
    assert(sentLine(r, "clip comp " + ac + " --take 1 --from 1 --to 2"));
    const double mid = tl.takeLit(ac, 1, 1.5);
    assert(mid > 0.0 && mid < 1.0 && tl.takeLit(ac, 3, 1.5) > 0.0);
    r.settle();
    assert(tl.takeLit(ac, 1, 1.5) == 1.0 && tl.takeLit(ac, 3, 1.5) == 0.0 && tl.takeLit(ac, 3, 2.5) == 1.0);
    // a double-click on take 2: the whole clip
    const artboard::Rect t2 = world(tl, tl.takeRowRect(ac, 2));
    r.click(x2 + 40.0, cy(t2));
    r.click(x2 + 40.0, cy(t2));
    r.settle();
    assert(sentLine(r, "clip comp " + ac + " --take 2") && tl.takeLit(ac, 2, 0.5) == 1.0 && tl.takeLit(ac, 2, 3.5) == 1.0);
    // hidden again
    r.click(cr.x + 20.0, cy(cr), 2);
    r.settle();
    r.click(menu.itemRect(menuItem(r, "Hide takes")));
    r.settle();
    assert(!tl.takesShown(ac) && tl.rowCount() == rows);
    pass("takes under their clip (R-REC-2): Show takes inserts a row per take, the last lit; a drag across take 1 is one `clip comp`, its light crossing over eased; a double-click on take 2 the whole clip; Hide takes");
}

static void test_a_clips_fades_gain_and_edits()
{
    // R-EDIT-1: an audio clip's fade handles and gain line — dragged, the drawing is the pointer's; let go, ONE `set`;
    // moved by the model, eased. Two clips overlapping on a lane show their crossfade. The menu: Reverse (its mark eased
    // in), Consolidate the clips it runs into, Slice ▸, Split at the time bar; Ctrl+E splits the selected clip there
    sltest::Rig r("ui-clipedit", 1280, 800);
    r.cmd("project new " + r.song("Edit") + ".slp --bpm 120");
    r.cmd("clip add --src vox.wav --at 0 --length 4");              // looping half a second: 4 beats, 112 px
    r.settle();
    auto &tl = r.app->project().timeline();
    const std::string ac = r.svc->model().clips[0].id;
    auto wx = [&](double beat) { return world(tl, artboard::Rect{tl.beatToX(beat), 0, 0, 0}).x; };
    // the fade in's handle, dragged a beat: the drawing follows the pointer, then ONE line
    const artboard::Rect hi = world(tl, tl.fadeHandleRect(ac, false));
    assert(hi.w > 0 && tl.clipFadeLive(ac, false) == 0.0);
    r.drag(cx(hi), cy(hi), cx(hi) + 28.0, cy(hi), 6, false);
    assert(tl.clipFadeLive(ac, false) == 1.0 && tl.clipHeardFadeLive(ac, false) == 1.0);
    assert(!sentLine(r, "set " + ac + ".fadeIn=1"));                // not until it is let go
    r.app->pointer(2, cx(hi) + 28.0, cy(hi), 0, r.now);
    r.frame();
    assert(sentLine(r, "set " + ac + ".fadeIn=1") && r.svc->model().clips[0].fadeIn == 1.0);
    r.frame();
    assert(tl.clipFadeLive(ac, false) == 1.0);                      // where it was let go: no jump back and forth
    // moved by the model: eased
    r.cmd("set " + ac + ".fadeIn=0.5");
    r.frame();
    r.frame();
    const double easing = tl.clipFadeLive(ac, false);
    assert(easing > 0.5 && easing < 1.0);
    r.settle();
    assert(tl.clipFadeLive(ac, false) == 0.5);
    // the fade out's handle, half a beat in from the end
    const artboard::Rect ho = world(tl, tl.fadeHandleRect(ac, true));
    r.drag(cx(ho), cy(ho), cx(ho) - 14.0, cy(ho), 6);
    r.frame();
    assert(sentLine(r, "set " + ac + ".fadeOut=0.5"));
    // the gain line: up from 0 dB, louder; the line is where the pointer left it
    const artboard::Rect cr = world(tl, tl.clipRect(ac));
    const double gy = world(tl, artboard::Rect{0, tl.gainLineY(ac), 0, 0}).y;
    r.drag(wx(2.0), gy, wx(2.0), gy - 6.0, 6);
    r.frame();
    const double louder = r.svc->model().clips[0].gain;
    assert(louder > 5.0 && louder < 7.0 && tl.clipGainLive(ac) == louder);
    assert(std::fabs(world(tl, artboard::Rect{0, tl.gainLineY(ac), 0, 0}).y - (gy - 6.0)) < 0.5);
    r.cmd("set " + ac + ".gain=-6");
    r.frame();
    r.frame();
    const double g = tl.clipGainLive(ac);
    assert(g < louder && g > -6.0);
    r.settle();
    assert(std::fabs(tl.clipGainLive(ac) + 6.0) < 1e-9);
    // a clip over its end on the lane: the crossfade drawn, eased in
    r.cmd("clip add --src vox.wav --at 3 --length 2");
    std::string over;
    for (const auto &c : r.svc->model().clips)
        if (c.at == 3.0) over = c.id;
    r.frame();
    r.frame();
    const double xf = tl.clipHeardFadeLive(ac, true);
    assert(xf > 0.5 && xf < 1.0);                                   // its own 0.5 growing to the overlap's 1
    r.settle();
    assert(tl.clipHeardFadeLive(ac, true) == 1.0 && tl.clipHeardFadeLive(over, false) == 1.0 && tl.clipFadeLive(ac, true) == 0.5);
    // the menu: Reverse — its mark eased in; Consolidate the two it runs into; a loop is not sliced
    auto &menu = r.app->menu();
    r.click(cr.x + 30.0, cy(cr), 2);
    r.settle();
    assert(menuItem(r, "Reverse") >= 0 && menuItem(r, "Consolidate 2 clips") >= 0 && menuItem(r, "Slice \xE2\x96\xB8") < 0);
    r.click(menu.itemRect(menuItem(r, "Reverse")));
    r.frame();
    r.frame();
    assert(sentLine(r, "set " + ac + ".reverse=true") && tl.clipReversedLive(ac) > 0.0 && tl.clipReversedLive(ac) < 1.0);
    r.settle();
    r.click(cr.x + 30.0, cy(cr), 2);
    r.settle();
    assert(menuItem(r, "Play forwards") >= 0);
    r.click(menu.itemRect(menuItem(r, "Consolidate 2 clips")));
    r.settle();
    assert(sentLine(r, "clip consolidate " + ac + " " + over));
    // a plain clip: Slice ▸ Every half beat; Split at the time bar; Ctrl+E
    r.cmd("clip add --src vox.wav --at 8");
    std::string plain;
    for (const auto &c : r.svc->model().clips)
        if (c.at == 8.0) plain = c.id;
    r.cmd("transport seek 8.5");
    r.settle();
    const artboard::Rect pr = world(tl, tl.clipRect(plain));
    r.click(cx(pr), cy(pr), 2);
    r.settle();
    assert(menuItem(r, "Split at the time bar") >= 0);
    r.click(menu.itemRect(menuItem(r, "Slice \xE2\x96\xB8")));
    r.settle();
    assert(menuItem(r, "At its hits") >= 0 && menuItem(r, "To a kit, a slice a beat") >= 0);
    r.click(menu.itemRect(menuItem(r, "Every half beat")));
    r.settle();
    assert(sentLine(r, "clip slice " + plain + " --every 0.5") && r.svc->model().clips.size() == 3);
    r.cmd("undo");
    r.settle();
    r.click(cx(pr), cy(pr), 2);
    r.settle();
    r.click(menu.itemRect(menuItem(r, "Split at the time bar")));
    r.settle();
    assert(sentLine(r, "clip split " + plain + " --at 8.5"));
    r.cmd("undo");
    r.settle();
    r.sent.clear();
    r.click(cx(pr), cy(pr));                                        // selected
    r.settle();
    r.key('E', true);
    r.settle();
    assert(sentLine(r, "clip split " + plain + " --at 8.5"));
    pass("a clip's edits (R-EDIT-1): fade handles and the gain line follow the pointer and send ONE `set` when let go; moved by the model, eased; an overlap's crossfade drawn, eased in; the menu's Reverse (eased), Consolidate 2 clips, Slice ▸ Every half beat, Split at the time bar; Ctrl+E splits there");
}

static void test_a_clip_warped_from_its_menu()
{
    // R-WARP-1: Tempo… typed is ONE `set <ac>.bpm`; Warp to the song's tempo one `set <ac>.warp=true`, its tag ("60 bpm")
    // eased in; Pitch ▸ +7 one `set <ac>.pitch=7`, the tag crossing over to "60 bpm · +7 st"; Unwarp
    sltest::Rig r("ui-warp", 1280, 800);
    r.cmd("project new " + r.song("Warp") + ".slp --bpm 120");
    r.cmd("clip add --src vox.wav --at 0 --length 4");
    r.settle();
    auto &tl = r.app->project().timeline();
    auto &menu = r.app->menu();
    const std::string ac = r.svc->model().clips[0].id;
    const artboard::Rect cr = world(tl, tl.clipRect(ac));
    auto open = [&] {
        r.click(cr.x + 30.0, cy(cr), 2);
        r.settle();
    };
    open();
    assert(menuItem(r, "Warp to the song's tempo") >= 0 && menuItem(r, "Tempo\xE2\x80\xA6") >= 0 && menuItem(r, "Pitch \xE2\x96\xB8") >= 0);
    r.click(menu.itemRect(menuItem(r, "Tempo\xE2\x80\xA6")));
    r.settle();
    assert(menu.isRenaming());
    artboard::KeyEvent typed;
    typed.type = artboard::KeyEvent::Type::Text;
    typed.text = "60";
    r.app->key(typed);
    r.key(13);
    r.settle();
    assert(sentLine(r, "set " + ac + ".bpm=60") && r.svc->model().clips[0].bpm == 60.0);
    open();
    r.click(menu.itemRect(menuItem(r, "Warp to the song's tempo")));
    r.frame();
    r.frame();
    assert(sentLine(r, "set " + ac + ".warp=true") && r.svc->model().clips[0].warp);
    const double in = tl.clipTagAlpha(ac, "60 bpm");
    assert(in > 0.0 && in < 1.0);
    r.settle();
    assert(tl.clipTagAlpha(ac, "60 bpm") == 1.0);
    open();
    r.click(menu.itemRect(menuItem(r, "Pitch \xE2\x96\xB8")));
    r.settle();
    assert(menuItem(r, "0 (as recorded)  \xC2\xB7 now") >= 0);
    r.click(menu.itemRect(menuItem(r, "+7 st")));
    r.frame();
    r.frame();
    const std::string both = "60 bpm \xC2\xB7 +7 st";
    assert(sentLine(r, "set " + ac + ".pitch=7") && r.svc->model().clips[0].pitch == 7.0);
    const double over = tl.clipTagAlpha(ac, both), out = tl.clipTagAlpha(ac, "60 bpm");
    assert(over > 0.0 && over < 1.0 && out > 0.0 && out < 1.0);
    r.settle();
    assert(tl.clipTagAlpha(ac, both) == 1.0 && tl.clipTagAlpha(ac, "60 bpm") == 0.0);
    open();
    assert(menuItem(r, "Unwarp") >= 0);
    r.click(menu.itemRect(menuItem(r, "Unwarp")));
    r.settle();
    assert(sentLine(r, "set " + ac + ".warp=false") && !r.svc->model().clips[0].warp && tl.clipTagAlpha(ac, "+7 st") == 1.0);
    pass("a clip warped from its menu (R-WARP-1): Tempo… typed is one `set .bpm`; Warp one `set .warp=true`, its tag eased in; Pitch ▸ +7 one line, the tag crossing over; Unwarp");
}

static void test_tempo_changes_on_the_ruler()
{
    // R-TEMPO-1: a change on the ruler's band fades in; dragged, its flag is the pointer's and the release ONE `tempo
    // move --at`; a tempo changed elsewhere counts there; its menu — Glide into it, Tempo… typed — one line each; the
    // ruler's menu adds one, typed; the song bar shows the tempo where the time bar is and sets the change in effect
    // there; Delete fades it out
    sltest::Rig r("ui-tempo", 1280, 800);
    r.cmd("project new " + r.song("Tempo") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 32");
    r.settle();
    auto &tl = r.app->project().timeline();
    auto &menu = r.app->menu();
    r.cmd("tempo add --at 8 --bpm 140");
    const std::string tp = r.svc->model().tempos[0].id;
    r.frame();
    r.frame();
    const double in = tl.tempoAlpha(tp);
    assert(in > 0.0 && in < 1.0);
    r.settle();
    assert(tl.tempoAlpha(tp) == 1.0 && tl.tempoBeat(tp) == 8.0);
    // dragged four beats on: the pointer's, then one line
    const artboard::Rect f = world(tl, tl.tempoRect(tp));
    r.drag(f.x + 4.0, cy(f), f.x + 4.0 + 4 * 28.0, cy(f), 6, false);
    assert(tl.tempoBeat(tp) == 12.0 && !sentLine(r, "tempo move " + tp + " --at 12"));
    r.app->pointer(2, f.x + 4.0 + 4 * 28.0, cy(f), 0, r.now);
    r.frame();
    assert(sentLine(r, "tempo move " + tp + " --at 12") && r.svc->model().tempos[0].at == 12.0);
    // changed elsewhere: its flag counts there
    r.cmd("tempo move " + tp + " --bpm 100");
    r.frame();
    r.frame();
    const double counting = tl.tempoShown(tp);
    assert(counting < 140.0 && counting > 100.0);
    r.settle();
    assert(tl.tempoShown(tp) == 100.0);
    // its menu: glide into it; its tempo typed
    const artboard::Rect f2 = world(tl, tl.tempoRect(tp));
    r.click(f2.x + 4.0, cy(f2), 2);
    r.settle();
    assert(menuItem(r, "Glide into it") >= 0 && menuItem(r, "Delete tempo change") >= 0);
    r.click(menu.itemRect(menuItem(r, "Glide into it")));
    r.settle();
    assert(sentLine(r, "tempo move " + tp + " --ramp true") && r.svc->model().tempos[0].ramp);
    r.click(f2.x + 4.0, cy(f2), 2);
    r.settle();
    r.click(menu.itemRect(menuItem(r, "Tempo\xE2\x80\xA6")));
    r.settle();
    artboard::KeyEvent typed;
    typed.type = artboard::KeyEvent::Type::Text;
    typed.text = "90";
    r.app->key(typed);
    r.key(13);
    r.settle();
    assert(sentLine(r, "tempo move " + tp + " --bpm 90"));
    // the ruler's menu: a change from here, typed
    const double x4 = world(tl, artboard::Rect{tl.beatToX(4.0), 0, 0, 0}).x;
    r.click(x4 + 1.0, f2.y + f2.h * 0.5, 2);
    r.settle();
    assert(menuItem(r, "Tempo change here\xE2\x80\xA6") >= 0);
    r.click(menu.itemRect(menuItem(r, "Tempo change here\xE2\x80\xA6")));
    r.settle();
    typed.text = "75";
    r.app->key(typed);
    r.key(13);
    r.settle();
    assert(sentLine(r, "tempo add --at 4 --bpm 75") && r.svc->model().tempos.size() == 2);
    // the song bar: the tempo where the time bar is, and the line that changes it there
    auto &bar = r.app->project().bar();
    r.cmd("transport seek 14");
    r.settle();
    assert(std::fabs(bar.bpmShown() - 90.0) < 1e-6 && bar.tempoLine("95") == "tempo move " + tp + " --bpm 95");
    r.cmd("transport seek 2");
    r.settle();
    assert(std::fabs(bar.bpmShown() - 120.0) < 1e-6 && bar.tempoLine("95") == "set project.bpm=95");
    // deleted: it fades out
    const artboard::Rect f3 = world(tl, tl.tempoRect(tp));
    r.click(f3.x + 4.0, cy(f3), 2);
    r.settle();
    r.click(menu.itemRect(menuItem(r, "Delete tempo change")));
    r.frame();
    r.frame();
    assert(sentLine(r, "tempo delete " + tp));
    const double out = tl.tempoAlpha(tp);
    assert(out > 0.0 && out < 1.0);
    r.settle();
    assert(tl.tempoAlpha(tp) == 0.0);
    pass("tempo changes on the ruler (R-TEMPO-1): one fades in; dragged, the pointer's, then one `tempo move --at`; changed elsewhere it counts there; Glide into it and Tempo… one line each; the ruler's Tempo change here… typed; the song bar shows the tempo at the time bar and sets the change in effect; Delete fades it out");
}

static void test_presets_in_the_window()
{
    // R-PRESET-1: the browser's Presets tab lists them by type, the factory's marked; an instrument's preset dragged onto
    // the lanes is ONE `clip add --instrument … --preset`; a device window's Presets — a preset is ONE `preset load`,
    // Save as preset… typed ONE `preset save`, and the tab grows the new one in
    sltest::Rig r("ui-presets", 1280, 800);
    std::filesystem::remove_all(r.dir + "/presets");
    std::filesystem::create_directories(r.dir + "/presets/factory/synth");
    {
        std::ofstream f(r.dir + "/presets/factory/synth/Warm.slpreset");
        f << "arstro-preset = 1\ntype = synth\nfilter.cutoff = 1800.0\n";
    }
    r.cmd("project new " + r.song("Presets") + ".slp --bpm 120");
    r.cmd("preset list"); // the folders read afresh
    r.settle();
    auto &br = r.app->project().browser();
    r.click(world(br, br.tabRect(arstro::solaris_ui::Browser::kSong)));
    r.settle();
    const double w0 = br.tabRect(arstro::solaris_ui::Browser::kSong).w, w1 = br.tabRect(arstro::solaris_ui::Browser::kPresets).w;
    r.click(world(br, br.tabRect(arstro::solaris_ui::Browser::kPresets)));
    r.frame();
    r.frame();
    const double sliding = br.tabHighlightW();
    assert(w0 != w1 && sliding > std::min(w0, w1) && sliding < std::max(w0, w1)); // the highlight's width eases with its place
    r.settle();
    assert(br.tab() == arstro::solaris_ui::Browser::kPresets && br.tabHighlightW() == w1);
    int warm = -1;
    for (int i = 0; i < br.rowCount(); ++i)
        if (br.row(i).kind == "preset" && br.row(i).label == "Warm") warm = i;
    assert(warm >= 1 && br.row(warm - 1).kind == "header" && br.row(warm).value == "synth" && br.row(warm).note == "factory" && br.row(warm).role == "instrument");
    // dragged onto the lanes: a new instrument with its preset, one line
    auto &tl = r.app->project().timeline();
    const artboard::Rect row = world(br, br.rowRect(warm));
    const double x = world(tl, artboard::Rect{tl.beatToX(4.0), 0, 0, 0}).x, y = world(tl, artboard::Rect{0, arstro::solaris_ui::Timeline::kRulerH + 30.0, 0, 0}).y;
    r.drag(cx(row), cy(row), x + 1.0, y, 8);
    r.settle();
    assert(sentLine(r, "clip add --instrument synth --preset Warm --at 4 --length 4 --lane new"));
    const std::string dv = r.svc->project()->racks.back().devices[0].id;
    // its window's Presets: one loaded, one saved
    auto &wl = r.app->project().windows();
    wl.openDevice(dv);
    r.settle();
    auto &p = *wl.devicePanel(dv);
    auto &menu = r.app->menu();
    r.click(world(p, p.presetsRect()));
    r.settle();
    assert(menuItem(r, "Warm  \xC2\xB7 factory") >= 0 && menuItem(r, "Save as preset\xE2\x80\xA6") >= 0);
    r.click(menu.itemRect(menuItem(r, "Warm  \xC2\xB7 factory")));
    r.settle();
    assert(sentLine(r, "preset load " + dv + " \"Warm\""));
    r.click(world(p, p.presetsRect()));
    r.settle();
    r.click(menu.itemRect(menuItem(r, "Save as preset\xE2\x80\xA6")));
    r.settle();
    artboard::KeyEvent typed;
    typed.type = artboard::KeyEvent::Type::Text;
    typed.text = "Mine";
    r.app->key(typed);
    r.key(13);
    r.frame();
    r.frame();
    assert(sentLine(r, "preset save " + dv + " \"Mine\""));
    int mine = -1;
    for (int i = 0; i < br.rowCount(); ++i)
        if (br.row(i).kind == "preset" && br.row(i).label == "Mine") mine = i;
    assert(mine >= 0 && br.row(mine).note.empty());
    const double grows = br.rowAlpha(mine);
    assert(grows > 0.0 && grows < 1.0);                              // grown in, not snapped
    r.settle();
    assert(br.rowAlpha(mine) == 1.0);
    pass("presets in the window (R-PRESET-1): the Presets tab by type, the factory's marked; dragged onto the lanes, one `clip add --instrument --preset`; a device window's Presets — a preset loads in one line, Save as preset… typed one line, the tab growing it in");
}

static void test_templates_in_the_window()
{
    // R-PRESET-2: Home's From template… — the templates in a menu, the factory's marked; one picked, the host asked where,
    // ONE `project new … --template`; File › Save as Template… typed is ONE `template save`; File › New from Template…
    // offers it then
    sltest::Rig r("ui-templates", 1280, 800);
    auto quote = [](const std::string &x) { return x.find_first_of(" \t\"") == std::string::npos ? x : "\"" + x + "\""; }; // App's rule
    const std::filesystem::path shipped = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "templates";
    std::filesystem::remove_all(r.dir + "/templates");
    std::filesystem::create_directories(r.dir + "/templates/factory");
    std::filesystem::copy(shipped, r.dir + "/templates/factory", std::filesystem::copy_options::recursive);
    r.cmd("template list"); // the folders read afresh
    r.settle();
    auto &home = r.app->home();
    auto &menu = r.app->menu();
    std::string where = r.song("From Beat");
    int asked = 0;
    r.app->onPickSongToCreate = [&] { ++asked; r.app->newSongPicked(where); };
    r.click(world(home, home.actionRect(2)));
    r.settle();
    assert(menuItem(r, "Beat  \xC2\xB7 factory") >= 0 && menuItem(r, "Piano Ballad  \xC2\xB7 factory") >= 0);
    r.click(menu.itemRect(menuItem(r, "Beat  \xC2\xB7 factory")));
    r.settle();
    assert(asked == 1 && sentLine(r, "project new " + quote(where + ".slp") + " --template Beat"));
    std::set<std::string> names;
    for (const auto &st : r.svc->model().strips) names.insert(st.name);
    assert(r.svc->model().screen == "project" && names.count("Drums") && names.count("Pad") && r.svc->model().bpm == 124.0);
    // File › Save as Template…, typed
    auto &ms = r.app->project().bar().menus();
    int saveAs = -1, fromT = -1;
    for (int i = 0; i < (int)ms.menu(0).items.size(); ++i)
    {
        if (ms.menu(0).items[(size_t)i].label == "Save as Template\xE2\x80\xA6") saveAs = i;
        if (ms.menu(0).items[(size_t)i].label == "New from Template\xE2\x80\xA6") fromT = i;
    }
    assert(saveAs >= 0 && fromT >= 0 && ms.menu(0).items[5].label == "Export\xE2\x80\xA6"); // the others where they were
    r.click(world(ms, ms.titleRect(0)));
    r.settle();
    r.click(world(ms, ms.itemRect(0, saveAs)));
    r.settle();
    assert(menu.isRenaming());
    artboard::KeyEvent typed;
    typed.type = artboard::KeyEvent::Type::Text;
    typed.text = "My Beat";
    r.app->key(typed);
    r.key(13);
    r.settle();
    assert(sentLine(r, "template save \"My Beat\""));
    // File › New from Template… offers it now, unmarked
    where = r.song("Again");
    r.click(world(ms, ms.titleRect(0)));
    r.settle();
    r.click(world(ms, ms.itemRect(0, fromT)));
    r.settle();
    assert(menuItem(r, "My Beat") >= 0);
    r.click(menu.itemRect(menuItem(r, "My Beat")));
    r.settle();
    assert(asked == 2 && sentLine(r, "project new " + quote(where + ".slp") + " --template \"My Beat\""));
    // a plain New Song after: no template carried over
    where = r.song("Plain");
    r.app->newSongPicked(where);
    assert(sentLine(r, "project new " + quote(where + ".slp")));
    pass("templates in the window (R-PRESET-2): Home's From template… lists them, the factory's marked; picked, the host asks where and one `project new --template`; File › Save as Template… typed one `template save`; New from Template… offers it; a plain new song carries none");
}

static void test_the_sample_library_in_the_browser()
{
    // R-PRESET-3: a sound's star is ONE `sample favourite` (lit, eased); its menu's Tag… typed is ONE `sample tag`, Remove
    // tag one `sample untag`; under the folders, Search samples… typed is ONE `browse --search` and the list crosses to
    // what it found, back to the folders by its first row; Favourites one `browse --favourites`
    sltest::Rig r("ui-library", 1280, 800);
    std::filesystem::remove(r.dir + "/library");
    r.cmd("folder add " + r.dir);
    r.cmd("project new " + r.song("Lib") + ".slp --bpm 120");
    r.settle();
    auto &b = r.app->project().browser();
    auto &menu = r.app->menu();
    auto quote = [](const std::string &x) { return x.find_first_of(" \t\"") == std::string::npos ? x : "\"" + x + "\""; };
    auto find = [&](const std::string &label) {
        for (int i = 0; i < b.rowCount(); ++i)
            if (b.row(i).label == label) return i;
        return -1;
    };
    r.click(world(b, b.tabRect(0)));
    r.settle();
    r.click(world(b, b.rowRect(0)));                                         // the folder
    r.settle();
    int k = find("808 kick.wav");
    const std::string kick = b.row(k).value;
    assert(k > 0 && b.starRect(k).w > 0 && b.starAmount(kick) == 0.0);
    r.click(world(b, b.starRect(k)));
    r.frame();
    r.frame();
    assert(sentLine(r, "sample favourite " + quote(kick) + " on"));
    const double lighting = b.starAmount(kick);
    assert(lighting > 0.0 && lighting < 1.0);
    r.settle();
    assert(b.starAmount(kick) == 1.0);
    // Tag…, typed
    k = find("808 kick.wav");
    r.click(cx(world(b, b.rowRect(k))) - 40.0, cy(world(b, b.rowRect(k))), 2);
    r.settle();
    r.click(menu.itemRect(menuItem(r, "Tag\xE2\x80\xA6")));
    r.settle();
    artboard::KeyEvent typed;
    typed.type = artboard::KeyEvent::Type::Text;
    typed.text = "punchy";
    r.app->key(typed);
    r.key(13);
    r.settle();
    assert(sentLine(r, "sample tag " + quote(kick) + " punchy"));
    bool tagged = false;
    for (const auto &e : r.svc->model().library) tagged |= e.path == kick && e.tags == std::vector<std::string>{"punchy"} && e.favourite;
    assert(tagged);
    // under the folders: a search, typed
    r.click(world(b, b.rowRect(0)));                                         // up
    r.settle();
    const int sr = find("Search samples\xE2\x80\xA6");
    assert(sr >= 1 && b.row(sr).kind == "search");
    r.click(world(b, b.rowRect(sr)));
    r.settle();
    typed.text = "open";
    r.app->key(typed);
    r.key(13);
    r.frame();
    r.frame();
    assert(sentLine(r, "browse --search open") && b.search() == "open");
    r.settle();
    assert(b.row(0).kind == "clearsearch" && find("open hat.wav") > 0 && find("808 kick.wav") < 0);
    // back to the folders by its first row
    r.click(world(b, b.rowRect(0)));
    r.settle();
    assert(b.search().empty() && b.row(0).kind == "folder");
    // the favourites
    r.click(world(b, b.rowRect(find("Favourites"))));
    r.settle();
    assert(sentLine(r, "browse --favourites") && b.row(0).kind == "clearsearch" && b.rowCount() == 2 && b.row(1).value == kick);
    // Remove tag, from its menu there
    r.click(cx(world(b, b.rowRect(1))) - 40.0, cy(world(b, b.rowRect(1))), 2);
    r.settle();
    r.click(menu.itemRect(menuItem(r, "Remove tag \xC2\xB7 punchy")));
    r.settle();
    assert(sentLine(r, "sample untag " + quote(kick) + " punchy"));
    pass("the sample library in the browser (R-PRESET-3): a star one `sample favourite`, lit eased; Tag… typed one `sample tag`; Search samples… typed one `browse --search`, the list crossing to what it found, back by its first row; Favourites one `browse --favourites`; Remove tag one `sample untag`");
}

static void test_a_vca_in_the_dock()
{
    // R-MIX-19 (Round 13, part 3): "+ Line" offers a VCA (one line); a strip's menu "VCA ▸" — the song's VCAs — is one
    // `set <ch>.vca=`; the member's tag fades in, its dB easing as the VCA moves (caught mid-tween); a VCA's card has no
    // pan or route to press, and names whom it moves
    sltest::Rig r("ui-vca", 1280, 800);
    r.cmd("project new " + r.song("Vca") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");                // ch_2
    r.settle();
    auto &d = r.app->project().dock();
    auto &menu = r.app->menu();
    const std::string mx = r.svc->model().mixers[0].id;
    r.click(world(d, d.addLineRect()));
    r.settle();
    int vca = -1;
    for (int i = 0; i < menu.itemCount(); ++i)
        if (menu.item(i).label == "VCA") vca = i;
    assert(menu.isOpen() && vca == 2);
    r.click(menu.itemRect(vca));
    r.settle();
    assert(sentLine(r, "strip add --kind vca --mixer " + mx));
    std::string v;
    for (const auto &st : r.svc->model().strips)
        if (st.kind == "vca") v = st.id;
    assert(!v.empty());
    // ch_2's menu: VCA ▸ → the VCA, one line
    const artboard::Rect card = world(d, d.cardRect("ch_2"));
    r.click(card.x + 20.0, card.y + 16.0, 2);
    r.settle();
    int sub = -1;
    for (int i = 0; i < menu.itemCount(); ++i)
        if (menu.item(i).label.rfind("VCA", 0) == 0) sub = i;
    assert(menu.isOpen() && sub >= 0);
    r.click(menu.itemRect(sub));
    r.settle();
    assert(menu.isOpen() && menu.item(0).label == "VCA");
    r.click(menu.itemRect(0));
    r.frame();
    r.frame();
    assert(sentLine(r, "set ch_2.vca=" + v));
    assert(d.vcaAmount("ch_2") > 0.0 && d.vcaAmount("ch_2") < 1.0); // the tag fading in
    r.settle();
    assert(d.vcaAmount("ch_2") == 1.0 && d.vcaDbShown("ch_2") == 0.0);
    // the VCA moved from a shell: the member's tag eases to its dB
    r.cmd("set " + v + ".gain=-6");
    r.frame();
    r.frame();
    const double mid = d.vcaDbShown("ch_2");
    assert(mid < 0.0 && mid > -6.0);
    r.settle();
    assert(d.vcaDbShown("ch_2") == -6.0);
    // the VCA's card: no pan to drag, no route to press — nothing sent
    const size_t before = r.sent.size();
    const artboard::Rect vc = world(d, d.cardRect(v));
    r.drag(vc.x + vc.w * 0.5, vc.y + 176.5, vc.x + vc.w * 0.8, vc.y + 176.5, 6); // where a pan would be (170 … 183 under the card top)
    r.settle();
    assert(r.sent.size() == before);
    for (const auto &st : r.svc->model().strips)
        if (st.id == v) assert(st.members == std::vector<std::string>{"ch_2"});
    std::printf("    the member's VCA tag mid-ease at %.2f dB (to −6)\n", mid);
    pass("a VCA in the dock (R-MIX-19): + Line › VCA one line; a strip's VCA ▸ one `set .vca=`; the member's tag fades in and its dB eases (mid-tween); the VCA's card has no pan or route to press");
}

static void test_per_strip_tools_in_the_dock()
{
    // R-MIX-20 (Round 14, part 3): a strip's menu — Invert Phase, Width ▸, and on a bus Solo-safe — one line each; its
    // card's Ø and width tag, a safe bus's ring on its S, each caught fading in; the master's Mono one line, lit eased
    sltest::Rig r("ui-tools", 1280, 800);
    r.cmd("project new " + r.song("Tools") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");                // ch_2
    const std::string mx = r.svc->model().mixers[0].id;
    r.cmd("strip add --kind bus --name Verb --mixer " + mx);               // ch_3, on this page
    r.settle();
    auto &d = r.app->project().dock();
    auto &menu = r.app->menu();
    auto pick = [&](const std::string &card, const std::string &prefix) {
        const artboard::Rect c = world(d, d.cardRect(card));
        r.click(c.x + 20.0, c.y + 16.0, 2);
        r.settle();
        int at = -1;
        for (int i = 0; i < menu.itemCount(); ++i)
            if (menu.item(i).label.rfind(prefix, 0) == 0) at = i;
        assert(menu.isOpen() && at >= 0);
        r.click(menu.itemRect(at));
    };
    pick("ch_2", "Invert Phase");
    r.frame();
    r.frame();
    assert(sentLine(r, "set ch_2.phase=on") && d.phaseAmount("ch_2") > 0.0 && d.phaseAmount("ch_2") < 1.0);
    r.settle();
    assert(d.phaseAmount("ch_2") == 1.0);
    pick("ch_2", "Width");
    r.settle();
    int w150 = -1;
    for (int i = 0; i < menu.itemCount(); ++i)
        if (menu.item(i).label.rfind("150 %", 0) == 0) w150 = i;
    assert(menu.isOpen() && w150 >= 0);
    r.click(menu.itemRect(w150));
    r.frame();
    r.frame();
    assert(sentLine(r, "set ch_2.width=150") && d.widthAmount("ch_2") > 0.0 && d.widthAmount("ch_2") < 1.0);
    r.settle();
    assert(d.widthAmount("ch_2") == 1.0);
    pick("ch_3", "Solo-safe");
    r.frame();
    r.frame();
    assert(sentLine(r, "set ch_3.soloSafe=true") && d.safeAmount("ch_3") > 0.0 && d.safeAmount("ch_3") < 1.0);
    r.settle();
    assert(d.safeAmount("ch_3") == 1.0);
    // a strip that is not a bus has no Solo-safe
    {
        const artboard::Rect c = world(d, d.cardRect("ch_2"));
        r.click(c.x + 20.0, c.y + 16.0, 2);
        r.settle();
        for (int i = 0; i < menu.itemCount(); ++i) assert(menu.item(i).label.rfind("Solo-safe", 0) != 0);
        r.click(600.0, 250.0); // dismissed as a user does: a click elsewhere
        r.settle();
        assert(!menu.isOpen());
    }
    // the master's Mono: the listener's, one line; lit eased
    r.click(world(d, d.monoRect()));
    r.frame();
    r.frame();
    assert(sentLine(r, "transport mono on") && d.monoAmount() > 0.0 && d.monoAmount() < 1.0);
    r.settle();
    assert(d.monoAmount() == 1.0 && r.svc->model().transport.mono);
    pass("per-strip tools in the dock (R-MIX-20): Invert Phase, Width ▸ and a bus's Solo-safe one line each — Ø, the width tag and the ring caught fading in; the master's Mono one line, lit eased");
}

static void test_versions_from_the_song_bar()
{
    // R-VER (Round 15, part 3): the song bar's version chip says "main"; its menu is the tree and New Version… (cosmo's
    // field: ONE line, opened); the label cross-fades, the ring eases in and the chip's width eases to the name, each
    // caught mid-way; Freeze from the menu, said on the chip; main opened from a shell eases back the same; the tree
    // opens a version in one line
    using arstro::solaris_ui::SongBar;
    sltest::Rig r("ui-versions", 1280, 800);
    r.cmd("project new " + r.song("Versions") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");
    r.settle();
    auto &bar = r.app->project().bar();
    auto &menu = r.app->menu();
    assert(bar.versionLabel() == "main" && bar.versionRing() == 0.0 && bar.versionWidth() > 0.0);
    const double w0 = bar.versionWidth();
    r.click(world(bar, bar.hitRect(SongBar::kVersion)));
    r.settle();
    assert(menu.isOpen() && menu.item(0).label == "main  \xC2\xB7 open" && menuItem(r, "New Version\xE2\x80\xA6") >= 0 && menuItem(r, "Freeze") < 0);
    r.click(menu.itemRect(menuItem(r, "New Version\xE2\x80\xA6")));
    r.settle();
    assert(menu.isRenaming());
    artboard::KeyEvent typed;
    typed.type = artboard::KeyEvent::Type::Text;
    typed.text = "Radio edit";
    r.app->key(typed);
    r.key(13);
    r.frame();
    r.frame();
    assert(sentLine(r, "version new \"Radio edit\" --open") && r.svc->model().version == "vr_1");
    assert(bar.versionLabel() == "Radio edit" && bar.versionFade() > 0.0 && bar.versionFade() < 1.0);
    assert(bar.versionRing() > 0.0 && bar.versionRing() < 1.0);
    r.frame();
    r.frame();
    const double wMid = bar.versionWidth();
    r.settle();
    assert(bar.versionFade() == 1.0 && bar.versionRing() == 1.0 && bar.versionWidth() > w0);
    assert(wMid > w0 && wMid < bar.versionWidth()); // "Radio edit" is wider than "main": the chip grew, eased

    // on a version, its menu offers what a version can do; Freeze is one line, said on the chip
    r.click(world(bar, bar.hitRect(SongBar::kVersion)));
    r.settle();
    assert(menuItem(r, "    Radio edit  \xC2\xB7 open") >= 0 && menuItem(r, "Rebase") >= 0 && menuItem(r, "Rename\xE2\x80\xA6") >= 0 &&
           menuItem(r, "Delete Version") >= 0);
    r.click(menu.itemRect(menuItem(r, "Freeze")));
    r.frame();
    r.frame();
    assert(sentLine(r, "version freeze vr_1") && bar.versionLabel() == "Radio edit \xC2\xB7 frozen" && bar.versionFade() < 1.0);
    r.settle();

    // main opened from a shell: the same ease, the ring going out
    r.cmd("version open main");
    r.frame();
    r.frame();
    assert(bar.versionLabel() == "main" && bar.versionRing() > 0.0 && bar.versionRing() < 1.0);
    r.settle();
    assert(bar.versionRing() == 0.0);

    // the tree: the version under main, opened by ONE line
    r.click(world(bar, bar.hitRect(SongBar::kVersion)));
    r.settle();
    const int at = menuItem(r, "    Radio edit  \xC2\xB7 frozen");
    assert(at >= 0);
    r.click(menu.itemRect(at));
    r.settle();
    assert(sentLine(r, "version open vr_1") && r.svc->model().version == "vr_1" && bar.versionRing() == 1.0);
    pass("versions from the song bar (R-VER): the chip says main; its menu the tree and New Version… — ONE line, opened; the label cross-fades, the ring and the width ease, caught mid-way; Freeze said on the chip; main from a shell eases back; the tree opens a version");
}

static void test_relink_and_collect_from_the_file_menu()
{
    // R-COLLECT-1 (Round 16, part 2): a song opened with files missing says so; File › Relink… lists them — Locate… is
    // the host's picker, then ONE `project relink <file> --to <picked>`: the found row fades out and the next slides up
    // into its place, each caught mid-way; Search a Folder… is ONE `project relink --folder <dir>`; with nothing missing
    // the card says so and shrinks, eased; File › Collect and Save is ONE line, said
    auto quote = [](const std::string &v) { return v.find_first_of(" \t\"") == std::string::npos && !v.empty() ? v : "\"" + v + "\""; }; // App's
    sltest::Rig r("ui-relink", 1280, 800);
    const std::string lib = r.dir + "/relink-lib", found = r.dir + "/relink-found";
    std::error_code ec;
    std::filesystem::remove_all(lib, ec);
    std::filesystem::remove_all(found, ec);
    std::filesystem::create_directories(lib);
    std::filesystem::create_directories(found + "/deep");
    for (const std::string &f : {lib + "/kick.wav", lib + "/hat.wav", found + "/deep/hat.wav", r.dir + "/relink-new-kick.wav"})
        std::ofstream(f, std::ios::binary) << "RIFF";
    const std::string song = r.song("Relink") + ".slp";
    r.cmd("project new " + song + " --bpm 120");
    r.cmd("clip add --src " + lib + "/kick.wav --at 0");
    r.cmd("clip add --src " + lib + "/hat.wav --at 4");
    r.cmd("project save");
    std::filesystem::remove(lib + "/kick.wav");
    std::filesystem::remove(lib + "/hat.wav");
    r.cmd("project close");
    r.settle();
    r.cmd("project open " + song);
    r.settle();
    assert(contains(r.app->toastText(), "2 files are missing") && contains(r.app->toastText(), "Relink"));
    const auto missing = r.svc->model().missing;
    assert(missing.size() == 2);
    const std::string kick = missing[0].path, hat = missing[1].path;

    auto &ms = r.app->project().bar().menus();
    auto fileItem = [&](const std::string &label) {
        for (int i = 0; i < (int)ms.menu(0).items.size(); ++i)
            if (ms.menu(0).items[(size_t)i].label == label) return i;
        return -1;
    };
    r.click(world(ms, ms.titleRect(0)));
    r.settle();
    assert(fileItem("Relink\xE2\x80\xA6") >= 0 && fileItem("Collect and Save") >= 0);
    r.click(world(ms, ms.itemRect(0, fileItem("Relink\xE2\x80\xA6"))));
    r.frame();
    r.frame();
    auto &rl = r.app->relinkSheet();
    assert(sentLine(r, "project files --missing"));                        // asked first: a file may have gone since
    assert(rl.isOpen() && rl.appearAmount() > 0.0 && rl.appearAmount() < 1.0); // it fades in …
    assert(rl.rowCount() == 2 && rl.rowAmount(kick) == 1.0 && rl.rowAmount(hat) == 1.0); // … its rows with it, not one by one
    r.settle();
    const double hatY = rl.rowY(hat), h2 = rl.cardHeight();
    assert(hatY > rl.rowY(kick));

    // Locate… on the kick: the host's picker, then ONE line; its row fades out, the hat slides up into its place
    std::string asked;
    r.app->onPickFile = [&](const std::string &title, std::function<void(const std::string &)> done) {
        asked = title;
        done(r.dir + "/relink-new-kick.wav");
    };
    r.click(rl.locateRect(kick));
    r.frame();
    r.frame();
    assert(asked == "Locate kick.wav" && sentLine(r, "project relink " + quote(kick) + " --to " + quote(r.dir + "/relink-new-kick.wav")));
    assert(rl.rowAmount(kick) > 0.0 && rl.rowAmount(kick) < 1.0);              // fading out where it is
    assert(rl.rowY(hat) < hatY && rl.rowY(hat) > rl.rowY(kick));               // sliding up, not there yet
    assert(rl.cardHeight() < h2 && rl.cardHeight() > h2 - hatY);               // the card shrinking with it, eased
    r.settle();
    assert(rl.rowCount() == 1 && rl.rowAmount(kick) < 0.0 && rl.rowY(hat) == 0.0 && rl.cardHeight() == h2 - hatY);

    // Search a Folder…: the host's folder picker, then ONE line; nothing missing — the card says so
    std::string where;
    r.app->onPickDirectory = [&](const std::string &title, std::function<void(const std::string &)> done) {
        where = title;
        done(found);
    };
    r.click(rl.searchRect());
    r.frame();
    assert(contains(where, "missing") && sentLine(r, "project relink --folder " + quote(found)));
    assert(r.svc->model().missing.empty());
    r.settle();
    assert(rl.rowCount() == 0);
    r.click(rl.doneRect());
    r.settle();
    assert(!rl.isOpen());

    // File › Collect and Save: ONE line, then said
    r.click(world(ms, ms.titleRect(0)));
    r.settle();
    r.click(world(ms, ms.itemRect(0, fileItem("Collect and Save"))));
    r.settle();
    assert(sentLine(r, "project collect --save") && contains(r.app->toastText(), "Collected") && !r.svc->model().dirty);
    pass("relink and collect from the File menu (R-COLLECT-1): opened with files missing, said; Relink… lists them — Locate… one line, the found row fading out and the next sliding up, caught mid-way; Search a Folder… one line; nothing missing said; Collect and Save one line, said");
}

static void test_the_video_window()
{
    // R-VIDEO-1 (Round 18, part 2): View › Show Video opens a window (faded in) with the frame at the model's video.time,
    // the host asked only when the time, the file or the box changed — at the box's size; outside the picture's span the
    // words say where the song is, the picture fading out and in over them, the words cross-fading; a jump
    // cross-dissolves, a step does not; playing asks frame after frame; resized, a frame at the new size; none, said;
    // Hide Video, and × relabels the menu
    sltest::Rig r("ui-video", 1280, 800);
    r.cmd("project new " + r.song("Film") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 64");
    r.cmd("video set " + r.dir + "/film.mp4 --offset 1"); // vd_1: ten seconds from 1 s (beat 2)
    r.settle();
    auto &ps = r.app->project();
    auto &ms = ps.bar().menus();
    auto viewItem = [&](const std::string &label) {
        for (int i = 0; i < (int)ms.menu(3).items.size(); ++i)
            if (ms.menu(3).items[(size_t)i].label == label) return i;
        return -1;
    };
    r.click(world(ms, ms.titleRect(3)));
    r.settle();
    assert(viewItem("Show Video") >= 0);
    r.click(world(ms, ms.itemRect(3, viewItem("Show Video"))));
    r.frame();
    r.frame();
    arstro::solaris_ui::FloatWindow *win = ps.windows().window("video");
    assert(win && win->appearAmount() > 0.0 && win->appearAmount() < 1.0); // faded in, not cut
    r.settle();
    arstro::solaris_ui::VideoPanel *vp = ps.windows().videoPanel();
    assert(vp && win->title() == "Video \xE2\x80\x94 film.mp4");
    // before it starts: said, nothing asked of the host
    assert(vp->words() == "Before the picture" && vp->pictureAmount() == 0.0 && r.videoAsks.empty());
    // the time bar at beat 4 (2 s): the picture's second 1 — asked once, at the box's size; the picture fades in
    r.cmd("transport seek 4");
    double mid = -1;
    for (int k = 0; k < 30 && mid < 0; ++k)
    {
        r.frame();
        const double a = vp->pictureAmount();
        if (a > 0.0 && a < 1.0) mid = a;
    }
    r.settle();
    const artboard::Rect box = vp->boxRect();
    assert(mid > 0.0 && vp->pictureAmount() == 1.0 && r.videoAsks.size() == 1);
    assert(r.videoAsks[0].file == r.svc->model().video.path && std::fabs(r.videoAsks[0].t - 1.0) < 1e-9);
    assert(r.videoAsks[0].w == (int)std::lround(box.w) && r.videoAsks[0].h == (int)std::lround(box.h) && std::fabs(vp->frameTime() - 1.0) < 1e-9);
    const artboard::Rect pic = vp->pictureRect(); // letterboxed: 16:9, centred in the box
    assert(std::fabs(pic.w / pic.h - 16.0 / 9.0) < 0.02 && std::fabs(pic.x + pic.w * 0.5 - box.w * 0.5) < 0.5 && std::fabs(pic.y + pic.h * 0.5 - box.h * 0.5) < 0.5);
    // a jump (to beat 12, 6 s: second 5) cross-dissolves; a step (an eighth of a beat) does not
    r.cmd("transport seek 12");
    double dmid = -1;
    for (int k = 0; k < 30 && dmid < 0; ++k)
    {
        r.frame();
        const double d = vp->dissolveAmount();
        if (d > 0.0 && d < 1.0) dmid = d;
    }
    r.settle();
    assert(dmid > 0.0 && vp->dissolveAmount() == 1.0 && std::fabs(vp->frameTime() - 5.0) < 1e-9);
    r.cmd("transport seek 12.125");
    bool stepped = true;
    for (int k = 0; k < 12; ++k)
    {
        r.frame();
        stepped = stepped && vp->dissolveAmount() == 1.0;
    }
    assert(stepped && std::fabs(r.videoAsks.back().t - 5.0625) < 1e-9);
    // nothing changed: nothing asked
    const size_t asked = r.videoAsks.size();
    r.pump(200);
    assert(r.videoAsks.size() == asked);
    // playing: frame after frame, each later than the last
    r.cmd("transport play");
    for (int k = 0; k < 400 && r.videoAsks.size() < asked + 4; ++k) { std::this_thread::sleep_for(std::chrono::milliseconds(16)); r.frame(); } // the rig plays at a quarter speed
    r.cmd("transport stop");
    assert(r.videoAsks.size() >= asked + 4);
    for (size_t i = asked + 1; i < r.videoAsks.size(); ++i) assert(r.videoAsks[i].t >= r.videoAsks[i - 1].t);
    // resized by its corner: a frame at the new size
    const artboard::Rect grip = world(*win, win->gripRect());
    r.drag(grip.x + grip.w * 0.5, grip.y + grip.h * 0.5, grip.x + grip.w * 0.5 + 120, grip.y + grip.h * 0.5 + 60);
    r.settle();
    assert(r.videoAsks.back().w == (int)std::lround(vp->boxRect().w) && r.videoAsks.back().w > r.videoAsks[0].w);
    // past its end (beat 30, 15 s): the picture fades out over the words, which cross-fade to "After"
    r.cmd("transport seek 30");
    double out = -1, words = -1;
    for (int k = 0; k < 30 && (out < 0 || words < 0); ++k)
    {
        r.frame();
        if (vp->pictureAmount() > 0.0 && vp->pictureAmount() < 1.0) out = vp->pictureAmount();
        if (vp->wordsAmount() > 0.0 && vp->wordsAmount() < 1.0) words = vp->wordsAmount();
    }
    r.settle();
    assert(out > 0.0 && words > 0.0 && vp->pictureAmount() == 0.0 && vp->words() == "After the picture");
    // none: said
    r.cmd("video remove");
    r.settle();
    assert(vp->words() == "No picture in this song" && win->title() == "Video");
    // Hide Video; reopened, × closes it and the menu says Show again
    r.click(world(ms, ms.titleRect(3)));
    r.settle();
    assert(viewItem("Hide Video") >= 0);
    r.click(world(ms, ms.itemRect(3, viewItem("Hide Video"))));
    r.settle();
    assert(!win->isOpen() && win->appearAmount() == 0.0 && viewItem("Show Video") >= 0);
    r.click(world(ms, ms.titleRect(3)));
    r.settle();
    r.click(world(ms, ms.itemRect(3, viewItem("Show Video"))));
    r.settle();
    assert(win->isOpen() && viewItem("Hide Video") >= 0);
    r.click(world(*win, win->closeRect()));
    r.settle();
    assert(!win->isOpen() && viewItem("Show Video") >= 0);
    pass("the video window (R-VIDEO-1): View › Show Video, faded in; the frame at the model's time, asked of the host once a change, at the box's size, letterboxed; the picture fading over the words — before, after, none; a jump dissolves, a step does not; playing, frame after frame; resized, a frame at the new size; Hide Video and × relabel the menu");
}

static void test_the_video_lane()
{
    // R-VIDEO-1 (Round 18, part 3): the picture's row between the ruler and the lanes — growing in with the lanes sliding
    // down under it; its span the model's beats; thumbnails asked of the host at the model's points, a few a frame, at the
    // row's height, each fading in; dragged, the pointer's with nothing sent, ONE `set vd_1.at=` on release, no jump; a
    // move made elsewhere eased there; removed, the row closing and the lanes sliding back
    sltest::Rig r("ui-video-lane", 1280, 800);
    r.cmd("project new " + r.song("Lane") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 32");
    r.settle();
    auto &tl = r.app->project().timeline();
    using arstro::solaris_ui::Timeline;
    const double row0 = tl.rowRect(0).y;
    assert(tl.videoRowAmount() == 0.0 && tl.top() == Timeline::kRulerH && row0 >= Timeline::kRulerH);
    r.cmd("video set " + r.dir + "/film.mp4 --offset 1"); // beat 2; its ten seconds end on beat 22
    double grow = -1, fadeIn = -1;
    size_t mostAFrame = 0;
    for (int k = 0; k < 40 && (grow < 0 || fadeIn < 0); ++k)
    {
        const size_t before = r.thumbAsks.size();
        r.frame();
        mostAFrame = std::max(mostAFrame, r.thumbAsks.size() - before);
        const double a = tl.videoRowAmount();
        if (a > 0.0 && a < 1.0)
        {
            grow = a;
            assert(std::fabs(tl.rowRect(0).y - (row0 + a * Timeline::kVideoH)) < 1e-6); // the lanes slide with it
        }
        const double f = tl.thumbAmount(0.0);
        if (f > 0.0 && f < 1.0) fadeIn = f;
    }
    r.settle();
    assert(grow > 0.0 && fadeIn > 0.0 && tl.videoRowAmount() == 1.0 && tl.rowRect(0).y == row0 + Timeline::kVideoH);
    assert(mostAFrame >= 1 && mostAFrame <= (size_t)Timeline::kThumbAsksPerFrame); // a few a frame: decodes never stall one
    artboard::Rect sr = tl.videoSpanRect();
    assert(std::fabs(sr.x - tl.beatToX(2.0)) < 1e-6 && std::fabs(sr.right() - tl.beatToX(22.0)) < 1e-6);
    // the thumbnails: at the model's points, the row's height, a few a frame, kept (none asked twice)
    const auto &pts = r.svc->model().video.thumbs;
    assert(pts.size() == 11 && r.thumbAsks.size() >= 3 && tl.thumbsDrawn() == (int)r.thumbAsks.size());
    std::set<long long> seen;
    for (const auto &a : r.thumbAsks)
    {
        bool onPoint = false;
        for (const auto &p : pts) onPoint = onPoint || std::fabs(p.time - a.t) < 1e-9;
        assert(onPoint && a.h == (int)std::lround(Timeline::kVideoH - 2.0 * Timeline::kThumbPad) && seen.insert(std::llround(a.t * 1000)).second);
    }
    // dragged two beats on: the pointer's while held, nothing sent; ONE line on release, and no jump after it
    const artboard::Rect wsr = world(tl, sr);
    const double y = wsr.y + wsr.h * 0.5, x0 = wsr.x + 30.0, dx = 2.0 * tl.pxPerBeat();
    const size_t sent0 = r.sent.size();
    r.drag(x0, y, x0 + dx, y, 8, false);
    assert(tl.videoDragging() && std::fabs(tl.videoAtLive() - 4.0) < 1e-9 && r.sent.size() == sent0);
    assert(std::fabs(tl.videoSpanRect().x - tl.beatToX(4.0)) < 1e-6);
    r.app->pointer(2, x0 + dx, y, 0, r.now);
    r.frame();
    assert(sentLine(r, "set vd_1.at=4") && r.sent.size() == sent0 + 1 && r.svc->model().video.offset == 2.0);
    assert(!tl.videoDragging() && tl.videoAtLive() == 4.0);
    r.frame();
    assert(tl.videoAtLive() == 4.0); // the model's answer is where it was let go: nothing moves
    // moved elsewhere (a shell's offset: 4 s is beat 8): eased there
    r.cmd("set vd_1.offset=4");
    double moving = -1;
    for (int k = 0; k < 30 && moving < 0; ++k)
    {
        r.frame();
        const double a = tl.videoAtLive();
        if (a > 4.0 && a < 8.0) moving = a;
    }
    r.settle();
    assert(moving > 0.0 && tl.videoAtLive() == 8.0);
    // removed: the row closes, the lanes slide back up
    r.cmd("video remove");
    double shut = -1;
    for (int k = 0; k < 30 && shut < 0; ++k)
    {
        r.frame();
        const double a = tl.videoRowAmount();
        if (a > 0.0 && a < 1.0) shut = a;
    }
    r.settle();
    assert(shut > 0.0 && tl.videoRowAmount() == 0.0 && tl.top() == Timeline::kRulerH && tl.rowRect(0).y == row0);
    pass("the video lane (R-VIDEO-1): the picture's row grows in between the ruler and the lanes, which slide with it; its span the model's beats; thumbnails at the model's points, the row's height, asked once, fading in; dragged the pointer's, ONE `set vd_1.at=` on release, no jump; moved elsewhere, eased; removed, closed");
}

static void test_export_with_the_video()
{
    // R-VIDEO-1 (Round 18, part 3): File › Export…'s "With the video" — greyed and inert while the song has no picture; on,
    // the Format is MP4 or MKV, the bit depth, quality and stems grey (eased), and Export… is ONE `render … --video`; off
    // again, the audio format it was left at
    sltest::Rig r("ui-export-video", 1280, 800);
    r.cmd("project new " + r.song("Scored") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");
    r.settle();
    std::string suggested;
    r.app->onPickSave = [&](const std::string &, const std::string &s, std::function<void(const std::string &)> done) {
        suggested = s;
        done(r.dir + "/Scored out" + s.substr(s.rfind('.')));
    };
    auto &ms = r.app->project().bar().menus();
    auto &ex = r.app->exportSheet();
    using ES = arstro::solaris_ui::ExportSheet;
    auto open = [&] {
        r.click(world(ms, ms.titleRect(0)));
        r.settle();
        r.click(world(ms, ms.itemRect(0, 5)));
        r.settle();
        assert(ex.isOpen());
    };
    auto pick = [&](int row, int i) {
        r.click(ex.controlRect(row));
        r.settle();
        assert(ex.comboOpen(row));
        r.click(ex.optionRect(row, i));
        r.settle();
    };
    open();
    assert(!ex.applies(ES::kVideo) && ex.rowAmount(ES::kVideo) == 0.0); // no picture: greyed …
    r.click(ex.controlRect(ES::kVideo));
    r.settle();
    assert(ex.flags() == "--format wav --bits 24");                      // … and inert
    r.key(27);
    r.settle();
    r.cmd("video set " + r.dir + "/score.mp4");
    r.settle();
    open();
    assert(ex.applies(ES::kVideo) && ex.rowAmount(ES::kVideo) == 1.0);
    r.click(ex.controlRect(ES::kVideo));
    r.frame();
    r.frame();
    assert(ex.rowAmount(ES::kBits) > 0.0 && ex.rowAmount(ES::kBits) < 1.0); // greying, eased
    r.settle();
    assert(ex.flags() == "--video" && ex.extension() == ".mp4" && ex.optionCount(ES::kFormat) == 2 && ex.optionLabel(ES::kFormat, 1) == "MKV");
    assert(ex.rowAmount(ES::kBits) == 0.0 && ex.rowAmount(ES::kQuality) == 0.0 && ex.rowAmount(ES::kStems) == 0.0 && ex.rowAmount(ES::kRate) == 1.0);
    pick(ES::kFormat, 1);
    pick(ES::kLoudness, 3);
    assert(ex.flags() == "--video --normalize -14 --ceiling -1" && ex.extension() == ".mkv");
    r.click(ex.exportRect());
    r.settle();
    assert(suggested == "Scored.mkv" && r.sent.back() == "render --out \"" + r.dir + "/Scored out.mkv\" --video --normalize -14 --ceiling -1");
    // off again: the audio format it was left at
    open();
    r.click(ex.controlRect(ES::kVideo));
    r.settle();
    assert(ex.flags() == "--format wav --bits 24 --normalize -14 --ceiling -1" && ex.optionCount(ES::kFormat) == 5);
    r.key(27);
    r.settle();
    pass("Export with the video (R-VIDEO-1): greyed and inert with no picture; on, MP4 or MKV, the bit depth, quality and stems greyed (eased), ONE `render … --video`; off, the audio format it was left at");
}

static void test_conflicts_are_asked_in_a_sheet()
{
    // R-COLLAB-1 (Round 19, part 3): the song's file changed by another window — reloaded, said in a toast; two nodes
    // changed both ways — merged, the Conflicts sheet opening on its own (faded in); Keep Mine and Take Theirs ONE line
    // each, the answered row fading out where it is and the next sliding up; the "all" buttons dimming, eased, with
    // nothing asked; Done; File › Conflicts… reopens it; Keep All Mine ONE line
    sltest::Rig r("ui-sync", 1280, 800);
    const std::string song = r.song("Shared") + ".slp";
    r.cmd("project new " + song + " --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4"); // ch_2
    r.cmd("project save");
    r.settle();
    arstro::solaris::SolarisService b(sltest::fakeHost(r.dir)); // the other window, on the same file
    auto other = [&b](const std::string &line) {
        std::string err;
        const bool ok = b.dispatchText(line, err);
        if (!ok) std::printf("    the other window refused `%s`: %s\n", line.c_str(), err.c_str());
        assert(ok);
    };
    auto gain = [&r]() {
        for (const auto &st : r.svc->model().strips)
            if (st.id == "ch_2") return st.gain;
        return 99.0;
    };
    other("project open " + song);
    // nothing unsaved here: reloaded, said
    other("set ch_2.gain=-6");
    other("project save");
    r.cmd("project sync");
    r.frame();
    assert(contains(r.app->toastText(), "Reloaded") && gain() == -6.0);
    r.settle();
    // the tempo and the strip changed both ways: merged, the sheet opening on its own
    r.cmd("set ch_2.gain=-3");
    r.cmd("set project.bpm=128");
    other("set ch_2.gain=-9");
    other("set project.bpm=140");
    other("project save");
    r.cmd("project sync");
    auto &cs = r.app->conflictsSheet();
    r.frame();
    r.frame();
    assert(cs.isOpen() && cs.appearAmount() > 0.0 && cs.appearAmount() < 1.0 && contains(r.app->toastText(), "2 to answer"));
    r.settle();
    assert(cs.rowCount() == 2 && cs.rowAmount("project:bpm") == 1.0 && cs.rowAmount("ch_2") == 1.0 && cs.allAmount() == 1.0);
    const double second = cs.rowY("ch_2");
    assert(cs.rowY("project:bpm") == 0.0 && second == arstro::solaris_ui::ConflictsSheet::kRowH);
    // what tells them apart: a header field whole; a strip's line, only the fields that differ
    assert(cs.shown("project:bpm", true) == std::vector<std::string>{"bpm = 128.0"} && cs.shown("project:bpm", false) == std::vector<std::string>{"bpm = 140.0"});
    assert(cs.shown("ch_2", true) == std::vector<std::string>{"gain=-3.0"} && cs.shown("ch_2", false) == std::vector<std::string>{"gain=-9.0"});
    // Keep Mine on the tempo: ONE line; its row fades out where it is, the strip's slides up into its place
    r.click(cs.mineRect("project:bpm"));
    r.frame();
    r.frame();
    assert(sentLine(r, "project sync --take mine project:bpm") && r.svc->project()->header.bpm == 128.0);
    assert(cs.rowAmount("project:bpm") > 0.0 && cs.rowAmount("project:bpm") < 1.0 && cs.rowY("ch_2") > 0.0 && cs.rowY("ch_2") < second);
    r.settle();
    assert(cs.rowAmount("project:bpm") == -1.0 && cs.rowY("ch_2") == 0.0);
    // Take Theirs on the strip: ONE line, the gain theirs; nothing asked — the "all" buttons dim, eased
    r.click(cs.theirsRect("ch_2"));
    r.frame();
    r.frame();
    assert(sentLine(r, "project sync --take theirs ch_2") && gain() == -9.0);
    assert(cs.allAmount() < 1.0 && cs.allAmount() > 0.35);
    r.settle();
    assert(cs.rowCount() == 0 && cs.allAmount() == 0.35 && r.svc->model().sync.conflicts.empty());
    r.click(cs.allMineRect()); // with nothing asked, they send nothing
    r.settle();
    assert(r.sent.back() == "project sync --take theirs ch_2");
    r.click(cs.doneRect());
    r.settle();
    assert(!cs.isOpen());
    // another round: File › Conflicts… reopens it; Keep All Mine — ONE line
    r.cmd("set ch_2.gain=-1");
    other("project sync");
    other("set ch_2.gain=-12");
    other("project save");
    r.cmd("project sync");
    r.settle();
    r.click(cs.doneRect());
    r.settle();
    assert(!cs.isOpen() && r.svc->model().sync.conflicts.size() == 1);
    auto &ms = r.app->project().bar().menus();
    r.click(world(ms, ms.titleRect(0)));
    r.settle();
    const int item = (int)ms.menu(0).items.size() - 1;
    assert(ms.menu(0).items[(size_t)item].label == "Conflicts\xE2\x80\xA6");
    r.click(world(ms, ms.itemRect(0, item)));
    r.settle();
    assert(cs.isOpen() && cs.rowCount() == 1);
    r.click(cs.allMineRect());
    r.settle();
    assert(sentLine(r, "project sync --take mine") && gain() == -1.0 && cs.rowCount() == 0);
    r.key(27);
    r.settle();
    assert(!cs.isOpen());
    pass("conflicts asked in a sheet (R-COLLAB-1): a reload said; a merge that asks opens the sheet, faded in; Keep Mine / Take Theirs ONE line each, the answered row fading where it is and the next sliding up; the \"all\" buttons dimming, eased, inert with nothing asked; File › Conflicts… reopens it; Keep All Mine ONE line");
}

static void test_the_ui_scale_and_high_contrast()
{
    // R-ACCESS-1 (Round 20, part 1): Settings › Interface — the UI scale and High contrast ONE line each; the shell zooms
    // there (caught mid-way, the logical size the window over the DRAWN scale) and a click lands where it is drawn; the
    // tokens fade to black and white (caught mid-way); and back, eased, from a shell's line
    sltest::Rig r("ui-access", 1280, 800);
    r.cmd("project new " + r.song("Big") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");
    r.settle();
    namespace palette = arstro::solaris_ui::palette;
    using arstro::solaris_ui::SettingsSheet;
    auto phys = [&r](artboard::Rect q) { // the rig clicks in window pixels: a logical place, at the drawn scale
        const double k = r.app->drawnUiScale();
        return artboard::Rect{q.x * k, q.y * k, q.w * k, q.h * k};
    };
    r.app->openSettings();
    r.settle();
    auto &sheet = r.app->settings();
    r.click(sheet.railRect(SettingsSheet::kInterface));
    r.settle();
    // 150 %: ONE line; the shell zooms there, laid out each frame at the scale drawn
    r.click(sheet.controlRect(SettingsSheet::kUiScale));
    r.settle();
    r.click(sheet.optionRect(SettingsSheet::kUiScale, 4));
    r.frame();
    r.frame();
    const double mid = r.app->drawnUiScale();
    assert(sentLine(r, "settings set uiScale=150") && mid > 1.0 && mid < 1.5 && std::fabs(r.app->logicalWidth() - 1280.0 / mid) < 1e-6);
    r.settle();
    assert(r.app->drawnUiScale() == 1.5 && std::fabs(r.app->logicalWidth() - 1280.0 / 1.5) < 1e-6 && std::fabs(r.app->logicalHeight() - 800.0 / 1.5) < 1e-6);
    {
        // DRAWN at the scale: the playhead (red) at 1.5 × its logical place, not at the logical place itself
        r.app->settings().close();
        r.settle();
        auto &tl = r.app->project().timeline();
        const artboard::Rect at = world(tl, artboard::Rect{tl.beatToX(0.0), 120.0, 0.0, 0.0});
        auto redNear = [&r](double x, double y) {
            for (int dx = -1; dx <= 1; ++dx)
            {
                const uint32_t p = r.pixel((int)std::floor(x) + dx, (int)y);
                const int R = (int)((p >> 16) & 0xFF), G = (int)((p >> 8) & 0xFF);
                if (R > G + 60) return true;
            }
            return false;
        };
        assert(redNear(at.x * 1.5, at.y * 1.5) && !redNear(at.x, at.y * 1.5));
        r.app->openSettings();
        r.settle();
        r.click(phys(sheet.railRect(SettingsSheet::kInterface)));
        r.settle();
    }
    const double themedBefore = arstro::solaris_ui::sharedTheme().tab.label.color.r; // the shared theme, built at normal
    // High contrast: ONE line; every token fades toward it
    r.click(phys(sheet.controlRect(SettingsSheet::kContrast)));
    r.frame();
    r.frame();
    const double c = r.app->contrastAmount();
    const double fgMid = palette::foreground().r;
    assert(sentLine(r, "settings set highContrast=on") && c > 0.0 && c < 1.0 && fgMid > 0xDB / 255.0 && fgMid < 1.0);
    r.settle();
    assert(r.app->contrastAmount() == 1.0 && palette::foreground().r == 1.0 && palette::background().r == 0.0 && palette::border().a > 0.4);
    assert(themedBefore < 0.5 && arstro::solaris_ui::sharedTheme().tab.label.color.r > 0.7); // the shared theme (Artboard's widgets) follows
    // a click lands where it is drawn: the File menu's title, at its place in the window
    r.key(27);
    r.settle();
    auto &ms = r.app->project().bar().menus();
    r.click(phys(world(ms, ms.titleRect(0))));
    r.settle();
    assert(ms.openIndex() == 0);
    r.click(phys(artboard::Rect{600.0, 500.0, 0.0, 0.0})); // away: closed
    r.settle();
    // back from a shell: eased the other way
    r.cmd("settings set uiScale=100 highContrast=off");
    r.frame();
    r.frame();
    assert(r.app->drawnUiScale() > 1.0 && r.app->drawnUiScale() < 1.5 && r.app->contrastAmount() > 0.0 && r.app->contrastAmount() < 1.0);
    r.settle();
    assert(r.app->drawnUiScale() == 1.0 && r.app->contrastAmount() == 0.0 && palette::foreground().r == 0xDB / 255.0 && r.app->logicalWidth() == 1280.0);
    pass("the UI scale and high contrast (R-ACCESS-1): Settings › Interface ONE line each; the shell zooms, laid out at the drawn scale, a click landing where it is drawn; the tokens fade to black and white; back from a shell, eased");
}

static void test_the_session_view()
{
    // R-LAUNCH-1 (Round 17, part 4): View › Show Session cross-fades the grid into the lanes' place; every gesture is ONE
    // line — an empty cell's menu (the song's patterns) → `slot add`, "+ Scene" → `scene add`, a slot → `launch` (stopped:
    // it plays at once, its fill easing in), a queued slot's pulse fading in and, stopped first, out; ■ → `launch stop`;
    // a scene's ▶ → `launch <sc>`; Back to Arrangement → `launch back`; a slot's Delete → `slot delete`; and back again
    sltest::Rig r("ui-session", 1280, 800);
    r.cmd("project new " + r.song("Session") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");                // ch_2, pt_1
    r.cmd("pattern new --name Lead --length 1");                           // pt_2
    r.cmd("scene add Verse");                                              // sc_1
    r.settle();
    auto &ps = r.app->project();
    auto &sv = ps.session();
    auto &ms = ps.bar().menus();
    auto &menu = r.app->menu();
    auto viewItem = [&](const std::string &label) {
        for (int i = 0; i < (int)ms.menu(3).items.size(); ++i)
            if (ms.menu(3).items[(size_t)i].label == label) return i;
        return -1;
    };
    r.click(world(ms, ms.titleRect(3)));
    r.settle();
    assert(viewItem("Show Session") >= 0);
    r.click(world(ms, ms.itemRect(3, viewItem("Show Session"))));
    r.frame();
    r.frame();
    assert(ps.sessionAmount() > 0.0 && ps.sessionAmount() < 1.0);           // cross-fading, not cut
    r.settle();
    assert(ps.sessionAmount() == 1.0);
    // an empty cell: the song's patterns → ONE line
    r.click(world(sv, sv.cellRect("sc_1", "ch_2")));
    r.settle();
    assert(menu.isOpen() && menuItem(r, "Lead") >= 0);
    r.click(menu.itemRect(menuItem(r, "Lead")));
    r.settle();
    assert(sentLine(r, "slot add sc_1 ch_2 --pattern pt_2") && r.svc->model().slots.size() == 1);
    r.click(world(sv, sv.addSceneRect()));
    r.settle();
    assert(sentLine(r, "scene add") && r.svc->model().scenes.size() == 2);
    // the slot, stopped: ONE launch; the transport starts and it plays at once — its fill easing in
    r.click(world(sv, sv.cellRect("sc_1", "ch_2")));
    double mid = -1;
    for (int k = 0; k < 30 && mid < 0; ++k)
    {
        r.frame();
        const double a = sv.playAmount("sl_1");
        if (a > 0.0 && a < 1.0) mid = a;
    }
    assert(sentLine(r, "launch sl_1") && r.svc->model().transport.playing && mid > 0.0);
    r.settle();
    assert(sv.playAmount("sl_1") == 1.0);
    // another slot, launched while playing: queued for the next bar — its pulse fades in; stopped first, it fades out
    r.cmd("slot add sc_2 ch_2 --pattern pt_1");                             // sl_2
    r.settle();
    r.click(world(sv, sv.cellRect("sc_2", "ch_2")));
    r.frame();
    r.frame();
    assert(sentLine(r, "launch sl_2") && sv.queuedAmount("sl_2") > 0.0 && sv.queuedAmount("sl_2") < 1.0);
    r.settle();
    assert(sv.queuedAmount("sl_2") == 1.0 && sv.playAmount("sl_2") == 0.0); // queued, not playing yet …
    assert(sv.playAmount("sl_1") == 1.0);                                   // … the first plays on until its bar
    r.click(world(sv, sv.stopRect("ch_2")));
    r.frame();
    r.frame();
    assert(sentLine(r, "launch stop ch_2") && sv.queuedAmount("sl_2") < 1.0);
    r.settle();
    assert(sv.queuedAmount("sl_2") == 0.0);
    r.click(world(sv, sv.sceneLaunchRect("sc_1")));
    r.settle();
    assert(sentLine(r, "launch sc_1"));
    r.click(world(sv, sv.backRect()));
    r.settle();
    assert(sentLine(r, "launch back"));
    // a slot's menu: Delete
    {
        const artboard::Rect c = world(sv, sv.cellRect("sc_2", "ch_2"));
        r.click(c.x + c.w * 0.5, c.y + c.h * 0.5, 2);
        r.settle();
        assert(menu.isOpen() && menuItem(r, "Delete Slot") >= 0);
        r.click(menu.itemRect(menuItem(r, "Delete Slot")));
        r.settle();
        assert(sentLine(r, "slot delete sl_2") && r.svc->model().slots.size() == 1);
    }
    r.cmd("transport stop");
    r.click(world(ms, ms.titleRect(3)));
    r.settle();
    r.click(world(ms, ms.itemRect(3, viewItem("Show Arrangement"))));
    r.settle();
    assert(ps.sessionAmount() == 0.0);
    pass("the session view (R-LAUNCH-1): View › Session cross-fades in; an empty cell's patterns, + Scene, a slot, ■, a scene's ▶, Back to Arrangement, Delete — ONE line each; a launched slot's fill eases in, a queued one's pulse fades in and out; and back");
}

static void test_a_tuners_window()
{
    // R-FX-17 (Round 12, part 3): a tuner's window — a band under its header: the note large, a needle for the cents
    // easing to each reading, the rest small; no pitch: a dash, the needle home to the centre, faded. Drawn from the
    // model only: the window's own (stopped: no pitch), then a panel bound to the service's model with what a tuner
    // hears put in, advanced on a clock — each change caught mid-tween
    sltest::Rig r("ui-tuner", 1440, 900);
    r.cmd("project new " + r.song("Tuner") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");                // ch_2, dv_1
    r.cmd("device add ch_2 --type tuner");                                 // dv_2
    r.settle();
    auto &wl = r.app->project().windows();
    wl.openDevice("dv_2");
    r.settle();
    using DP = arstro::solaris_ui::DevicePanel;
    auto &w = *wl.devicePanel("dv_2");
    assert(w.readingsH() == DP::kReadingsH && w.bodyTop() >= DP::kHeaderH + DP::kReadingsH); // its rows below the band
    assert(w.noteText() == "\xE2\x80\x94" && w.pitchedAmount() == 0.0 && w.needleCents() == 0.0 && w.readingsText().find("no pitch") == 0);
    arstro::solaris::AppModel m = r.svc->model();
    auto put = [&](double hz, double note, double ct) {
        for (auto &st : m.strips)
            for (auto &d : st.devices)
                if (d.id == "dv_2") { d.readings[0].value = hz; d.readings[1].value = note; d.readings[2].value = ct; d.readings[3].value = -9.0; }
    };
    DP p("dv_2");
    p.width.set(420.0);
    p.height.set(320.0);
    put(0.0, 0.0, 0.0);
    p.bind(m, false);
    p.advance(0.0);
    p.layout();
    assert(p.needleCents() == 0.0 && p.pitchedAmount() == 0.0);
    // it hears A4 23 cents sharp: the needle and the light ease there
    put(446.0, 69.0, 23.45);
    p.bind(m, false);
    p.advance(1000.0);
    p.advance(1050.0);
    const double mid = p.needleCents(), lit = p.pitchedAmount();
    assert(mid > 0.0 && mid < 23.45 && lit > 0.0 && lit < 1.0);
    p.advance(1400.0);
    assert(std::fabs(p.needleCents() - 23.45) < 1e-9 && p.pitchedAmount() == 1.0);
    assert(p.noteText() == "A4" && p.readingsText() == "446.0 Hz \xC2\xB7 +23 ct \xC2\xB7 \xE2\x88\x92" "9.0 dB");
    put(440.0, 69.0, -0.3); // a hair flat rounds to 0 ct — never "−0"
    p.bind(m, false);
    assert(contains(p.readingsText(), "\xC2\xB7 0 ct \xC2\xB7"));
    put(446.0, 69.0, 23.45);
    p.bind(m, false);
    p.advance(1400.0);
    // the pitch goes: a dash at once (a text has no in-between), the needle home and the light out, eased
    put(0.0, 0.0, 0.0);
    p.bind(m, false);
    p.advance(2000.0);
    p.advance(2050.0);
    assert(p.noteText() == "\xE2\x80\x94" && p.needleCents() > 0.0 && p.needleCents() < 23.45 && p.pitchedAmount() > 0.0 && p.pitchedAmount() < 1.0);
    p.advance(2400.0);
    assert(p.needleCents() == 0.0 && p.pitchedAmount() == 0.0 && p.readingsText().find("no pitch") == 0);
    // a device that measures nothing has no band
    DP s("dv_1");
    s.analogFace = false;
    s.bind(m, false);
    s.advance(0.0);
    assert(s.readingsH() == 0.0 && s.bodyTop() == DP::kHeaderH);
    std::printf("    the needle mid-tween at %.2f ct (its target 23.45), the light %.2f\n", mid, lit);
    pass("a tuner's window (R-FX-17): a band under the header — the note large, the needle easing to each reading (caught mid-tween, both ways), the rest small; no pitch a dash; a device that measures nothing has none");
}

static void test_a_plugins_device_window()
{
    // R-VST-6 (N26): a plugin's device window — its parameter list (an instrument's too: its face is its own editor),
    // "Editor" where Presets stands sending `device editor` (lit, eased, while its window is open; again, `--close`),
    // and offline a band sliding open under the header — why, and Reload (`device reload`) — the rows pushed with it;
    // each caught mid-tween
    sltest::Rig r("ui-plugin", 1440, 900);
    sltest::rigPluginCrash().clear();
    r.cmd("plugins scan");
    r.cmd("project new " + r.song("Plugin") + ".slp --bpm 120");
    r.cmd("clip add --plugin \"Sweet Pad\" --at 0 --length 4");          // ch_2, dv_1
    r.cmd("device add ch_2 --plugin \"Air Verb\"");                       // dv_2
    r.settle();
    auto &wl = r.app->project().windows();
    wl.openDevice("dv_1");
    r.settle();
    auto &p = *wl.devicePanel("dv_1");
    assert(p.isPlugin() && !p.face() && p.rowCount() == 2 && p.rowParam(0) == "p1" && p.rowParam(1) == "p2");
    r.click(world(p, p.editorRect()));
    r.frame();
    r.frame();
    assert(sentLine(r, "device editor dv_1") && p.editorAmount() > 0.0 && p.editorAmount() < 1.0);
    r.settle();
    assert(p.editorAmount() == 1.0);
    r.click(world(p, p.editorRect()));
    r.settle();
    assert(sentLine(r, "device editor dv_1 --close") && p.editorAmount() == 0.0);
    // it crashes: the band slides open, the rows with it
    const double top0 = p.bodyTop();
    const double row0 = p.rowRect(0).y;
    sltest::rigPluginCrash() = "0123456789ABCDEF0123456789ABCDEF";
    r.frame();
    r.frame();
    r.frame();
    assert(p.offlineAmount() > 0.0 && p.offlineAmount() < 1.0 && p.bodyTop() > top0 && p.bodyTop() < top0 + arstro::solaris_ui::DevicePanel::kOfflineH);
    r.settle();
    assert(p.offlineAmount() == 1.0 && std::fabs(p.bodyTop() - top0 - arstro::solaris_ui::DevicePanel::kOfflineH) < 1e-9);
    assert(p.rowRect(0).y > row0);
    const arstro::solaris::DeviceModel *d = nullptr;
    for (const auto &s : r.svc->model().strips)
        for (const auto &x : s.devices)
            if (x.id == "dv_1") d = &x;
    assert(d && d->offline && d->why == "its process ended (Segmentation fault)");
    // Reload: one line; playing again, the band slides shut
    sltest::rigPluginCrash().clear();
    r.click(world(p, p.reloadRect()));
    r.frame();
    r.frame();
    assert(sentLine(r, "device reload dv_1") && p.offlineAmount() > 0.0 && p.offlineAmount() < 1.0);
    r.settle();
    assert(p.offlineAmount() == 0.0 && p.bodyTop() == top0);
    // an effect plugin: its list too, Remove still there
    wl.openDevice("dv_2");
    r.settle();
    auto &q = *wl.devicePanel("dv_2");
    assert(q.isPlugin() && q.rowCount() == 3 && q.rowParam(0) == "p7");
    pass("a plugin's device window (R-VST-6, N26): its list, Editor sending device editor (lit, eased; again --close), offline a band sliding open with why and Reload (device reload), the rows pushed with it — each caught mid-tween");
}

static void test_midi_learn_in_the_window()
{
    // R-CTRL-1 (N27 part 2): a fader's menu — and a device row's — offers Learn MIDI CC (one line); the song bar's pill
    // eases in saying for what, a click on it cancels (one line) and it eases out; Forget CC <n> where one is mapped;
    // Settings › MIDI lists the mappings — one the song lacks dimmed — × forgets one (one line), its row shrinking out
    sltest::Rig r("ui-learn", 1440, 900);
    r.cmd("project new " + r.song("Learn") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4");                 // ch_2, dv_1
    r.settle();
    auto &d = r.app->project().dock();
    auto &bar = r.app->project().bar();
    menuPick(r, world(d, d.faderRect("ch_2")), "Learn MIDI CC");
    r.frame();
    r.frame();
    assert(sentLine(r, "map learn ch_2.gain") && bar.learnAmount() > 0.0 && bar.learnAmount() < 1.0 && bar.learnShown() == "ch_2.gain");
    r.settle();
    assert(bar.learnAmount() == 1.0);
    r.click(bar.hitRect(arstro::solaris_ui::SongBar::kLearn));
    r.frame();
    r.frame();
    assert(sentLine(r, "map learn --cancel") && bar.learnAmount() < 1.0 && bar.learnAmount() > 0.0);
    r.settle();
    assert(bar.learnAmount() == 0.0 && r.svc->model().mapLearning.empty());
    // mapped: Forget CC <n> in its menu — and a device row's menu offers Learn too
    r.cmd("map add 74 ch_2.gain");
    r.settle();
    r.click(world(d, d.faderRect("ch_2")), 2);
    r.settle();
    assert(menuItem(r, "Forget CC 74") >= 0 && menuItem(r, "Learn MIDI CC") >= 0);
    r.click(r.app->menu().itemRect(menuItem(r, "Forget CC 74")));
    r.settle();
    assert(sentLine(r, "map remove 74") && r.svc->model().settings.mappings.empty());
    auto &wl = r.app->project().windows();
    wl.openDevice("dv_1");
    r.settle();
    auto &p = *wl.devicePanel("dv_1");
    p.reveal("volume");
    r.settle();
    const int vol = p.face()->controlIndex("volume");
    menuPick(r, world(*p.face(), p.face()->dialRect(vol)), "Learn MIDI CC");
    assert(sentLine(r, "map learn dv_1.volume"));
    r.cmd("map learn --cancel");
    // Settings › MIDI: the mappings, one inert; × forgets one — its row shrinking out, caught mid-way
    r.cmd("map add 74 ch_2.gain");
    r.cmd("map add 21 dv_1.filter.cutoff");
    r.cmd("project new " + r.song("Learn2") + ".slp --bpm 120");               // dv_1 is not in this song
    r.cmd("clip add --src kick.wav --at 0");                                   // ch_2 is
    r.settle();
    r.app->openSettings();
    r.settle();
    auto &sh = r.app->settings();
    r.click(sh.railRect(arstro::solaris_ui::SettingsSheet::kMidi));
    r.settle();
    const auto &maps = r.svc->model().settings.mappings;
    assert(maps.size() == 2 && !maps[0].inert && maps[1].inert);
    assert(sh.mapRowAmount("74") == 1.0 && sh.mapRowAmount("21") == 1.0);
    r.click(sh.mapRemoveRect(1));
    r.frame();
    r.frame();
    assert(sentLine(r, "map remove 21") && sh.mapRowAmount("21") > 0.0 && sh.mapRowAmount("21") < 1.0);
    r.settle();
    assert(sh.mapRowAmount("21") == 0.0 && sh.mapRowAmount("74") == 1.0);
    pass("MIDI learn in the window (R-CTRL-1): Learn MIDI CC in a fader's and a knob's menu (one line); the song bar's pill eases in, a click cancels, it eases out; Forget CC; Settings › MIDI lists them, one inert, × forgets one, its row shrinking out");
}

static void test_a_surface_chosen_in_settings()
{
    // R-CTRL-2 (N28 part 2): Settings › MIDI — the control surface and its port are combos (each choice one `settings
    // set` line); the row's note says whether it is connected — cross-fading to its new text — and why not
    sltest::Rig r("ui-surface", 1440, 900);
    r.cmd("project new " + r.song("Surface") + ".slp --bpm 120");
    r.settle();
    r.app->openSettings();
    r.settle();
    using arstro::solaris_ui::SettingsSheet;
    auto &sh = r.app->settings();
    r.click(sh.railRect(SettingsSheet::kMidi));
    r.settle();
    assert(contains(sh.rowNote(SettingsSheet::kSurface), "Mackie Control or HUI"));
    auto pick = [&](int row, const std::string &label) {
        r.click(sh.controlRect(row));
        r.settle();
        int k = -1;
        for (int i = 0; i < sh.optionCount(row); ++i)
            if (sh.optionLabel(row, i) == label) k = i;
        assert(k >= 0);
        r.click(sh.optionRect(row, k));
    };
    pick(SettingsSheet::kSurface, "Mackie Control");
    r.frame();
    r.frame();
    assert(sentLine(r, "settings set surface=mcu") && sh.surfaceNoteAmount() > 0.0 && sh.surfaceNoteAmount() < 1.0);
    r.settle();
    assert(sh.rowNote(SettingsSheet::kSurface) == "not connected \xE2\x80\x94 no port \xE2\x80\x94 `settings set surfacePort=<port>`");
    pick(SettingsSheet::kSurfacePort, "Keystation 49");
    r.settle();
    assert(sentLine(r, "settings set \"surfacePort=Keystation 49\"") && sh.shownValue(SettingsSheet::kSurfacePort) == "Keystation 49");
    assert(sh.rowNote(SettingsSheet::kSurface) == "connected \xC2\xB7 strips 1\xE2\x80\x93" "8" && r.svc->model().surface.connected); // (a hex escape is greedy)
    pick(SettingsSheet::kSurface, "None");
    r.settle();
    assert(sentLine(r, "settings set surface=none") && !r.svc->model().surface.connected);
    pass("a surface chosen in Settings (R-CTRL-2): Settings › MIDI's Control surface and Surface port combos — one line each; the note says connected (its strips) or why not, cross-fading");
}

static void test_a_strip_freezes_from_its_menu()
{
    // R-PERF-3: the strip's menu — Freeze and Bounce on a strip nothing feeds, Unfreeze on a frozen one, each ONE line;
    // the rack recedes under the FROZEN pill, eased in and out
    sltest::Rig r("ui-freeze", 1280, 800);
    r.cmd("project new " + r.song("Freeze") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 4"); // ch_2
    r.cmd("strip add --kind bus --name Verb --mixer mx_1"); // ch_3, beside it, fed by ch_2's send
    r.cmd("send add ch_2 --to ch_3");
    r.settle();
    auto &d = r.app->project().dock();
    auto &menu = r.app->menu();
    auto open = [&](const std::string &ch) {
        const artboard::Rect card = world(d, d.cardRect(ch));
        r.click(card.x + 20.0, card.y + 16.0, 2);
        r.settle();
        assert(menu.isOpen());
    };
    // the bus is fed: neither offered
    open("ch_3");
    assert(menuItem(r, "Freeze") < 0 && menuItem(r, "Bounce to a new strip") < 0 && menuItem(r, "Unfreeze") < 0);
    r.click(5.0, 790.0); // away: the menu closes (Escape closes only a rename)
    r.settle();
    assert(!menu.isOpen());
    assert(d.frozenAmount("ch_2") == 0.0);
    open("ch_2");
    assert(menuItem(r, "Freeze") >= 0 && menuItem(r, "Bounce to a new strip") >= 0 && menuItem(r, "Unfreeze") < 0);
    r.click(menu.itemRect(menuItem(r, "Freeze")));
    assert(r.sent.back() == "strip freeze ch_2" && !r.svc->model().strips.empty());
    bool frozen = false;
    for (const auto &st : r.svc->model().strips) frozen |= st.id == "ch_2" && !st.frozen.empty();
    assert(frozen);
    r.frame();
    r.frame();
    const double in = d.frozenAmount("ch_2");
    assert(in > 0.0 && in < 1.0); // it recedes, eased
    r.settle();
    assert(d.frozenAmount("ch_2") == 1.0);
    // frozen: Unfreeze instead of Freeze; Bounce still offered
    open("ch_2");
    assert(menuItem(r, "Unfreeze") >= 0 && menuItem(r, "Freeze") < 0 && menuItem(r, "Bounce to a new strip") >= 0);
    r.click(menu.itemRect(menuItem(r, "Unfreeze")));
    assert(r.sent.back() == "strip unfreeze ch_2");
    r.frame();
    r.frame();
    const double out = d.frozenAmount("ch_2");
    assert(out > 0.0 && out < 1.0); // it returns, eased
    r.settle();
    assert(d.frozenAmount("ch_2") == 0.0);
    // bounce: a new strip
    const size_t strips = r.svc->model().strips.size();
    open("ch_2");
    r.click(menu.itemRect(menuItem(r, "Bounce to a new strip")));
    r.settle();
    assert(r.sent.back() == "strip bounce ch_2" && r.svc->model().strips.size() == strips + 1);
    pass("freeze from the mixer (R-PERF-3): Freeze and Bounce on a strip nothing feeds, Unfreeze on a frozen one — one line each; the rack recedes under FROZEN, eased in and out");
}

static void test_settings_puts_a_port_on_a_device()
{
    // R-DEV-1/5, Settings › Audio's PORTS: the open song's ports, each on its device — a click on Phones' field lists the
    // clock and every output; a pick is ONE `settings set port.` line, its field then names the device and its line
    // cross-fades to say where it plays now; a port added grows in, one removed shrinks out — caught mid-ease
    sltest::Rig r("ui-ports", 1280, 800);
    r.cmd("project new " + r.song("Ports") + ".slp --bpm 120");
    r.cmd("port add Phones --dir out");
    r.cmd("devices list");
    r.app->openSettings();
    r.settle();
    auto &st = r.app->settings();
    auto &menu = r.app->menu();
    assert(st.portRowAmount("Main") == 1.0 && st.portRowAmount("Phones") == 1.0);
    assert(st.portDeviceLabel("Phones") == "Clock device" && st.portNote("Phones") == "plays on the clock device");
    assert(st.portFieldRect("Phones").w > 0);
    st.revealRect(st.portFieldRect("Phones")); // under the devices: the page scrolls to it
    r.settle();
    const artboard::Rect f = st.portFieldRect("Phones");
    r.click(f.x + f.w * 0.5, f.y + f.h * 0.5);
    r.settle();
    assert(menu.isOpen() && menu.itemCount() == 4); // the clock, and the rig's three outputs
    assert(menu.item(0).label == "Clock device  \xC2\xB7 now");
    int usb = -1;
    for (int i = 0; i < menu.itemCount(); ++i)
        if (menu.item(i).label == "Scarlett 2i2 USB") usb = i;
    assert(usb > 0);
    r.click(menu.itemRect(usb));
    assert(r.sent.back() == "settings set port.Phones=usb_interface:0");
    r.frame();
    r.frame();
    const double mid = st.portNoteAmount("Phones");
    assert(mid > 0.0 && mid < 1.0); // its line cross-fading
    r.settle();
    assert(st.portDeviceLabel("Phones") == "Scarlett 2i2 USB" && st.portNoteAmount("Phones") == 1.0);
    assert(st.portNote("Phones") == "plays on Scarlett 2i2 USB from channel 1, held to the clock");
    // playing: Main's line says how the clock is (the rig's quiet device)
    r.cmd("transport play");
    r.settle();
    const std::string playingNote = st.portNote("Main");
    r.cmd("transport stop");
    r.settle();
    assert(playingNote.rfind("the clock \xC2\xB7 ", 0) == 0 && playingNote.find(" ms") != std::string::npos);
    assert(st.portNote("Main") == "plays on the clock device");
    // a port added grows in; undone, it shrinks out
    r.cmd("port add Cue --dir out");
    r.frame();
    r.frame();
    const double in = st.portRowAmount("Cue");
    assert(in > 0.0 && in < 1.0);
    r.settle();
    assert(st.portRowAmount("Cue") == 1.0);
    r.cmd("undo"); // the port gone again
    r.frame();
    r.frame();
    const double out = st.portRowAmount("Cue");
    assert(out > 0.0 && out < 1.0);
    r.settle();
    assert(st.portRowAmount("Cue") == 0.0);
    pass("Settings › Audio's ports (R-DEV-1/5): each port on its device — a pick from the field's list is one `settings set port.` line, the field naming the device, its line cross-fading; a port added grows in, undone shrinks out");
}

static void test_the_cpu_in_the_song_bar()
{
    // R-PERF-1: playing, the song bar's CPU readout catches up with the model's load — caught on the way — and, stopped,
    // eases back to 0 rather than dropping
    sltest::Rig r("ui-cpu", 1440, 900);
    r.cmd("project new " + r.song("Cpu") + ".slp --bpm 120");
    r.cmd("clip add --instrument synth --at 0 --length 16");
    r.cmd("notes add pt_1 \"C4@0:16 E4@0:16 G4@0:16\"");
    r.settle();
    auto &bar = r.app->project().bar();
    assert(bar.cpuShown() == 0.0 && bar.cpuRect().w > 0.0);
    r.cmd("transport play");
    for (int k = 0; k < 400 && r.svc->model().transport.cpu <= 0.0f; ++k)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        r.frame();
    }
    assert(r.svc->model().transport.cpu > 0.0f);
    r.frame();
    assert(bar.cpuShown() > 0.0 && bar.cpuShown() < r.svc->model().transport.cpu); // on its way up
    r.cmd("transport stop");
    r.frame();
    r.frame();
    assert(bar.cpuShown() > 0.0);                                                    // on its way down
    r.settle();
    assert(bar.cpuShown() == 0.0);
    pass("the CPU in the song bar (R-PERF-1): playing, the readout catches up with the load (caught on its way); stopped, it eases back to 0");
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    arstro::cosmo_v2::registerEmbeddedFonts();
    test_new_song_cross_fades_from_home();
    test_settings_combos_send_lines_and_ease();
    test_settings_in_pages();
    test_settings_folders();
    test_song_bar_and_keys();
    test_the_tempo_is_set_in_the_song_bar();
    test_space_returns_and_ctrl_space_stays();
    test_the_keys_are_the_machines();
    test_the_keyboard_section();
    test_the_history_tree();
    test_clips_are_copied_by_shift_drag_and_ctrl_c_v();
    test_the_pointer_says_a_drag_resizes();
    test_a_looping_automation_is_seen_and_made();
    test_a_recovered_song_is_offered();
    test_a_midi_file_dropped_on_a_track();
    test_home_unsaved_confirm_and_recents();
    test_browser_tabs_and_sample_drag();
    test_instrument_drop_and_clip_drag();
    test_an_instruments_face_is_an_analog_panel();
    test_a_lane_is_an_instruments_track();
    test_midi_clips_are_made_looped_moved_and_listed();
    test_ruler_seek_keys_and_selection();
    test_the_mixers_numbers_bind_from_the_dock();
    test_ids_shown_and_copied();
    test_lists_travel_when_the_song_changes_shape();
    test_mixer_dock_strips();
    test_meters_and_the_loudness_window();
    test_the_export_sheet_is_one_render_line();
    test_an_instrument_track_is_armed_from_its_header();
    test_mixer_matrix_effects_and_device_panel();
    test_song_bar_menus_and_settings_sections();
    test_device_window_lists_parameters_and_binds_them();
    test_automation_rows_draw_and_edit_curves();
    test_bezier_handles_and_the_automation_window();
    test_piano_roll_edits_the_pattern();
    test_the_rolls_lane_draws_the_wheels();
    test_the_rolls_note_tools();
    test_markers_on_the_ruler();
    test_time_edits_from_the_ruler();
    test_lane_groups_on_the_timeline();
    test_recording_audio_from_the_window();
    test_takes_under_their_clip();
    test_a_clips_fades_gain_and_edits();
    test_a_clip_warped_from_its_menu();
    test_tempo_changes_on_the_ruler();
    test_presets_in_the_window();
    test_templates_in_the_window();
    test_the_sample_library_in_the_browser();
    test_the_roll_plays_its_clip();
    test_the_roll_sounds_what_is_made();
    test_the_rolls_selection_and_what_is_done_to_it();
    test_the_length_a_click_makes_and_a_window_resized();
    test_the_wheel_scrolls_sideways_with_shift();
    test_a_short_drag_lands_where_it_was_let_go();
    test_a_kits_roll_names_its_pads();
    test_a_line_added_and_sources_relinked();
    test_a_strip_keys_a_later_compressor();
    test_a_strip_ducks_under_another_from_its_menu();
    test_the_loop_region_on_the_ruler();
    test_the_ruler_is_dragged();
    test_a_sample_dropped_on_a_sampler_loads_it();
    test_a_plugins_device_window();
    test_a_tuners_window();
    test_a_vca_in_the_dock();
    test_per_strip_tools_in_the_dock();
    test_versions_from_the_song_bar();
    test_relink_and_collect_from_the_file_menu();
    test_the_session_view();
    test_the_video_window();
    test_the_video_lane();
    test_export_with_the_video();
    test_conflicts_are_asked_in_a_sheet();
    test_the_ui_scale_and_high_contrast();
    test_midi_learn_in_the_window();
    test_a_surface_chosen_in_settings();
    test_the_cpu_in_the_song_bar();
    test_settings_puts_a_port_on_a_device();
    test_a_strip_freezes_from_its_menu();
    test_a_sample_is_heard_from_the_browser();
    test_the_grid_follows_the_zoom();
    std::printf("\n%d passed, 0 failed\n", passed);
    return 0;
}
