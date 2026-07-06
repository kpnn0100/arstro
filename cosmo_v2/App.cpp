#include "App.h"
#include "../cosmo_core/PresetLibrary.h"
#include "engine/EditParamsApf.h"   // apfImageCategories() for the category picker
#include "base/Parallel.h"          // par::setThreads() for the settings dialog
#include <algorithm>
#include <cstdio>
#include <ctime>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace { constexpr double kRailAnimMs = 200.0; }

    App::App(double width, double height)
        : mW(width), mH(height), mTheme(makeCosmoV2Theme()), mAccent(palette::primary())
    {
        mRoot = std::make_shared<Segment>();
        mRoot->width.set(width);
        mRoot->height.set(height);
        // Gestures route to whichever screen is active (launcher vs. editor).
        mRecognizer.setSink([this](const Gesture &g) {
            if (mScreen == Screen::Home) mHome->onGesture(g); else mRoot->onGesture(g);
        });

        mTopBar = std::make_shared<TopBar>();
        mTopBar->width.set(width);
        mTopBar->onRailToggle = [this] { toggleRail(); };
        mTopBar->onHome = [this] { requestHome(); };
        buildMenus();  // File/Settings/Develop/History/Preset dropdowns, wired to real actions
        mRoot->addChild(mTopBar);

        mLeftRail = std::make_shared<LeftRail>();
        mLeftRail->width.set(LeftRail::kOpenWidth);  // start open (matches mRailOpen{true}); observe() fire is then a no-op animate
        mLeftRail->tree()->onApply = [this](std::string relPath) {
            if (mSession.applyPreset(relPath)) { mLeftRail->tree()->setSelected(relPath); syncControlsToSlot(); }
        };
        mRoot->addChild(mLeftRail);

        // Single source of truth for "is the preset rail open": both the rail's
        // width and the top-bar toggle's highlight derive from it (via observe),
        // so they can never disagree -- and it fires now, initialising the toggle
        // highlighted because the rail starts open (fixes the start-up mismatch).
        mRailOpen.observe([this](const bool &open) {
            mTopBar->setRailOpen(open);
            mLeftRail->width.animateTo(open ? LeftRail::kOpenWidth : 0.0, kRailAnimMs, Easing::EaseOutCubic, mNowMs);
        });

        mCenterStage = std::make_shared<CenterStage>();
        mCenterStage->photo()->onModeChange = [this](int) { refreshPhotoForMode(); };
        mCenterStage->filmstrip()->onSelect = [this](int cell, bool shift, bool ctrl) {
            mSession.selectNode(cell, shift, ctrl);
            syncControlsToSlot();
        };
        mCenterStage->filmstrip()->onActivate = [this](int cell) {
            const auto cells = mSession.currentGroupCells();
            if (cell >= 0 && cell < (int)cells.size() && cells[cell].group)
            {
                mSession.navigateToGroup(cells[cell].node);
                syncControlsToSlot();
            }
        };
        mCenterStage->breadcrumb()->onCrumbClick = [this](int idx) {
            std::vector<int> chain;
            for (int n = mSession.currentGroup(); ; n = mSession.nodes()[n].parent) { chain.push_back(n); if (n == 0) break; }
            std::reverse(chain.begin(), chain.end());
            if (idx >= 0 && idx < (int)chain.size()) { mSession.navigateToGroup(chain[idx]); syncControlsToSlot(); }
        };
        mRoot->addChild(mCenterStage);

        mRightColumn = std::make_shared<RightColumn>(mSession);
        mRightColumn->actionBar()->onSave = [this] { presetSaveClicked(); };
        mRightColumn->actionBar()->onImport = [this] { if (onImportPresetRequested) onImportPresetRequested(); };
        mRightColumn->actionBar()->onExport = [this] { presetExportClicked(); };
        mRoot->addChild(mRightColumn);

        // On-photo mask overlay: a drag writes the dragged geometry back into the
        // selected mask (R-MASK). The overlay's active state + mask are pushed each
        // frame from the RightColumn in render().
        mCenterStage->photo()->maskOverlay()->onChange =
            [this](const MaskParams &m) { mRightColumn->writeSelectedMask(m); };

        // History-tree modal (added last -> topmost for hit-test + overlay). Clicking
        // a node jumps the image to that state; re-highlight the landed node.
        mHistoryView = std::make_shared<HistoryView>();
        mHistoryView->onSelect = [this](int node) {
            if (mSession.jumpToHistory(node)) { syncControlsToSlot(); mHistoryView->setCurrent(node); }
        };
        mRoot->addChild(mHistoryView);

        // Right-click context menu (root child so raising it wins hit-testing).
        // Photo area: group / add photo to the current group; filmstrip cell adds Delete.
        mContextMenu = std::make_shared<ContextMenu>();
        mRoot->addChild(mContextMenu);
        mCenterStage->photo()->onContext = [this](double x, double y) { openEditContext(x, y, -1); };
        mCenterStage->filmstrip()->onContext = [this](int cell, double x, double y) {
            if (cell >= 0)
            {
                // Keep an existing multi-selection when right-clicking WITHIN it, so
                // "Group Selection" groups ALL selected images; only replace the
                // selection when right-clicking a cell that isn't already selected.
                const auto &kids = mSession.nodes()[mSession.currentGroup()].kids;
                const auto &sel = mSession.selection();
                const bool inSel = cell < (int)kids.size() &&
                                   std::find(sel.begin(), sel.end(), kids[cell]) != sel.end();
                if (!inSel) { mSession.selectNode(cell, false, false); syncControlsToSlot(); }
            }
            openEditContext(x, y, cell);  // cell < 0 (empty strip) -> photo menu (Add Photo / Group / Ungroup)
        };

        // Modal category picker (preset save/export/import) and engine settings —
        // added last so they draw + hit-test on top of everything (R-PRESETPICK-3).
        mPresetDialog = std::make_shared<PresetDialog>(mAccent);
        mRoot->addChild(mPresetDialog);
        mSettingsDialog = std::make_shared<SettingsDialog>(mAccent);
        mSettingsDialog->onPreviewEdge = [this](int edge) { mSession.setPreviewEdge(edge); };
        mSettingsDialog->onThreads = [this](int n) { arstro::par::setThreads(n); mSession.submit(); };
        mRoot->addChild(mSettingsDialog);

        mConfirmDialog = std::make_shared<ConfirmDialog>(mAccent);
        mRoot->addChild(mConfirmDialog);

        // ── home screen (R-HOME): a standalone full-window launcher, not part of the
        // editor's mRoot tree. render()/input route to it while mScreen == Home. ──
        mHome = std::make_shared<HomeScreen>();
        mHome->width.set(mW); mHome->height.set(mH);
        mHome->onNewProject    = [this] { if (onNewProjectRequested) onNewProjectRequested(); };
        mHome->onOpenProject   = [this] { if (onOpenProjectRequested) onOpenProjectRequested(); };
        mHome->onImportCatalog = [this] { if (onImportCatalogRequested) onImportCatalogRequested(); };
        mHome->onOpenRecent    = [this](int idx) {
            if (idx >= 0 && idx < (int)mRecents.size() && onOpenRecentRequested) onOpenRecentRequested(mRecents[idx].path);
        };
        mHome->layout();

        layout();
    }

    void App::layout()
    {
        mHistoryView->x.set(0.0); mHistoryView->y.set(0.0);
        mHistoryView->width.set(mW); mHistoryView->height.set(mH);  // full-window modal
        mContextMenu->x.set(0.0); mContextMenu->y.set(0.0);
        mContextMenu->width.set(mW); mContextMenu->height.set(mH);
        mPresetDialog->x.set(0.0); mPresetDialog->y.set(0.0);
        mPresetDialog->width.set(mW); mPresetDialog->height.set(mH);       // full-window modal
        mSettingsDialog->x.set(0.0); mSettingsDialog->y.set(0.0);
        mSettingsDialog->width.set(mW); mSettingsDialog->height.set(mH);
        mConfirmDialog->x.set(0.0); mConfirmDialog->y.set(0.0);
        mConfirmDialog->width.set(mW); mConfirmDialog->height.set(mH);

        mTopBar->width.set(mW);
        mTopBar->layout();

        mLeftRail->x.set(0.0);
        mLeftRail->y.set(TopBar::kHeight);
        mLeftRail->height.set(mH - TopBar::kHeight);
        mLeftRail->layout();

        mRightColumn->x.set(mW - RightColumn::kWidth);
        mRightColumn->y.set(TopBar::kHeight);
        mRightColumn->height.set(mH - TopBar::kHeight);
        mRightColumn->layout();

        mCenterStage->x.set(mLeftRail->width.value());
        mCenterStage->y.set(TopBar::kHeight);
        mCenterStage->width.set(std::max(0.0, mW - mLeftRail->width.value() - RightColumn::kWidth));
        mCenterStage->height.set(mH - TopBar::kHeight);
        mCenterStage->layout();
    }

    void App::toggleRail()
    {
        // Flip the ONE source of truth; the observer set in the ctor updates both
        // the toggle highlight and the rail width, so they stay in lockstep.
        mRailOpen.set(!mRailOpen.get());
    }

    void App::refreshPresetTree()
    {
        mLeftRail->tree()->setRoots(cosmo::PresetLibrary::scan(mSession.presetDir()));
    }

    void App::registerThumb(int slot)
    {
        const auto *thumb = mSession.thumbForSlot(slot);
        if (thumb) mCenterStage->filmstrip()->addThumb(thumb->rgba.data(), thumb->w, thumb->h);
    }

    void App::refreshPhotoForMode()
    {
        auto photo = mCenterStage->photo();
        const int mode = photo->mode();
        auto setBefore = [&](std::shared_ptr<artboard::ImageView> view) {
            if (const auto *b = mSession.renderBefore(); b && b->width > 0)
                view->setImage(b->rgba.data(), b->width, b->height);
        };
        if (mode == PhotoCanvas::Before)
        {
            setBefore(photo->imageView());
        }
        else
        {
            if (mLastAfterFrame.width > 0)
                photo->imageView()->setImage(mLastAfterFrame.rgba.data(), mLastAfterFrame.width, mLastAfterFrame.height);
            if (mode == PhotoCanvas::Split)
                setBefore(photo->beforeView());
        }
    }

    namespace
    {
        std::string filenameOf(const std::string &path)
        {
            const auto slash = path.find_last_of('/');
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }
    }

    void App::buildMenus()
    {
        auto ms = mTopBar->menuStrip();
        // When a menu opens, raise the TopBar to the front of the root's children
        // so its dropdown wins HIT-TESTING over the body widgets (left rail /
        // center stage / right column) it visually overlaps. The dropdown is only
        // "on top" in the overlay DRAW pass; without this, a click on a dropdown
        // item that overlaps a body widget is stolen by that widget (menu items
        // then never fire). MenuStrip::setOpen already raise()s itself within the
        // TopBar; this raises the TopBar within the root.
        ms->onOpenChanged = [this](int open) { if (open >= 0) mTopBar->raise(); };

        ms->addMenu({"File", {
            {"Home",       [this] { showHome(); }},
            {"Open...",    [this] { if (onOpenRequested) onOpenRequested(); }},
            {"Save",       [this] { saveSession(); }},
            {"Save As...", [this] { if (onSaveAsRequested) onSaveAsRequested(); }},
        }});
        ms->addMenu({"Settings", {
            {"Engine Settings...", [this] { openSettingsDialog(); }},
            {"Reset Workspace",    [this] { resetWorkspace(); syncControlsToSlot(); }},
        }});
        ms->addMenu({"Develop", {
            {"Copy Settings",        [this] { copySettings(); }},
            {"Paste to Selected",    [this] { pasteSettings(false); }},
            {"Paste to All Images",  [this] { pasteSettings(true); }},
            {"Group Selection",      [this] { mSession.createGroupFromSelection(); syncControlsToSlot(); }},
            {"Ungroup Selection",    [this] { mSession.ungroupSelected(); syncControlsToSlot(); }},
        }});
        ms->addMenu({"History", {
            {"Undo   (Ctrl+Z)",      [this] { undo(); }},
            {"Redo   (Ctrl+Y)",      [this] { redo(); }},
            {"Show History Tree...", [this] { openHistoryView(); }},
        }});
        ms->addMenu({"Preset", {
            {"Save Preset...",   [this] { presetSaveClicked(); }},
            {"Import Preset...", [this] { if (onImportPresetRequested) onImportPresetRequested(); }},
        }});
    }

    void App::copySettings()
    {
        if (const EditParams *p = mSession.curParams()) { mClipboard = *p; mHasClipboard = true; }
    }

    void App::pasteSettings(bool toAll)
    {
        if (!mHasClipboard) return;
        if (toAll)
            for (int s = 0; s < mSession.imageCount(); ++s) mSession.applyParamsToSlot(s, mClipboard);
        else
            for (int s : mSession.selectedImageSlots()) mSession.applyParamsToSlot(s, mClipboard);
        syncControlsToSlot();
    }

    void App::openHistoryView()
    {
        // Snapshot the current image's branching history (parent + label per node)
        // into the modal tree view (like cosmo's openHistoryView). No image / no
        // history -> nothing to show.
        const cosmo::History *h = mSession.currentHistory();
        if (!h || h->nodes.empty()) return;
        std::vector<HistoryView::Node> nodes;
        nodes.reserve(h->nodes.size());
        for (const auto &n : h->nodes) nodes.push_back({n.parent, n.label});
        mHistoryView->show(std::move(nodes), h->current);
    }

    void App::openEditContext(double x, double y, int cell)
    {
        // Photo/filmstrip right-click: group the selection or add a photo to the
        // current group (all photos live in the current group -- see cosmo's
        // showPhotoContext / showCellContext).
        std::vector<ContextMenu::Item> items;
        items.push_back({"Add Photo...",      [this] { if (onOpenRequested) onOpenRequested(); }});
        items.push_back({"Group Selection",   [this] { mSession.createGroupFromSelection(); syncControlsToSlot(); }});
        items.push_back({"Ungroup Selection", [this] { mSession.ungroupSelected(); syncControlsToSlot(); }});
        if (cell >= 0)
            items.push_back({"Delete",        [this] { deleteSelected(); }});
        mContextMenu->open(std::move(items), x, y);
    }

    void App::syncControlsToSlot()
    {
        const int slot = mSession.currentSlot();
        if (slot >= 0)
            mTopBar->setFilename(filenameOf(mSession.currentSourcePath()), slot + 1, mSession.imageCount());
        else
            mTopBar->setFilename("", 0, 0);

        // Breadcrumb: the group path, plus the selected image's own filename as
        // the trailing crumb (matching the Figma mock's "Library > Album >
        // river_02.jpg") when an image, not a group, is being edited.
        std::vector<std::string> crumbs = mSession.breadcrumbPath();
        if (mSession.editGroup() < 0 && slot >= 0)
        {
            const std::string name = filenameOf(mSession.currentSourcePath());
            if (!name.empty()) crumbs.push_back(name);
        }
        mCenterStage->breadcrumb()->setPath(crumbs);

        // Filmstrip: the current group's children + which cell(s) are selected.
        std::vector<Filmstrip::Cell> cells;
        for (const auto &c : mSession.currentGroupCells())
            cells.push_back({c.group, c.node, c.slot, c.name, c.count});
        mCenterStage->filmstrip()->setCells(cells);

        const auto &kids = mSession.nodes()[mSession.currentGroup()].kids;
        const auto &sel = mSession.selection();
        std::vector<int> selCells;
        int primary = -1;
        for (int c = 0; c < (int)kids.size(); ++c)
            if (std::find(sel.begin(), sel.end(), kids[c]) != sel.end()) selCells.push_back(c);
        for (int c = 0; c < (int)kids.size(); ++c)
            if ((mSession.editGroup() >= 0 && kids[c] == mSession.editGroup()) ||
                (mSession.editGroup() < 0 && !mSession.nodes()[kids[c]].group && mSession.nodes()[kids[c]].slot == slot))
                primary = c;
        mCenterStage->filmstrip()->setSelection(selCells, primary);

        mRightColumn->syncToSlot();
    }

    void App::setSize(double width, double height)
    {
        if (width < 320) width = 320;
        if (height < 240) height = 240;
        mW = width; mH = height;
        mRoot->width.set(width); mRoot->height.set(height);
        if (mHome) { mHome->width.set(width); mHome->height.set(height); mHome->layout(); }
        layout();
    }

    void App::pointer(int kind, double x, double y, int button, double timeMs, bool alt, bool shift, bool ctrl)
    {
        // A press outside the open menu's bar/dropdown area closes it first (the
        // press still goes on to do its own thing afterward), matching cosmo's
        // MenuBar outside-click dismissal.
        if (kind == 0)
        {
            auto ms = mTopBar->menuStrip();
            if (ms->openIndex() >= 0 && !ms->pointInActiveArea(ms->toLocal(Point{x, y})))
                ms->close();
        }
        RawPointer::Kind k = kind == 0 ? RawPointer::Kind::Down
                             : kind == 2 ? RawPointer::Kind::Up
                                         : RawPointer::Kind::Move;
        PointerButton b = button == 2 ? PointerButton::Right : PointerButton::Left;
        RawPointer rp{k, Point{x, y}, b, timeMs};
        rp.alt = alt; rp.shift = shift; rp.ctrl = ctrl;
        mRecognizer.feed(rp);
    }

    void App::wheel(double x, double y, double delta, bool ctrl)
    {
        if (mScreen == Screen::Home) { mHome->scrollBy(delta); return; }  // launcher grid scroll
        if (mHistoryView->isOpen()) { mHistoryView->scrollBy(delta); return; }  // modal owns the wheel

        // Ctrl + wheel over the photo = zoom about the cursor (R-ZOOM-1). The photo's
        // world rect is CenterStage's origin + PhotoCanvas's origin within it.
        if (ctrl && delta != 0.0)
        {
            auto photo = mCenterStage->photo();
            const double px = mCenterStage->x.value() + photo->x.value();
            const double py = mCenterStage->y.value() + photo->y.value();
            const double pw = photo->width.value(), ph = photo->height.value();
            if (x >= px && x <= px + pw && y >= py && y <= py + ph)
            {
                photo->zoomAbout(delta > 0 ? 1.15 : 1.0 / 1.15, Point{x - px, y - py});
                // Keep a high-res original sharp when magnified; back to base at 1x (R-ZOOM-4).
                if (photo->zoom() > 1.0) mSession.setPreviewZoom(photo->zoom());
                else                     mSession.resetPreviewResolution();
                mSession.submit();  // re-render at the new preview resolution
            }
            return;
        }

        const double railX = mLeftRail->x.value(), railY = mLeftRail->y.value();
        if (x >= railX && x <= railX + mLeftRail->width.value() &&
            y >= railY && y <= railY + mLeftRail->height.value())
        {
            mLeftRail->scrollBy(delta);
            return;
        }
        const double colX = mRightColumn->x.value(), colY = mRightColumn->y.value();
        if (x >= colX && x <= colX + mRightColumn->width.value() &&
            y >= colY && y <= colY + mRightColumn->height.value())
        {
            mRightColumn->scrollActivePanel(delta);
        }
        // Center-stage ctrl+scroll zoom wires in with mask/crop tool support.
    }

    void App::render(IRenderTarget &target, double nowMs)
    {
        mNowMs = nowMs;
        mScreenFade.update(nowMs);

        // Home screen: render the launcher instead of the editor, then the transition
        // scrim on top (cross-fades on screen switch — R-HOME-1 / R-G-1).
        if (mScreen == Screen::Home)
        {
            mHome->width.set(mW); mHome->height.set(mH);
            mHome->layout();
            mHome->advance(nowMs);
            target.save();
            target.setTransform(Transform::identity());
            mHome->render(target);
            mHome->renderOverlay(target);
            const double a = mScreenFade.value();
            if (a > 0.001) drawRoundedRect(target, Rect{0, 0, mW, mH}, 0.0, Paint::filled(Color{palette::background().r, palette::background().g, palette::background().b, a}));
            target.restore();
            return;
        }

        mSession.tick(nowMs);
        mRoot->advance(nowMs);
        // Re-run manual composite layout every frame (cheap arithmetic) so
        // children stay in sync while the rail's width Property is mid-animation
        // -- matching the design brief's "the edit area reflows in step, it
        // doesn't just get covered or revealed" for the rail toggle.
        layout();

        // Show the on-photo mask editor only while the Mask tab is open with a mask
        // selected; otherwise it stays click-through (R-MASK-1/2). Mask tab == index 2.
        {
            auto ov = mCenterStage->photo()->maskOverlay();
            const MaskParams *sel = mRightColumn->selectedMaskParams();
            if (mRightColumn->activeTab() == 2 && sel) ov->setMask(*sel, true);
            else                                       ov->setMask(MaskParams{}, false);
        }

        RenderService::Frame f;
        if (mSession.renderService().tryAcquire(f) && f.width > 0)
        {
            mLastAfterFrame = f;
            refreshPhotoForMode();
            mRightColumn->histogram()->setHistogram(f.hist);
            // Curve/mixer background histograms wire in with those tabs.
        }

        target.save();
        target.setTransform(Transform::identity());
        drawRoundedRect(target, Rect{0, 0, mW, mH}, 0.0, Paint::filled(palette::background()));
        target.restore();

        mRoot->render(target);
        mRoot->renderOverlay(target);

        // Cross-fade scrim when we just switched into the editor.
        const double a = mScreenFade.value();
        if (a > 0.001)
        {
            target.save();
            target.setTransform(Transform::identity());
            drawRoundedRect(target, Rect{0, 0, mW, mH}, 0.0, Paint::filled(Color{palette::background().r, palette::background().g, palette::background().b, a}));
            target.restore();
        }
    }

    int App::openImage(const uint8_t *rgba, int w, int h, const std::string &name, const std::string &path)
    {
        const int slot = mSession.openImage(rgba, w, h, name, path);
        if (slot >= 0) registerThumb(slot);
        syncControlsToSlot();
        return slot;
    }

    int App::openImageInto(int parentNode, const uint8_t *rgba, int w, int h, const std::string &name, const std::string &path)
    {
        const int slot = mSession.openImageInto(parentNode, rgba, w, h, name, path);
        if (slot >= 0) registerThumb(slot);
        return slot;
    }

    void App::selectImage(int slot)
    {
        mSession.selectImage(slot);
        syncControlsToSlot();
    }

    void App::deleteSelected()
    {
        mSession.deleteSelected();
        syncControlsToSlot();
    }

    void App::renameGroup(const std::string &name)
    {
        // The rename target is tracked by the (not-yet-built) filmstrip context
        // menu; until then this is a no-op seam for linux_main.cpp to call into.
        (void)name;
    }

    namespace
    {
        // {category-key -> {key,label,checked=true}} rows for the picker.
        std::vector<PresetDialog::Row> buildPresetRows(const std::vector<std::string> &keys)
        {
            std::vector<PresetDialog::Row> rows;
            rows.reserve(keys.size());
            for (const auto &k : keys) rows.push_back({k, cosmo::PresetLibrary::categoryLabel(k), true});
            return rows;
        }
    }

    bool App::importPresetFrom(const std::string &path)
    {
        std::vector<std::string> present;
        if (!mSession.importPresetFrom(path, present)) return false;
        // Category picker: choose which of the present categories to apply (R-PRESETPICK).
        mPresetDialog->show("Import preset", "Apply", buildPresetRows(present),
            [this](std::vector<std::string> cats) { mSession.applyImport(cats); syncControlsToSlot(); });
        return true;
    }

    void App::presetSaveClicked()
    {
        if (mSession.imageCount() == 0) return;
        mPresetDialog->show("Save preset", "Continue", buildPresetRows(arstro::apfImageCategories()),
            [this](std::vector<std::string> cats) {
                mSession.setPendingCategories(std::move(cats));
                if (onSavePresetRequested) onSavePresetRequested();  // host name dialog -> savePreset(name)
            });
    }

    void App::presetExportClicked()
    {
        if (mSession.imageCount() == 0) return;
        mPresetDialog->show("Export preset", "Continue", buildPresetRows(arstro::apfImageCategories()),
            [this](std::vector<std::string> cats) {
                mSession.setPendingCategories(std::move(cats));
                if (onExportPresetRequested) onExportPresetRequested();  // host path dialog -> exportPresetTo(path)
            });
    }

    void App::openSettingsDialog()
    {
        // Raw setting (0 = Auto), not the resolved count, so the Auto chip reads right.
        mSettingsDialog->show(mSession.previewEdge(), arstro::par::threadsRef());
    }

    namespace
    {
        std::string formatBytes(long long b)
        {
            if (b <= 0) return "";
            const char *u[] = {"B", "KB", "MB", "GB", "TB"};
            double v = (double)b; int i = 0;
            while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
            char buf[32];
            std::snprintf(buf, sizeof buf, v < 10 && i > 0 ? "%.1f %s" : "%.0f %s", v, u[i]);
            return buf;
        }
        std::string relativeTime(long long thenEpoch)
        {
            if (thenEpoch <= 0) return "";
            const long long now = (long long)std::time(nullptr);
            long long d = now - thenEpoch; if (d < 0) d = 0;
            if (d < 60) return "just now";
            if (d < 3600) return std::to_string(d / 60) + "m ago";
            if (d < 86400) return std::to_string(d / 3600) + "h ago";
            if (d < 172800) return "Yesterday";
            if (d < 604800) return std::to_string(d / 86400) + " days ago";
            if (d < 2592000) return std::to_string(d / 604800) + " weeks ago";
            if (d < 31536000) return std::to_string(d / 2592000) + " months ago";
            return std::to_string(d / 31536000) + " years ago";
        }
    }

    void App::refreshHome()
    {
        mRecents = cosmo::ProjectStore::recents();
        std::vector<HomeScreen::CardInfo> cards;
        cards.reserve(mRecents.size());
        for (size_t i = 0; i < mRecents.size(); ++i)
        {
            const auto &r = mRecents[i];
            HomeScreen::CardInfo ci;
            ci.name = r.name;
            ci.photos = std::to_string(r.photoCount) + (r.photoCount == 1 ? " photo" : " photos");
            ci.size = formatBytes(r.sizeBytes);
            ci.date = relativeTime(r.lastOpened);
            ci.recentIndex = (int)i;
            cards.push_back(std::move(ci));
        }
        mHome->setRecents(cards);
        // Ask the host to decode each project's first image into a cover thumbnail.
        for (size_t i = 0; i < mRecents.size(); ++i)
            if (!mRecents[i].firstImagePath.empty() && onDecodeThumbnail)
                onDecodeThumbnail((int)i, mRecents[i].firstImagePath);
    }

    void App::showHome()
    {
        if (mConfirmDialog) mConfirmDialog->close();  // don't leave a modal lingering in the editor tree
        mScreen = Screen::Home;
        refreshHome();
        mScreenFade.set(1.0);
        mScreenFade.animateTo(0.0, 220.0, Easing::EaseOutCubic, mNowMs);
    }

    void App::showEditor()
    {
        mScreen = Screen::Editor;
        // Centre the project name in the top bar (R-HOME item 2): the .cmp stem.
        const std::string wp = mSession.workspacePath();
        std::string stem = wp;
        if (auto s = stem.find_last_of("/\\"); s != std::string::npos) stem = stem.substr(s + 1);
        if (auto d = stem.find_last_of('.'); d != std::string::npos) stem = stem.substr(0, d);
        mTopBar->setProjectName(stem);
        syncControlsToSlot();
        mScreenFade.set(1.0);
        mScreenFade.animateTo(0.0, 220.0, Easing::EaseOutCubic, mNowMs);
    }

    void App::requestHome()
    {
        // Unsaved edits -> ask to save or discard (discard is destructive/red); a
        // clean project just returns to the launcher (R-HOME item 1).
        if (!mSession.isDirty()) { showHome(); return; }
        ConfirmDialog::Button save{"Save", false, true, [this] { saveWorkspace(); showHome(); }};
        ConfirmDialog::Button discard{"Discard", true, false, [this] { mSession.markClean(); showHome(); }};
        ConfirmDialog::Button cancel{"Cancel", false, false, {}};
        mConfirmDialog->show("Unsaved changes",
                             "Save your changes to this project before leaving?",
                             {cancel, discard, save});
    }

    bool App::key(const artboard::KeyEvent &e)
    {
        const bool handled = (mScreen == Screen::Home) ? mHome->dispatchKey(e) : mRoot->dispatchKey(e);
        // On Home the launcher owns the keyboard: swallow unhandled keys so the
        // editor's plain-key shortcuts (o/s/Delete) don't fire behind it.
        return handled || mScreen == Screen::Home;
    }

    void App::undo()
    {
        if (mSession.undo()) syncControlsToSlot();
    }

    void App::redo()
    {
        if (mSession.redo()) syncControlsToSlot();
    }

    void App::applyParams(const EditParams &p)
    {
        mSession.applyParams(p);
        syncControlsToSlot();
    }

    void App::saveSession()
    {
        if (mSession.currentSlot() < 0) return;
        if (mSession.sessionPath().empty())
        {
            if (onSaveAsRequested) onSaveAsRequested();
        }
        else
            mSession.saveSessionAs(mSession.sessionPath());
    }

    void App::saveWorkspace()
    {
        if (mSession.imageCount() == 0) return;
        if (mSession.workspacePath().empty())
        {
            if (onSaveWorkspaceRequested) onSaveWorkspaceRequested();
        }
        else
            mSession.saveWorkspaceAs(mSession.workspacePath());
    }

    void App::finishWorkspaceLoad(const std::string &path)
    {
        mSession.finishWorkspaceLoad(path);
        syncControlsToSlot();
    }
}
}
