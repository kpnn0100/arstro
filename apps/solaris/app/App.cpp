#include "App.h"
#include "../../interstellar/app/widgets/TextFit.h"
#include "widgets/FloatWindow.h"
#include "widgets/PianoRoll.h"
#include "KeyCombo.h"
#include "Keymap.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

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
        // R-AGENT-4: the Agent panel is a view; what it asks for is the grammar's four lines (R-AGENT-1)
        {
            auto &ag = mProject->agent();
            ag.setIntro(say("Ask your agent to write, arrange or mix this song. It works on the song you have open, and every change is one step of Undo."));
            ag.onSend = [this](const std::string &text) {
                std::string t = text; // one grammar line: no newline, and its quotes are the line's
                for (char &ch : t) { if (ch == '\n' || ch == '\r') ch = ' '; else if (ch == '"') ch = '\''; }
                dispatch("assist send \"" + t + "\"");
            };
            ag.onStart = [this] { dispatch("assist start"); };
            ag.onStop = [this] { dispatch("assist stop"); };
            ag.onDetect = [this] { if (mProject->agentOpen()) dispatch("assist agents"); }; // a closed panel asks the machine nothing
            ag.onConnect = [this](const std::string &agent) { // the setting, then the start: as a script would
                std::string v = agent;
                for (char &ch : v) if (ch == '"') ch = '\'';
                if (dispatch("settings set \"agent=" + v + "\"")) dispatch("assist start");
            };
        }
        mSettings = std::make_shared<SettingsSheet>();
        mExport = std::make_shared<ExportSheet>();
        mRelink = std::make_shared<RelinkSheet>();
        mConflicts = std::make_shared<ConflictsSheet>();
        mConfirm = std::make_shared<cosmo_v2::ConfirmDialog>(palette::primary());
        // R-FAM-3 (was R-UI-2 amended's always-outlined): outlined beside a primary button — orange beside a filled red read
        // as one weight — filled when it is the only action
        mConfirm->setDestructiveStyle(cosmo_v2::ConfirmDialog::DestructiveStyle::Auto);
        mMenu = std::make_shared<cosmo_v2::ContextMenu>();
        // R-FAM-4: the family's toast (lifted from this file, R-UI-15) — placed under the song bar, its words shown in the
        // language of each pass
        mToast = std::make_shared<cosmo_v2::Toast>();
        mToast->translate = [](const std::string &stored) { return shown(stored); };
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
        // R-TIME-4 (amended 2026-11-17): Loop and Metronome in the song bar — ONE line each
        bar.onMetronomeToggle = [this] {
            const auto &m = mHooks.model ? mHooks.model() : emptyModel();
            dispatch(std::string("settings set metronome=") + (m.settings.metronome ? "off" : "on")); // View › Metronome's line
        };
        bar.onLoopToggle = [this] {
            const auto &m = mHooks.model ? mHooks.model() : emptyModel();
            if (m.transport.loopTo > m.transport.loopFrom) { dispatch("transport loop off"); return; }
            // on: the region this window last saw looped in this song — else the whole song, to a whole bar (an empty
            // song: four bars). The service forgets a loop it turns off; `transport loop on` is its to add (PROGRESS.md)
            double from = mLoopFrom, to = mLoopTo;
            if (!(to > from) || mLoopSong != m.projectPath)
            {
                const double bar = std::max(1, std::atoi(m.sig.c_str()));
                double end = 0.0;
                for (const auto &c : m.clips) end = std::max(end, c.at + c.length);
                from = 0.0;
                to = end > 0.0 ? std::ceil(end / bar - 1e-9) * bar : 4.0 * bar;
            }
            dispatch("transport loop " + Timeline::beatText(from) + " " + Timeline::beatText(to));
        };
        bar.onSave = [this] { dispatch("project save"); };
        bar.onCommand = [this](const std::string &line) { return dispatch(line); }; // the tempo (R-UI-13)
        bar.onMenu = [this](std::vector<cosmo_v2::ContextMenu::Item> items, Point world) { openMenu(std::move(items), world); }; // R-VER
        bar.onEditText = [this](const std::string &cur, Point world, std::function<void(const std::string &)> done) {
            mMenu->onRename = std::move(done);
            mMenu->openRename(cur, world.x, world.y);
        };
        mProject->onCommand = [this](const std::string &line) { return dispatch(line); };
        mProject->onNotice = [this](const std::string &s) { showToast(s); };
        mProject->onCopy = [this](const std::string &text) { copy(text); };
        mProject->windows().onRollOpened = [this] { mRollKeys = true; }; // R-ROLL-8 (D-25): a roll opened takes the keys
        mProject->windows().videoFrame = [this](const std::string &file, double t, int bw, int bh, VideoImage &out) { // R-VIDEO-1
            return onVideoFrame && onVideoFrame(file, t, bw, bh, out);
        };
        mProject->timeline().videoThumb = [this](const std::string &file, double t, int bw, int bh, VideoImage &out) { // R-VIDEO-1: the lane's
            return onVideoThumb ? onVideoThumb(file, t, bw, bh, out) : onVideoFrame && onVideoFrame(file, t, bw, bh, out);
        };
        mProject->browser().onOpenSettings = [this] { openSettings(); };
        mProject->onMenu = [this](std::vector<cosmo_v2::ContextMenu::Item> items, Point world) { openMenu(std::move(items), world); };
        mProject->onRename = [this](const std::string &cur, Point world, std::function<void(const std::string &)> done) {
            mMenu->onRename = std::move(done);
            mMenu->openRename(cur, world.x, world.y);
        };

        mSettings->onCommand = [this](const std::string &line) { dispatch(line); };
        mSettings->onMenu = [this](std::vector<cosmo_v2::ContextMenu::Item> items, Point world) { openMenu(std::move(items), world); }; // R-DEV-1
        // R-EXPORT-1: the sheet's choices, then the host names the file — ONE render line
        // R-COLLECT-1: Relink's rows and its search — the host names the file or the folder, then ONE line
        mRelink->onLocate = [this](const std::string &path) {
            const auto slash = path.find_last_of('/');
            if (onPickFile)
                onPickFile(fillIn(tr("Locate {1}"), slash == std::string::npos ? path : path.substr(slash + 1)),
                           [this, path](const std::string &p) { dispatch("project relink " + quote(path) + " --to " + quote(p)); });
        };
        mRelink->onSearchFolder = [this] {
            if (onPickDirectory) onPickDirectory(tr("Search a folder for the missing files"), [this](const std::string &d) { dispatch("project relink --folder " + quote(d)); });
        };
        mConflicts->onCommand = [this](const std::string &line) { return dispatch(line); }; // R-COLLAB-1: Keep Mine, Take Theirs
        mExport->onExport = [this](const std::string &flags, const std::string &suggested) {
            if (onPickSave) onPickSave(tr("Export the mix"), suggested, [this, flags](const std::string &p) { beginRender("render --out " + quote(p) + " " + flags, p); });
        };
        mSettings->onAddFolder = [this] {
            if (onPickFolder) onPickFolder([this](const std::string &path) { dispatch("folder add " + quote(path)); });
        };

        buildMenus();
        auto sink = [this](const Gesture &g) {
            if (!mRenderLine.empty() || mRenderAmt.value() > 0.001) return; // R-RENDER-4: a render about to run, or running, takes all input
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
            if (mScreen == "home") mHome->onGesture(g);
            else mProject->onGesture(g);
            // R-ROLL-8: the keyboard follows the pointer — a press in a roll's window, its title bar too, gives it the keys;
            // judged once the press is routed, so a roll it brought to the front counts (D-25)
            if (g.type == Gesture::Type::Down && mScreen == "project") mRollKeys = mProject->windows().inFrontRoll(g.pos);
        };
        mRecognizer.setSink(sink);
        mKeyClicks.setSink(sink); // R-ACCESS-1: a node's activation is the click it stands for — through the same routes
        layoutAll();
    }

    // ── the song bar's menus (R-UI-3 amended): every item a command line or a host picker ────────

    std::string App::selectedClip() const { return mProject->timeline().selectedClip(); }

    void App::buildMenus()
    {
        // four menus, in the family's order (R-FAM-1): their rows are refreshMenus's — what a row says, the key it shows
        // and whether it can act all follow the model, the keymap and the selection
        for (const char *title : {"File", "Edit", "Song", "View"}) mProject->bar().menus().addMenu({title, {}});
        refreshMenus(mHooks.model ? mHooks.model() : emptyModel());
    }

    void App::refreshMenus(const solaris::AppModel &m)
    {
        using Item = cosmo_v2::MenuStrip::Item;
        // what a row can act on now (R-FAM-1): a row that cannot is disabled, not hidden
        const std::vector<std::string> sel = mProject->timeline().selectedClips();
        const bool anyClip = !sel.empty(), canPaste = !mClipClipboard.empty();
        bool noteClip = false;
        {
            const std::string one = selectedClip();
            for (const auto &c : m.clips) noteClip |= c.id == one && !c.pattern.empty();
        }
        std::string key = m.undoLabel + "|" + m.redoLabel + "|" + (mProject->dockOpen() ? "1" : "0") + (mProject->browserOpen() ? "1" : "0") + (mProject->sessionOpen() ? "1" : "0") +
                          (mProject->windows().isOpen("video") ? "1" : "0") + (mProject->agentOpen() ? "1" : "0") + (m.settings.metronome ? "1" : "0") + (m.settings.showIds ? "1" : "0") +
                          (anyClip ? "1" : "0") + (canPaste ? "1" : "0") + (noteClip ? "1" : "0") + (m.history.empty() ? "0" : "1");
        for (const char *id : {"song.new", "song.open", "song.save", "song.saveAs", "song.settings", "song.undo", "song.redo", "lanes.duplicate", "lanes.delete", "lanes.copy", "lanes.paste",
                               "lanes.split"})
            key += "|" + keyHint(m, id);
        if (key == mMenuKey) return; // rows change only when what they say does — a re-keyed action, a selection made
        mMenuKey = key;
        auto row = [&m](const std::string &label, std::function<void()> action, const char *keyId = nullptr, bool enabled = true) {
            Item it{label, std::move(action)};
            if (keyId) it.shortcut = keyHint(m, keyId); // the key the keymap has NOW (R-SET-4), in the right-aligned column
            it.enabled = enabled;
            return it;
        };
        auto model = [this]() -> const solaris::AppModel & { return mHooks.model ? mHooks.model() : emptyModel(); };
        auto again = [this] { mMenuKey.clear(); refreshMenus(mHooks.model ? mHooks.model() : emptyModel()); }; // Show ⇄ Hide

        setMenuWords(0, say("File"), {
            row(say("New Song\xE2\x80\xA6"), [this] { songAction(mHooks.model ? mHooks.model() : emptyModel(), "song.new"); }, "song.new"),
            // R-PRESET-2: a song started from a template
            row(say("New from Template\xE2\x80\xA6"), [this] { pickTemplate(Point{60.0, 30.0}); }),
            row(say("Open\xE2\x80\xA6"), [this] { songAction(mHooks.model ? mHooks.model() : emptyModel(), "song.open"); }, "song.open"),
            Item::sep(),
            row(say("Save"), [this] { dispatch("project save"); }, "song.save"),
            row(say("Save As\xE2\x80\xA6"), [this] { songAction(mHooks.model ? mHooks.model() : emptyModel(), "song.saveAs"); }, "song.saveAs"),
            row(say("Save as Template\xE2\x80\xA6"), [this] { // … and the open song kept as one
                    mMenu->onRename = [this](const std::string &n) { if (!n.empty()) dispatch("template save " + quote(n)); };
                    mMenu->openRename("", 60.0, 30.0);
                }),
            // R-COLLECT-1: every file the song names gathered into its folder and saved — ONE line; what is missing, a window
            row(say("Collect and Save"), [this] {
                    if (!dispatch("project collect --save")) return;
                    const auto &m = mHooks.model ? mHooks.model() : emptyModel();
                    const size_t n = m.missing.size();
                    showToast(n == 0 ? say("Collected into the song's folder, and saved")
                                     : sayn((long)n, "Collected and saved \xE2\x80\x94 {1} file is still missing (File \xE2\x80\xBA Relink\xE2\x80\xA6)",
                                            "Collected and saved \xE2\x80\x94 {1} files are still missing (File \xE2\x80\xBA Relink\xE2\x80\xA6)", (long)n),
                              n == 0 ? ToastKind::Info : ToastKind::Warning); // done; done, and something is owed (R-UI-15)
                }),
            Item::sep(),
            // R-FAM-6: the quick WAV of the mix — "Render…" beside "Export…" were two words for one idea
            row(say("Export Mix as WAV\xE2\x80\xA6"), [this] {
                    const auto &m = mHooks.model ? mHooks.model() : emptyModel();
                    if (onPickSave) onPickSave(tr("Export the mix as WAV"), m.projectName + ".wav", [this](const std::string &p) { beginRender("render --out " + quote(p), p); });
                }),
            row(say("Export\xE2\x80\xA6"), [this] { mExport->show(mHooks.model ? mHooks.model() : emptyModel()); }), // R-EXPORT-1: the deliverable
            row(say("Render Stems\xE2\x80\xA6"), [this] {
                    const auto &m = mHooks.model ? mHooks.model() : emptyModel();
                    std::string ids;
                    for (const auto &s : m.strips) ids += (ids.empty() ? "" : ",") + s.id;
                    if (onPickSave && !ids.empty())
                        onPickSave(tr("Render the mix and a stem per strip"), m.projectName + ".wav",
                                   [this, ids](const std::string &p) { beginRender("render --out " + quote(p) + " --stems " + ids, p); });
                }),
            Item::sep(),
            row(say("Relink\xE2\x80\xA6"), [this] {
                    dispatch("project files --missing"); // a file may have gone since the last command: ask, then show the answer
                    mRelink->show(mHooks.model ? mHooks.model() : emptyModel());
                }),
            // R-COLLAB-1: what a sync with the song's file asked — answered here, ONE line each
            row(say("Conflicts\xE2\x80\xA6"), [this] { mConflicts->show(mHooks.model ? mHooks.model() : emptyModel()); }),
            Item::sep(),
            row(say("Settings\xE2\x80\xA6"), [this] { openSettings(); }, "song.settings"), // R-FAM-1: the family's place for it
            Item::sep(),
            row(say("Home"), [this] { requestHome(); }),
        });
        // what undo would take back is the service's words (its history's label) — shown as they are
        const std::string undo = m.undoLabel.empty() ? say("Undo") : say("Undo {1}", m.undoLabel), redo = m.redoLabel.empty() ? say("Redo") : say("Redo {1}", m.redoLabel);
        setMenuWords(1, say("Edit"), {
            row(undo, [this] { dispatch("undo"); }, "song.undo", !m.undoLabel.empty()),
            row(redo, [this] { dispatch("redo"); }, "song.redo", !m.redoLabel.empty()),
            row(say("History\xE2\x80\xA6"), [this] { openHistory(); }, nullptr, !m.history.empty()), // R-HIST-3, as cosmo's History menu has it
            Item::sep(),
            row(say("Copy Clip"), [this, model] { songAction(model(), "lanes.copy"); }, "lanes.copy", anyClip),
            row(say("Paste Clip"), [this, model] { pasteClip(model()); }, "lanes.paste", canPaste),
            row(say("Duplicate Clip"), [this, model] { songAction(model(), "lanes.duplicate"); }, "lanes.duplicate", anyClip),
            row(say("Delete Clip"), [this, model] { songAction(model(), "lanes.delete"); }, "lanes.delete", anyClip),
            row(say("Split Clip at the Time Bar"), [this, model] { songAction(model(), "lanes.split"); }, "lanes.split", anyClip),
        });
        setMenuWords(2, say("Song"), {
            row(say("Add Mixer"), [this] { dispatch("mixer add"); }),
            row(say("Add Bus"), [this] { dispatch("strip add --kind bus"); }),
            row(say("Add Audio Line"), [this] { dispatch("strip add --kind audio"); }),
            row(say("Add Lane"), [this] { dispatch("lane add"); }),
            // R-UI-16: the registry's instruments — one picked is ONE line: a strip, its track, a first clip
            row(say("Add Instrument\xE2\x80\xA6"), [this] {
                    auto &ms = mProject->bar().menus();
                    const Rect title = worldRect(ms, ms.titleRect(2));
                    openMenu(mProject->timeline().instrumentMenu(), Point{title.x, title.bottom() + 2.0});
                }),
            Item::sep(),
            row(say("Quantize Clip"), [this] {
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
                }, nullptr, noteClip),
        });
        setMenuWords(3, say("View"), {
            row(mProject->dockOpen() ? say("Hide Mixer") : say("Show Mixer"), [this, again] { mProject->setDockOpen(!mProject->dockOpen()); again(); }),
            row(mProject->browserOpen() ? say("Hide Browser") : say("Show Browser"), [this, again] { mProject->setBrowserOpen(!mProject->browserOpen()); again(); }),
            row(mProject->sessionOpen() ? say("Show Arrangement") : say("Show Session"), [this, again] { mProject->setSessionOpen(!mProject->sessionOpen()); again(); }), // R-LAUNCH-1
            row(mProject->windows().isOpen("video") ? say("Hide Video") : say("Show Video"), [this, again] { // R-VIDEO-1: the song's picture, in a window
                    if (mProject->windows().isOpen("video")) mProject->windows().close("video");
                    else mProject->windows().openVideo().onClosed = again; // closed by its ×: the menu says Show again
                    again();
                }),
            Item::sep(),
            row(say("Zoom to Fit"), [this] { mProject->timeline().zoomToFit(); }), // R-UI-17: the whole song in view (the view's own, as zoom is)
            Item::sep(),
            row(m.settings.metronome ? say("Metronome: On") : say("Metronome: Off"), [this] {
                    const auto &mm = mHooks.model ? mHooks.model() : emptyModel();
                    dispatch(std::string("settings set metronome=") + (mm.settings.metronome ? "off" : "on"));
                }),
            row(m.settings.showIds ? say("Hide IDs") : say("Show IDs"), [this] { // R-UI-11: a machine setting, so a script can turn it on too
                    const auto &mm = mHooks.model ? mHooks.model() : emptyModel();
                    dispatch(std::string("settings set showIds=") + (mm.settings.showIds ? "off" : "on"));
                }),
            Item::sep(),
            row(mProject->agentOpen() ? say("Hide Agent") : say("Show Agent"), [this, again] { mProject->setAgentOpen(!mProject->agentOpen()); again(); }), // R-AGENT-4: the last row
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

    void App::showToast(const std::string &text, ToastKind kind)
    {
        // words (`say`) or the service's line; a setter has no clock — the next frame starts it, and one that replaces a
        // toast still showing cross-fades with it (the widget's, R-FAM-4)
        mToast->show(kind, text);
    }

    Rect App::toastRect() const
    {
        const Rect r = mToast->rect(); // the toast's own coordinates: the App places it
        return Rect{r.x + mToast->x.value(), r.y + mToast->y.value(), r.w, r.h};
    }

    // ── R-ACCESS-2: the words stored in cosmo's widgets, kept as words ─────────────────────────────

    void App::openMenu(std::vector<cosmo_v2::ContextMenu::Item> items, Point world)
    {
        mContextWords.clear();
        for (auto &it : items)
        {
            mContextWords.push_back(it.label);
            it.label = shown(it.label);
        }
        ++mWordsGen;
        mMenu->open(std::move(items), world.x, world.y);
    }

    void App::showConfirm(std::function<std::string()> title, std::function<std::string()> message, std::vector<cosmo_v2::ConfirmDialog::Button> buttons)
    {
        mConfirmTitle = std::move(title);
        mConfirmMessage = std::move(message);
        mConfirmWords.clear();
        for (auto &b : buttons)
        {
            mConfirmWords.push_back(b.label);
            b.label = shown(b.label);
        }
        ++mWordsGen;
        mConfirm->show(mConfirmTitle(), mConfirmMessage(), std::move(buttons));
    }

    void App::setMenuWords(int menu, const std::string &title, std::vector<cosmo_v2::MenuStrip::Item> items)
    {
        auto &ms = mProject->bar().menus();
        if ((int)mMenuWords.size() <= menu) mMenuWords.resize((size_t)menu + 1);
        auto &w = mMenuWords[(size_t)menu];
        w.first = title;
        w.second.clear();
        std::vector<std::string> labels;
        for (auto &it : items)
        {
            w.second.push_back(it.label);
            it.label = shown(it.label);
            labels.push_back(it.label);
        }
        if (menu >= ms.menuCount()) ms.addMenu({shown(title), std::move(items)});
        else
        {
            ms.setItems(menu, std::move(items));
            ms.relabel(menu, shown(title), labels);
        }
        ++mWordsGen;
    }

    void App::relabel()
    {
        mSettings->relabel(); // the sheets keep their own words (their combos' choices) and know when they changed
        mExport->relabel();
        const std::string &lang = solaris_strings::language();
        if (lang == mRelabelLang && mWordsGen == mRelabelGen) return;
        mRelabelLang = lang;
        mRelabelGen = mWordsGen;
        auto &ms = mProject->bar().menus();
        for (size_t i = 0; i < mMenuWords.size() && (int)i < ms.menuCount(); ++i)
        {
            std::vector<std::string> labels;
            for (const auto &l : mMenuWords[i].second) labels.push_back(shown(l));
            ms.relabel((int)i, shown(mMenuWords[i].first), labels);
        }
        if (mMenu->isOpen() && (int)mContextWords.size() == mMenu->itemCount())
        {
            std::vector<std::string> labels;
            for (const auto &l : mContextWords) labels.push_back(shown(l));
            mMenu->relabel(labels);
        }
        mMenu->renameTitle = tr("Rename");
        if (mConfirmTitle && mConfirmMessage)
        {
            std::vector<std::string> labels;
            for (const auto &l : mConfirmWords) labels.push_back(shown(l));
            mConfirm->relabel(mConfirmTitle(), mConfirmMessage(), labels);
        }
        mHistory->title = tr("History"); // the family's card title: sentence case, 14 px (cosmo R-FAMILY-7) — no longer upper-cased
        if (!mHistoryHint.empty()) mHistory->hint = shown(mHistoryHint);
    }

    void App::advanceLanguage(const solaris::AppModel &m, double nowMs)
    {
        // R-ACCESS-2: the model's setting (auto: the OS's) — a switch dissolves the window, never a cut
        const std::string want = solaris_strings::resolve(m.settings.language, mOsLanguage);
        if (mLangTo.empty())
        {
            mLangFrom = mLangTo = want; // the first frame: placed, nowhere to dissolve from
            mLangFade.set(1.0);
        }
        else if (want != mLangTo)
        {
            if (want == mLangFrom && mLangFade.isAnimating()) // back the way it came: the dissolve turns round where it is
            {
                const double v = mLangFade.value();
                std::swap(mLangFrom, mLangTo);
                mLangFade.set(1.0 - v);
                mLangFade.animateTo(1.0, kLanguageDissolveMs * v, Easing::Linear, nowMs);
            }
            else
            {
                mLangFrom = mLangFade.value() >= 0.5 ? mLangTo : mLangFrom; // from what shows most
                mLangTo = want;
                mLangFade.set(0.0);
                mLangFade.animateTo(1.0, kLanguageDissolveMs, Easing::Linear, nowMs); // a dissolve is a linear alpha ramp
            }
        }
        mLangFade.update(nowMs);
        solaris_strings::setLanguage(mLangTo); // what lays out, and what answers between frames (the keyboard, AT-SPI)
    }

    void App::copy(const std::string &text)
    {
        // R-UI-11: the host's clipboard; a copy says so
        mLastActivityMs = mNowMs;
        if (!onCopy) { showToast(say("This window has no clipboard")); return; }
        onCopy(text);
        showToast(say("Copied {1}", text), ToastKind::Info);
    }

    bool App::dispatch(const std::string &line)
    {
        mLastActivityMs = mNowMs;
        if (!mHooks.dispatch) return false;
        std::string err;
        if (mHooks.dispatch(line, err)) return true;
        showToast(err.empty() ? say("refused: {1}", line) : err); // the service's own words, as they are
        return false;
    }

    // ── R-RENDER-4: a render from the window is seen before it runs, and its result said ──────────────────────────────

    void App::beginRender(const std::string &line, const std::string &file)
    {
        mLastActivityMs = mNowMs;
        if (!mRenderLine.empty()) return; // one at a time: the modal already has the window
        mRenderLine = line;
        const auto slash = file.find_last_of('/');
        mRenderFile = slash == std::string::npos ? file : file.substr(slash + 1);
        mRenderOpening = true; // a setter has no clock: the next frame starts the fade
        mRenderPainted = false;
    }

    void App::renderFinished(const std::string &out, const std::string &peakDb)
    {
        const auto slash = out.find_last_of('/');
        const std::string name = slash == std::string::npos ? out : out.substr(slash + 1);
        showToast(say("Rendered {1} \xE2\x80\x94 peak {2} dBFS", name, peakDb), ToastKind::Info);
    }

    Rect App::renderCardRect() const
    {
        const double w = std::min(360.0, mW - 32.0), h = 119.0; // a modal's 8/16 rhythm: 20 pad, three lines, 12, the bar
        return Rect{(mW - w) * 0.5, (mH - h) * 0.5, w, h};
    }

    double App::renderSweep() const
    {
        // R-FAM-6: no fraction — a segment sweeps. A hand-rolled ramp: it branches on reduced motion itself (§2.6).
        // It starts a quarter of the way in, so the frame that stands while the service works shows it on the track
        if (artboard::reducedMotion()) return 0.5;
        const double p = std::fmod(std::max(0.0, mNowMs - mRenderT0) / kRenderSweepMs + 0.25, 1.0);
        return applyEasing(Easing::EaseInOutCubic, p);
    }

    Rect App::renderBarRect() const
    {
        const Rect c = renderCardRect();
        return Rect{c.x + 20.0, c.bottom() - 20.0 - kRenderBarH, c.w - 40.0, kRenderBarH};
    }

    Rect App::renderSegmentRect() const
    {
        // the segment travels from just before the track to just past it; what is outside the track is not drawn
        const Rect b = renderBarRect();
        const double w = b.w * kRenderSegment, x = b.x - w + (b.w + w) * renderSweep();
        const double l = std::max(b.x, x), r = std::min(b.right(), x + w);
        return Rect{l, b.y, std::max(0.0, r - l), b.h};
    }

    void App::paintRender(IRenderTarget &target) const
    {
        const double a = mRenderAmt.value();
        if (a <= 0.001) return;
        auto faded = [a](Color c) { c.a *= a; return c; };
        drawRoundedRect(target, Rect{0, 0, mW, mH}, 0.0, Paint::filled(surface::scrim(a)));
        const Rect c = renderCardRect();
        const double pad = 20.0, room = c.w - 2.0 * pad;
        drawRoundedRect(target, c, radius::control(), Paint::filledStroked(faded(palette::popover()), faded(palette::border()), 1.0));
        target.setFill(faded(palette::foreground()));
        target.drawText(textfit::ellipsize(target, tr("Rendering\xE2\x80\xA6"), room, 14.0, font::sansSemiBold()), c.x + pad, c.y + pad + 14.0, 14.0, font::sansSemiBold());
        target.setFill(faded(palette::secondaryForeground()));
        target.drawText(textfit::ellipsize(target, mRenderFile, room, 11.0, font::mono()), c.x + pad, c.y + pad + 38.0, 11.0, font::mono()); // a file's name: mono
        target.setFill(faded(palette::mutedForeground()));
        target.drawText(textfit::ellipsize(target, tr("The window waits until the file is written."), room, 11.0, font::sans()), c.x + pad, c.y + pad + 60.0, 11.0, font::sans());
        // R-FAM-6: the family's 3 px progress bar — a neutral track, the accent's segment sweeping along it
        const Rect bar = renderBarRect(), seg = renderSegmentRect();
        drawRoundedRect(target, bar, radius::pill(), Paint::filled(palette::whiteAlpha(0.08 * a)));
        if (seg.w > 0.5) drawRoundedRect(target, seg, std::min(radius::pill(), seg.w * 0.5), Paint::filled(faded(palette::primary())));
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
        if (m.templates.empty()) { showToast(say("No templates yet \xE2\x80\x94 File \xE2\x80\xBA Save as Template\xE2\x80\xA6 keeps a song as one"), ToastKind::Info); return; }
        std::vector<cosmo_v2::ContextMenu::Item> items;
        for (const auto &t : m.templates)
        {
            const std::string name = t.name;
            items.push_back({name + (t.factory ? "  \xC2\xB7 " + say("factory") : std::string()), [this, name] {
                                 mNewTemplate = name;
                                 if (onPickSongToCreate) onPickSongToCreate();
                             }});
        }
        openMenu(std::move(items), world);
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
        auto first = [&](const std::string &id) { return keyHint(m, id); };
        std::string hint = say("click a node to jump");
        if (!first("song.undo").empty()) hint += "  \xC2\xB7  " + say("{1} undo", first("song.undo"));
        if (!first("song.redo").empty()) hint += "  \xC2\xB7  " + say("{1} redo", first("song.redo"));
        mHistoryHint = hint;
        ++mWordsGen;
        mHistory->hint = shown(hint);
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
        saved.label = say("Open the saved one");
        saved.onClick = [this, path] { dispatch("project discard-recovery " + quote(path)); };
        recover.label = say("Recover");
        recover.primary = true;
        recover.onClick = [this, path] { dispatch("project recover " + quote(path)); };
        const double s = m.recoveryAge;
        const std::string name = m.projectName;
        showConfirm([] { return tr("Recover unsaved changes?"); },
                    [s, name] {
                        const long n = std::lround(s < 90 ? s : s < 5400 ? s / 60.0 : s / 3600.0);
                        const std::string age = fillIn(s < 90 ? trn(n, "{1} second", "{1} seconds") : s < 5400 ? trn(n, "{1} minute", "{1} minutes") : trn(n, "{1} hour", "{1} hours"),
                                                     std::to_string(n));
                        return fillIn(tr("Solaris kept changes to \"{1}\" that were never saved ({2} ago). Recover them, or open the song as it was last saved?"), name, age);
                    },
                    {saved, recover});
    }

    void App::openSettings()
    {
        dispatch("devices list"); // the device chips are what is connected NOW — the MIDI ports too (a surface's, R-CTRL-2)
        mSettings->show();
    }

    void App::requestHome()
    {
        askUnsaved([this] { dispatch("project close"); });
    }

    void App::requestQuit()
    {
        // R-FAM-2: the window's close button — a song with unsaved edits asks first; what is not saved is closed as
        // Home's "Don't save" closes it (the service drops its autosave: nothing offers to recover what was declined)
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        mLastActivityMs = mNowMs;
        if (m.screen != "project" || !m.dirty) { if (onQuit) onQuit(); return; }
        askUnsaved([this] {
            const auto &now = mHooks.model ? mHooks.model() : emptyModel();
            if (now.dirty) dispatch("project close"); // Don't save
            if (onQuit) onQuit();
        });
    }

    void App::askUnsaved(std::function<void()> then)
    {
        // R-FAM-3: the family's question — Save changes to "<name>"? · Cancel · Don't save · Save
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        if (!m.dirty) { then(); return; }
        cosmo_v2::ConfirmDialog::Button save, dont, cancel;
        save.label = say("Save");
        save.primary = true;
        save.onClick = [this, then] { if (dispatch("project save")) then(); }; // a refused save keeps the song, and says why
        dont.label = say("Don't save"); // (an ASCII apostrophe: the dialog sizes a button by its bytes)
        dont.destructive = true;
        dont.onClick = then;
        cancel.label = say("Cancel");
        const std::string name = m.projectName.empty() ? say("Untitled") : m.projectName;
        showConfirm([name] { return fillIn(tr("Save changes to \"{1}\"?"), shown(name)); }, [] { return tr("Your changes will be lost if you don't save them."); }, {cancel, dont, save});
    }

    std::string App::windowTitle() const
    {
        // R-FAM-8: *<song> — Solaris unsaved, <song> — Solaris saved, Solaris on Home ("Solaris" is a name: no language's)
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        if (m.screen != "project") return "Solaris";
        return std::string(m.dirty ? "*" : "") + (m.projectName.empty() ? tr("Untitled") : m.projectName) + " \xE2\x80\x94 Solaris";
    }

    void App::bindIfStale()
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        // live state moves without a revision: the playing transport, a preview being heard (and its end)
        if (m.revision == mBoundRevision && !m.transport.playing && !m.audition.playing && m.audition.playing == mBoundAudition) return;
        mBoundRevision = m.revision;
        mBoundAudition = m.audition.playing;
        // R-AGENT-4: the conversation is the model's; the panel eases in whatever arrived
        mProject->agent().setCurrentAgent(m.settings.agent);
        mProject->agent().setConversation(m.assist);
        if (m.transport.loopTo > m.transport.loopFrom && m.transport.loopClip.empty()) // R-TIME-4 amended: the song's own loop, remembered
        {
            mLoopFrom = m.transport.loopFrom;
            mLoopTo = m.transport.loopTo;
            mLoopSong = m.projectPath;
        }
        mHome->bind(m);
        mProject->bind(m);
        mSettings->bind(m);
        mRelink->bind(m); // R-COLLECT-1: what is missing now
        mConflicts->bind(m); // R-COLLAB-1: what is asked now
        if (m.sync.count != mSyncSeen) // the song followed its file: said — and what it asks, shown
        {
            if (m.sync.count > mSyncSeen && !m.sync.said.empty()) showToast(m.sync.said, ToastKind::Info);
            if (m.sync.count > mSyncSeen && !m.sync.conflicts.empty() && !mConflicts->isOpen()) mConflicts->show(m);
            mSyncSeen = m.sync.count;
        }
        if (m.projectPath != mMissingSaidFor) // a song opened with files missing says so, once
        {
            mMissingSaidFor = m.projectPath;
            const size_t n = m.missing.size();
            if (n > 0) showToast(sayn((long)n, "{1} file is missing \xE2\x80\x94 File \xE2\x80\xBA Relink\xE2\x80\xA6", "{1} files are missing \xE2\x80\x94 File \xE2\x80\xBA Relink\xE2\x80\xA6", (long)n),
                                 ToastKind::Warning); // the song IS open: owed, not refused (R-UI-15)
        }
        syncHistory(m);
        if (m.recovery.empty()) mRecoveryAsked.clear();
        else if (m.recovery != mRecoveryAsked) { mRecoveryAsked = m.recovery; askRecovery(m); }
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
        if (mHome->opacity.isAnimating() || mProject->opacity.isAnimating() || mToast->isShowing()) return true;
        if (mScale.isAnimating() || mContrast.isAnimating() || mFocusOn || mRingA.value() > 0.0) return true; // R-ACCESS-1: a zoom, a fade, the ring
        if (m.assist.state == "starting" || m.assist.state == "busy") return true; // R-AGENT-4: the working line pulses while the agent works
        if (!mRenderLine.empty() || mRenderCloseWanted || mRenderAmt.isAnimating() || mRenderAmt.value() > 0.0) return true; // R-RENDER-4
        if (m.settings.uiScale != mScaleWant || (m.settings.highContrast ? 1 : 0) != mContrastWant) return true;
        if (mLangFade.isAnimating() || solaris_strings::resolve(m.settings.language, mOsLanguage) != mLangTo) return true; // R-ACCESS-2: a dissolve
        if (mSettings->appearAmount() > 0.0 || mExport->appearAmount() > 0.0 || mRelink->appearAmount() > 0.0 || mConflicts->appearAmount() > 0.0 || mMenu->isOpen() || mHistory->appearAmount() > 0.0) return true;
        return nowMs - mLastActivityMs < kActiveWindowMs;
    }

    void App::pointer(int kind, double x, double y, int button, double timeMs, bool alt, bool shift, bool ctrl)
    {
        x = (x - mOx) / mScale.value(); // R-ACCESS-1: the inverse of the root transform, once, at the entry point (R-HOST-2: its origin too)
        y = (y - mOy) / mScale.value();
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

    void App::wheel(double x, double y, double notches, bool ctrl, bool shift, bool alt)
    {
        x = (x - mOx) / mScale.value(); // R-ACCESS-1, R-HOST-2
        y = (y - mOy) / mScale.value();
        mLastActivityMs = mNowMs;
        RawPointer rp{};
        rp.kind = RawPointer::Kind::Scroll;
        rp.pos = Point{x, y};
        rp.timeMs = mNowMs;
        rp.ctrl = ctrl;
        rp.shift = shift; // D-5: the roll's and the lanes' sideways scroll
        rp.alt = alt;     // R-ROLL-13: the roll's Alt+wheel
        rp.scroll = Point{0.0, -notches * 40.0}; // + notches = wheel up = toward the start (Interstellar's notch)
        mRecognizer.feed(rp);
    }

    // ── R-ACCESS-1: the accessible list, and the keyboard's focus over it ─────────────────────────────────────────────

    std::vector<A11yNode> App::accessibles()
    {
        std::vector<A11yNode> out;
        auto &ms = mProject->bar().menus();
        // a modal's alone while it is open
        if (mMenu->isOpen())
        {
            if (mMenu->isRenaming()) return out; // a field being typed in: the keys are its
            for (int i = 0; i < mMenu->itemCount(); ++i)
                out.push_back(a11yNode("menu.item." + std::to_string(i), "menuitem", mMenu->item(i).label, mMenu->itemRect(i)));
            return out;
        }
        if (mConfirm->isOpen())
        {
            for (int i = 0; i < mConfirm->buttonCount(); ++i)
            {
                A11yNode n = a11yNode("confirm." + std::to_string(i), "button", mConfirm->buttonLabel(i), worldRect(*mConfirm, mConfirm->buttonRect(i)));
                n.activate = [this, i] { mConfirm->activate(i); };
                out.push_back(n);
            }
            return out;
        }
        if (mSettings->isOpen()) { mSettings->accessibles(out); return out; }
        if (mExport->isOpen()) { mExport->accessibles(out); return out; }
        if (mRelink->isOpen()) { mRelink->accessibles(out); return out; }
        if (mConflicts->isOpen()) { mConflicts->accessibles(out); return out; }
        if (mHistory->isOpen()) return out; // the history's tree: Not yet
        if (mScreen == "home") { mHome->accessibles(out); return out; }
        if (ms.openIndex() >= 0) // a menu of the song bar's, open: its items
        {
            const int open = ms.openIndex();
            for (int i = 0; i < (int)ms.menu(open).items.size(); ++i)
            {
                const auto &it = ms.menu(open).items[(size_t)i];
                if (it.separator) continue; // a rule is not a control; a row keeps its index (R-FAM-1)
                A11yNode n = a11yNode("menu." + english(mMenuWords[(size_t)open].first) + "." + std::to_string(i), "menuitem", it.label, worldRect(ms, ms.itemRect(open, i)), it.shortcut);
                n.enabled = it.enabled;
                out.push_back(n);
            }
            return out;
        }
        // the song view, in reading order
        std::vector<A11yNode> bar;
        mProject->bar().accessibles(bar);
        for (auto &n : bar)
        {
            out.push_back(n);
            if (n.key == "bar.home") // the menus' titles, where they sit: after the wordmark (R-FAM-1)
                for (int i = 0; i < ms.menuCount(); ++i)
                    out.push_back(a11yNode("menu." + english(mMenuWords[(size_t)i].first), "menu", fillIn(tr("{1} menu"), ms.menu(i).title), worldRect(ms, ms.titleRect(i))));
        }
        if (mProject->browserOpen()) mProject->browser().accessibles(out);
        if (!mProject->sessionOpen()) mProject->timeline().accessibles(out); // the session grid: Not yet
        if (mProject->dockOpen()) mProject->dock().accessibles(out);
        mProject->windows().accessibles(out);
        return out;
    }

    bool App::activateNode(const std::string &key)
    {
        for (auto &n : accessibles())
            if (n.key == key)
            {
                if (!n.enabled) return false;
                if (n.activate) n.activate();
                else clickAt(Point{n.bounds.x + n.bounds.w * 0.5, n.bounds.y + n.bounds.h * 0.5});
                return true;
            }
        return false;
    }

    bool App::stepNode(const std::string &key, int dir, bool fine)
    {
        for (auto &n : accessibles())
            if (n.key == key)
            {
                if (!n.enabled || !n.step) return false;
                n.step(dir, fine);
                return true;
            }
        return false;
    }

    void App::clickAt(Point p)
    {
        // the click a node stands for — a press and a release at its centre, through the same routes as a pointer's,
        // on a recogniser of its own whose clock runs a second a click, so two never make a double-click
        mKeyClickClock += 1000.0;
        RawPointer rp{};
        rp.pos = p;
        rp.button = PointerButton::Left;
        rp.kind = RawPointer::Kind::Down;
        rp.timeMs = mKeyClickClock;
        mKeyClicks.feed(rp);
        rp.kind = RawPointer::Kind::Up;
        rp.timeMs = mKeyClickClock + 1.0;
        mKeyClicks.feed(rp);
    }

    bool App::focusKeyPress(const KeyEvent &e)
    {
        if (e.type != KeyEvent::Type::Down || e.ctrl || e.alt) return false;
        if (mSettings->isOpen() && !mSettings->listeningFor().empty()) return false; // a key being captured is the sheet's
        if (mMenu->isOpen() && mMenu->isRenaming()) return false;                  // a field being typed in
        const int k = e.keyCode;
        if (k == 9) // Tab, Shift+Tab: the next / the previous enabled node
        {
            std::vector<std::string> keys;
            for (const auto &n : accessibles())
                if (n.enabled && n.bounds.w > 0 && n.bounds.h > 0) keys.push_back(n.key);
            if (keys.empty()) return true;
            const auto it = std::find(keys.begin(), keys.end(), mFocusKey);
            const int n = (int)keys.size();
            int at = 0;
            if (mFocusOn && it != keys.end()) at = ((int)(it - keys.begin()) + (e.shift ? -1 : 1) + n) % n;
            else at = e.shift ? n - 1 : 0;
            mFocusKey = keys[(size_t)at];
            mFocusOn = true;
            if (onFocusMoved) onFocusMoved(mFocusKey);
            return true;
        }
        if (!mFocusOn) return false;
        if (k == 27) { mFocusOn = false; return true; }                       // Escape: let go
        if (k == 13 || k == 32) { activateNode(mFocusKey); return true; }     // Enter, Space: the click it stands for
        if (k >= 37 && k <= 40) { stepNode(mFocusKey, k == 37 || k == 40 ? -1 : 1, e.shift); return true; } // the arrows: its step
        return false;
    }

    void App::advanceFocus(double nowMs)
    {
        // the ring: over the focused node, travelling between nodes, fading in and out — a node gone lets the focus go
        Rect want{0, 0, 0, 0};
        bool on = false;
        if (mFocusOn)
        {
            for (const auto &n : accessibles())
                if (n.key == mFocusKey) { want = n.bounds; on = true; break; }
            if (!on) mFocusOn = false;
        }
        const double a = on ? 1.0 : 0.0;
        if (a != mRingWantA)
        {
            mRingA.animateTo(a, a > 0.0 ? motion::kHoverMs : motion::kModalCloseMs, Easing::EaseOutCubic, nowMs);
            mRingWantA = a;
        }
        if (on)
        {
            if (!mRingPlaced || (mRingA.value() <= 0.001 && !mRingA.isAnimating()))
            {
                mRingX.set(want.x); mRingY.set(want.y); mRingW.set(want.w); mRingH.set(want.h);
                mRingPlaced = true;
            }
            else if (want.x != mRingTarget.x || want.y != mRingTarget.y || want.w != mRingTarget.w || want.h != mRingTarget.h)
            {
                mRingX.animateTo(want.x, motion::kSlideMs, Easing::EaseOutCubic, nowMs);
                mRingY.animateTo(want.y, motion::kSlideMs, Easing::EaseOutCubic, nowMs);
                mRingW.animateTo(want.w, motion::kSlideMs, Easing::EaseOutCubic, nowMs);
                mRingH.animateTo(want.h, motion::kSlideMs, Easing::EaseOutCubic, nowMs);
            }
            mRingTarget = want;
        }
        for (auto *p : {&mRingX, &mRingY, &mRingW, &mRingH, &mRingA}) p->update(nowMs);
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
        if (!mRenderLine.empty() || mRenderAmt.value() > 0.001) return true; // R-RENDER-4: modal — it swallows the keys
        if (focusKeyPress(e)) return true; // R-ACCESS-1: Tab, and while the focus is held Enter / Space / the arrows / Escape
        if (mMenu->isOpen()) { mMenu->dispatchKey(e); return true; }
        if (mConfirm->isOpen()) { mConfirm->handleKey(e); return true; }
        if (mSettings->isOpen()) return mSettings->handleKey(e);
        if (mExport->isOpen()) return mExport->handleKey(e);
        if (mRelink->isOpen()) return mRelink->handleKey(e);
        if (mConflicts->isOpen()) return mConflicts->handleKey(e);
        // R-AGENT-4: typing to the agent — the Space in "make a beat" is not Play. The focused field takes the key.
        if (mScreen == "project" && mProject->agentOpen() && mProject->agent().isTyping()) { mProject->dispatchKey(e); return true; }
        if (e.type != KeyEvent::Type::Down) return false;
        // R-FAM-1: an open menu of the song bar's takes the arrows, Home / End, Enter and Escape
        if (mScreen == "project" && mProject->bar().menus().openIndex() >= 0 && mProject->bar().menus().menuKey(e)) return true;
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
        // unmoved, right after the last copy. R-CLIP-11: copied clips, the earliest at the time bar and the rest keeping
        // their spacing and lanes — ONE line too
        if (mClipClipboard.empty()) return false;
        double first = 1e300, last = -1e300;
        for (const auto &id : mClipClipboard)
        {
            const solaris::ClipModel *src = nullptr;
            for (const auto &c : m.clips)
                if (c.id == id) src = &c;
            if (!src) { showToast(say("{1} is gone \xE2\x80\x94 copy a clip again", id)); mClipClipboard.clear(); return true; }
            first = std::min(first, src->at);
            last = std::max(last, src->at + src->length);
        }
        const double pos = m.transport.position;
        const double at = mPasteAt >= 0.0 && std::fabs(pos - mPasteAt) < 1e-9 ? mPasteNext : pos;
        mProject->timeline().expectCopy(false); // the copies are selected (they fade in: nothing showed them yet)
        bool ok = false;
        if (mClipClipboard.size() == 1)
            ok = dispatch("clip duplicate " + mClipClipboard[0] + " --at " + Timeline::beatText(std::round(at * 960.0) / 960.0)); // on the tick
        else
        {
            std::string ids;
            for (const auto &id : mClipClipboard) ids += (ids.empty() ? "" : " ") + id;
            ok = dispatch("clips duplicate \"" + ids + "\" --by " + Timeline::beatText(std::round((at - first) * 960.0) / 960.0));
        }
        if (ok)
        {
            mPasteAt = pos;
            mPasteNext = at + (last - first);
        }
        return true;
    }

    std::string App::keyHint(const solaris::AppModel &m, const std::string &action)
    {
        // a menu names the key the keymap has NOW (R-SET-4) — re-keyed, the hint follows; no key, no hint
        // — as its character where it has one ("Ctrl+," for the keymap's "Ctrl+Comma": what the key cap says)
        static const std::pair<const char *, const char *> kChars[] = {{"Comma", ","}, {"Period", "."}, {"Slash", "/"}, {"Semicolon", ";"}, {"Equal", "="}, {"Minus", "-"},
                                                                       {"Backquote", "`"}, {"BracketLeft", "["}, {"BracketRight", "]"}, {"Backslash", "\\"}, {"Quote", "'"}};
        for (const auto &k : m.settings.keys)
            if (k.id == action && !k.keys.empty())
            {
                std::string combo = k.keys.front();
                const size_t plus = combo.find_last_of('+');
                const std::string last = plus == std::string::npos ? combo : combo.substr(plus + 1);
                for (const auto &c : kChars)
                    if (last == c.first) combo = combo.substr(0, combo.size() - last.size()) + c.second;
                return combo;
            }
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
        // R-FAM-8: New and Open are the host's pickers — on Home too
        if (id == "song.new") { mNewTemplate.clear(); if (onPickSongToCreate) onPickSongToCreate(); return true; }
        if (id == "song.open") { if (onPickSongToOpen) onPickSongToOpen(); return true; }
        if (id.empty() || mScreen != "project") return false;
        if (id == "song.saveAs")
        {
            if (onPickSave) onPickSave(tr("Save the song as"), m.projectName + ".slp", [this](const std::string &p) { dispatch("project save " + quote(p)); });
            return true;
        }
        if (id == "transport.play" || id == "transport.stopHere")
        {
            // play/stop goes back where the time bar was put; stop-here leaves it where it was heard (R-TIME-7/8)
            if (m.transport.playing) return dispatch(id == "transport.play" ? "transport stop --return" : "transport stop");
            if (mRollKeys) // R-ROLL-6 (amended): a roll's ▶ while it has the keys — a click on the lanes gives Space to the song
                if (PianoRoll *r = mProject->windows().frontRoll())
                    if (r->togglePlay()) return true;
            return dispatch("transport play");
        }
        if (id == "transport.toStart") return dispatch("transport seek 0");
        if (id == "song.save") return dispatch("project save");
        if (id == "song.undo") return dispatch("undo");
        if (id == "song.redo") return dispatch("redo"); // R-EDM-1
        if (id == "lanes.paste") return pasteClip(m);
        // R-CLIP-11: what is done to the selected clip is done to the selection — ONE line however many
        const std::vector<std::string> sel = mProject->timeline().selectedClips();
        if (sel.empty()) return false;
        std::string ids;
        for (const auto &c : sel) ids += (ids.empty() ? "" : " ") + c;
        if (id == "lanes.delete") return dispatch(sel.size() == 1 ? "clip delete " + ids : "clips delete \"" + ids + "\"");
        if (id == "lanes.duplicate")
        {
            mProject->timeline().expectCopy(false); // the copies are selected, so Duplicate again runs on after them
            return dispatch(sel.size() == 1 ? "clip duplicate " + ids : "clips duplicate \"" + ids + "\""); // linked copies after them
        }
        if (id == "lanes.split") // R-EDIT-1: the clip clicked last
            return dispatch("clip split " + mProject->timeline().selectedClip() + " --at " + Timeline::beatText(std::round(m.transport.position * 960.0) / 960.0));
        if (id == "lanes.copy")
        {
            // R-CLIP-10: nothing is sent — the window keeps which clips; Ctrl+V makes the copies
            mClipClipboard = sel;
            mPasteAt = -1.0;
            const std::string paste = keyHint(m, "lanes.paste");
            const std::string n = std::to_string(sel.size());
            if (sel.size() == 1)
                showToast(paste.empty() ? say("Clip copied \xE2\x80\x94 Paste puts it at the time bar")
                                        : say("Clip copied \xE2\x80\x94 {1} puts it at the time bar", paste),
                          ToastKind::Info);
            else
                showToast(paste.empty() ? say("{1} clips copied \xE2\x80\x94 Paste puts them at the time bar", n)
                                        : say("{1} clips copied \xE2\x80\x94 {2} puts them at the time bar", n, paste),
                          ToastKind::Info);
            return true;
        }
        return false;
    }

    void App::render(IRenderTarget &target, double nowMs)
    {
        mNowMs = nowMs;
        // R-RENDER-4: the frame before this one painted the "Rendering…" modal whole — NOW the line goes out. The service
        // runs it to its end here, so this frame's clock is stale when it returns: what was showing is painted again, as
        // it was, and everything the render changed (the modal's close, the toast) starts on the next frame's clock
        if (!mRenderLine.empty() && mRenderPainted)
        {
            const std::string line = std::move(mRenderLine);
            mRenderLine.clear();
            mRenderPainted = false;
            dispatch(line);
            mRenderCloseWanted = true;
            paintPass(target, rootTransform(), mLangTo);
            return;
        }
        if (mRenderOpening)
        {
            mRenderAmt.animateTo(1.0, motion::kModalOpenMs, Easing::EaseOutCubic, nowMs);
            mRenderOpening = false;
            mRenderT0 = nowMs; // the sweep's clock
        }
        else if (mRenderCloseWanted)
        {
            mRenderAmt.animateTo(0.0, motion::kModalCloseMs, Easing::EaseOutCubic, nowMs);
            mRenderCloseWanted = false;
        }
        mRenderAmt.update(nowMs);
        bindIfStale();
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        refreshMenus(m); // R-FAM-1: every frame — a selection made moves no revision, and it enables rows
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
        // the toast: fades in, HOLDS, fades out slowly — the family's widget, in the band under the song bar (R-FAM-4)
        mToast->x.set(0.0);
        mToast->y.set(kToastTop);
        mToast->width.set(mW);
        mToast->height.set(cosmo_v2::Toast::kHeight);
        mToast->advance(nowMs);
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
        advanceLanguage(m, nowMs); // R-ACCESS-2
        const Transform root = rootTransform();

        // everything advances ONCE a frame …
        for (Segment *s : {(Segment *)mHome.get(), (Segment *)mProject.get()})
        {
            const bool wanted = (s == mHome.get()) == (mScreen == "home");
            if (!wanted && s->opacity.value() <= 0.0 && !s->opacity.isAnimating()) continue;
            if (s == mHome.get()) mHome->layout();
            else mProject->layout();
            s->advance(nowMs);
        }
        // the modals float over whichever screen opened them; advanced always, so a close can finish
        mSettings->advance(nowMs);
        mExport->advance(nowMs);   // R-EXPORT-1
        mRelink->advance(nowMs);   // R-COLLECT-1
        mConflicts->advance(nowMs); // R-COLLAB-1
        mModalRoot->advance(nowMs);
        advanceFocus(nowMs);       // R-ACCESS-1
        // … and paints once — or, while a language switch dissolves, twice: the old language, then the new one over it at
        // the dissolve's amount (the new one last, so every widget's hit rectangles are the new language's — R-ACCESS-2)
        if (mLangFrom != mLangTo && mLangFade.value() < 1.0)
        {
            paintPass(target, root, mLangFrom);
            target.pushLayer(mLangFade.value());
            paintPass(target, root, mLangTo);
            target.popLayer();
        }
        else paintPass(target, root, mLangTo);
    }

    void App::paintPass(IRenderTarget &target, const Transform &root, const std::string &language)
    {
        solaris_strings::setLanguage(language);
        relabel();
        target.save();
        target.setTransform(Transform::translation(mOx, mOy)); // R-HOST-2: the frame another window gave it
        drawRoundedRect(target, Rect{0, 0, mPhysW, mPhysH}, 0.0, Paint::filled(palette::background()));
        target.restore();
        for (Segment *s : {(Segment *)mHome.get(), (Segment *)mProject.get()})
        {
            if (s->opacity.value() <= Segment::kOpacityEpsilon) continue;
            s->render(target, root);
            s->renderOverlay(target, root);
        }
        mSettings->render(target, root);        // its card, its pages and their controls (R-SET-5/6) …
        mSettings->renderOverlay(target, root); // … and an open combo's list, over everything
        mExport->render(target, root);
        mExport->renderOverlay(target, root);
        mRelink->render(target, root);
        mConflicts->render(target, root);
        mModalRoot->renderOverlay(target, root);

        // R-RENDER-4: "Rendering…" over everything but the toast; a frame that painted it whole lets the line go
        if (mRenderAmt.value() > 0.001)
        {
            target.save();
            target.setTransform(root);
            paintRender(target);
            target.restore();
            if (!mRenderLine.empty() && mRenderAmt.value() >= 1.0 && !mRenderAmt.isAnimating()) mRenderPainted = true;
        }

        // R-ACCESS-1: the keyboard's focus ring, over everything but the toast
        if (const double fa = mRingA.value(); fa > 0.001)
        {
            const Rect fr = focusRing();
            target.save();
            target.setTransform(root);
            Color c = palette::primary();
            c.a = 0.9 * fa;
            drawRoundedRect(target, Rect{fr.x - 2.0, fr.y - 2.0, fr.w + 4.0, fr.h + 4.0}, radius::control() + 2.0, Paint::stroked(c, 2.0));
            target.restore();
        }
        // R-UI-15 / R-FAM-4: the toast, over everything — drawn in logical units, as everything else (R-ACCESS-1)
        mToast->renderOverlay(target, root);
    }
}
}
