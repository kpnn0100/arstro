/*
 *  solaris_ui — solaris_app_shots: every named state as a PNG, headless (R-UI-6, design rule §8).
 *
 *  The REAL App over the REAL service (tests/Rig.h), the embedded faces registered first, a fixed
 *  16 ms clock. Every shot at 1440×900 AND 1024×640 unless --size; mid-transition shots stop
 *  between two frames of a tween on purpose.
 *
 *     solaris_app_shots [--outdir DIR] [--only SUBSTR] [--size WxH] [--check] [--list]
 *
 *  --check fails if any shot is a single colour (a blank frame is the cheapest broken state to miss).
 */
#include "../Rig.h"
#include "../../widgets/PianoRoll.h"
#include <thread>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace
{
    struct Shot
    {
        std::string name;
        std::function<void(sltest::Rig &)> run;
    };

    void songWithStrips(sltest::Rig &r, const std::string &name)
    {
        r.cmd("project new \"" + r.song(name) + "\" --bpm 128 --name \"" + name + "\"");
        r.cmd("strip add --kind instrument --instrument drums --name Drums");
        r.cmd("clip add --strip ch_2 --length 16");
        r.cmd("note add pt_1 --pitch 36 --at 0");
        r.cmd("strip add --kind instrument --instrument synth --name Bass");
        r.cmd("clip add --strip ch_3 --length 16");
        r.cmd("clip add --src \"" + r.dir + "/vocal take 3.wav\" --at 8");
        r.cmd("strip add --kind bus --name Verb");
        r.cmd("send add ch_3 --to ch_5 --gain -8");
    }

    /** The song, with a rack on the bus and the master, and a few faders moved — so the mixer has something to show. */
    void mixedSong(sltest::Rig &r, const std::string &name)
    {
        songWithStrips(r, name);
        r.cmd("device add ch_5 --type reverb");
        r.cmd("device add ch_3 --type eq");
        r.cmd("device add master --type compressor");
        r.cmd("set ch_2.gain=-4.5 ch_3.pan=-0.35 ch_4.gain=-9 ch_4.pan=0.4 ch_4.mute=true ch_5.gain=-3");
        r.cmd("send add ch_4 --to ch_5 --gain -12 --pre");
    }
    using sltest::world;

    std::vector<Shot> shots()
    {
        return {
            {"home-empty", [](sltest::Rig &r) { r.settle(); }},
            {"home-cards",
             [](sltest::Rig &r) {
                 for (const char *n : {"Night Drive", "Summer Demo", "Untitled 4", "Remix (radio edit, extended name that runs long)"})
                 {
                     songWithStrips(r, n);
                     r.cmd("project save");
                     if (std::string(n) == "Remix (radio edit, extended name that runs long)")
                         std::filesystem::remove(r.song("Untitled 4")); // one song moved away while this was open: its card says so
                     r.cmd("project close");
                 }
                 r.settle();
             }},
            {"settings-open",
             [](sltest::Rig &r) {
                 r.cmd("folder add \"/home/me/Music/Samples/Drum Kits/909\"");
                 r.cmd("folder add /home/me/Music/Loops");
                 r.cmd("settings set output=usb_interface bufferSize=128");
                 r.settle();
                 r.app->openSettings();
                 r.settle();
             }},
            {"settings-mid-open",
             [](sltest::Rig &r) {
                 r.settle();
                 r.app->openSettings();
                 r.pump(3 * sltest::Rig::kFrameMs); // ~48 ms into the 150 ms fade
             }},
            {"settings-chip-changing",
             [](sltest::Rig &r) {
                 r.settle();
                 r.app->openSettings();
                 r.settle();
                 r.click(r.app->settings().chipRect(2, 3)); // 96 kHz: its fill eases in
                 r.pump(5 * sltest::Rig::kFrameMs);
             }},
            {"project-open",
             [](sltest::Rig &r) {
                 songWithStrips(r, "Night Drive");
                 r.settle();
             }},
            {"home-to-project-mid",
             [](sltest::Rig &r) {
                 r.settle();
                 r.cmd("project new \"" + r.song("Fresh") + "\" --name Fresh");
                 r.pump(8 * sltest::Rig::kFrameMs); // half-way through the 260 ms cross-fade
             }},
            {"toast-refusal",
             [](sltest::Rig &r) {
                 songWithStrips(r, "Night Drive");
                 r.settle();
                 r.click(r.app->project().bar().hitRect(1)); // Play — this rig has no output device
                 r.pump(300.0);
             }},
            {"browser-instruments",
             [](sltest::Rig &r) {
                 songWithStrips(r, "Night Drive");
                 r.settle();
                 r.app->project().browser().setTab(1);
                 r.settle();
             }},
            {"browser-folder",
             [](sltest::Rig &r) {
                 r.cmd("folder add /music/Samples");
                 songWithStrips(r, "Night Drive");
                 r.settle();
                 auto &b = r.app->project().browser();
                 const artboard::Rect row = b.rowRect(0);
                 r.click(row.x + 20.0 + b.x.value(), row.y + row.h * 0.5 + b.y.value());
                 r.settle();
             }},
            {"drag-sample-mid",
             [](sltest::Rig &r) {
                 r.cmd("folder add /music/Samples");
                 songWithStrips(r, "Night Drive");
                 r.settle();
                 auto &b = r.app->project().browser();
                 r.click(b.rowRect(0).x + 20.0 + b.x.value(), b.rowRect(0).y + 10.0 + b.y.value());
                 r.settle();
                 const artboard::Rect row = b.rowRect(2);
                 auto &tl = r.app->project().timeline();
                 r.drag(row.x + 40.0 + b.x.value(), row.y + 10.0 + b.y.value(), tl.x.value() + tl.beatToX(6.0) + 3.0,
                        tl.y.value() + tl.rowRect(1).y + 20.0, 8, false); // held over the Bass lane at beat 6
                 r.pump(200.0);
             }},
            {"clip-dragging",
             [](sltest::Rig &r) {
                 songWithStrips(r, "Night Drive");
                 r.settle();
                 auto &tl = r.app->project().timeline();
                 const artboard::Rect c = tl.clipRect("ac_3");
                 const double ox = tl.x.value(), oy = tl.y.value();
                 r.drag(ox + c.x + 8.0, oy + c.y + 10.0, ox + c.x + 8.0 + 4 * tl.pxPerBeat(), oy + tl.rowRect(0).y + 20.0, 8, false);
             }},
            {"clip-selected-zoomed",
             [](sltest::Rig &r) {
                 songWithStrips(r, "Night Drive");
                 r.settle();
                 auto &tl = r.app->project().timeline();
                 const double ox = tl.x.value(), oy = tl.y.value();
                 for (int i = 0; i < 3; ++i) r.app->wheel(ox + tl.beatToX(2.0), oy + 60.0, 1.0, true); // Ctrl+wheel: zoom in about beat 2
                 r.settle();
                 const artboard::Rect c = tl.clipRect("ac_1");                   // begins off-screen once zoomed:
                 r.click(ox + std::max(c.x, arstro::solaris_ui::Timeline::kHeaderW) + 30.0, oy + c.y + 20.0); // click its visible part
                 r.settle();
             }},
            {"lanes-zoomed-in",   // R-UI-10: twelve notches in — bars, beats, halves … thirty-seconds; the beats named; Snap 1/128
             [](sltest::Rig &r) {
                 songWithStrips(r, "Night Drive");
                 r.settle();
                 auto &tl = r.app->project().timeline();
                 const double ox = tl.x.value(), oy = tl.y.value();
                 for (int i = 0; i < 12; ++i) r.app->wheel(ox + tl.beatToX(8.0), oy + 60.0, 1.0, true); // about the vocal's start
                 r.settle();
             }},
            {"lanes-zoomed-out",  // R-UI-10: all the way out — bars only, every other bar named; Snap Bar
             [](sltest::Rig &r) {
                 songWithStrips(r, "Night Drive");
                 r.settle();
                 auto &tl = r.app->project().timeline();
                 const double ox = tl.x.value(), oy = tl.y.value();
                 for (int i = 0; i < 8; ++i) r.app->wheel(ox + tl.beatToX(0.0) + 1.0, oy + 60.0, -1.0, true);
                 r.settle();
             }},
            {"lanes-zoom-mid",    // mid Ctrl+wheel: a level caught mid-fade, the step's two names cross-fading
             [](sltest::Rig &r) {
                 songWithStrips(r, "Night Drive");
                 r.settle();
                 auto &tl = r.app->project().timeline();
                 const double ox = tl.x.value(), oy = tl.y.value();
                 for (int i = 0; i < 3; ++i) r.app->wheel(ox + tl.beatToX(8.0), oy + 60.0, 1.0, true);
                 r.pump(5 * sltest::Rig::kFrameMs);
             }},
            {"mixer-sources",
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.settle();
             }},
            {"mixer-buses",
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.settle();
                 r.app->project().dock().setTab(1);
                 r.settle();
             }},
            {"mixer-tab-mid",
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.settle();
                 r.app->project().dock().setTab(1);
                 r.pump(4 * sltest::Rig::kFrameMs); // the pages mid cross-fade, the highlight mid-slide
             }},
            {"mixer-matrix",
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.settle();
                 r.app->project().dock().setTab(2);
                 r.settle();
             }},
            {"mixer-folded",
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.settle();
                 auto &d = r.app->project().dock();
                 r.click(world(d, d.foldRect("ch_1")));
                 r.settle();
             }},
            {"device-panel",
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.settle();
                 auto &d = r.app->project().dock();
                 r.click(world(d, d.chipRect("ch_3", 0))); // Bass's Basic Synth
                 r.settle();
             }},
            {"device-window-bound",
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.cmd("auto create dv_2.filter.cutoff");
                 r.cmd("set dv_2.filter.res=0.62");          // the last change: lit
                 r.cmd("set dv_2.osc2.level=\"=0.5 + 0.25 * sin(beat * pi)\"");
                 r.settle();
                 auto &d = r.app->project().dock();
                 r.click(world(d, d.chipRect("ch_3", 0)));    // Bass's Basic Synth
                 r.click(world(d, d.chipRect("ch_3", 1)));    // and its EQ, on top
                 r.settle();
                 r.app->project().windows().window("dev:dv_2")->open(); // back to the front
                 r.settle();
             }},
            {"automation-rows",
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.cmd("auto create dv_2.filter.cutoff");
                 r.cmd("auto point move au_1 --at 0 --value 300");
                 r.cmd("auto point add au_1 --at 4 --value 4000 --shape smooth");
                 r.cmd("auto point add au_1 --at 8 --value 900 --shape hold");
                 r.cmd("auto point add au_1 --at 12 --value 600");
                 r.cmd("auto create ch_4.gain");
                 r.cmd("auto point move au_2 --at 0 --value -24");
                 r.cmd("auto point add au_2 --at 6 --value 0");
                 r.settle();
             }},
            {"mixer-bound",   // R-MIX-16: faders, a pan, a send and the master driven by formulas, each saying what drives it
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.cmd("auto create ch_3.gain");                                  // Bass's fader rides an automation
                 r.cmd("auto point move au_1 --at 0 --value -18");
                 r.cmd("auto point add au_1 --at 8 --value 0 --shape smooth");
                 r.cmd("set ch_2.gain=\"=ch_3.gain - 2\"");                        // the drums follow it: a link
                 r.cmd("set ch_2.pan=\"=0.6 * sin(beat * pi / 4)\"");              // an auto-pan
                 r.cmd("set sd_1.gain=\"=au_1 - 6\"");                             // the send rides it too
                 r.cmd("set project.masterGain=\"=-2 + 2 * sin(beat * pi / 8)\"");
                 r.cmd("transport seek 3");                                        // the values where the transport is
                 r.settle();
             }},
            {"show-ids",      // R-UI-11: every id beside its name — lanes, clips, automations, the dock, a device's rows
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.cmd("auto create dv_2.filter.cutoff");
                 r.cmd("settings set showIds=on");
                 r.settle();
                 auto &d = r.app->project().dock();
                 r.click(world(d, d.chipRect("ch_3", 0)));                        // Bass's Basic Synth: its rows' addresses
                 r.settle();
             }},
            {"show-ids-mid",  // mid-transition: the ids fading in (View › Show IDs), the names giving way
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.settle();
                 r.cmd("settings set showIds=on");
                 r.frame();
                 r.frame();
                 r.frame();
                 r.frame();
             }},
            {"copied-toast",  // a copy says so, in the accent (a refusal's toast is red)
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.settle();
                 auto &d = r.app->project().dock();
                 r.click(world(d, d.faderRect("ch_3")), 2);
                 r.settle();
                 auto &menu = r.app->menu();
                 for (int i = 0; i < menu.itemCount(); ++i)
                     if (menu.item(i).label == "Copy Address") { r.click(menu.itemRect(i)); break; }
                 r.pump(300.0);
             }},
            {"automation-bezier",   // R-AUTO-10: bezier points with their handles, Interstellar's model drawn by the engine's keys
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.cmd("auto create dv_2.filter.cutoff");
                 r.cmd("auto point move au_1 --at 0 --value 80");
                 r.cmd("auto point add au_1 --at 6 --value 6000 --shape bezier");
                 r.cmd("auto point shape au_1 --at 6 --speed-in 2500 --influence-in 55 --speed-out -900 --influence-out 40");
                 r.cmd("auto point add au_1 --at 14 --value 300");
                 r.cmd("auto create ch_3.pan");
                 r.cmd("auto point move au_2 --at 0 --value -0.8");
                 r.cmd("auto point shape au_2 --at 0 --shape bezier --speed-out 0 --influence-out 80");
                 r.cmd("auto point add au_2 --at 8 --value 0.7 --shape bezier");
                 r.cmd("auto point shape au_2 --at 8 --speed-in 0.35 --influence-in 45 --speed-out -0.1 --influence-out 60");
                 r.cmd("auto point add au_2 --at 13 --value -0.4 --shape smooth");
                 r.settle();
                 auto &tl = r.app->project().timeline();
                 const artboard::Rect lanes = world(tl, tl.rowRect(0));
                 r.app->wheel(lanes.x + 400.0, lanes.y + 10.0, -4.0); // the small window: the rows are under the dock until scrolled to
                 r.settle();
             }},
            {"automation-window",   // R-AUTO-11: a double-click on the row's header — its facts, all the model's
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.cmd("auto create dv_2.filter.cutoff");
                 r.cmd("auto point move au_1 --at 0 --value 300");
                 r.cmd("auto point add au_1 --at 6 --value 3000 --shape bezier");
                 r.cmd("auto point shape au_1 --at 6 --speed-in 700 --influence-in 55 --speed-out -250 --influence-out 40");
                 r.cmd("auto point add au_1 --at 14 --value 500 --shape hold");
                 r.cmd("set dv_2.osc2.level=\"=0.4 + 0.0001 * au_1\"");
                 r.cmd("transport seek 5");
                 r.settle();
                 auto &tl = r.app->project().timeline();
                 const artboard::Rect lanes = world(tl, tl.rowRect(0));
                 r.app->wheel(lanes.x + 400.0, lanes.y + 10.0, -4.0); // the small window: its row is under the dock until scrolled to
                 r.settle();
                 const artboard::Rect row = world(tl, tl.autoRowRect("au_1"));
                 r.click(row.x + 30.0, row.y + row.h * 0.5);
                 r.click(row.x + 30.0, row.y + row.h * 0.5);
                 r.settle();
             }},
            {"piano-roll",
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 // a bassline: roots and fifths, the offbeats softer
                 const int line[8][2] = {{45, 110}, {45, 70}, {52, 100}, {45, 72}, {48, 112}, {48, 68}, {55, 96}, {50, 80}};
                 for (int i = 0; i < 8; ++i)
                     r.cmd("note add pt_2 --pitch " + std::to_string(line[i][0]) + " --at " + std::to_string(i * 0.5) + " --length " +
                           (i % 2 ? "0.25" : "0.5") + " --vel " + std::to_string(line[i][1]));
                 r.settle();
                 r.app->project().windows().openRoll("pt_2");
                 r.settle();
             }},
            {"piano-roll-note-in",   // mid-transition: a note fading in where it was clicked
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.cmd("note add pt_2 --pitch 45 --at 0 --length 1");
                 r.settle();
                 r.app->project().windows().openRoll("pt_2");
                 r.settle();
                 r.cmd("note add pt_2 --pitch 52 --at 1 --length 1");
                 r.frame();
                 r.frame();
                 r.frame();
             }},
            {"step-mode",
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 for (int s = 0; s < 16; ++s)
                 {
                     const std::string at = std::to_string(s * 0.25);
                     if (s % 4 == 0 && s) r.cmd("note add pt_1 --pitch 36 --at " + at + " --length 0.25");
                     if (s % 8 == 4) r.cmd("note add pt_1 --pitch 39 --at " + at + " --length 0.25");
                     if (s % 2 == 0) r.cmd("note add pt_1 --pitch 42 --at " + at + " --length 0.25 --vel " + (s % 4 ? "70" : "100"));
                     if (s % 4 == 2) r.cmd("note add pt_1 --pitch 46 --at " + at + " --length 0.25 --vel 90");
                 }
                 r.settle();
                 auto &wl = r.app->project().windows();
                 wl.openRoll("pt_1");
                 wl.roll("pt_1")->setMode(arstro::solaris_ui::PianoRoll::Steps);
                 r.settle();
             }},
            {"browser-audition",   // R-EDM-9: a sample being heard, its row filling as it plays
             [](sltest::Rig &r) {
                 r.cmd("folder add /music/Samples");
                 songWithStrips(r, "Night Drive");
                 r.settle();
                 auto &b = r.app->project().browser();
                 r.click(world(b, b.rowRect(0)));
                 r.settle();
                 for (int i = 0; i < b.rowCount(); ++i)
                     if (b.row(i).label == "808 kick.wav") { r.click(world(b, b.rowRect(i))); break; }
                 std::this_thread::sleep_for(std::chrono::milliseconds(900)); // the preview runs on the clock: let it get going
                 r.move(10.0, 600.0);                                          // the pointer away: the fill, not the hover
                 r.pump(400.0);
             }},
            {"sampler-window",   // R-EDM-8: a sampler names its sound; its parameters from the registry
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.cmd("strip add --kind instrument --instrument sampler --name \"Vox Chop\" --sample \"" + r.dir + "/vocal take 3.wav\"");
                 std::string dv;
                 for (const auto &st : r.svc->model().strips)
                     for (const auto &d : st.devices)
                         if (d.type == "sampler") dv = d.id;
                 r.cmd("set " + dv + ".mode=one-shot " + dv + ".start=0.25 " + dv + ".reverse=on");
                 r.settle();
                 r.app->project().windows().openDevice(dv);
                 r.settle();
             }},
            {"loop-region",   // R-EDM-7: the brace on the ruler, the region tinted on the lanes
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.cmd("transport loop 4 12");
                 r.settle();
             }},
            {"loop-dragging", // mid Shift-drag: the brace is the pointer's
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.settle();
                 auto &tl = r.app->project().timeline();
                 const artboard::Rect ruler = world(tl, tl.rulerRect());
                 const artboard::Rect tw = world(tl, artboard::Rect{0, 0, 0, 0});
                 r.drag(tw.x + tl.beatToX(2.0), ruler.y + ruler.h * 0.5, tw.x + tl.beatToX(6.6), ruler.y + ruler.h * 0.5, 6, false, true);
             }},
            {"ruler-scrubbing", // R-TIME-6: the playhead held by the pointer on the ruler
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.settle();
                 auto &tl = r.app->project().timeline();
                 const artboard::Rect ruler = world(tl, tl.rulerRect());
                 const artboard::Rect tw = world(tl, artboard::Rect{0, 0, 0, 0});
                 r.drag(tw.x + tl.beatToX(1.0), ruler.y + ruler.h * 0.25, tw.x + tl.beatToX(5.3), ruler.y + ruler.h * 0.25, 6, false);
             }},
            {"loop-brace-moving", // R-TIME-6: the loop's brace taken by its body, mid-drag
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.cmd("transport loop 4 8");
                 r.settle();
                 auto &tl = r.app->project().timeline();
                 const artboard::Rect ruler = world(tl, tl.rulerRect());
                 const artboard::Rect tw = world(tl, artboard::Rect{0, 0, 0, 0});
                 r.drag(tw.x + tl.beatToX(5.0), ruler.y + ruler.h * 0.75, tw.x + tl.beatToX(8.0), ruler.y + ruler.h * 0.75, 6, false);
             }},
            {"mixer-sidechain",   // the kick keys the bass's compressor; a limiter on the master (R-MIX-15, R-EDM-4)
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.cmd("device add ch_3 --type compressor");
                 r.cmd("set dv_4.sidechain=on dv_4.threshold=-30");
                 r.cmd("send add ch_2 --to ch_3 --sidechain --pre");
                 r.cmd("device add master --type limiter");
                 r.settle();
             }},
            {"mixer-add-line",
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.settle();
                 auto &d = r.app->project().dock();
                 r.click(world(d, d.addLineRect()));
                 r.settle();
             }},
            {"clip-play-through",
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.cmd("strip add --kind instrument --instrument synth --name Lead");
                 r.settle();
                 auto &tl = r.app->project().timeline();
                 const artboard::Rect c = world(tl, tl.clipRect(r.svc->model().clips[1].id)); // Bass's clip
                 r.click(c.x + 30.0, c.y + c.h * 0.5, 2);
                 r.settle();
                 r.click(r.app->menu().itemRect(0)); // Play through ▸
                 r.settle();
             }},
            {"dock-folded",
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.settle();
                 auto &d = r.app->project().dock();
                 r.click(world(d, d.toggleRect()));
                 r.settle();
             }},
            {"menu-file-open",
             [](sltest::Rig &r) {
                 mixedSong(r, "Night Drive");
                 r.cmd("set ch_2.gain=-3");
                 r.settle();
                 auto &ms = r.app->project().bar().menus();
                 r.click(world(ms, ms.titleRect(1))); // Edit: it names what undo would take back
                 r.settle();
             }},
            {"confirm-unsaved",
             [](sltest::Rig &r) {
                 songWithStrips(r, "Night Drive");
                 r.settle();
                 r.click(r.app->project().bar().hitRect(0)); // Home with unsaved changes
                 r.settle();
             }},
        };
    }
}

int main(int argc, char **argv)
{
    std::string outdir = "shots", only;
    int fw = 0, fh = 0;
    bool check = false, list = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--outdir" && i + 1 < argc) outdir = argv[++i];
        else if (a == "--only" && i + 1 < argc) only = argv[++i];
        else if (a == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &fw, &fh);
        else if (a == "--check") check = true;
        else if (a == "--list") list = true;
    }
    arstro::cosmo_v2::registerEmbeddedFonts();
    std::filesystem::create_directories(outdir);
    std::vector<std::pair<int, int>> sizes = fw > 0 ? std::vector<std::pair<int, int>>{{fw, fh}} : std::vector<std::pair<int, int>>{{1440, 900}, {1024, 640}};
    int written = 0, blank = 0;
    for (const auto &s : shots())
    {
        if (!only.empty() && s.name.find(only) == std::string::npos) continue;
        if (list) { std::printf("%s\n", s.name.c_str()); continue; }
        for (const auto &sz : sizes)
        {
            sltest::Rig r("shot-" + s.name + "-" + std::to_string(sz.first), sz.first, sz.second);
            s.run(r);
            r.frame();
            const std::string path = outdir + "/" + s.name + "-" + std::to_string(sz.first) + "x" + std::to_string(sz.second) + ".png";
            r.write(path);
            // a blank frame: every sampled pixel the same
            bool uniform = true;
            const uint32_t first = r.pixel(0, 0);
            for (int y = 0; y < sz.second && uniform; y += 7)
                for (int x = 0; x < sz.first; x += 7)
                    if (r.pixel(x, y) != first) { uniform = false; break; }
            if (uniform) { ++blank; std::fprintf(stderr, "BLANK %s\n", path.c_str()); }
            ++written;
            std::printf("wrote %s\n", path.c_str());
        }
    }
    if (check && blank) return 1;
    std::printf("%d shots, %d blank\n", written, blank);
    return 0;
}
