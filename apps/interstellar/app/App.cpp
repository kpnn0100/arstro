#include "App.h"
#include <filesystem>
#include "widgets/CommandLine.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;
    using interstellar::Screen;

    namespace
    {
        constexpr double kActiveWindowMs = 700.0;   // longer than the longest tween (520 ms in cosmo)
        constexpr int kKeyBackspace = 8, kKeyEsc = 27, kKeySpace = 32, kKeyLeft = 37, kKeyRight = 39, kKeyDelete = 46;
        constexpr int kKeyComma = 188, kKeyPeriod = 190;   // R-EDT-1: Insert, Overwrite (OEM codes, as the host sends them)

    }

    const interstellar::AppModel &App::emptyModel()
    {
        static const interstellar::AppModel m;
        return m;
    }

    App::App(AppHooks hooks, double width, double height)
        : mHooks(std::move(hooks)), mW(width), mH(height), mPhysW(width), mPhysH(height)
    {
        installInterstellarAccent();   // FIRST: before any widget reads the accent

        mHome = std::make_shared<HomeScreen>();
        mHome->thumbnail = mHooks.thumbnail;
        mHome->onNewProject = [this] { if (onPickProjectToCreate) onPickProjectToCreate(); };
        mHome->onOpenProject = [this] { if (onPickProjectToOpen) onPickProjectToOpen(); };
        mHome->onSettings = [this] { openSettings(); };
        mHome->onOpenRecent = [this](int i) {
            const auto &m = mHooks.model ? mHooks.model() : emptyModel();
            std::vector<interstellar::RecentModel> rec = m.recents;
            std::stable_sort(rec.begin(), rec.end(), [](const auto &a, const auto &b) { return a.lastOpened > b.lastOpened; });
            if (i < 0 || i >= (int)rec.size()) return;
            mLoading->setProjectName(rec[i].name);   // the loading screen names what is opening
            dispatch("project open " + cmd::quote(rec[i].path));
        };
        mLoading = std::make_shared<LoadingView>();
        mEdit = std::make_shared<EditScreen>();
        mEdit->onCommand = [this](const std::string &l) { return dispatch(l); };
        mEdit->onAddFootage = [this] { if (onPickFootage) onPickFootage(); };
        mEdit->onHome = [this] { requestHome(); };
        mEdit->gradeDeck()->thumbnail = mHooks.thumbnail;
        mEdit->timeline()->peaksFor = mHooks.audioPeaks;   // R-AUD-7
        mEdit->onRackContext = [this](int i, Point at) { openRackContext(i, at); };
        mEdit->onCapture = [this](Rect r) { openCaptureMenu(r); };
        mEdit->monitor()->onAngle = [this](int k) { dispatch("multicam angle " + std::to_string(k)); };   // R-EDT-5
        // the image-processing stack (R-FX-5): the catalog menu and a row's menu
        // the CLIP switch in the scopes paints the monitor's overlay from the frame it shows
        mEdit->gradeInspector()->scopes()->onClipWarning = [this](bool on) {
            if (on && !mFrame.empty()) mEdit->monitor()->setClipMask(clipMaskOf(mFrame));
            mEdit->monitor()->setClipWarning(on);
        };
        mEdit->gradeInspector()->onAddEffect = [this](Rect r) { openAddEffectMenu(r); };
        mEdit->gradeInspector()->onPluginContext = [this](const std::string &id, Point w) { openPluginContext(id, w); };
        // a LUT effect's file row (R-COLOR-5): the host picks the .cube, the line sets it
        mEdit->gradeInspector()->effectPanel()->onChooseFile = [this](const std::string &id, const std::string &key) {
            if (onPickLutToOpen)
                onPickLutToOpen([this, id, key](const std::string &path) { dispatch("set " + cmd::quote(id + "." + key + "=" + path)); });
        };
        // the Cut tab, like an editor (R-UI-14)
        mEdit->onDropSource = [this](const std::string &src, const std::string &track, double at) { dropSource(src, track, at); };
        mEdit->onClipContext = [this](const std::string &id, Point w) { openClipContext(id, w); };
        mEdit->onKeyContext = [this](const std::string &a, double t, Point w) { openKeyContext(a, t, w); };
        mEdit->onKeyPlotContext = [this](double t, Point w) { openKeyPlotContext(t, w); };
        mEdit->onLaneContext = [this](const std::string &trk, double t, Point w) { openLaneContext(trk, t, w); };
        // the ref-frame slider previews in the monitor while dragged, nothing committed (R-RACK-3)
        mEdit->gradeDeck()->onPreview = [this](const std::string &bind, double t) {
            mPreviewBind = t < 0 ? std::string() : bind;
            mPreviewAt = t;
            noteActivity();
        };
        mEdit->contextMenu()->onRename = [this](const std::string &typed) {
            const std::string name = cmd::bindName(typed);
            if (!name.empty() && !mRenameTarget.empty() && name != mRenameTarget)
                dispatch("rack rename " + cmd::quote(mRenameTarget) + " " + cmd::quote(name));
            mRenameTarget.clear();
        };
        // Engine Settings: COSMO's dialog, one instance for Home and Edit (cosmo R-SETTINGS-5).
        // Each chip is a `settings set` line — the service owns and persists the values; the
        // Input row is hidden because Interstellar has no touch shell.
        mSettings = std::make_shared<cosmo_v2::SettingsDialog>(palette::primary());
        mSettings->setInputRowShown(false);
        mSettings->onUiScale = [this](int v) { dispatch("settings set uiScale=" + std::to_string(v)); };
        mSettings->onPreviewEdge = [this](int v) { dispatch("settings set previewEdge=" + std::to_string(v)); };
        mSettings->onThreads = [this](int v) { dispatch("settings set threads=" + std::to_string(v)); };
        mSettings->onCpuPercent = [this](int v) { dispatch("settings set cpuPercent=" + std::to_string(v)); };
        mSettings->onUseGpu = [this](bool on) { dispatch(std::string("settings set useGpu=") + (on ? "1" : "0")); };
        // Interstellar's own row, after cosmo's (opt-in: cosmo's dialog has none). R-PLAY-3
        mSettings->setExtraRows({{"Hardware video", "H.264 / H.265 on the video unit", {"Off", "On"}, 0,
                                  [this](int i) { dispatch(std::string("settings set hardwareVideo=") + (i ? "1" : "0")); }},
                                 {"Preview cache", "graded frames, built when idle", {"Off", "On"}, 1,   // R-PLAY-1
                                  [this](int i) { dispatch(std::string("settings set previewCache=") + (i ? "1" : "0")); }}});
        buildMenus();

        mRecognizer.setSink([this](const Gesture &g) {
            if (mSettings->isOpen()) { mSettings->onGesture(g); return; }   // a modal owns input
            if (mScreen == Screen::Home)
                mHome->onGesture(g);
            else if (mScreen == Screen::Edit)
                mEdit->onGesture(g);
            // Screen::Loading swallows input: the transition is not interactive
        });
        layoutAll();
    }

    void App::setSize(double width, double height)
    {
        mPhysW = std::max(1.0, width);
        mPhysH = std::max(1.0, height);
        noteActivity();
        applyLogicalSize();
    }

    // ── screen scale: cosmo's R-SCALE, as cosmo's App does it ───────────────────────────
    // The DRAWN scale eases (260 ms, the shell zoom); while it moves, the logical box and the
    // whole layout are re-derived from it every frame — deriving them once would zoom the
    // transform and leave the panels at the old size, which is worse than a snap.

    void App::setUiScale(int percent, bool animate)
    {
        if (percent == mUiScale && mScaleBound) return;
        mUiScale = percent;
        if (animate) mScaleAnim.animateTo(percent / 100.0, 260.0, Easing::EaseOutCubic, mNowMs);
        else mScaleAnim.set(percent / 100.0);
        applyLogicalSize();
    }

    void App::applyLogicalSize()
    {
        const double sc = std::max(0.25, mScaleAnim.value());
        mW = std::max(1.0, mPhysW / sc);
        mH = std::max(1.0, mPhysH / sc);
        layoutAll();
    }

    Transform App::rootTransform() const
    {
        const double sc = mScaleAnim.value();
        return Transform::scaling(sc, sc);
    }

    void App::openSettings()
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        const auto &st = m.settings;
        mSettings->setExtraSelected(0, st.hardwareVideo ? 1 : 0);
        mSettings->setExtraSelected(1, st.previewCache ? 1 : 0);
        mSettings->show(st.uiScale, mMaxUiScale, st.previewEdge, st.threads, st.cpuPercent, st.useGpu, st.gpuAvailable, false);
        noteActivity();
    }

    std::string App::selectedBind() const
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        if (m.selectedRack < 0 || m.selectedRack >= (int)m.rack.size()) return std::string();
        return m.rack[(size_t)m.selectedRack].bindName;
    }

    /** Cosmo's right-click menu on a rack node (cosmo's openEditContext, Interstellar's items).
     *  Right-clicking OUTSIDE the selection selects that node first; inside it, the selection stays,
     *  so "Group Selection" groups all of it — cosmo's rule. */
    void App::openRackContext(int i, Point at)
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        if (i < 0 || i >= (int)m.rack.size()) return;
        const auto n = m.rack[(size_t)i];
        if (n.bindName.empty()) return;
        const std::string b = cmd::quote(n.bindName);
        if (!n.selected) dispatch("rack select " + b);
        const auto &m2 = mHooks.model ? mHooks.model() : emptyModel();   // after the select
        std::string selection;
        int selected = 0;
        for (const auto &r : m2.rack)
            if (r.selected && !r.bindName.empty()) { selection += " " + cmd::quote(r.bindName); ++selected; }
        std::vector<cosmo_v2::ContextMenu::Item> items;
        items.push_back({"Add Footage...", [this] { if (onPickFootage) onPickFootage(); }});
        items.push_back({selected > 1 ? "Group Selection  (Ctrl+G)" : "Group  (Ctrl+G)", [this] { dispatch("rack group new"); }});
        if (n.group) items.push_back({"Ungroup", [this, b] { dispatch("rack ungroup " + b); }});
        if (!n.failed)
            items.push_back({n.bypass ? "Enable Filter" : "Disable Filter",
                             [this, name = n.bindName, on = !n.bypass] { dispatch("set " + name + ".bypass=" + (on ? "1" : "0")); }});
        items.push_back({"Rename...", [this, name = n.bindName] {
            mRenameTarget = name;
            mEdit->contextMenu()->enterRenameMode(name);
        }});
        if (!n.group && !n.failed) items.push_back({"Duplicate as Variant  (Ctrl+D)", [this, b] { dispatch("rack duplicate " + b); }});
        // R-MEDIA-3: a missing file, found where it is now
        if (!n.group && n.failed && n.offlineWhy.empty())
            items.push_back({"Relink...", [this, b, file = std::filesystem::path(n.media).filename().string()] {
                if (onPickMediaToRelink)
                    onPickMediaToRelink(file, [this, b](const std::string &p) { dispatch("media relink " + b + " " + cmd::quote(p)); });
            }});
        // R-MEDIA-2: a lighter file for the monitor
        if (!n.group && !n.failed && n.video)
        {
            bool making = false;
            for (const auto &j : m2.proxyJobs) making = making || (j.rackObj == n.rackObj && (j.state == "queued" || j.state == "running"));
            if (!making) items.push_back({n.proxy.empty() ? "Make Proxy" : "Make Proxy Again", [this, b] { dispatch("proxy make " + b); }});
            if (!n.proxy.empty() || making) items.push_back({making ? "Cancel Proxy" : "Remove Proxy", [this, b] { dispatch("proxy remove " + b); }});
        }
        if (!n.group)
        {
            // R-COLOR-2: what the source IS — the list opens in place of this menu
            std::string now = "Rec.709";
            for (const auto &c : m2.colourInputs) if (c.id == n.input) now = c.label;
            items.push_back({"Input Colour (" + now + ")...", [this, name = n.bindName, at] { openInputColourMenu(name, at); }});
            // R-COLOR-5/6: an input LUT in, the source's colour out
            items.push_back({n.lut.empty() ? "Input LUT..." : "Change Input LUT...", [this, name = n.bindName] {
                if (onPickLutToOpen)
                    onPickLutToOpen([this, name](const std::string &path) { dispatch("set " + cmd::quote(name + ".lut=" + path)); });
            }});
            if (!n.lut.empty()) items.push_back({"Remove Input LUT", [this, name = n.bindName] { dispatch("set " + cmd::quote(name + ".lut=none")); }});
            if (!n.failed)
                items.push_back({"Export LUT...", [this, name = n.bindName, b] {
                    if (onPickLutToSave)
                        onPickLutToSave(name + ".cube", [this, b](const std::string &path) { dispatch("lut export " + b + " --out " + cmd::quote(path)); });
                }});
        }
        if (!n.failed) items.push_back({"Copy Grade", [this, b] { dispatch("grade copy " + b); }});
        if (m2.hasGradeClipboard)
            items.push_back({selected > 1 ? "Paste Grade to Selection" : "Paste Grade", [this, selection] { dispatch("grade paste" + selection); }});
        if (!n.group && n.usedBy == 0) items.push_back({"Remove from Rack", [this, b] { dispatch("rack remove " + b); }});
        mEdit->contextMenu()->open(std::move(items), at.x, at.y);
        noteActivity();
    }

    // ── the menu bar: cosmo's File / Develop / History / Settings / Preset, Interstellar's words ──
    // Every item is a command line or a host picker — the menus add no behaviour of their own.

    void App::buildMenus()
    {
        auto ms = mEdit->topBar()->menus();
        ms->addMenu({"File", {
            {"Home",                         [this] { requestHome(); }},
            {"Open...      (Ctrl+O)",        [this] { if (onPickProjectToOpen) onPickProjectToOpen(); }},
            {"Save         (Ctrl+S)",        [this] { dispatch("project save"); }},
            {"Save As...   (Ctrl+Shift+S)",  [this] { if (onPickSaveAs) onPickSaveAs(); }},
            {"Add Footage...",               [this] { if (onPickFootage) onPickFootage(); }},
            {"Export Still...",              [this] { if (onPickStillToExport) onPickStillToExport(); }},
            // R-XCH: a cut for another editor, and back
            {"Import Timeline...",           [this] {
                 if (onPickTimelineToImport)
                     onPickTimelineToImport([this](const std::string &p) { dispatch("interchange import " + cmd::quote(p)); });
             }},
            {"Export Timeline...",           [this] {
                 const auto &m = mHooks.model ? mHooks.model() : emptyModel();
                 std::string name;
                 for (const auto &tl : m.timelines) if (tl.id == m.currentTimeline) name = tl.name;
                 if (name.empty() || !onPickTimelineToExport) return;
                 onPickTimelineToExport(name + ".fcpxml", [this, name](const std::string &p) {
                     dispatch("interchange export " + cmd::quote(name) + " --out " + cmd::quote(p));
                 });
             }},
            {"Relink Media...",              [this] { openRelinkMenu(Point(width() * 0.25, 40.0)); }},   // R-MEDIA-3
            {"Render...",                    [this] { mEdit->setTab(EditScreen::Deliver); }},
        }});
        ms->addMenu({"Edit", {
            {"Undo         (Ctrl+Z)",        [this] { dispatch("undo"); }},
            {"Redo         (Ctrl+Y)",        [this] { dispatch("redo"); }},
            {"Copy Grade   (Ctrl+C)",        [this] { const auto b = selectedBind(); if (!b.empty()) dispatch("grade copy " + cmd::quote(b)); }},
            {"Paste Grade to Selected (Ctrl+V)", [this] { const auto b = selectedBind(); if (!b.empty()) dispatch("grade paste " + cmd::quote(b)); }},
            {"Paste Grade to All Sources",   [this] { dispatch("grade paste --all"); }},
            {"Group Selection  (Ctrl+G)",    [this] { dispatch("rack group new"); }},
            // R-EDT-1: three-point editing, the keys beside them
            {"Mark In      (I)",             [this] { const auto &mm = mHooks.model ? mHooks.model() : emptyModel(); dispatch(std::string("mark in") + (mEdit->tab() == EditScreen::Cut && !mm.sourceView.empty() ? " --source" : "")); }},
            {"Mark Out     (O)",             [this] { const auto &mm = mHooks.model ? mHooks.model() : emptyModel(); dispatch(std::string("mark out") + (mEdit->tab() == EditScreen::Cut && !mm.sourceView.empty() ? " --source" : "")); }},
            {"Clear In / Out",               [this] { const auto &mm = mHooks.model ? mHooks.model() : emptyModel(); dispatch(std::string("mark clear") + (mEdit->tab() == EditScreen::Cut && !mm.sourceView.empty() ? " --source" : "")); }},
            {"Insert       (,)",             [this] { dispatch("edit insert"); }},
            {"Overwrite    (.)",             [this] { dispatch("edit overwrite"); }},
            {"Ungroup",                      [this] { const auto b = selectedBind(); if (!b.empty()) dispatch("rack ungroup " + cmd::quote(b)); }},
            {"Duplicate as Variant (Ctrl+D)", [this] { const auto b = selectedBind(); if (!b.empty()) dispatch("rack duplicate " + cmd::quote(b)); }},
        }});
        ms->addMenu({"Settings", {
            {"Engine Settings...",           [this] { openSettings(); }},
        }});
        ms->addMenu({"Workspace", {
            {"Grade        (1)",             [this] { mEdit->setTab(EditScreen::Grade); }},
            {"Cut          (2)",             [this] { mEdit->setTab(EditScreen::Cut); }},
            {"Deliver      (3)",             [this] { mEdit->setTab(EditScreen::Deliver); }},
            {"Reset Workspace",              [this] { mEdit->setTab(EditScreen::Grade); mEdit->timeline()->resetView(); mEdit->monitor()->resetZoom(); }},
        }});
        ms->addMenu({"Preset", {}});
        ms->addMenu({"Colour", {}});
        refreshPresetMenu(mHooks.model ? mHooks.model() : emptyModel());
        refreshColourMenu(mHooks.model ? mHooks.model() : emptyModel());
    }

    /** The Workspace menu: the tabs, and the project's proxy switch (R-MEDIA-2), marked when on. */
    void App::refreshWorkspaceMenu(const interstellar::AppModel &m)
    {
        const int key = m.screen == interstellar::Screen::Edit ? (m.useProxies ? 2 : 1) : 0;
        if (key == mWorkspaceMenuFor) return;
        mWorkspaceMenuFor = key;
        std::vector<cosmo_v2::MenuStrip::Item> items = {
            {"Grade        (1)",             [this] { mEdit->setTab(EditScreen::Grade); }},
            {"Cut          (2)",             [this] { mEdit->setTab(EditScreen::Cut); }},
            {"Deliver      (3)",             [this] { mEdit->setTab(EditScreen::Deliver); }},
            {"Reset Workspace",              [this] { mEdit->setTab(EditScreen::Grade); mEdit->timeline()->resetView(); mEdit->monitor()->resetZoom(); }},
        };
        if (key)
        {
            items.push_back({std::string(m.useProxies ? "\xE2\x80\xA2  " : "     ") + "Use Proxies", [this, on = !m.useProxies] { dispatch(std::string("proxy use ") + (on ? "on" : "off")); }});
            items.push_back({"     Make Proxies for All Video", [this] { dispatch("proxy make"); }});
        }
        mEdit->topBar()->menus()->setItems(3, std::move(items));
    }

    void App::refreshColourMenu(const interstellar::AppModel &m)
    {
        const std::string menuKey = m.workingSpace + (m.matteView ? "|matte" : "");
        if (menuKey == mColourMenuFor) return;
        mColourMenuFor = menuKey;
        // the current working space is marked with a dot; the others are indented to line up
        auto mark = [&](const char *id) { return m.workingSpace == id ? std::string("\xE2\x80\xA2  ") : std::string("     "); };
        std::vector<cosmo_v2::MenuStrip::Item> items = {
            {mark("rec709") + "Rec.709 Working Space", [this] { dispatch("colour working rec709"); }},
            {mark("acescct") + "ACEScct Working Space", [this] { dispatch("colour working acescct"); }},
            {"Input Colour of Selected...", [this] {
                 const auto b = selectedBind();
                 if (!b.empty()) openInputColourMenu(b, Point(width() * 0.5, 40.0));
             }},
            // R-CLR-1: the key a qualifier or a window makes, looked at
            {std::string(m.matteView ? "\xE2\x80\xA2  " : "     ") + "Show Matte  (Shift+H)", [this, on = !m.matteView] {
                 dispatch(std::string("view matte ") + (on ? "on" : "off"));
                 if (on) mEdit->setTab(EditScreen::Grade);
             }},
        };
        mEdit->topBar()->menus()->setItems(5, std::move(items));
    }

    void App::openInputColourMenu(const std::string &bind, Point at)
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        std::string current = "rec709";
        bool group = false;
        for (const auto &r : m.rack)
            if (r.bindName == bind) { current = r.input; group = r.group; }
        if (group) return;
        std::vector<cosmo_v2::ContextMenu::Item> items;
        for (const auto &c : m.colourInputs)
            items.push_back({(c.id == current ? std::string("\xE2\x80\xA2  ") : std::string("     ")) + c.label,
                             [this, bind, id = c.id] { dispatch("set " + cmd::quote(bind) + ".input=" + id); }});
        mEdit->contextMenu()->open(std::move(items), at.x, at.y);
        noteActivity();
    }

    void App::refreshPresetMenu(const interstellar::AppModel &m)
    {
        std::vector<std::string> names;
        for (const auto &p : m.presets) names.push_back(p.name);
        if (names == mPresetNames && !mPresetNames.empty()) return;
        mPresetNames = names;
        auto ask = [this](std::function<void(const std::string &)> then) {
            mEdit->namePrompt()->show("Save preset", "The selected source's grade, saved to the library as an .apf.", "", "Save",
                                      std::move(then));
        };
        std::vector<cosmo_v2::MenuStrip::Item> items = {
            {"Save Preset...",   [this, ask] {
                 const auto b = selectedBind();
                 if (b.empty()) return;
                 ask([this, b](const std::string &name) {
                     if (!name.empty()) dispatch("preset save " + cmd::quote(name) + " --node " + cmd::quote(b));
                 });
             }},
            {"Import Preset...", [this] { if (onPickPresetToImport) onPickPresetToImport(); }},
        };
        for (const auto &n : names)
            items.push_back({"Apply  " + n, [this, n] {
                const auto b = selectedBind();
                if (!b.empty()) dispatch("preset apply " + cmd::quote(n) + " --node " + cmd::quote(b));
            }});
        mEdit->topBar()->menus()->setItems(4, std::move(items));
    }

    void App::layoutAll()
    {
        for (Segment *s : std::initializer_list<Segment *>{mHome.get(), mLoading.get(), mEdit.get(), mSettings.get()})
        {
            s->x.set(0); s->y.set(0); s->width.set(mW); s->height.set(mH);
        }
        mHome->layout();
        mEdit->layout();
    }

    double App::screenOpacity(Screen s) const
    {
        switch (s)
        {
        case Screen::Home: return mHome->opacity.value();
        case Screen::Loading: return mLoading->opacity.value();
        default: return mEdit->opacity.value();
        }
    }

    Segment *App::activeRoot()
    {
        switch (mScreen)
        {
        case Screen::Home: return mHome.get();
        case Screen::Loading: return mLoading.get();
        default: return mEdit.get();
        }
    }

    bool App::dispatch(const std::string &line)
    {
        noteActivity();
        if (!mHooks.dispatch) return false;
        std::string err;
        const bool ok = mHooks.dispatch(line, err);
        if (!ok) mEdit->showRefusal(err.empty() ? "Refused: " + line : err);
        return ok;
    }

    void App::openProjectPicked(const std::string &path)
    {
        if (path.empty()) return;
        const auto slash = path.find_last_of('/');
        mLoading->setProjectName(slash == std::string::npos ? path : path.substr(slash + 1));
        dispatch("project open " + cmd::quote(path));
    }

    void App::newProjectPicked(const std::string &path)
    {
        if (path.empty()) return;
        const auto slash = path.find_last_of('/');
        mLoading->setProjectName(slash == std::string::npos ? path : path.substr(slash + 1));
        dispatch("project new " + cmd::quote(path));
    }

    void App::footagePicked(const std::vector<std::string> &paths)
    {
        if (paths.empty()) return;
        std::string line = "rack add";
        for (const auto &p : paths) line += " " + cmd::quote(p);
        dispatch(line);
    }

    void App::saveAsPicked(const std::string &path)
    {
        if (!path.empty()) dispatch("project save " + cmd::quote(path));
    }

    void App::presetImportPicked(const std::string &path)
    {
        if (!path.empty()) dispatch("preset import " + cmd::quote(path));
    }

    void App::stillExportPicked(const std::string &path)
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        if (path.empty() || m.currentTimeline.empty()) return;
        dispatch("export-still --timeline " + cmd::quote(m.currentTimeline) + " --out " + cmd::quote(path) + " --at " +
                 cmd::seconds(m.playhead, m.fps > 0 ? m.fps : 24.0));
    }

    void App::frameSavePicked(const std::string &path)
    {
        if (path.empty()) return;
        dispatch("capture --out " + cmd::quote(path) + (mCaptureBind.empty() ? std::string() : " --source " + cmd::quote(mCaptureBind)));
    }

    /** What the capture button captures: the Grade target alone in Grade (what its monitor shows),
     *  the timeline at the playhead elsewhere — "" (R-UI-11). */
    std::string App::captureBind() const
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        if (mEdit->tab() != EditScreen::Grade || m.selectedRack < 0 || m.selectedRack >= (int)m.rack.size()) return std::string();
        const auto &n = m.rack[(size_t)m.selectedRack];
        return n.group || n.failed ? std::string() : n.bindName;
    }

    /** The capture button's menu (R-UI-11): cosmo's ContextMenu under the button — Copy Frame puts
     *  the full-resolution frame on the clipboard through the host, Save Frame… asks for a path. */
    void App::openCaptureMenu(Rect at)
    {
        mCaptureBind = captureBind();
        std::vector<cosmo_v2::ContextMenu::Item> items;
        if (mHooks.copyFrame)
            items.push_back({"Copy Frame", [this] {
                std::string err;
                if (!mHooks.copyFrame(mCaptureBind, err)) mEdit->showRefusal(err.empty() ? "Could not copy the frame" : err);
                else mEdit->showNotice("Frame copied to the clipboard");
            }});
        items.push_back({"Save Frame...", [this] { if (onPickFrameToSave) onPickFrameToSave(); }});
        mEdit->contextMenu()->open(std::move(items), at.x, at.y + at.h);
        noteActivity();
    }

    // ── the image-processing stack (R-FX-5) ──

    /** "+ Add" in the IMAGE PROCESSING list: the catalog, each item `effect add <node> --type <t>`;
     *  the new effect is selected so its parameters are what shows next. */
    void App::openAddEffectMenu(Rect at)
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        const std::string node = selectedBind();
        if (node.empty()) return;
        std::vector<cosmo_v2::ContextMenu::Item> items;
        for (const auto &t : m.effectTypes)
            items.push_back({t.label, [this, node, type = t.type] {
                std::set<std::string> before;
                for (const auto &e : (mHooks.model ? mHooks.model() : emptyModel()).effects) before.insert(e.id);
                if (!dispatch("effect add " + cmd::quote(node) + " --type " + type)) return;
                for (const auto &e : (mHooks.model ? mHooks.model() : emptyModel()).effects)
                    if (!before.count(e.id)) mEdit->gradeInspector()->plugins()->select(e.id);
            }});
        mEdit->contextMenu()->open(std::move(items), at.x, at.y + at.h);
        noteActivity();
    }

    /** Right-click on a plugin row: reorder, switch, remove — Cosmo's row only switches. */
    void App::openPluginContext(const std::string &id, Point at)
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        std::vector<cosmo_v2::ContextMenu::Item> items;
        if (id.empty())
        {
            if (m.selectedRack < 0 || m.selectedRack >= (int)m.rack.size()) return;
            const auto &n = m.rack[(size_t)m.selectedRack];
            items.push_back({n.bypass ? "Enable Cosmo" : "Disable Cosmo", [this, b = n.bindName, on = !n.bypass] {
                dispatch("set " + cmd::quote(b + ".bypass=" + (on ? "1" : "0")));
            }});
        }
        else
        {
            const interstellar::EffectModel *e = nullptr;
            int count = 0;
            for (const auto &x : m.effects) if (x.id == id) e = &x;
            if (!e) return;
            for (const auto &x : m.effects) count += x.node == e->node;
            const std::string q = cmd::quote(id);
            if (e->order > 0) items.push_back({"Move Up", [this, q, to = e->order - 1] { dispatch("effect move " + q + " --to " + std::to_string(to)); }});
            if (e->order < count - 1) items.push_back({"Move Down", [this, q, to = e->order + 1] { dispatch("effect move " + q + " --to " + std::to_string(to)); }});
            items.push_back({e->enabled ? "Disable" : "Enable", [this, q, on = !e->enabled] { dispatch("set " + q + ".enabled=" + (on ? "1" : "0")); }});
            items.push_back({"Remove", [this, q] { dispatch("effect remove " + q); }});
        }
        mEdit->contextMenu()->open(std::move(items), at.x, at.y);
        noteActivity();
    }

    /** R-ANIM-2/4: a keyframe's menu — the presets, the sides typed as numbers, delete. */
    void App::openKeyContext(const std::string &address, double t, Point at)
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        const interstellar::AnimModel *a = nullptr;
        for (const auto &x : m.anims) if (x.address == address) a = &x;
        if (!a) return;
        const interstellar::KeyframeModel *k = nullptr;
        for (const auto &x : a->keys) if (std::fabs(x.t - t) < 5e-4) k = &x;
        if (!k) return;
        const std::string head = "key set " + cmd::quote(address) + " --at " + cmd::num(k->t);
        std::vector<cosmo_v2::ContextMenu::Item> items;
        for (const auto &p : std::vector<std::pair<const char *, const char *>>{
                 {"Linear", "linear"}, {"Ease", "ease"}, {"Ease In", "ease-in"}, {"Ease Out", "ease-out"}, {"Hold", "hold"}})
            items.push_back({p.first, [this, head, e = std::string(p.second)] { dispatch(head + " --ease " + e); }});
        if (a->owner == "rack")
            for (const auto &r : m.rack)
                if (r.rackObj == a->node)
                    items.push_back({"Edit in Grade at This Key", [this, bind = r.bindName, tt = k->t] {
                        // Grade stands on the key's source frame: what it shows there is what the key holds
                        dispatch("rack select " + cmd::quote(bind));
                        dispatch("rack frame " + cmd::quote(bind) + " --at " + cmd::num(tt));
                        setTab(0);
                    }});
        if (!a->shape)
        items.push_back({"Speed & Influence\xE2\x80\xA6", [this, head, address, kk = *k] {
            // the sides as numbers: what a speed is (units per second) and how far it reaches (%)
            mEdit->namePrompt()->showFields(
                "Keyframe at " + cmd::num(kk.t) + " s", address,
                {{"Value", cmd::num(kk.v)},
                 {"Incoming speed /s", cmd::num(kk.speedIn)}, {"Incoming influence %", cmd::num(kk.inflIn)},
                 {"Outgoing speed /s", cmd::num(kk.speedOut)}, {"Outgoing influence %", cmd::num(kk.inflOut)}},
                "Apply", [this, head, kk](const std::vector<std::string> &v) {
                    // only what was changed: a speed given makes its side a bezier, so an untouched
                    // linear side must not be sent back as "speed 0"
                    if (v.size() < 5) return;
                    const std::string was[5] = {cmd::num(kk.v), cmd::num(kk.speedIn), cmd::num(kk.inflIn), cmd::num(kk.speedOut), cmd::num(kk.inflOut)};
                    const char *flag[5] = {"value", "speed-in", "influence-in", "speed-out", "influence-out"};
                    std::string line = head;
                    for (int i = 0; i < 5; ++i)
                        if (v[(size_t)i] != was[i]) line += std::string(" --") + flag[i] + " " + v[(size_t)i];
                    if (line != head) dispatch(line);
                });
        }});
        if (KeyGraph *g = keyGraphShown())
        {
            const std::string list = g->selectionList().empty() ? address + "@" + cmd::num(k->t) : g->selectionList();
            const int n = std::max(1, g->selectionCount());
            items.push_back({n > 1 ? "Copy " + std::to_string(n) + " Keys  (Ctrl+C)" : std::string("Copy Key  (Ctrl+C)"),
                             [this, list] { dispatch("key copy --keys " + cmd::quote(list)); }});
        }
        items.push_back({"Delete Key", [this, address, tt = k->t] { dispatch("key remove " + cmd::quote(address) + " --at " + cmd::num(tt)); }});
        mEdit->contextMenu()->open(std::move(items), at.x, at.y);
        noteActivity();
    }

    KeyGraph *App::keyGraphShown()
    {
        auto tl = mEdit->timeline();
        if (mEdit->tab() != EditScreen::Cut || !tl || tl->keyLaneAmount() < 0.5) return nullptr;
        return tl->keyLane()->graph().get();
    }

    /** R-ANIM-7: the key lane's empty plot — paste the copied keys at the playhead, here, or onto
     *  the front property. */
    void App::openKeyPlotContext(double t, Point at)
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        if (m.keyClipboardCount <= 0) return;
        auto tl = mEdit->timeline();
        const double now = tl->keyLane()->now();
        std::vector<cosmo_v2::ContextMenu::Item> items;
        items.push_back({"Paste Keys at Playhead  (Ctrl+V)", [this, now] { dispatch("key paste --at " + cmd::num(now)); }});
        items.push_back({"Paste Keys Here", [this, t] { dispatch("key paste --at " + cmd::num(t)); }});
        const std::string front = tl->keyLane()->selected();
        if (m.keyClipboardCurves == 1 && !front.empty())
            items.push_back({"Paste onto " + front, [this, front, now] { dispatch("key paste --to " + cmd::quote(front) + " --at " + cmd::num(now)); }});
        mEdit->contextMenu()->open(std::move(items), at.x, at.y);
        noteActivity();
    }

    // ── the Cut tab, like an editor (R-UI-14) ──

    namespace
    {
        double clipLen(const interstellar::ClipModel &c) { return c.duration > 0 ? c.duration : (c.out - c.in) / std::max(1e-6, c.speed); }
    }

    /** A source dropped from the bin: `clip add` there — the rest of the source, the service's
     *  default. With no video track to land on, one is made first (two lines, two undo steps). */
    void App::dropSource(const std::string &src, const std::string &track, double at)
    {
        const auto &m0 = mHooks.model ? mHooks.model() : emptyModel();
        const double fps = m0.fps > 0 ? m0.fps : 24.0;
        std::string trk = track;
        if (trk.empty())
        {
            std::set<std::string> before;
            for (const auto &tk : m0.tracks) before.insert(tk.id);
            if (!dispatch("track add --kind video")) return;
            const auto &m1 = mHooks.model ? mHooks.model() : emptyModel();
            for (const auto &tk : m1.tracks)
                if (!tk.audio && !before.count(tk.id)) trk = tk.id;
            if (trk.empty()) return;
        }
        dispatch("clip add --track " + cmd::quote(trk) + " --src " + cmd::quote(src) + " --in 0 --at " + cmd::seconds(at, fps));
    }

    /** A fresh marker name: m1, m2 … the first one no marker has. */
    std::string App::freshMarkerName() const
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        std::set<std::string> taken;
        for (const auto &mk : m.markers) taken.insert(mk.name);
        for (int k = 1;; ++k)
        {
            const std::string n = "m" + std::to_string(k);
            if (!taken.count(n)) return n;
        }
    }

    /** Right-click on a clip: every cut operation that applies to it, each a command line. */
    void App::openClipContext(const std::string &id, Point at)
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        const interstellar::ClipModel *c = nullptr;
        for (const auto &x : m.clips) if (x.id == id) c = &x;
        if (!c) return;
        if (m.selectedClip != id) dispatch("clip select " + cmd::quote(id));
        const double fps = m.fps > 0 ? m.fps : 24.0;
        const std::string q = cmd::quote(id);
        const auto clip = *c;   // the menu outlives this model snapshot
        std::vector<cosmo_v2::ContextMenu::Item> items;
        if (m.playhead > clip.at + 1e-9 && m.playhead < clip.at + clipLen(clip) - 1e-9)
            items.push_back({"Split at Playhead  (S)", [this, q, t = m.playhead, fps] { dispatch("clip split " + q + " --at " + cmd::seconds(t, fps)); }});
        items.push_back({"Copy  (Ctrl+C)", [this, q] { dispatch("clip copy " + q); }});
        items.push_back({"Cut  (Ctrl+X)", [this, q] { if (dispatch("clip copy " + q)) dispatch("clip delete " + q); }});
        if (m.hasClipClipboard) items.push_back({"Paste at Playhead  (Ctrl+V)", [this] { dispatch("clip paste"); }});
        items.push_back({"Delete  (Del)", [this, q] { dispatch("clip delete " + q); }});
        items.push_back({"Ripple Delete  (Shift+Del)", [this, q] { dispatch("clip delete " + q + " --ripple"); }});
        if (!clip.audio)
        {
            // a dissolve into the next clip that TOUCHES this one on its track (R-TL-4 holds the outgoing one)
            for (const auto &x : m.clips)
                if (x.track == clip.track && x.id != clip.id && std::fabs(x.at - (clip.at + clipLen(clip))) < 0.5 / fps)
                {
                    items.push_back({"Add Dissolve to Next", [this, q, b = cmd::quote(x.id)] { dispatch("transition add --between " + q + "," + b + " --dur 0.5"); }});
                    break;
                }
            for (double sp : {0.5, 1.0, 2.0})
                if (std::fabs(clip.speed - sp) > 1e-9)
                    items.push_back({"Speed " + std::to_string((int)std::lround(sp * 100)) + "%", [this, q, sp] { dispatch("clip speed " + q + " " + cmd::num(sp)); }});
            if (clip.nested)
                items.push_back({"Open Timeline " + clip.srcName, [this, s = clip.srcName] { dispatch("timeline open " + cmd::quote(s)); }});   // R-EDT-4
            else if (!clip.srcName.empty())
                items.push_back({"Show Source in Grade", [this, s = clip.srcName] { dispatch("rack select " + cmd::quote(s)); mEdit->setTab(EditScreen::Grade); }});
        }
        mEdit->contextMenu()->open(std::move(items), at.x, at.y);
        noteActivity();
    }

    /** Right-click on an empty lane: tracks, a paste or a marker at that time. */
    void App::openLaneContext(const std::string &track, double t, Point at)
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        const double fps = m.fps > 0 ? m.fps : 24.0;
        std::vector<cosmo_v2::ContextMenu::Item> items;
        if (m.hasClipClipboard)
            items.push_back({"Paste Here", [this, track, t, fps] {
                dispatch("clip paste --at " + cmd::seconds(t, fps) + (track.empty() ? std::string() : " --track " + cmd::quote(track)));
            }});
        items.push_back({"Add Marker Here  (M at the playhead)", [this, t, fps] { dispatch("marker add " + freshMarkerName() + " --at " + cmd::seconds(t, fps)); }});
        bool video = false;
        for (const auto &tk : m.tracks) video = video || (tk.id == track && !tk.audio);
        if (video && track != m.targetTrack)
            items.push_back({"Target for Insert / Overwrite", [this, track] { dispatch("edit target " + cmd::quote(track)); }});   // R-EDT-1
        bool placeable = false;
        for (const auto &tl : m.timelines) placeable = placeable || tl.placeable;
        if (video && placeable)
            items.push_back({"Place Timeline Here...", [this, track, t, at] { openPlaceTimelineMenu(track, t, at); }});   // R-EDT-4
        items.push_back({"Add Video Track", [this] { dispatch("track add --kind video"); }});
        items.push_back({"Add Audio Track", [this] { dispatch("track add --kind audio"); }});
        mEdit->contextMenu()->open(std::move(items), at.x, at.y);
        noteActivity();
    }

    /** R-MEDIA-3: every offline source in one list — each found by hand, or all by searching a folder. */
    void App::openRelinkMenu(Point at)
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        std::vector<cosmo_v2::ContextMenu::Item> items;
        int missing = 0;
        for (const auto &n : m.rack)
        {
            if (n.group || !n.failed) continue;
            const std::string file = std::filesystem::path(n.media).filename().string();
            const std::string name = n.cosmoName.empty() ? n.bindName : n.cosmoName;
            if (!n.offlineWhy.empty())
            {
                items.push_back({name + " \xE2\x80\x94 " + n.offlineWhy, [] {}});   // nothing to locate: a decoder is missing
                continue;
            }
            ++missing;
            items.push_back({"Locate " + name + " (" + file + ")...", [this, b = cmd::quote(n.bindName), file] {
                if (onPickMediaToRelink)
                    onPickMediaToRelink(file, [this, b](const std::string &p) { dispatch("media relink " + b + " " + cmd::quote(p)); });
            }});
        }
        if (missing > 0)
            items.push_back({"Search a Folder for " + std::string(missing > 1 ? "All..." : "It..."), [this] {
                if (onPickFolder) onPickFolder([this](const std::string &dir) { dispatch("media relink --search " + cmd::quote(dir)); });
            }});
        if (items.empty()) items.push_back({"No media is offline", [] {}});
        mEdit->contextMenu()->open(std::move(items), at.x, at.y);
        noteActivity();
    }

    /** R-EDT-4: the timelines that can go inside this one, as a list in place of the lane menu. */
    void App::openPlaceTimelineMenu(const std::string &track, double t, Point at)
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        const double fps = m.fps > 0 ? m.fps : 24.0;
        std::vector<cosmo_v2::ContextMenu::Item> items;
        for (const auto &tl : m.timelines)
            if (tl.placeable)
                items.push_back({std::string(tl.depth * 2, ' ') + tl.name, [this, track, t, fps, name = tl.name] {
                    dispatch("clip add --track " + cmd::quote(track) + " --src " + cmd::quote(name) + " --in 0 --at " + cmd::seconds(t, fps));
                }});
        if (items.empty()) return;
        mEdit->contextMenu()->open(std::move(items), at.x, at.y);
        noteActivity();
    }

    void App::requestHome()
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        if (!m.dirty) { dispatch("project close"); return; }
        mEdit->confirm()->show("Save changes to " + (m.projectName.empty() ? std::string("this project") : m.projectName) + "?",
                               "Edits since the last save are lost if you close without saving.",
                               {{"Cancel", false, false, [] {}},
                                {"Don't save", true, false, [this] { dispatch("project close"); }},
                                {"Save", false, true, [this] { if (dispatch("project save")) dispatch("project close"); }}});
    }

    bool App::textEditing() const
    {
        Segment *f = Segment::focusedInGroup(0);
        if (!f || !f->hasFocus() || !dynamic_cast<TextBox *>(f)) return false;
        // focused AND actually on screen: every ancestor shown, and its tab page the one up
        for (const Segment *s = f; s; s = s->parent())
        {
            if (s->isFadedOut()) return false;
            if (auto *page = dynamic_cast<const FadePage *>(s); page && !page->shown()) return false;
        }
        return true;
    }

    void App::pointer(int kind, double x, double y, int button, double timeMs, bool alt, bool shift, bool ctrl)
    {
        noteActivity();
        const RawPointer::Kind k = kind == 0 ? RawPointer::Kind::Down : (kind == 2 ? RawPointer::Kind::Up : RawPointer::Kind::Move);
        if (k == RawPointer::Kind::Down) mPointerDown = true;
        RawPointer rp{};
        rp.kind = k;
        const double sc = std::max(0.25, mScaleAnim.value());
        rp.pos = Point{x / sc, y / sc};   // the host speaks pixels; the tree is laid out in logical units
        rp.button = button == 2 ? PointerButton::Right : PointerButton::Left;
        rp.timeMs = timeMs;
        rp.alt = alt; rp.shift = shift; rp.ctrl = ctrl;
        mRecognizer.feed(rp);
        if (k == RawPointer::Kind::Up) mPointerDown = false;
    }

    void App::wheel(double x, double y, double notches, bool ctrl)
    {
        noteActivity();
        RawPointer rp{};
        rp.kind = RawPointer::Kind::Scroll;
        const double sc = std::max(0.25, mScaleAnim.value());
        rp.pos = Point{x / sc, y / sc};
        rp.timeMs = mNowMs;
        rp.ctrl = ctrl;
        rp.scroll = Point{0.0, -notches * shell::wheelNotchPx()};   // + notches = wheel up = toward the start
        mRecognizer.feed(rp);
    }

    bool App::key(const KeyEvent &e)
    {
        noteActivity();
        if (mScreen == Screen::Loading) return true;
        if (mSettings->isOpen()) return true;   // the modal owns the keyboard (its chips are pointer-only, as in cosmo)
        if (mScreen == Screen::Home)
        {
            mHome->dispatchKey(e);
            return true;   // the launcher owns the keyboard
        }
        // Edit: a modal owns the keyboard while it is up
        if (mEdit->confirm()->isOpen()) return mEdit->confirm()->handleKey(e) || true;
        if (mEdit->contextMenu()->isOpen())   // the menu owns the keyboard (its rename field types)
        {
            mEdit->contextMenu()->dispatchKey(e);
            return true;
        }
        if (mEdit->namePrompt()->isOpen()) return mEdit->namePrompt()->handleKey(e);
        auto versions = mEdit->topBar()->versions();
        if (versions->isOpen())
        {
            if (e.type == KeyEvent::Type::Down && e.keyCode == kKeyEsc) versions->close();
            return true;
        }
        if (textEditing()) return mEdit->dispatchKey(e);
        if (e.type == KeyEvent::Type::Up && (e.keyCode == 'K' || e.keyCode == 'k')) { mKDown = false; return true; }   // R-EDT-2
        if (e.type != KeyEvent::Type::Down) return false;
        if (e.ctrl && editKey(e)) return true;

        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        const double fps = m.fps > 0 ? m.fps : 24.0;
        // R-EDT-5: Alt+1…9 switches the multicam clip under the playhead to that angle, from here
        if (e.alt && e.keyCode >= '1' && e.keyCode <= '9' && mEdit->tab() == EditScreen::Cut && !m.multicamClip.empty())
        {
            if (e.keyCode - '0' <= (int)m.multicamAngles.size()) dispatch("multicam angle " + std::to_string(e.keyCode - '0'));
            return true;
        }
        switch (e.keyCode)
        {
        case kKeySpace: dispatch(m.playing ? "pause" : "play"); return true;
        case kKeyLeft:
        case kKeyRight:
        {
            const double d = (e.keyCode == kKeyLeft ? -1.0 : 1.0) / fps;
            // the source viewer's own playhead while it shows a source (R-EDT-1)
            if (mEdit->tab() == EditScreen::Cut && !m.sourceView.empty()) dispatch("source playhead " + cmd::seconds(std::max(0.0, m.sourcePlayhead + d), fps));
            else dispatch("playhead " + cmd::seconds(m.playhead + d, fps));
            return true;
        }
        // R-EDT-2: J/K/L — K held, J and L step a frame
        case 'J': case 'L':
        {
            const bool fwd = e.keyCode == 'L';
            if (mKDown) dispatch("playhead " + cmd::seconds(std::max(0.0, m.playhead + (fwd ? 1.0 : -1.0) / fps), fps));
            else dispatch(fwd ? "shuttle forward" : "shuttle back");
            return true;
        }
        case 'K':
            mKDown = true;
            if (m.playing) dispatch("shuttle stop");
            return true;
        // R-EDT-1: marks (the source's while the viewer shows one), Insert and Overwrite
        case 'I': case 'O':
        {
            const bool src = mEdit->tab() == EditScreen::Cut && !m.sourceView.empty();
            dispatch(std::string("mark ") + (e.keyCode == 'I' ? "in" : "out") + (src ? " --source" : ""));
            return true;
        }
        case kKeyComma: dispatch("edit insert"); return true;
        case kKeyPeriod: dispatch("edit overwrite"); return true;
        case kKeyEsc:
            if (mEdit->tab() == EditScreen::Cut && !m.sourceView.empty()) { dispatch("source view none"); return true; }
            return false;
        case kKeyDelete:
        case kKeyBackspace:
            // Shift: RIPPLE — the gap closes (R-UI-14)
            if (mEdit->tab() == EditScreen::Cut && !m.selectedClip.empty())
            {
                dispatch("clip delete " + cmd::quote(m.selectedClip) + (e.shift ? " --ripple" : ""));
                return true;
            }
            return false;
        case 'H':
            // R-CLR-1: Shift+H — the matte, in Grade (a colourist's highlight key)
            if (e.shift && mEdit->tab() == EditScreen::Grade) { dispatch(std::string("view matte ") + (m.matteView ? "off" : "on")); return true; }
            return false;
        case 'M':
            if (mEdit->tab() == EditScreen::Cut && m.duration > 0.0)
            {
                dispatch("marker add " + freshMarkerName() + " --at " + cmd::seconds(m.playhead, fps));
                return true;
            }
            return false;
        case 'S':
            if (mEdit->tab() == EditScreen::Cut && !m.selectedClip.empty())
            {
                dispatch("clip split " + cmd::quote(m.selectedClip) + " --at " + cmd::seconds(m.playhead, fps));
                return true;
            }
            return false;
        case '1': case '2': case '3':
            mEdit->setTab(e.keyCode - '1');
            return true;
        default:
            return false;
        }
    }

    /** Cosmo's accelerators: Ctrl+Z / Ctrl+Y (Ctrl+Shift+Z) / Ctrl+S / Ctrl+Shift+S / Ctrl+O, and
     *  Ctrl+C / Ctrl+V for a grade on the Grade tab (cosmo's Copy Settings / Paste to Selected). */
    bool App::editKey(const KeyEvent &e)
    {
        switch (e.keyCode)
        {
        case 'Z': dispatch(e.shift ? "redo" : "undo"); return true;
        case 'Y': dispatch("redo"); return true;
        case 'S':
            if (e.shift) { if (onPickSaveAs) onPickSaveAs(); }
            else dispatch("project save");
            return true;
        case 'O': if (onPickProjectToOpen) onPickProjectToOpen(); return true;
        case 'G': dispatch("rack group new"); return true;   // Group Selection
        case 'D':
        {
            // Duplicate as Variant (R-RACK-5): the Grade target, when it is a source
            const auto &m = mHooks.model ? mHooks.model() : emptyModel();
            if (m.selectedRack < 0 || m.selectedRack >= (int)m.rack.size()) return false;
            const auto &n = m.rack[(size_t)m.selectedRack];
            if (n.group || n.failed || n.bindName.empty()) return false;
            dispatch("rack duplicate " + cmd::quote(n.bindName));
            return true;
        }
        case 'C':
        case 'V':
        case 'X':
        {
            if (mEdit->tab() == EditScreen::Cut)
            {
                // keys first, when the key lane holds a selection or the clipboard holds keys (R-ANIM-7)
                const auto &m = mHooks.model ? mHooks.model() : emptyModel();
                if (KeyGraph *g = keyGraphShown())
                {
                    if (e.keyCode == 'C' && g->selectionCount() > 0) { dispatch("key copy --keys " + cmd::quote(g->selectionList())); return true; }
                    if (e.keyCode == 'V' && m.keyClipboardCount > 0)
                    {
                        dispatch("key paste --at " + cmd::num(mEdit->timeline()->keyLane()->now()));
                        return true;
                    }
                }
                // a CLIP on the Cut tab (R-TL-6): copy / cut / paste at the playhead
                if (e.keyCode == 'V') { if (!m.hasClipClipboard) return false; dispatch("clip paste"); return true; }
                if (m.selectedClip.empty()) return false;
                const std::string q = cmd::quote(m.selectedClip);
                if (dispatch("clip copy " + q) && e.keyCode == 'X') dispatch("clip delete " + q);
                return true;
            }
            if (e.keyCode == 'X' || mEdit->tab() != EditScreen::Grade) return false;
            const auto b = selectedBind();
            if (b.empty()) return false;
            dispatch(std::string(e.keyCode == 'C' ? "grade copy " : "grade paste ") + cmd::quote(b));
            return true;
        }
        default: return false;
        }
    }

    void App::bindIfStale(double nowMs)
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        // R-EDT-5: in Cut, the multicam clip under the playhead offers its angles on the monitor — every
        // frame, since a tab switch changes it without a new model
        mEdit->monitor()->setAngles(m.multicamAngles, m.multicamAngle,
                                    mEdit->tab() == EditScreen::Cut && m.sourceView.empty() && !m.multicamClip.empty());
        if (mBound && m.revision == mSeenRevision)
        {
            if (m.screen == Screen::Edit) fetchFrame(m, false);   // the monitor may have resized
            return;
        }
        mBound = true;
        mSeenRevision = m.revision;
        noteActivity();
        // The screen scale follows the service's setting — whoever changed it (cosmo's R-SCALE:
        // `settings set uiScale=125` from a script resizes the window the same as the chip).
        if (m.settings.uiScale > 0 && (m.settings.uiScale != mUiScale || !mScaleBound))
        {
            setUiScale(m.settings.uiScale, mScaleBound);   // first placement sets; later ones ease
            mScaleBound = true;
        }
        refreshPresetMenu(m);
        refreshColourMenu(m);
        refreshWorkspaceMenu(m);
        // Home's recents are unknown until the service has published once (revision 0):
        // that is the loading state, drawn as skeleton cards.
        mHome->setLoading(m.revision == 0);
        mHome->bind(m);
        if (m.screen != Screen::Home && !m.projectName.empty()) mLoading->setProjectName(m.projectName);
        // R-EDT-1: in Cut, a source in the viewer takes the transport
        mEdit->transport()->setSourceMode(mEdit->tab() == EditScreen::Cut && !m.sourceView.empty());
        mEdit->bind(m, mPointerDown, nowMs);
        if (m.screen == Screen::Edit) fetchFrame(m, false);
    }

    void App::fetchFrame(const interstellar::AppModel &m, bool force)
    {
        auto mon = mEdit->monitor();
        const int edge = mon->wantedProxyEdge();
        mon->setProxyEdge(edge);
        // Grade has no transport and no playhead (R-UI-3, amended): the monitor shows the Grade
        // target ALONE, graded, at its reference frame — or at the ref-frame slider's time while it
        // is dragged (R-RACK-3). A group or no target falls through to the timeline at the playhead.
        if (mEdit->tab() == EditScreen::Grade && mHooks.renderSource && m.selectedRack >= 0 && m.selectedRack < (int)m.rack.size())
        {
            const auto &n = m.rack[(size_t)m.selectedRack];
            if (!n.group && !n.failed && !n.bindName.empty())
            {
                fetchSource(m, n, edge, force);
                return;
            }
        }
        if (mEdit->tab() == EditScreen::Cut && !m.sourceView.empty() && mHooks.renderSource)
        {
            fetchViewer(m, edge, force);
            return;
        }
        mFetchedBind.clear();
        mon->setTimecode(cmd::timecode(m.playhead, m.fps));
        std::string cap;
        for (const auto &tl : m.timelines) if (tl.id == m.currentTimeline) cap = tl.name.empty() ? tl.id : tl.name;
        if (m.width > 0 && m.height > 0) cap += (cap.empty() ? "" : "  \xC2\xB7  ") + std::to_string(m.width) + "\xC3\x97" + std::to_string(m.height);
        // playing: the size the read-ahead keeps up at (R-PLAY-2) — the paused frame is sharp again
        if (m.playing && m.playbackEdge > 0)
            cap += "  \xC2\xB7  \xE2\x96\xB6 " + (m.playbackFromCache ? std::string("cached") : std::to_string(m.playbackEdge) + " px");
        if (m.useProxies) cap += "  \xC2\xB7  proxies";   // R-MEDIA-2: said on the picture it changes
        mon->setCaption(cap);

        // is there a picture to show? (a clip under the playhead in the resolved timeline)
        bool clipHere = false;
        for (const auto &c : m.clips)
        {
            if (c.audio) continue;
            const double dur = c.duration > 0 ? c.duration : (c.out - c.in) / std::max(1e-6, c.speed);
            if (m.playhead >= c.at - 1e-9 && m.playhead < c.at + dur) { clipHere = true; break; }
        }
        // Nothing cut here: the service shows the Grade target's reference frame, graded — so
        // choosing a frame or grading before the first cut is visible. Empty only without one.
        if (!clipHere && !m.hasGradeTarget) { mon->setState(Monitor::State::Empty); return; }

        const bool changed = force || m.frameSeq != mFetchedSeq || edge != mFetchedEdge || m.currentTimeline != mFetchedTimeline ||
                             std::fabs(m.playhead - mFetchedAt) > 1e-9 || m.revision != mFetchedRevision || mFetchedSource;
        if (!changed) return;
        const bool samePlace = std::fabs(m.playhead - mFetchedAt) <= 1e-9;
        const bool scrubbing = mEdit->transport()->scrubbing() || mEdit->timeline()->dragging();
        mFetchedSeq = m.frameSeq;
        mFetchedEdge = edge;
        mFetchedAt = m.playhead;
        mFetchedTimeline = m.currentTimeline;
        mFetchedRevision = m.revision;
        mFetchedSource = false;
        if (!mHooks.renderFrame || !mHooks.renderFrame(m.playhead, edge, mFrame) || mFrame.empty())
        {
            mon->setState(Monitor::State::Loading);   // a clip is there; its pixels are not (yet)
            return;
        }
        // a new picture at the SAME time is a content change (a grade, a version) and dissolves;
        // a new time is the video moving, which is its own animation (gotcha 10)
        mon->setFrame(mFrame, samePlace && !m.playing && !scrubbing);
        mon->setState(Monitor::State::Frame);
        showScopes(mFrame);
    }

    /** The Grade monitor: one source, graded, at its reference frame or the slider's preview. */
    void App::fetchSource(const interstellar::AppModel &m, const interstellar::RackNodeModel &n, int edge, bool force)
    {
        auto mon = mEdit->monitor();
        const bool previewing = mPreviewAt >= 0.0 && mPreviewBind == n.bindName;
        const double at = previewing ? mPreviewAt : n.frame;
        const double fps = n.mediaFps > 0 ? n.mediaFps : m.fps;
        mon->setTimecode(cmd::timecode(at, fps));
        std::string cap = n.bindName;
        if (n.video) cap += std::string("  \xC2\xB7  ") + (previewing ? "seek " : "ref ") + cmd::timecode(at, fps);
        if (m.sourceWidth > 0 && m.sourceHeight > 0)
            cap += "  \xC2\xB7  " + std::to_string(m.sourceWidth) + "\xC3\x97" + std::to_string(m.sourceHeight);
        if (m.useProxies && !n.proxy.empty()) cap += "  \xC2\xB7  proxy";   // R-MEDIA-2: this picture is its proxy
        if (m.matteView) cap += "  \xC2\xB7  matte";                         // R-CLR-1: the key, not the picture
        mon->setCaption(cap);

        const bool changed = force || !mFetchedSource || m.frameSeq != mFetchedSeq || edge != mFetchedEdge ||
                             n.bindName != mFetchedBind || std::fabs(at - mFetchedAt) > 1e-9 || m.revision != mFetchedRevision;
        if (!changed) return;
        const bool samePlace = n.bindName == mFetchedBind && std::fabs(at - mFetchedAt) <= 1e-9;
        mFetchedSource = true;
        mFetchedSeq = m.frameSeq;
        mFetchedEdge = edge;
        mFetchedAt = at;
        mFetchedBind = n.bindName;
        mFetchedRevision = m.revision;
        // previewing asks for the time itself; at rest, t < 0 = "its reference frame" (the service's
        // own value, so a commit and the model never race)
        if (!mHooks.renderSource(n.bindName, previewing ? at : -1.0, edge, mFrame) || mFrame.empty())
        {
            if (!samePlace || mon->state() != Monitor::State::Frame) mon->setState(Monitor::State::Loading);
            return;
        }
        // a grade change on the same frame dissolves; a seek is the picture moving (gotcha 10)
        mon->setFrame(mFrame, samePlace && !previewing);
        mon->setState(Monitor::State::Frame);
        showScopes(mFrame);
    }

    /** R-EDT-1: the SOURCE viewer — the viewed source, graded, at the viewer's playhead. */
    void App::fetchViewer(const interstellar::AppModel &m, int edge, bool force)
    {
        auto mon = mEdit->monitor();
        double fps = m.fps;
        for (const auto &r : m.rack) if (r.bindName == m.sourceView && r.mediaFps > 0) fps = r.mediaFps;
        const double at = m.sourcePlayhead;
        mon->setTimecode(cmd::timecode(at, fps));
        std::string cap = "SOURCE  \xC2\xB7  " + m.sourceView;
        if (m.sourceIn >= 0 || m.sourceOut >= 0)
            cap += "  \xC2\xB7  " + (m.sourceIn >= 0 ? cmd::timecode(m.sourceIn, fps) : std::string("\xE2\x80\x94")) + " \xE2\x80\x93 " +
                   (m.sourceOut >= 0 ? cmd::timecode(m.sourceOut, fps) : std::string("\xE2\x80\x94"));
        for (const auto &r : m.rack)
            if (m.useProxies && r.bindName == m.sourceView && !r.proxy.empty()) cap += "  \xC2\xB7  proxy";   // R-MEDIA-2
        mon->setCaption(cap);
        const bool changed = force || !mFetchedSource || m.frameSeq != mFetchedSeq || edge != mFetchedEdge || m.sourceView != mFetchedBind ||
                             std::fabs(at - mFetchedAt) > 1e-9 || m.revision != mFetchedRevision;
        if (!changed) return;
        const bool samePlace = m.sourceView == mFetchedBind && std::fabs(at - mFetchedAt) <= 1e-9;
        mFetchedSource = true;
        mFetchedSeq = m.frameSeq;
        mFetchedEdge = edge;
        mFetchedAt = at;
        mFetchedBind = m.sourceView;
        mFetchedRevision = m.revision;
        if (!mHooks.renderSource(m.sourceView, at, edge, mFrame) || mFrame.empty())
        {
            if (!samePlace || mon->state() != Monitor::State::Frame) mon->setState(Monitor::State::Loading);
            return;
        }
        mon->setFrame(mFrame, samePlace);
        mon->setState(Monitor::State::Frame);
        showScopes(mFrame);
    }

    /** The SCOPES of the frame the monitor shows (R-UI-15), and the monitor's clip overlay when on. */
    void App::showScopes(const interstellar::Raster &frame)
    {
        auto gi = mEdit->gradeInspector();
        gi->setScopes(scopesOf(frame));
        if (gi->scopes()->clipWarning()) mEdit->monitor()->setClipMask(clipMaskOf(frame));
    }

    bool App::needsRedraw(double nowMs) const
    {
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        if (m.revision != mPaintedRevision || m.frameSeq != mPaintedFrameSeq) return true;
        if (m.playing || m.screen == Screen::Loading) return true;
        if (mHome->opacity.isAnimating() || mLoading->opacity.isAnimating() || mEdit->opacity.isAnimating()) return true;
        for (const auto &r : m.renders) if (r.state == "running") return true;
        for (const auto &n : m.rack) if (n.pending) return true;   // spinner cells turn
        if (mHooks.thumbnailEpoch && mHooks.thumbnailEpoch() != mThumbEpoch) return true;   // a still landed
        return nowMs - mLastActivityMs < kActiveWindowMs;
    }

    void App::render(IRenderTarget &target, double nowMs)
    {
        mNowMs = nowMs;
        const auto &m = mHooks.model ? mHooks.model() : emptyModel();
        bindIfStale(nowMs);
        // Stills the host decodes off the UI thread arrive later than they were asked for: when
        // its epoch moves, the views re-ask for what they are missing (D-5, D-6).
        if (mHooks.thumbnailEpoch)
        {
            const unsigned ep = mHooks.thumbnailEpoch();
            if (ep != mThumbEpoch)
            {
                mThumbEpoch = ep;
                mHome->thumbnailsArrived();
                mHome->bind(m);
                mEdit->gradeDeck()->thumbnailsArrived();
                mEdit->gradeDeck()->bind(m);
                noteActivity();
            }
        }

        // screen cross-fade: the model says which screen; the view travels there
        const Screen want = m.screen;
        auto rootOf = [this](Screen s) -> Segment * {
            return s == Screen::Home ? (Segment *)mHome.get() : (s == Screen::Loading ? (Segment *)mLoading.get() : (Segment *)mEdit.get());
        };
        if (!mScreenInit)
        {
            for (Screen s : {Screen::Home, Screen::Loading, Screen::Edit}) rootOf(s)->opacity.set(s == want ? 1.0 : 0.0);
            mScreen = want;
            mScreenInit = true;
        }
        else if (want != mScreen)
        {
            for (Screen s : {Screen::Home, Screen::Loading, Screen::Edit})
                if (s == want || rootOf(s)->opacity.value() > 0.0)
                    rootOf(s)->opacity.animateTo(s == want ? 1.0 : 0.0, motion::kScreenFadeMs, Easing::EaseInOutCubic, nowMs);
            mScreen = want;
            noteActivity();
        }
        mRecognizer.advance(nowMs);
        const bool zooming = mScaleAnim.isAnimating();
        mScaleAnim.update(nowMs);
        if (zooming) { applyLogicalSize(); noteActivity(); }
        const Transform rt = rootTransform();

        drawRoundedRect(target, Rect{0, 0, mPhysW, mPhysH}, 0.0, Paint::filled(palette::background()));
        target.save();
        target.setTransform(rt);
        // draw order: the outgoing layer under the incoming one; Edit is laid out every frame
        // (cosmo's convention) so geometry follows any animated property mid-tween
        for (Screen s : {Screen::Home, Screen::Loading, Screen::Edit})
        {
            Segment *r = rootOf(s);
            const bool wanted = s == mScreen;
            if (!wanted && r->opacity.value() <= 0.0 && !r->opacity.isAnimating()) continue;
            if (s == Screen::Home) mHome->layout();
            if (s == Screen::Edit) mEdit->layout();
            r->advance(nowMs);
        }
        for (Screen s : {Screen::Home, Screen::Loading, Screen::Edit})
        {
            Segment *r = rootOf(s);
            if (r->opacity.value() <= Segment::kOpacityEpsilon) continue;
            r->render(target, rt);
            r->renderOverlay(target, rt);
        }
        // Settings floats over whichever screen opened it; advanced unconditionally so its close
        // can finish (design rule §1) — it draws nothing when shut.
        mSettings->advance(nowMs);
        mSettings->renderOverlay(target, rt);
        target.restore();
        mPaintedRevision = m.revision;
        mPaintedFrameSeq = m.frameSeq;
    }
}
}
