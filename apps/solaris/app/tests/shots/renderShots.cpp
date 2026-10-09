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
