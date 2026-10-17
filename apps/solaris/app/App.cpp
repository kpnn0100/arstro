#include "App.h"
#include "../../interstellar/app/widgets/TextFit.h"
#include "widgets/FloatWindow.h"
#include "widgets/PianoRoll.h"
#include "KeyCombo.h"
#include "Keymap.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;

    namespace
    {
        constexpr double kActiveWindowMs = 600.0; // redraw this long after any input: hovers finish
    }

    const solaris::AppModel &App::emptyModel()
    {
        static const solaris::AppModel m;
        return m;
    }

    std::string App::quote(const std::string &s)
    {
        return s.find_first_of(" \t\"") == std::string::npos && !s.empty() ? s : "\"" + s + "\"";
    }

    App::App(AppHooks hooks, double width, double height) : mHooks(std::move(hooks)), mW(width), mH(height)
    {
        mPhysW = std::max(1.0, width); // R-ACCESS-1: the window as the host gives it — at 100 % the logical size too
        mPhysH = std::max(1.0, height);
        installSolarisAccent(); // FIRST: widgets read the accent when they are built
        mHome = std::make_shared<HomeScreen>();
        mProject = std::make_shared<ProjectScreen>();
        mSettings = std::make_shared<SettingsSheet>();
        mExport = std::make_shared<ExportSheet>();
        mRelink = std::make_shared<RelinkSheet>();
        mConflicts = std::make_shared<ConflictsSheet>();
        mConfirm = std::make_shared<cosmo_v2::ConfirmDialog>(palette::primary());
        mMenu = std::make_shared<cosmo_v2::ContextMenu>();
        mModalRoot = std::make_shared<Segment>();
        mHistory = std::make_shared<cosmo_v2::HistoryView>();
        mHistory->easeCurrent = true; // R-HIST-3: the highlight travels, it never jumps (design rule §1)
        mHistory->onSelect = [this](int i) {
            // a node clicked is ONE line; the view stays open and follows the model to it (syncHistory)
            if (i >= 0 && i < (int)mHistoryIds.size()) dispatch("history jump " + std::to_string(mHistoryIds[(size_t)i]));
        };
        mModalRoot->addChild(mHistory);
        mModalRoot->addChild(mConfirm);
        mModalRoot->addChild(mMenu);

        mHome->onNewSong = [this] { mNewTemplate.clear(); if (onPickSongToCreate) onPickSongToCreate(); };
        mHome->onNewFromTemplate = [this](Point world) { pickTemplate(world); }; // R-PRESET-2
        mHome->onOpenSong = [this] { if (onPickSongToOpen) onPickSongToOpen(); };
        mHome->onSettings = [this] { openSettings(); };
        mHome->onOpenRecent = [this](const std::string &p) { dispatch("project open " + quote(p)); };
        mHome->onForgetRecent = [this](const std::string &p) { dispatch("recents remove " + quote(p)); };

        SongBar &bar = mProject->bar();
        bar.onHome = [this] { requestHome(); };
        bar.onPlayToggle = [this] {
            const auto &m = mHooks.model ? mHooks.model() : emptyModel();
            dispatch(m.transport.playing ? "transport stop --return" : "transport play"); // Stop: back where the time bar was put (R-TIME-7)
        };
        bar.onRecordToggle = [this] {
            // R-MIDI-2: record the armed strip's keyboard — the service refuses with nothing armed (the toast says so)
            const auto &m = mHooks.model ? mHooks.model() : emptyModel();
            dispatch(m.transport.recording ? "transport stop" : "transport record");
        };
        bar.onSave = [this] { dispatch("project save"); };
        bar.onSettings = [this] { openSettings(); };
        bar.onCommand = [this](const std::string &line) { return dispatch(line); }; // the tempo (R-UI-13)
        bar.onMenu = [this](std::vector<cosmo_v2::ContextMenu::Item> items, Point world) { mMenu->open(std::move(items), world.x, world.y); }; // R-VER
        bar.onEditText = [this](const std::string &cur, Point world, std::function<void(const std::string &)> done) {
            mMenu->onRename = std::move(done);
            mMenu->openRename(cur, world.x, world.y);
        };
        mProject->onCommand = [this](const std::string &line) { return dispatch(line); };
        mProject->onNotice = [this](const std::string &s) { showToast(s); };
        mProject->onCopy = [this](const std::string &text) { copy(text); };
        mProject->windows().videoFrame = [this](const std::string &file, double t, int bw, int bh, VideoImage &out) { // R-VIDEO-1
            return onVideoFrame && onVideoFrame(file, t, bw, bh, out);
        };
        mProject->timeline().videoThumb = [this](const std::string &file, double t, int bw, int bh, VideoImage &out) { // R-VIDEO-1: the lane's
            return onVideoThumb ? onVideoThumb(file, t, bw, bh, out) : onVideoFrame && onVideoFrame(file, t, bw, bh, out);
        };
        mProject->browser().onOpenSettings = [this] { openSettings(); };
        mProject->onMenu = [this](std::vector<cosmo_v2::ContextMenu::Item> items, Point world) { mMenu->open(std::move(items), world.x, world.y); };
        mProject->onRename = [this](const std::string &cur, Point world, std::function<void(const std::string &)> done) {
            mMenu->onRename = std::move(done);
            mMenu->openRename(cur, world.x, world.y);
        };

        mSettings->onCommand = [this](const std::string &line) { dispatch(line); };
        mSettings->onMenu = [this](std::vector<cosmo_v2::ContextMenu::Item> items, Point world) { mMenu->open(std::move(items), world.x, world.y); }; // R-DEV-1
        // R-EXPORT-1: the sheet's choices, then the host names the file — ONE render line
        // R-COLLECT-1: Relink's rows and its search — the host names the file or the folder, then ONE line
        mRelink->onLocate = [this](const std::string &path) {
            const auto slash = path.find_last_of('/');
            if (onPickFile)
                onPickFile("Locate " + (slash == std::string::npos ? path : path.substr(slash + 1)),
                           [this, path](const std::string &p) { dispatch("project relink " + quote(path) + " --to " + quote(p)); });
        };
        mRelink->onSearchFolder = [this] {
            if (onPickDirectory) onPickDirectory("Search a folder for the missing files", [this](const std::string &d) { dispatch("project relink --folder " + quote(d)); });
        };
        mConflicts->onCommand = [this](const std::string &line) { return dispatch(line); }; // R-COLLAB-1: Keep Mine, Take Theirs
        mExport->onExport = [this](const std::string &flags, const std::string &suggested) {
            if (onPickSave) onPickSave("Export the mix", suggested, [this, flags](const std::string &p) { dispatch("render --out " + quote(p) + " " + flags); });
        };
        mSettings->onAddFolder = [this] {
            if (onPickFolder) onPickFolder([this](const std::string &path) { dispatch("folder add " + quote(path)); });
        };

        buildMenus();
        mRecognizer.setSink([this](const Gesture &g) {
            if (mMenu->isOpen()) { mMenu->onGesture(g); return; }       // a modal owns input
            if (mHistory->isOpen())
            {
                if (g.type == Gesture::Type::Scroll) mHistory->scrollBy(-g.delta.y / 40.0); // a notch is a row
                else mHistory->onGesture(g);
                return;
            }
            if (g.type == Gesture::Type::RightClick && mScreen == "project" && !mConfirm->isOpen() && !mSettings->isOpen() && !mExport->isOpen() && !mRelink->isOpen() &&
                !mConflicts->isOpen() && mProject->contextClick(g.pos))
                return; // a window's parameter row: its menu (a slider would swallow the click)
            if (mConfirm->isOpen()) { mConfirm->onGesture(g); return; }
            if (mSettings->isOpen()) { mSettings->onGesture(g); return; }
            if (mExport->isOpen()) { mExport->onGesture(g); return; }
            if (mRelink->isOpen()) { mRelink->onGesture(g); return; }
            if (mConflicts->isOpen()) { mConflicts->onGesture(g); return; }
            if (g.type == Gesture::Type::Down && mScreen == "project")
            {
                // R-ROLL-8: the keyboard follows the pointer — a press in the front roll gives it the keys
                PianoRoll *r = mProject->windows().frontRoll();
                mRollKeys = r && r->localBounds().contains(r->toLocal(g.pos));
            }
            if (mScreen == "home") mHome->onGesture(g);
            else mProject->onGesture(g);
        });
        layoutAll();
    }

    // ── the song bar's menus (R-UI-3 amended): every item a command line or a host picker ────────

    std::string App::selectedClip() const { return mProject->timeline().selectedClip(); }

    void App::buildMenus()
    {
        auto &ms = mProject->bar().menus();
        ms.addMenu({"File", {
            {"New Song\xE2\x80\xA6", [this] { mNewTemplate.clear(); if (onPickSongToCreate) onPickSongToCreate(); }},
            {"Open\xE2\x80\xA6", [this] { if (onPickSongToOpen) onPickSongToOpen(); }},
            {"Save            (Ctrl+S)", [this] { dispatch("project save"); }},
            {"Save As\xE2\x80\xA6", [this] {
                 const auto &m = mHooks.model ? mHooks.model() : emptyModel();
                 if (onPickSave) onPickSave("Save the song as", m.projectName + ".slp", [this](const std::string &p) { dispatch("project save " + quote(p)); });
             }},
            {"Render\xE2\x80\xA6", [this] {
                 const auto &m = mHooks.model ? mHooks.model() : emptyModel();
                 if (onPickSave) onPickSave("Render the mix", m.projectName + ".wav", [this](const std::string &p) { dispatch("render --out " + quote(p)); });
             }},
            {"Export\xE2\x80\xA6", [this] { mExport->show(mHooks.model ? mHooks.model() : emptyModel()); }}, // R-EXPORT-1
            // R-COLLECT-1: every file the song names gathered into its folder and saved — ONE line; what is missing, a window
            {"Collect and Save", [this] {
                 if (!dispatch("project collect --save")) return;
                 const auto &m = mHooks.model ? mHooks.model() : emptyModel();
                 const size_t n = m.missing.size();
                 showToast(n == 0 ? std::string("Collected into the song's folder, and saved")
                                  : "Collected and saved \xE2\x80\x94 " + std::to_string(n) + (n == 1 ? " file is" : " files are") + " still missing (File \xE2\x80\xBA Relink\xE2\x80\xA6)");
             }},
            {"Relink\xE2\x80\xA6", [this] {
                 dispatch("project files --missing"); // a file may have gone since the last command: ask, then show the answer
                 mRelink->show(mHooks.model ? mHooks.model() : emptyModel());
             }},
            {"Render Stems\xE2\x80\xA6", [this] {
                 const auto &m = mHooks.model ? mHooks.model() : emptyModel();
                 std::string ids;
                 for (const auto &s : m.strips) ids += (ids.empty() ? "" : ",") + s.id;
                 if (onPickSave && !ids.empty())
                     onPickSave("Render the mix and a stem per strip", m.projectName + ".wav",
                                [this, ids](const std::string &p) { dispatch("render --out " + quote(p) + " --stems " + ids); });
             }},
            {"Home", [this] { requestHome(); }},
            // R-PRESET-2: a song started from a template; the open song kept as one
            {"New from Template\xE2\x80\xA6", [this] { pickTemplate(Point{60.0, 30.0}); }},
            {"Save as Template\xE2\x80\xA6", [this] {
                 mMenu->onRename = [this](const std::string &n) { if (!n.empty()) dispatch("template save " + quote(n)); };
                 mMenu->openRename("", 60.0, 30.0);
             }},
            // R-COLLAB-1: what a sync with the song's file asked — answered here, ONE line each
            {"Conflicts\xE2\x80\xA6", [this] { mConflicts->show(mHooks.model ? mHooks.model() : emptyModel()); }},
        }});
        ms.addMenu({"Edit", {}});   // filled by refreshMenus: its labels name what undo would take back
        ms.addMenu({"Song", {
            {"Add Mixer", [this] { dispatch("mixer add"); }},
            {"Add Bus", [this] { dispatch("strip add --kind bus"); }},
            {"Add Audio Line", [this] { dispatch("strip add --kind audio"); }},
            {"Add Lane", [this] { dispatch("lane add"); }},
            {"Quantize Clip", [this] {
                 // the selected note clip's pattern at the lanes' snap step (R-UI-3) — 1/32 of a beat when nothing snaps
                 const auto &m = mHooks.model ? mHooks.model() : emptyModel();
                 const std::string sel = selectedClip();
                 for (const auto &c : m.clips)
                     if (c.id == sel && !c.pattern.empty())
                     {
                         const double step = std::min(4.0, std::max(1.0 / 32.0, mProject->timeline().snapStep()));
                         char grid[32];
                         std::snprintf(grid, sizeof grid, "%.10g", step); // a power-of-two step prints exactly
                         dispatch("pattern quantize " + c.pattern + " --grid " + grid);
                     }
             }},
        }});
        ms.addMenu({"View", {}});   // filled by refreshMenus: Show / Hide follows the state
        refreshMenus(mHooks.model ? mHooks.model() : emptyModel());
    }

    void App::refreshMenus(const solaris::AppModel &m)
    {
        const std::string key = m.undoLabel + "|" + m.redoLabel + "|" + (mProject->dockOpen() ? "1" : "0") + (mProject->browserOpen() ? "1" : "0") + (mProject->sessionOpen() ? "1" : "0") +
                                (mProject->windows().isOpen("video") ? "1" : "0") + (m.settings.metronome ? "1" : "0") + (m.settings.showIds ? "1" : "0") + keyHint(m, "song.undo") + keyHint(m, "song.redo") +
                                keyHint(m, "lanes.duplicate") + keyHint(m, "lanes.delete") + keyHint(m, "lanes.copy") + keyHint(m, "lanes.paste") + keyHint(m, "lanes.split");
        if (key == mMenuKey) return; // labels change only when what they name does — a re-keyed action too
        mMenuKey = key;
        auto &ms = mProject->bar().menus();
        const std::string undo = m.undoLabel.empty() ? "Undo" : "Undo " + m.undoLabel, redo = m.redoLabel.empty() ? "Redo" : "Redo " + m.redoLabel;
        ms.setItems(1, {
            {undo + keyHint(m, "song.undo"), [this] { dispatch("undo"); }},
            {redo + keyHint(m, "song.redo"), [this] { dispatch("redo"); }},
            {"Show History Tree\xE2\x80\xA6", [this] { openHistory(); }}, // R-HIST-3, as cosmo's History menu has it
            {"Duplicate Clip" + keyHint(m, "lanes.duplicate"), [this] { const auto c = selectedClip(); if (!c.empty()) dispatch("clip duplicate " + c); }},
            {"Copy Clip" + keyHint(m, "lanes.copy"), [this] { songAction(mHooks.model ? mHooks.model() : emptyModel(), "lanes.copy"); }},
            {"Paste Clip" + keyHint(m, "lanes.paste"), [this] { pasteClip(mHooks.model ? mHooks.model() : emptyModel()); }},
            {"Delete Clip" + keyHint(m, "lanes.delete"), [this] { const auto c = selectedClip(); if (!c.empty()) dispatch("clip delete " + c); }},
            {"Split Clip at the Time Bar" + keyHint(m, "lanes.split"), [this] { songAction(mHooks.model ? mHooks.model() : emptyModel(), "lanes.split"); }},
        });
        ms.setItems(3, {
            {mProject->dockOpen() ? "Hide Mixer" : "Show Mixer", [this] { mProject->setDockOpen(!mProject->dockOpen()); mMenuKey.clear(); refreshMenus(mHooks.model ? mHooks.model() : emptyModel()); }},
            {mProject->browserOpen() ? "Hide Browser" : "Show Browser", [this] { mProject->setBrowserOpen(!mProject->browserOpen()); mMenuKey.clear(); refreshMenus(mHooks.model ? mHooks.model() : emptyModel()); }},
            {m.settings.metronome ? "Metronome: On" : "Metronome: Off", [this] {
                 const auto &mm = mHooks.model ? mHooks.model() : emptyModel();
                 dispatch(std::string("settings set metronome=") + (mm.settings.metronome ? "off" : "on"));
             }},
            {m.settings.showIds ? "Hide IDs" : "Show IDs", [this] { // R-UI-11: a machine setting, so a script can turn it on too
                 const auto &mm = mHooks.model ? mHooks.model() : emptyModel();
                 dispatch(std::string("settings set showIds=") + (mm.settings.showIds ? "off" : "on"));
             }},
            {"Settings\xE2\x80\xA6   (Ctrl+,)", [this] { openSettings(); }},
            {mProject->sessionOpen() ? "Show Arrangement" : "Show Session", [this] { mProject->setSessionOpen(!mProject->sessionOpen()); mMenuKey.clear(); refreshMenus(mHooks.model ? mHooks.model() : emptyModel()); }}, // R-LAUNCH-1
            {mProject->windows().isOpen("video") ? "Hide Video" : "Show Video", [this] { // R-VIDEO-1: the song's picture, in a window
                 if (mProject->windows().isOpen("video")) mProject->windows().close("video");
                 else
                     mProject->windows().openVideo().onClosed = [this] { // closed by its ×: the menu says Show again
                         mMenuKey.clear();
                         refreshMenus(mHooks.model ? mHooks.model() : emptyModel());
                     };
                 mMenuKey.clear();
                 refreshMenus(mHooks.model ? mHooks.model() : emptyModel());
             }},
        });
    }

    void App::layoutAll()
    {
        for (Segment *s : {(Segment *)mHome.get(), (Segment *)mProject.get(), (Segment *)mSettings.get(), (Segment *)mExport.get(), (Segment *)mRelink.get(), (Segment *)mConflicts.get(), (Segment *)mModalRoot.get(), (Segment *)mConfirm.get(),
                           (Segment *)mMenu.get(), (Segment *)mHistory.get()})
        {
            s->x.set(0);
            s->y.set(0);
            s->width.set(mW);
            s->height.set(mH);
        }
    }

    void App::setSize(double width, double height)
    {
        mPhysW = std::max(1.0, width);
        mPhysH = std::max(1.0, height);
        mLastActivityMs = mNowMs;
        applyLogicalSize();
    }

    void App::applyLogicalSize()
    {
        // logical units from here down: the window divided by the scale DRAWN now (R-ACCESS-1)
        const double s = std::max(0.25, mScale.value());
        mW = std::max(1.0, mPhysW / s);
        mH = std::max(1.0, mPhysH / s);
        layoutAll();
    }

    Segment *App::activeRoot() { return mScreen == "home" ? (Segment *)mHome.get() : (Segment *)mProject.get(); }

    double App::screenOpacity(const std::string &screen) const
    {
        return screen == "home" ? mHome->opacity.value() : mProject->opacity.value();
    }

    void App::showToast(const std::string &text, bool refusal)
    {
        mToastText = text;
        mToastRefusal = refusal;
        mToastWanted = true;
    }

    void App::copy(const std::string &text)
    {
        // R-UI-11: the host's clipboard; a copy says so
        mLastActivityMs = mNowMs;
        if (!onCopy) { showToast("This window has no clipboard"); return; }
        onCopy(text);
        showToast("Copied " + text, false);
    }

    bool App::dispatch(const std::string &line)
    {
        mLastActivityMs = mNowMs;
        if (!mHooks.dispatch) return false;
        std::string err;
        if (mHooks.dispatch(line, err)) return true;
        showToast(err.empty() ? "refused: " + line : err);
        return false;
    }

    void App::openSongPicked(const std::string &path) { dispatch("project open " + quote(path)); }

    void App::newSongPicked(const std::string &path)
    {
        std::string p = path;
        if (p.size() < 4 || p.compare(p.size() - 4, 4, ".slp") != 0) p += ".slp";
        // R-PRESET-2: from the template picked before the host asked where — ONE line either way
        const std::string from = mNewTemplate;
        mNewTemplate.clear();
        dispatch("project new " + quote(p) + (from.empty() ? std::string() : " --template " + quote(from)));
    }

    void App::pickTemplate(Point world)
    {
        // R-PRESET-2: the templates, yours and the factory's; one picked, the host asks where the new song goes
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        if (m.templates.empty()) { showToast("No templates yet \xE2\x80\x94 File \xE2\x80\xBA Save as Template\xE2\x80\xA6 keeps a song as one"); return; }
        std::vector<cosmo_v2::ContextMenu::Item> items;
        for (const auto &t : m.templates)
        {
            const std::string name = t.name;
            items.push_back({name + (t.factory ? "  \xC2\xB7 factory" : ""), [this, name] {
                                 mNewTemplate = name;
                                 if (onPickSongToCreate) onPickSongToCreate();
                             }});
        }
        mMenu->open(std::move(items), world.x, world.y);
    }

    void App::openHistory()
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        if (m.history.empty()) return; // no song: no tree
        mHistoryIds.clear();
        mHistoryLabels.clear();
        std::vector<cosmo_v2::HistoryView::Node> nodes;
        int cur = -1;
        for (const auto &n : m.history)
        {
            int parent = -1;
            for (size_t j = 0; j < mHistoryIds.size(); ++j)
                if (mHistoryIds[j] == n.parent) parent = (int)j; // oldest first: a parent is always listed before its child
            if (n.id == m.historyCurrent) cur = (int)nodes.size();
            nodes.push_back(cosmo_v2::HistoryView::Node{parent, n.label});
            mHistoryIds.push_back(n.id);
            mHistoryLabels.push_back(n.label);
        }
        // the hint names this machine's keys (R-SET-4)
        auto first = [&](const std::string &id) {
            const std::string h = keyHint(m, id);
            return h.empty() ? std::string() : h.substr(4, h.size() - 5); // "   (Ctrl+Z)" → "Ctrl+Z"
        };
        std::string hint = "click a node to jump";
        if (!first("song.undo").empty()) hint += "  \xC2\xB7  " + first("song.undo") + " undo";
        if (!first("song.redo").empty()) hint += "  \xC2\xB7  " + first("song.redo") + " redo";
        mHistory->hint = hint;
        mHistory->show(std::move(nodes), cur);
    }

    void App::syncHistory(const solaris::AppModel &m)
    {
        if (!mHistory->isOpen()) return;
        if (m.history.empty()) { mHistory->hide(); return; } // the song closed
        std::vector<int> ids;
        std::vector<std::string> labels;
        for (const auto &n : m.history) { ids.push_back(n.id); labels.push_back(n.label); }
        if (ids != mHistoryIds || labels != mHistoryLabels) { openHistory(); return; } // a new tree (an edit from a shell): its snapshot
        for (size_t i = 0; i < ids.size(); ++i)
            if (ids[i] == m.historyCurrent && (int)i != mHistory->current()) mHistory->setCurrent((int)i); // the highlight eases there
    }

    void App::askRecovery(const solaris::AppModel &m)
    {
        // R-SAFE-2: a song opened with an autosave newer than it — the saved one is open until this is answered
        const std::string path = m.recovery;
        cosmo_v2::ConfirmDialog::Button saved, recover;
        saved.label = "Open the Saved One";
        saved.onClick = [this, path] { dispatch("project discard-recovery " + quote(path)); };
        recover.label = "Recover";
        recover.primary = true;
        recover.onClick = [this, path] { dispatch("project recover " + quote(path)); };
        char age[64];
        const double s = m.recoveryAge;
        if (s < 90) std::snprintf(age, sizeof age, "%.0f seconds", s);
        else if (s < 5400) std::snprintf(age, sizeof age, "%.0f minutes", s / 60.0);
        else std::snprintf(age, sizeof age, "%.0f hours", s / 3600.0);
        mConfirm->show("Recover unsaved changes?",
                       "Solaris kept changes to \"" + m.projectName + "\" that were never saved (" + age + " ago). Recover them, or open the song as it was last saved?",
                       {saved, recover});
    }

    void App::openSettings()
    {
        dispatch("devices list"); // the device chips are what is connected NOW — the MIDI ports too (a surface's, R-CTRL-2)
        mSettings->show();
    }

    void App::requestHome()
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        if (!m.dirty) { dispatch("project close"); return; }
        cosmo_v2::ConfirmDialog::Button save, discard, cancel;
        save.label = "Save";
        save.primary = true;
        save.onClick = [this] { if (dispatch("project save")) dispatch("project close"); };
        discard.label = "Discard";
        discard.destructive = true;
        discard.onClick = [this] { dispatch("project close"); };
        cancel.label = "Cancel";
        mConfirm->show("Unsaved changes", "Save \"" + m.projectName + "\" before going Home?", {cancel, discard, save});
    }

    void App::bindIfStale()
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        // live state moves without a revision: the playing transport, a preview being heard (and its end)
        if (m.revision == mBoundRevision && !m.transport.playing && !m.audition.playing && m.audition.playing == mBoundAudition) return;
        mBoundRevision = m.revision;
        mBoundAudition = m.audition.playing;
        mHome->bind(m);
        mProject->bind(m);
        mSettings->bind(m);
        mRelink->bind(m); // R-COLLECT-1: what is missing now
        mConflicts->bind(m); // R-COLLAB-1: what is asked now
        if (m.sync.count != mSyncSeen) // the song followed its file: said — and what it asks, shown
        {
            if (m.sync.count > mSyncSeen && !m.sync.said.empty()) showToast(m.sync.said, false);
            if (m.sync.count > mSyncSeen && !m.sync.conflicts.empty() && !mConflicts->isOpen()) mConflicts->show(m);
            mSyncSeen = m.sync.count;
        }
        if (m.projectPath != mMissingSaidFor) // a song opened with files missing says so, once
        {
            mMissingSaidFor = m.projectPath;
            const size_t n = m.missing.size();
            if (n > 0) showToast(std::to_string(n) + (n == 1 ? " file is" : " files are") + " missing \xE2\x80\x94 File \xE2\x80\xBA Relink\xE2\x80\xA6");
        }
        syncHistory(m);
        if (m.recovery.empty()) mRecoveryAsked.clear();
        else if (m.recovery != mRecoveryAsked) { mRecoveryAsked = m.recovery; askRecovery(m); }
        refreshMenus(m);
        if (m.settings.reducedMotion != mAppReducedMotion)
        {
            mAppReducedMotion = m.settings.reducedMotion;
            artboard::setReducedMotion(mOsReducedMotion || mAppReducedMotion); // the setting, or the OS's (design rule §2.6)
        }
    }

    bool App::needsRedraw(double nowMs) const
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        if (m.revision != mBoundRevision || m.transport.playing || m.audition.playing || m.audition.playing != mBoundAudition) return true;
        if (mHome->opacity.isAnimating() || mProject->opacity.isAnimating() || mToast.isAnimating() || mToast.value() > 0.0) return true;
        if (mScale.isAnimating() || mContrast.isAnimating()) return true; // R-ACCESS-1: a zoom, a contrast fade
        if (m.settings.uiScale != mScaleWant || (m.settings.highContrast ? 1 : 0) != mContrastWant) return true;
        if (mSettings->appearAmount() > 0.0 || mExport->appearAmount() > 0.0 || mRelink->appearAmount() > 0.0 || mConflicts->appearAmount() > 0.0 || mMenu->isOpen() || mHistory->appearAmount() > 0.0) return true;
        return nowMs - mLastActivityMs < kActiveWindowMs;
    }

    void App::pointer(int kind, double x, double y, int button, double timeMs, bool alt, bool shift, bool ctrl)
    {
        x /= mScale.value(); // R-ACCESS-1: the inverse of the root transform, once, at the entry point
        y /= mScale.value();
        mLastActivityMs = mNowMs;
        if (kind == 0)
        {
            mProject->setInteracting(true);
            // a press outside the open menu closes it first, as cosmo's does; the press then does its own thing
            auto &ms = mProject->bar().menus();
            if (ms.openIndex() >= 0 && !ms.pointInActiveArea(ms.toLocal(Point{x, y}))) ms.close();
        }
        if (kind == 2) mProject->setInteracting(false);
        RawPointer rp{};
        mPointer = Point{x, y};
        rp.kind = kind == 0 ? RawPointer::Kind::Down : (kind == 2 ? RawPointer::Kind::Up : RawPointer::Kind::Move);
        rp.pos = Point{x, y};
        rp.button = button == 2 ? PointerButton::Right : PointerButton::Left;
        rp.timeMs = timeMs;
        rp.alt = alt; rp.shift = shift; rp.ctrl = ctrl;
        mRecognizer.feed(rp);
    }

    void App::wheel(double x, double y, double notches, bool ctrl, bool shift)
    {
        x /= mScale.value(); // R-ACCESS-1
        y /= mScale.value();
        mLastActivityMs = mNowMs;
        RawPointer rp{};
        rp.kind = RawPointer::Kind::Scroll;
        rp.pos = Point{x, y};
        rp.timeMs = mNowMs;
        rp.ctrl = ctrl;
        rp.shift = shift; // D-5: the roll's and the lanes' sideways scroll
        rp.scroll = Point{0.0, -notches * 40.0}; // + notches = wheel up = toward the start (Interstellar's notch)
        mRecognizer.feed(rp);
    }

    Cursor App::cursor() const
    {
        // a modal takes the pointer: the arrow over it (and over what it covers)
        if (mMenu->isOpen() || mConfirm->isOpen() || mSettings->isOpen() || mExport->isOpen() || mRelink->isOpen() || mConflicts->isOpen() || mHistory->isOpen() || mScreen != "project") return Cursor::Default;
        return mProject->cursorAt(mPointer);
    }

    bool App::key(const KeyEvent &e)
    {
        mLastActivityMs = mNowMs;
        if (mMenu->isOpen()) { mMenu->dispatchKey(e); return true; }
        if (mConfirm->isOpen()) { mConfirm->handleKey(e); return true; }
        if (mSettings->isOpen()) return mSettings->handleKey(e);
        if (mExport->isOpen()) return mExport->handleKey(e);
        if (mRelink->isOpen()) return mRelink->handleKey(e);
        if (mConflicts->isOpen()) return mConflicts->handleKey(e);
        if (e.type != KeyEvent::Type::Down) return false;
        if (mHistory->isOpen())
        {
            // R-HIST-3: modal — Escape closes it; the undo and redo keys walk the tree it shows
            if (e.keyCode == 27 && !e.ctrl && !e.alt && !e.shift) { mHistory->hide(); return true; }
            const auto &hm = mHooks.model ? mHooks.model() : emptyModel();
            const std::string id = keyActionFor(hm, solaris::comboText(e.keyCode, e.ctrl, e.alt, e.shift), "song");
            if (id == "song.undo" || id == "song.redo") return songAction(hm, id);
            return true;
        }
        // R-SET-4: the combo is looked up in the model's keymap — the keys are this machine's settings
        const std::string combo = solaris::comboText(e.keyCode, e.ctrl, e.alt, e.shift);
        if (combo.empty()) return false;
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        if (mScreen == "project" && mRollKeys) // R-ROLL-8: the roll the pointer last pressed in takes its keys first
            if (PianoRoll *r = mProject->windows().frontRoll())
            {
                const std::string id = keyActionFor(m, combo, "roll");
                if (r->action(id)) return true;
                if (id == "roll.copy" || id == "roll.paste") return true; // the notes' even with none to copy — never a clip's (R-CLIP-10)
            }
        return songAction(m, keyActionFor(m, combo, "song"));
    }

    bool App::pasteClip(const solaris::AppModel &m)
    {
        // R-CLIP-10: a duplicate of the copied clip on its own lane, AT THE TIME BAR — ONE line; again, the time bar
        // unmoved, right after the last copy
        if (mClipClipboard.empty()) return false;
        const solaris::ClipModel *src = nullptr;
        for (const auto &c : m.clips)
            if (c.id == mClipClipboard) src = &c;
        if (!src) { showToast(mClipClipboard + " is gone \xE2\x80\x94 copy a clip again"); mClipClipboard.clear(); return true; }
        const double pos = m.transport.position;
        const double at = mPasteAt >= 0.0 && std::fabs(pos - mPasteAt) < 1e-9 ? mPasteNext : pos;
        const double length = src->length;
        mProject->timeline().expectCopy(false); // the copy is selected (it fades in: nothing showed it yet)
        char b[32];
        std::snprintf(b, sizeof b, "%.6g", std::round(at * 960.0) / 960.0); // on the tick
        if (dispatch("clip duplicate " + mClipClipboard + " --at " + b))
        {
            mPasteAt = pos;
            mPasteNext = at + length;
        }
        return true;
    }

    std::string App::keyHint(const solaris::AppModel &m, const std::string &action)
    {
        // a menu names the key the keymap has NOW (R-SET-4) — re-keyed, the hint follows; no key, no hint
        for (const auto &k : m.settings.keys)
            if (k.id == action && !k.keys.empty()) return "   (" + k.keys.front() + ")";
        return std::string();
    }

    std::string App::keyActionFor(const solaris::AppModel &m, const std::string &combo, const std::string &group)
    {
        for (const auto &k : m.settings.keys)
            if (solaris::keyGroup(k.area) == group && std::find(k.keys.begin(), k.keys.end(), combo) != k.keys.end()) return k.id;
        return std::string();
    }

    bool App::songAction(const solaris::AppModel &m, const std::string &id)
    {
        if (id == "song.settings") { openSettings(); return true; }
        if (id.empty() || mScreen != "project") return false;
        if (id == "transport.play" || id == "transport.stopHere")
        {
            // play/stop goes back where the time bar was put; stop-here leaves it where it was heard (R-TIME-7/8)
            if (m.transport.playing) return dispatch(id == "transport.play" ? "transport stop --return" : "transport stop");
            if (PianoRoll *r = mProject->windows().frontRoll()) // a roll in front: its ▶ (R-ROLL-6)
                if (r->togglePlay()) return true;
            return dispatch("transport play");
        }
        if (id == "transport.toStart") return dispatch("transport seek 0");
        if (id == "song.save") return dispatch("project save");
        if (id == "song.undo") return dispatch("undo");
        if (id == "song.redo") return dispatch("redo"); // R-EDM-1
        if (id == "lanes.paste") return pasteClip(m);
        const std::string sel = mProject->timeline().selectedClip();
        if (sel.empty()) return false;
        if (id == "lanes.delete") return dispatch("clip delete " + sel);
        if (id == "lanes.duplicate") return dispatch("clip duplicate " + sel); // a linked copy after it
        if (id == "lanes.split") return dispatch("clip split " + sel + " --at " + Timeline::beatText(std::round(m.transport.position * 960.0) / 960.0)); // R-EDIT-1
        if (id == "lanes.copy")
        {
            // R-CLIP-10: nothing is sent — the window keeps which clip; Ctrl+V makes the copy
            mClipClipboard = sel;
            mPasteAt = -1.0;
            showToast("Clip copied \xE2\x80\x94 " + std::string(keyHint(m, "lanes.paste").empty() ? "Paste" : keyHint(m, "lanes.paste").substr(4, keyHint(m, "lanes.paste").size() - 5)) +
                          " puts it at the time bar",
                      false);
            return true;
        }
        return false;
    }

    void App::render(IRenderTarget &target, double nowMs)
    {
        mNowMs = nowMs;
        bindIfStale();
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        // the screen cross-fade: the model says which screen; the view travels there
        const std::string want = m.screen == "project" ? "project" : "home";
        if (!mScreenInit)
        {
            mHome->opacity.set(want == "home" ? 1.0 : 0.0);
            mProject->opacity.set(want == "project" ? 1.0 : 0.0);
            mScreen = want;
            mScreenInit = true;
        }
        else if (want != mScreen)
        {
            mHome->opacity.animateTo(want == "home" ? 1.0 : 0.0, motion::kScreenFadeMs, Easing::EaseInOutCubic, nowMs);
            mProject->opacity.animateTo(want == "project" ? 1.0 : 0.0, motion::kScreenFadeMs, Easing::EaseInOutCubic, nowMs);
            mScreen = want;
        }
        // the toast: fades in, HOLDS, fades out slowly
        if (mToastWanted)
        {
            mToast.animateTo(1.0, motion::kToastInMs, Easing::EaseOutCubic, nowMs);
            mToastShownAt = nowMs;
            mToastWanted = false;
        }
        if (mToast.value() > 0.0 && !mToast.isAnimating() && nowMs - mToastShownAt > motion::kToastHoldMs)
            mToast.animateTo(0.0, motion::kToastOutMs, Easing::EaseInOutCubic, nowMs);
        mToast.update(nowMs);
        mRecognizer.advance(nowMs);
        // R-ACCESS-1: the scale and the contrast are the model's settings, eased here (a setting has no clock)
        {
            const int sc = m.settings.uiScale > 0 ? m.settings.uiScale : 100;
            if (mScaleWant < 0) mScale.set(sc / 100.0); // the first frame: placed, nowhere to zoom from
            else if (sc != mScaleWant) mScale.animateTo(sc / 100.0, kScaleAnimMs, Easing::EaseOutCubic, nowMs);
            mScaleWant = sc;
            mScale.update(nowMs);
            if (std::fabs(mW - std::max(1.0, mPhysW / mScale.value())) > 1e-9 || std::fabs(mH - std::max(1.0, mPhysH / mScale.value())) > 1e-9)
                applyLogicalSize(); // each frame of a zoom: the layout follows the drawn scale
            const int hc = m.settings.highContrast ? 1 : 0;
            if (mContrastWant < 0) mContrast.set(hc);
            else if (hc != mContrastWant) mContrast.animateTo(hc, kScaleAnimMs, Easing::EaseInOutCubic, nowMs);
            mContrastWant = hc;
            mContrast.update(nowMs);
            palette::setContrast(mContrast.value()); // every token read from here on is blended by it
        }
        const Transform root = rootTransform();

        drawRoundedRect(target, Rect{0, 0, mPhysW, mPhysH}, 0.0, Paint::filled(palette::background()));
        for (Segment *s : {(Segment *)mHome.get(), (Segment *)mProject.get()})
        {
            const bool wanted = (s == mHome.get()) == (mScreen == "home");
            if (!wanted && s->opacity.value() <= 0.0 && !s->opacity.isAnimating()) continue;
            if (s == mHome.get()) mHome->layout();
            else mProject->layout();
            s->advance(nowMs);
        }
        for (Segment *s : {(Segment *)mHome.get(), (Segment *)mProject.get()})
        {
            if (s->opacity.value() <= Segment::kOpacityEpsilon) continue;
            s->render(target, root);
            s->renderOverlay(target, root);
        }
        // the modals float over whichever screen opened them; advanced always, so a close can finish
        mSettings->advance(nowMs);
        mSettings->render(target, root);        // its card, its pages and their controls (R-SET-5/6) …
        mSettings->renderOverlay(target, root); // … and an open combo's list, over everything
        mExport->advance(nowMs);                // R-EXPORT-1: the same, for File › Export…
        mExport->render(target, root);
        mExport->renderOverlay(target, root);
        mRelink->advance(nowMs);                // R-COLLECT-1: File › Relink…
        mRelink->render(target, root);
        mConflicts->advance(nowMs);             // R-COLLAB-1: what a sync asked
        mConflicts->render(target, root);
        mModalRoot->advance(nowMs);
        mModalRoot->renderOverlay(target, root);

        const double ta = mToast.value();
        if (ta > 0.001 && !mToastText.empty())
        {
            target.save();
            target.setTransform(root); // R-ACCESS-1: drawn in logical units, as everything else
            const double px = 12.0;
            const std::string s = textfit::ellipsize(target, mToastText, std::min(640.0, mW - 64.0), px, font::sans());
            const double w = target.measureText(s, px, font::sans()) + 32.0, h = 34.0;
            const Rect r{(mW - w) * 0.5, mH - 24.0 - h + (1.0 - ta) * 12.0, w, h};
            Color bg = palette::popover(), bd = mToastRefusal ? palette::destructive() : palette::primary(), fg = palette::foreground();
            bg.a *= ta; bd.a *= 0.6 * ta; fg.a *= ta;
            drawRoundedRect(target, r, radius::control(), Paint::filledStroked(bg, bd, 1.0));
            target.setFill(fg);
            target.drawText(s, r.x + 16.0, textfit::baseline(r.y + h * 0.5, px), px, font::sans());
            target.restore();
        }
    }
}
}
