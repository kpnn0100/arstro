// solaris_ui tests — the assembled App over the REAL service: clicks at the geometry the widgets
// publish, asserting the command lines that went out and the model that came back, and that every
// transition is a tween — a LIVE value caught between its ends, never only the end (design rule §1).
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../Rig.h"
#include <cassert>
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

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    arstro::cosmo_v2::registerEmbeddedFonts();
    test_new_song_cross_fades_from_home();
    test_settings_chips_send_lines_and_ease();
    test_settings_folders();
    test_song_bar_and_keys();
    test_home_unsaved_confirm_and_recents();
    std::printf("\n%d passed, 0 failed\n", passed);
    return 0;
}
