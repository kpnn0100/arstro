#include "App.h"
#include "core/PresetLibrary.h"
#include "widgets/TextMetrics.h"    // estimateTextWidth() for the transition labels
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

    namespace
    {
        constexpr double kRailAnimMs = 200.0;
        constexpr double kIntroMs = 460.0;    // wordmark fly + name grow + backdrop reveal
        constexpr double kRevealMs = 520.0;   // cover expands into the editor photo stage
        constexpr double kProgressMs = 200.0; // progress-bar ease toward the real fraction
        constexpr double kReturnMs = 480.0;    // wordmark flies back to home (over enter+hold)
        constexpr double kEnterMs = 300.0;     // return part 1: editor fades to the star-sky
        constexpr double kReturnHoldMs = 200.0; // return part 2: star-sky beat
        constexpr double kExitMs = 320.0;      // return part 3: home fades in from the star-sky
        constexpr double kMinLoadingMs = 260.0;  // keep the progress bar visible at least this long
        const Color kLoadingBg{0x0A / 255.0, 0x0A / 255.0, 0x0A / 255.0, 1.0};  // deep near-black loading backdrop (darker than the editor/home bg)

        Rect lerpRect(const Rect &a, const Rect &b, double t)
        {
            return Rect{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                        a.w + (b.w - a.w) * t, a.h + (b.h - a.h) * t};
        }
    }

    App::App(double width, double height)
        : mW(width), mH(height), mTheme(makeCosmoV2Theme()), mAccent(palette::primary())
    {
        mRoot = std::make_shared<Segment>();
        mRoot->width.set(width);
        mRoot->height.set(height);
        // Gestures route to whichever screen is active (launcher vs. editor).
        mRecognizer.setSink([this](const Gesture &g) {
            if (mScreen == Screen::Home) mHome->onGesture(g);
            else if (mScreen == Screen::Editor) mRoot->onGesture(g);
            // Screen::Loading swallows input — the transition is non-interactive.
        });

        mTopBar = std::make_shared<TopBar>();
        mTopBar->width.set(width);
        mTopBar->onRailToggle = [this] { toggleRail(); };
        mTopBar->onHome = [this] { requestHome(); };
        mTopBar->onNameClick = [this] {  // click the top-right group name -> rename it (DR-TREE-5)
            const int g = mSession.editGroup();
            if (g < 0 || g >= (int)mSession.nodes().size()) return;
            mRenameTargetNode = g;
            mContextMenu->openRename(mSession.nodes()[g].name, mW - 236.0, TopBar::kHeight + 2.0);
        };
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
        mContextMenu->onRename = [this](const std::string &name) { renameGroup(name); };
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
        mSettingsDialog->onUseGpu = [this](bool on) { mSession.setUseGpu(on); };  // setUseGpu re-renders (R-GPU)
        mRoot->addChild(mSettingsDialog);

        // Batch export modal (R-EXPORT). Its two host seams: a native folder chooser
        // for "Change…", and the batch write itself (which the host runs incrementally
        // so the UI never blocks -- R-EXPORT-6).
        mExportDialog = std::make_shared<ExportDialog>(mAccent);
        mExportDialog->onChooseDestination = [this] { if (onChooseExportFolderRequested) onChooseExportFolderRequested(); };
        mExportDialog->onExport = [this](ExportDialog::Request r) {
            if (onExportBatchRequested) onExportBatchRequested(std::move(r));
        };
        mRoot->addChild(mExportDialog);

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
            mOpenFromRect = mHome->lastOpenCardRect();   // fly the whole card from where it sits (R-LOADING)
            mOpenCard = mHome->lastOpenCardInfo();       // ...showing the same item at centre
            if (idx >= 0 && idx < (int)mRecents.size() && onOpenRecentRequested) onOpenRecentRequested(mRecents[idx].path);
        };
        mHome->layout();

        // Open-project transition assets (R-LOADING): a centred cover image and a
        // twinkling star-sky backdrop for the loading screen.
        mCover = std::make_shared<ImageView>();
        mCover->setFit(ImageView::Fit::Cover);  // cropped, exactly like the grid card thumbnail
        mStars.init(220);  // more, smaller specks

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
        mExportDialog->x.set(0.0); mExportDialog->y.set(0.0);
        mExportDialog->width.set(mW); mExportDialog->height.set(mH);       // full-window modal
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

    void App::resetWorkspace()
    {
        mSession.resetWorkspace();
        // Clear the editor's visible state so opening the next project fades in FRESH
        // rather than showing the previous session's photo/thumbnails during the reveal.
        // (resetWorkspace restarts slot ids at 0, so the filmstrip thumb pool must
        // restart in lockstep or new cells would index the old project's thumbnails.)
        mLastAfterFrame = RenderService::Frame{};
        mCenterStage->photo()->imageView()->clearImage();
        mCenterStage->photo()->beforeView()->clearImage();
        mCenterStage->filmstrip()->clearThumbs();
        mCenterStage->breadcrumb()->setPath({});
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
            {"Home",                     [this] { showHome(); }},
            {"Open...",                  [this] { if (onOpenRequested) onOpenRequested(); }},
            {"Save        (Ctrl+S)",     [this] { saveWorkspace(); }},
            {"Save As...  (Ctrl+Shift+S)", [this] { if (onSaveWorkspaceRequested) onSaveWorkspaceRequested(); }},
            {"Export...",                [this] { openExportDialog(); }},
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

    void App::openExportDialog()
    {
        // R-EXPORT: snapshot the group tree into the modal in PRE-ORDER, skipping the
        // root group itself (the "Select all" button already covers "everything") so
        // the tree opens on the project's own top-level groups and photos.
        if (mSession.imageCount() == 0) return;   // nothing to export
        const auto &nodes = mSession.nodes();
        std::vector<ExportDialog::Node> out;
        std::function<void(int, int)> walk = [&](int node, int parentIdx) {
            for (int k : nodes[node].kids)
            {
                ExportDialog::Node n;
                n.parent = parentIdx;
                n.group = nodes[k].group;
                n.name = nodes[k].group ? nodes[k].name : mSession.nameForSlot(nodes[k].slot);
                if (!nodes[k].group)
                {
                    n.slot = nodes[k].slot;
                    n.sourcePath = mSession.sourcePathForSlot(nodes[k].slot);
                    if (n.name.empty()) n.name = "(missing image)";
                }
                const int myIdx = (int)out.size();
                out.push_back(std::move(n));
                if (nodes[k].group) walk(k, myIdx);
            }
        };
        walk(0, -1);

        // Seed the ticks from an explicit selection when there is one; otherwise every
        // image (the reference modal's default, and the useful one for a batch).
        std::vector<int> preselect;
        if (!mSession.selection().empty()) preselect = mSession.selectedImageSlots();
        mExportDialog->show(std::move(out), preselect);
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
        const auto cells = mSession.currentGroupCells();
        // R-BYPASS-3: right-clicking any cell (group OR image) offers a filter
        // disable/enable toggle. The label names the CURRENT state's inverse, so the
        // item always reads as what the click will do.
        if (cell >= 0 && cell < (int)cells.size())
        {
            const int node = cells[cell].node;
            const bool off = mSession.isBypassed(node);
            items.push_back({off ? "Enable Filter" : "Disable Filter", [this, node] {
                mSession.toggleBypass(node);
                syncControlsToSlot();   // repaint the dim scrim, the badge and the strip
            }});
        }
        // Right-clicking a group offers Rename (morphs the menu into the rename field, DR-TREE-5).
        if (cell >= 0 && cell < (int)cells.size() && cells[cell].group)
        {
            const int node = cells[cell].node;
            const std::string name = cells[cell].name;
            items.push_back({"Rename Group", [this, node, name] {
                mRenameTargetNode = node;
                mContextMenu->enterRenameMode(name);
            }});
        }
        if (cell >= 0)
            items.push_back({"Delete",        [this] { deleteSelected(); }});
        mContextMenu->open(std::move(items), x, y);
    }

    void App::syncControlsToSlot()
    {
        const int slot = mSession.currentSlot();
        if (mSession.editGroup() >= 0 && mSession.editGroup() < (int)mSession.nodes().size())
        {
            // Editing a group: show its (clickable) name; its settings stack onto members.
            mRenameTargetNode = mSession.editGroup();
            mTopBar->setGroupName(mSession.nodes()[mSession.editGroup()].name);
        }
        else if (slot >= 0)
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

        // R-BYPASS-4: dim the edit stack while the item being edited has its filter off.
        mRightColumn->setBypassed(mSession.editTargetBypassed());

        // Filmstrip: the current group's children + which cell(s) are selected.
        std::vector<Filmstrip::Cell> cells;
        for (const auto &c : mSession.currentGroupCells())
            cells.push_back({c.group, c.node, c.slot, c.name, c.count, c.bypassed});
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
        if (mScreen == Screen::Loading) return;                           // non-interactive transition
        if (mHistoryView->isOpen()) { mHistoryView->scrollBy(delta); return; }  // modal owns the wheel
        if (mExportDialog->isOpen()) { mExportDialog->scrollBy(delta, x, y); return; }  // R-EXPORT-2 tree/body scroll

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

        if (mScreen == Screen::Loading) { renderTransition(target, nowMs); return; }
        renderEditor(target, nowMs);
    }

    void App::renderEditor(IRenderTarget &target, double nowMs)
    {
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
            if (mRightColumn->maskTabActive() && sel) ov->setMask(*sel, true);
            else                                      ov->setMask(MaskParams{}, false);
        }

        // While a batch export is running, the host's worker owns RenderService's
        // full-render channel; refreshPhotoForMode() can re-enter that same channel via
        // renderBefore()/renderPreviewSync, which holds only ONE pending request, so two
        // callers would steal each other's frame. Nothing can submit a new preview
        // behind the modal anyway, so simply leave the last frame on screen until the
        // export finishes.
        RenderService::Frame f;
        if (!exportInProgress() && mSession.renderService().tryAcquire(f) && f.width > 0)
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
        // The rename target is set when rename mode opens (context menu / top-bar name).
        if (mRenameTargetNode < 0 || mRenameTargetNode >= (int)mSession.nodes().size() || name.empty()) return;
        mSession.renameGroup(mRenameTargetNode, name);
        syncControlsToSlot();  // refresh the breadcrumb + top-bar group name
    }

    bool App::isTextEditing() const
    {
        // "The in-app keyboard is busy": the group-rename field (DR-TREE-5), or the
        // Export modal, which is keyboard-modal whether or not one of its two text
        // fields is focused -- either way the host must not fire its plain-key
        // shortcuts (o / s / Delete) behind it.
        if (mContextMenu && mContextMenu->isRenaming()) return true;
        return mExportDialog && mExportDialog->isOpen();
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
        mSettingsDialog->show(mSession.previewEdge(), arstro::par::threadsRef(),
                              mSession.useGpu(), mSession.gpuAvailable());
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
        const bool fromProject = (mScreen == Screen::Editor);
        if (mConfirmDialog) mConfirmDialog->close();  // don't leave a modal lingering in the editor tree

        if (!fromProject)  // initial launch (or already transitioning): straight to home
        {
            refreshHome();  // populate the launcher now
            mScreen = Screen::Home;
            mReturning = false;
            mHome->setWordmarkHidden(false);
            mScreenFade.set(1.0);
            mScreenFade.animateTo(0.0, 220.0, Easing::EaseOutCubic, mNowMs);
            return;
        }

        // Return transition (reverse of the open, R-LOADING-6): editor fades out to
        // the dark star-sky, a brief beat, then the home fades in — in 3 parts. The
        // wordmark flies from the top-bar slot back to its big home position; the
        // sidebar wordmark is hidden until it lands. refreshHome() is DEFERRED to the
        // ReturnLoad phase (see renderReturn) so the loading screen fades in
        // immediately and the (thumbnail) work happens behind it — not as a freeze
        // before the transition even starts.
        mScreen = Screen::Loading;   // non-interactive transition; renderTransition -> renderReturn
        mReturning = true;
        mPhase = Phase::ReturnEnter;
        mPhaseT0 = mNowMs;
        mHome->setWordmarkHidden(true);
        mEnterFade.set(0.0);  mEnterFade.animateTo(1.0, kEnterMs, Easing::EaseOutCubic, mNowMs);
        mExitFade.set(0.0);
        mReturn.set(1.0);     mReturn.animateTo(0.0, kReturnMs, Easing::EaseOutCubic, mNowMs);  // lands as exit begins
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

    // ── open-project transition (R-LOADING) ─────────────────────────────────────
    void App::beginOpenTransition(const std::string &projectName)
    {
        if (mConfirmDialog) mConfirmDialog->close();
        mLoadName = projectName;
        mLoadStatus = "Preparing\xE2\x80\xA6";  // "Preparing…" until the host reports the first item
        mScreen = Screen::Loading;
        mPhase = Phase::Intro;
        mPhaseT0 = mNowMs;
        mCoverReady = false;
        mLoadComplete = false;
        mCover->clearImage();
        mLoadDone = 0; mLoadTotal = 0;
        mCoverFrom = mOpenFromRect;              // consume the clicked-card rect (empty for Open-dialog)
        mOpenFromRect = Rect{0, 0, 0, 0};
        mLoadCard = mOpenCard;                   // the whole item to show centred (empty for Open-dialog)
        mLoadCard.name = projectName;            // ...its name is always the opening project
        mOpenCard = ProjectCardData{};
        mLoadingStarted = false;                 // part 1 is pure animation; decode starts at part 2
        mIntro.set(0.0);    mIntro.animateTo(1.0, kIntroMs, Easing::EaseOutCubic, mNowMs);
        mReveal.set(0.0);
        mProgress.set(0.0);
        mCoverFade.set(0.0);
        mBarFade.set(0.0);
    }

    void App::setLoadingCover(const uint8_t *rgba, int w, int h)
    {
        if (!rgba || w <= 0 || h <= 0) return;
        mCover->setImage(rgba, w, h);
        if (!mCoverReady)  // fade the cover in the first time it becomes available
        {
            mCoverReady = true;
            mCoverFade.set(0.0);
            mCoverFade.animateTo(1.0, 240.0, Easing::EaseOutCubic, mNowMs);
        }
    }

    void App::setLoadProgress(int done, int total)
    {
        mLoadDone = done; mLoadTotal = total;
        const double f = total > 0 ? std::min(1.0, std::max(0.0, (double)done / total)) : 0.0;
        mProgress.animateTo(f, kProgressMs, Easing::EaseOutCubic, mNowMs);
    }

    void App::setLoadStatus(const std::string &text)
    {
        mLoadStatus = text;  // shown above the progress bar (what is currently loading)
    }

    void App::finishOpenTransition()
    {
        // The load is done. Prime the editor beneath (project name), fill the bar,
        // and flag completion — the reveal itself only begins once the intro
        // animation has fully played (see renderTransition: "animation first").
        std::string stem = mSession.workspacePath();
        if (auto s = stem.find_last_of("/\\"); s != std::string::npos) stem = stem.substr(s + 1);
        if (auto d = stem.find_last_of('.'); d != std::string::npos) stem = stem.substr(0, d);
        mTopBar->setProjectName(stem);
        syncControlsToSlot();
        mProgress.animateTo(1.0, 120.0, Easing::EaseOutCubic, mNowMs);
        mLoadComplete = true;
    }

    void App::beginReveal()
    {
        mPhase = Phase::Reveal;
        mPhaseT0 = mNowMs;
        mReveal.set(0.0);  mReveal.animateTo(1.0, kRevealMs, Easing::EaseOutCubic, mNowMs);
        mScreenFade.set(0.0);  // the reveal drives the editor fade-in itself (no renderEditor scrim)
    }

    void App::drawWordmark(IRenderTarget &target, double p, double alpha) const
    {
        // p: 0 = home sidebar position (46 px) .. 1 = editor top-bar slot (13 px).
        if (alpha <= 0.001) return;
        const double sz = 46.0 + (13.0 - 46.0) * p;
        const double x = 32.0 + (9.75 /*TopBar left pad*/ - 32.0) * p;
        const double base = 96.0 + (19.2 - 96.0) * p;
        const double sp = -0.03 * sz;  // same spacing formula as HomeScreen + TopBar (consistent wordmark)
        Color fg = palette::foreground(); fg.a *= alpha;
        Color dot = palette::primary(); dot.a *= alpha;
        target.setFill(fg);
        target.drawText("cosmo", x, base, sz, font::sansSemiBold(), sp);
        target.setFill(dot);
        target.drawText(".", x + estimateTextWidth("cosmo", sz), base, sz, font::sansSemiBold(), sp);
    }

    void App::renderTransition(IRenderTarget &target, double nowMs)
    {
        if (mReturning) { renderReturn(target, nowMs); return; }  // editor→home reverse

        mIntro.update(nowMs); mReveal.update(nowMs); mProgress.update(nowMs);
        mCoverFade.update(nowMs); mBarFade.update(nowMs);

        // Part 1 (intro) -> Part 2 (loading): the intro is PURE ANIMATION; only now do
        // we ask the host to start decoding, and begin fading the progress bar in.
        if (mPhase == Phase::Intro && !mIntro.isAnimating())
        {
            mPhase = Phase::Loading;
            mPhaseT0 = nowMs;
            mBarFade.animateTo(1.0, 160.0, Easing::EaseOutCubic, nowMs);
            if (!mLoadingStarted) { mLoadingStarted = true; if (onLoadingReady) onLoadingReady(); }
        }
        // Part 2 -> Part 3 (reveal): once the decode finished AND the bar has been
        // visible long enough (so it never just flashes).
        if (mPhase == Phase::Loading && mLoadComplete && (nowMs - mPhaseT0) >= kMinLoadingMs)
            beginReveal();
        if (mPhase == Phase::Reveal && !mReveal.isAnimating())  // reveal done -> hand off to the editor
        {
            mScreen = Screen::Editor;
            mPhase = Phase::None;
            renderEditor(target, nowMs);
            return;
        }

        // The clicked recent item flies to the centre AS THE WHOLE CARD (cropped
        // thumbnail + name + photo count + size + last-edit date) at its card size — a
        // pure move, never an expand (R-LOADING-0). A progress bar the SAME WIDTH as the
        // card sits under it, with a "what's loading" status line just above the bar
        // (R-LOADING-1). Open-dialog opens (no source card) use a default card size.
        const double cw = (mCoverFrom.w > 0.0) ? mCoverFrom.w : std::clamp(mW * 0.22, 220.0, 320.0);
        const double ch = (mCoverFrom.h > 0.0) ? mCoverFrom.h : cw * 9.0 / 16.0 + projectcard::kMetaH;
        const double kStatusGap = 30.0, kBarGap = 16.0, kBarH = 4.0;
        const double stackH = ch + kStatusGap + kBarGap;   // card + status line + bar
        const Rect card{(mW - cw) * 0.5, (mH - stackH) * 0.5, cw, ch};
        const double statusBaseY = card.y + ch + kStatusGap;
        const Rect barRect{card.x, statusBaseY + kBarGap, cw, kBarH};

        // Draw the whole card (shared chrome + the cropped cover over its thumbnail band),
        // the whole thing faded by `alpha`; the cover also fades in via mCoverFade.
        auto drawCard = [&](const Rect &cr, double alpha) {
            if (alpha <= 0.001) return;
            drawProjectCardChrome(target, cr, mLoadCard, 0.0, alpha);
            if (mCoverReady)
            {
                const Rect thumb{cr.x, cr.y, cr.w, cr.w * 9.0 / 16.0};
                mCover->x.set(thumb.x); mCover->y.set(thumb.y);
                mCover->width.set(thumb.w); mCover->height.set(thumb.h);
                mCover->render(target);
                const double coverA = mCoverFade.value() * alpha;
                if (coverA < 0.999)  // fade the cover toward the backdrop (card fade + cover fade-in)
                {
                    Color s = kLoadingBg; s.a = 1.0 - coverA;
                    drawRoundedRect(target, thumb, 0.0, Paint::filled(s));
                }
            }
        };
        // Status line ("what is loading") + the card-width progress bar, one fading unit
        // directly under the card. `alpha` fades the whole unit in/out.
        auto drawStatusAndBar = [&](double alpha) {
            if (alpha <= 0.001) return;
            if (!mLoadStatus.empty())
            {
                Color c = palette::whiteAlpha(0.62); c.a *= alpha;
                target.setFill(c);
                // Left-aligned to the card's (and the bar's) left edge, not centred.
                target.drawText(mLoadStatus, card.x, statusBaseY, 13.0, font::sansMedium());
            }
            Color track = palette::whiteAlpha(0.12); track.a *= alpha;
            drawRoundedRect(target, barRect, kBarH * 0.5, Paint::filled(track));
            const double fillW = barRect.w * mProgress.value();
            if (fillW > 0.5)
            {
                Color fill = palette::primary(); fill.a *= alpha;
                drawRoundedRect(target, Rect{barRect.x, barRect.y, fillW, kBarH}, kBarH * 0.5, Paint::filled(fill));
            }
        };

        // ── Part 3: reveal — the editor materializes over the (matching) dark
        //    backdrop while the loading elements dissolve in place (no move). ──
        if (mPhase == Phase::Reveal)
        {
            const double reveal = mReveal.value();
            const double fadeOut = std::clamp(reveal / 0.45, 0.0, 1.0);        // loading elements dissolve first
            const double fadeIn = std::clamp((reveal - 0.40) / 0.60, 0.0, 1.0); // editor materialises after

            // Continuous dark backdrop (matches the editor bg, #1) so nothing flashes.
            target.save();
            target.setTransform(Transform::identity());
            drawRoundedRect(target, Rect{0, 0, mW, mH}, 0.0, Paint::filled(kLoadingBg));
            target.restore();

            // Editor components fade IN on top of the backdrop (#3).
            if (fadeIn > 0.001)
            {
                renderEditor(target, nowMs);  // mScreenFade==0 -> no internal scrim
                if (fadeIn < 0.999)
                {
                    target.save();
                    target.setTransform(Transform::identity());
                    Color s = kLoadingBg; s.a = 1.0 - fadeIn;
                    drawRoundedRect(target, Rect{0, 0, mW, mH}, 0.0, Paint::filled(s));
                    target.restore();
                }
            }

            // Loading elements fade OUT in place on top — no move (#5). While they
            // fade the editor is still mostly scrimmed, so they dissolve over dark.
            target.save();
            target.setTransform(Transform::identity());
            if (fadeOut < 0.999)
            {
                const double a = 1.0 - fadeOut;
                mStars.draw(target, Rect{0, 0, mW, mH}, a, nowMs);
                drawCard(card, a);       // the whole item, fading out in place
                drawStatusAndBar(a);     // status line + card-width bar
            }
            // Wordmark cross-fade (#2): the loading wordmark fades out AS the editor's
            // fades in (via the scrim) — same slot, same time, so it never doubles.
            drawWordmark(target, 1.0, 1.0 - fadeIn);
            target.restore();
            return;
        }

        // ── Part 1 (intro) + Part 2 (loading): gray star-sky backdrop ──
        const double intro = mIntro.value();
        target.save();
        target.setTransform(Transform::identity());
        drawRoundedRect(target, Rect{0, 0, mW, mH}, 0.0, Paint::filled(kLoadingBg));
        mStars.draw(target, Rect{0, 0, mW, mH}, intro, nowMs);  // small specks fade in with the intro

        // The whole card flies from where it sat in the grid to the centre in part 1
        // (same size — a pure move); at intro==1 it rests at `card`. It fades in with the
        // intro. Open-dialog opens (no source rect) simply fade in at `card`.
        const Rect cbox = (mCoverFrom.w > 0.0) ? lerpRect(mCoverFrom, card, intro) : card;
        drawCard(cbox, intro);

        drawWordmark(target, intro);  // flies home->top-bar in part 1; parked at 1 in part 2

        // Status line + card-width progress bar — fade in together in PART 2 (mBarFade),
        // directly under the card; the bar fills with the eased decode fraction.
        drawStatusAndBar(mBarFade.value());

        target.restore();
    }

    void App::renderReturn(IRenderTarget &target, double nowMs)
    {
        mEnterFade.update(nowMs); mExitFade.update(nowMs); mReturn.update(nowMs);

        // Advance the return phases.
        if (mPhase == Phase::ReturnEnter && !mEnterFade.isAnimating())
        {
            mPhase = Phase::ReturnLoad;
            mPhaseT0 = nowMs;
            refreshHome();  // "loading later": prepare the launcher (recents + cached thumbnails)
                            // NOW, behind the fully-shown loading screen — not as a click-time freeze
        }
        if (mPhase == Phase::ReturnLoad && (nowMs - mPhaseT0) >= kReturnHoldMs)
        {
            mPhase = Phase::ReturnExit;
            mExitFade.set(0.0);
            mExitFade.animateTo(1.0, kExitMs, Easing::EaseOutCubic, nowMs);
        }
        if (mPhase == Phase::ReturnExit && !mExitFade.isAnimating())  // done -> land on home
        {
            mScreen = Screen::Home;
            mReturning = false;
            mPhase = Phase::None;
            mHome->setWordmarkHidden(false);  // the flown copy has landed; hand off to the sidebar
            mHome->width.set(mW); mHome->height.set(mH); mHome->layout(); mHome->advance(nowMs);
            target.save(); target.setTransform(Transform::identity());
            mHome->render(target); mHome->renderOverlay(target);
            target.restore();
            return;
        }

        const double p = mReturn.value();  // 1=top-bar .. 0=home
        target.save();
        target.setTransform(Transform::identity());

        if (mPhase == Phase::ReturnExit)
        {
            // Home fades IN from the star-sky: render it, then a dark+stars overlay
            // fading OUT on top.
            const double out = 1.0 - mExitFade.value();
            mHome->width.set(mW); mHome->height.set(mH); mHome->layout(); mHome->advance(nowMs);
            mHome->render(target); mHome->renderOverlay(target);
            Color s = kLoadingBg; s.a = out;
            drawRoundedRect(target, Rect{0, 0, mW, mH}, 0.0, Paint::filled(s));
            mStars.draw(target, Rect{0, 0, mW, mH}, out, nowMs);
        }
        else if (mPhase == Phase::ReturnEnter)
        {
            // Editor fades OUT to the star-sky: render it, then a dark+stars overlay
            // fading IN on top (bg matches the editor bg, so no colour flash).
            const double in = mEnterFade.value();
            target.restore();
            renderEditor(target, nowMs);
            target.save();
            target.setTransform(Transform::identity());
            Color s = kLoadingBg; s.a = in;
            drawRoundedRect(target, Rect{0, 0, mW, mH}, 0.0, Paint::filled(s));
            mStars.draw(target, Rect{0, 0, mW, mH}, in, nowMs);
        }
        else  // ReturnLoad: full star-sky beat
        {
            drawRoundedRect(target, Rect{0, 0, mW, mH}, 0.0, Paint::filled(kLoadingBg));
            mStars.draw(target, Rect{0, 0, mW, mH}, 1.0, nowMs);
        }

        drawWordmark(target, p);  // flies top-bar -> home across the whole return
        target.restore();
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
        if (mScreen == Screen::Loading) return true;  // swallow keys during the transition
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
