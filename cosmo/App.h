/*
 *  cosmo_v2 by arstro — the Figma-exact editor UI, wired to cosmo_core's
 *  EditSession for real editing behavior. Mirrors CosmoApp's public shape
 *  (host-callback seam, session/workspace passthroughs) so linux_main.cpp's
 *  native dialog glue is nearly identical to cosmo's -- but the Segment tree
 *  built here is an entirely new visual language, not a reference to cosmo's.
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include "core/EditSession.h"
#include "Theme.h"
#include "widgets/TopBar.h"
#include "widgets/LeftRail.h"
#include "widgets/CenterStage.h"
#include "widgets/RightColumn.h"
#include "widgets/HistoryView.h"
#include "widgets/ContextMenu.h"
#include "widgets/PresetDialog.h"
#include "widgets/ExportDialog.h"
#include "widgets/SettingsDialog.h"
#include "widgets/ConfirmDialog.h"
#include "widgets/HomeScreen.h"
#include "widgets/Starfield.h"
#include "core/ProjectStore.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class App
    {
    public:
        App(double width, double height);

        void render(artboard::IRenderTarget &target, double nowMs);
        void pointer(int kind, double x, double y, int button, double timeMs, bool alt = false, bool shift = false, bool ctrl = false);
        void wheel(double x, double y, double delta, bool ctrl);
        /** Forward a key/text event; returns true if consumed (host suppresses its
         *  own shortcut for that key). */
        bool key(const artboard::KeyEvent &e);
        void setSize(double width, double height);

        // ── home screen / projects (R-HOME) ──
        void showHome();     // leave the editor, show the project launcher (refreshes recents)
        void showEditor();   // enter the editor (after a project is created/opened)
        bool onHomeScreen() const { return mScreen == Screen::Home; }

        // ── animated open-project transition (R-LOADING) ──
        /** Begin the home→loading transition: the wordmark flies to the top-bar
         *  slot, the project name grows in centred over a star-sky loading screen.
         *  The host then decodes the project incrementally, feeding setLoadingCover
         *  / setLoadProgress, and finally calls finishOpenTransition(). */
        void beginOpenTransition(const std::string &projectName);
        /** Push the project's cover (first image) so it can be shown centred while
         *  loading and then expanded into the editor's first preview. */
        void setLoadingCover(const uint8_t *rgba, int w, int h);
        /** Update the loading progress bar (0 = start, done==total = complete). */
        void setLoadProgress(int done, int total);
        /** Set the load-status line shown above the progress bar (what is being loaded,
         *  e.g. "Loading  IMG_1234.jpg"). The host feeds this per item as it decodes. */
        void setLoadStatus(const std::string &text);
        /** Loading finished: reveal the editor (the loading elements fade out in place). */
        void finishOpenTransition();
        bool inOpenTransition() const { return mScreen == Screen::Loading; }
        /** Fired once the intro animation completes (part 1 → part 2): the host starts
         *  the actual decode ONLY now, so part 1 is pure animation (no I/O). */
        std::function<void()> onLoadingReady;
        /** True once a loading-screen cover image has been supplied. */
        bool hasLoadingCover() const { return mCoverReady; }
        /** Host pushes a decoded cover for recent `recentIndex` (first image). */
        void setHomeThumbnail(int recentIndex, const uint8_t *rgba, int w, int h)
        { if (mHome) mHome->setThumbnail(recentIndex, rgba, w, h); }
        std::function<void()> onNewProjectRequested;      // host: Save-As .cmp dialog -> newProject
        std::function<void()> onOpenProjectRequested;     // host: Open .cmp dialog -> openProject
        std::function<void()> onImportCatalogRequested;   // host: multi-image dialog -> new project
        std::function<void(const std::string &cmpPath)> onOpenRecentRequested;  // host: load that .cmp
        std::function<void(int recentIndex, const std::string &imagePath)> onDecodeThumbnail;  // host decodes -> setHomeThumbnail

        int openImage(const uint8_t *rgba, int w, int h, const std::string &name, const std::string &path = "");
        void selectImage(int slot);
        void deleteSelected();
        int imageCount() const { return mSession.imageCount(); }
        const uint8_t *exportFullRes(int &w, int &h) { return mSession.exportFullRes(w, h); }
        /** Batch-export passthroughs (R-EXPORT): the host renders one selected slot
         *  per idle step and reports back through setExportProgress(). */
        const uint8_t *exportFullResSlot(int slot, int &w, int &h) { return mSession.exportFullResSlot(slot, w, h); }
        std::string sourcePathForSlot(int slot) const { return mSession.sourcePathForSlot(slot); }
        std::string nameForSlot(int slot) const { return mSession.nameForSlot(slot); }

        // ── File-menu actions wired by the host (it owns the file dialogs) ──
        std::function<void()> onOpenRequested;
        std::function<void()> onSaveRequested;
        std::function<void()> onSaveAsRequested;
        std::function<void()> onExportRequested;   // host: quick single-image save dialog (bare 's') -> exportFullRes
        /** The Export modal's "Change…" (R-EXPORT-3): the host shows a native folder
         *  chooser and answers with setExportDestination(). */
        std::function<void()> onChooseExportFolderRequested;
        /** The Export modal's Export button (R-EXPORT-6): the host writes the batch
         *  incrementally, feeding setExportProgress() so the UI keeps painting. */
        using ExportRequest = ExportDialog::Request;
        std::function<void(ExportRequest)> onExportBatchRequested;
        void setExportDestination(const std::string &dir) { if (mExportDialog) mExportDialog->setDestination(dir); }
        void setExportProgress(int done, int total, const std::string &name)
        { if (mExportDialog) mExportDialog->setExportProgress(done, total, name); }
        void cancelExport() { if (mExportDialog) mExportDialog->cancelExport(); }
        /** True while a batch is being written. The host's export worker calls
         *  exportFullResSlot() off the UI thread, and RenderService's full/sync render
         *  channel holds ONE pending request at a time — so while this is true the UI
         *  thread must not enter that channel itself (see App::render). */
        bool exportInProgress() const { return mExportDialog && mExportDialog->isExporting(); }
        std::function<void()> onSavePresetRequested;
        std::function<void()> onExportPresetRequested;
        std::function<void()> onImportPresetRequested;
        std::function<void()> onRenameGroupRequested;
        std::function<void()> onSaveWorkspaceRequested;
        std::function<void()> onLoadWorkspaceRequested;
        void renameGroup(const std::string &name);
        /** R-BROWSE-2: select the previous/next cell of the current group (`dir` -1/+1),
         *  image or group chip alike, and scroll the rack to keep it in view. Clamped at
         *  both ends — no wrap. Returns false when it could not move. */
        bool stepSelection(int dir);
        /** True while the in-app rename field is focused — the host suppresses its
         *  single-key shortcuts so typed keys go to the field (DR-TREE-5). */
        bool isTextEditing() const;

        void setPresetDir(const std::string &dir) { mSession.setPresetDir(dir); refreshPresetTree(); }
        bool savePreset(const std::string &name) { const bool ok = mSession.savePreset(name); if (ok) refreshPresetTree(); return ok; }
        bool exportPresetTo(const std::string &path) { return mSession.exportPresetTo(path); }
        /** NOTE: applies every present category immediately -- the category-picker
         *  modal (Figma brief frame 8) is one of the secondary states scoped for a
         *  follow-up pass, so import is "quick apply all" until it lands. */
        bool importPresetFrom(const std::string &path);
        bool applyPreset(const std::string &name) { return mSession.applyPreset(name); }
        bool deletePresetFile(const std::string &name) { const bool ok = mSession.deletePresetFile(name); if (ok) refreshPresetTree(); return ok; }

        void undo();
        void redo();
        bool canUndo() const { return mSession.canUndo(); }
        bool canRedo() const { return mSession.canRedo(); }

        std::string currentSourcePath() const { return mSession.currentSourcePath(); }
        void applyParams(const EditParams &p);
        void saveSession();
        bool saveSessionAs(const std::string &path) { return mSession.saveSessionAs(path); }
        static bool readSessionFile(const std::string &path, std::string &imagePath, EditParams &params)
        { return cosmo::EditSession::readSessionFile(path, imagePath, params); }

        using WorkspaceEntry = cosmo::EditSession::WorkspaceEntry;
        static bool readWorkspaceFile(const std::string &path, std::vector<WorkspaceEntry> &out)
        { return cosmo::EditSession::readWorkspaceFile(path, out); }
        bool saveWorkspaceAs(const std::string &path) { return mSession.saveWorkspaceAs(path); }
        void saveWorkspace();
        std::string currentWorkspacePath() const { return mSession.workspacePath(); }
        void resetWorkspace();  // reset the session AND clear the editor's visible state
        int addWorkspaceGroup(int parentNode, const std::string &name, const arstro::EditParams &params,
                              const cosmo::History &history = cosmo::History{}, bool bypass = false)
        { return mSession.addWorkspaceGroup(parentNode, name, params, history, bypass); }
        int openImageInto(int parentNode, const uint8_t *rgba, int w, int h, const std::string &name, const std::string &path);
        /** Loader fast path (R-LOADPERF-2): the decoded buffer is MOVED into the engine
         *  and the filmstrip thumbnail was already built on the loader thread, so the UI
         *  thread pays neither the ~100 MB copy nor the downsample. */
        int openImageInto(int parentNode, std::vector<uint8_t> &&rgba, int w, int h,
                          const std::string &name, const std::string &path,
                          cosmo::EditSession::Thumb &&thumb);
        int addWorkspaceMissingImage(int parentNode, const std::string &name)
        { return mSession.addWorkspaceMissingImage(parentNode, name); }
        void applyParamsToSlot(int slot, const EditParams &p) { mSession.applyParamsToSlot(slot, p); }
        void applyParamsToSlot(int slot, const EditParams &p, const cosmo::History &history)
        { mSession.applyParamsToSlot(slot, p, history); }
        /** R-BYPASS-6: restore a loaded image leaf's "filter disabled" flag. */
        void setSlotBypass(int slot, bool on) { mSession.setSlotBypass(slot, on); }
        void finishWorkspaceLoad(const std::string &path);

    private:
        void layout();
        /** Push the current slot's params into every panel that isn't backed by
         *  live queries -- filled in as each panel lands (no-op until then). */
        void syncControlsToSlot();
    public:
        /** Re-push ONLY the browse chrome (filmstrip cells + selection + the top bar's
         *  n/total). The host calls this per image while a project streams in behind the
         *  already-revealed editor (R-LOADPERF-3), so newly arrived photos appear in the
         *  filmstrip without disturbing the develop panels mid-edit. */
        void refreshLibrary();
        /** R-LOADUX-1: build the project's whole tree BEFORE any decoding, so the rack
         *  shows its real size from the first frame. Returns the node index per entry
         *  (groups included) so the host can attach pixels to the right leaf later. */
        std::vector<int> buildPendingTree(const std::vector<WorkspaceEntry> &entries);
        /** R-LOADUX-1: give a pending leaf its decoded pixels + prebuilt thumbnail. */
        int attachImage(int node, std::vector<uint8_t> &&rgba, int w, int h,
                        const std::string &path, cosmo::EditSession::Thumb &&thumb);
        void markImageFailed(int node) { mSession.markImageFailed(node); }
        /** R-LOADUX-3: streaming progress shown along the top of the photo rack. */
        void setStreamProgress(int done, int total)
        { mCenterStage->filmstrip()->setLoadProgress(done, total); }
    private:
        void refreshPresetTree();
        void toggleRail();
        void registerThumb(int slot);
        /** Push the correct image(s) into the photo canvas for its current
         *  before/split/after mode (before = original, after = edited, split = both). */
        void refreshPhotoForMode();
        void buildMenus();          // populate the TopBar's MenuStrip + wire actions
        void copySettings();        // Develop ▸ Copy Settings
        void pasteSettings(bool toAll);  // Develop ▸ Paste to Selected / to All Images
        void openHistoryView();     // History ▸ Show History Tree… (in-app, like cosmo)
        void presetSaveClicked();   // Save Preset -> category picker -> host name dialog
        void presetExportClicked(); // Export Preset -> category picker -> host path dialog
        void openSettingsDialog();  // Settings ▸ Engine Settings… (modal)
        void openExportDialog();    // File ▸ Export… (R-EXPORT): snapshot the tree into the modal
        void refreshHome();         // rebuild the home grid from ProjectStore + request thumbnails
        void requestHome();         // wordmark click: prompt to save/discard if dirty, else go home

        enum class Screen { Home, Loading, Editor };
        void openEditContext(double x, double y, int cell);  // right-click menu (cell<0 = photo area)
        void renderEditor(artboard::IRenderTarget &target, double nowMs);      // the editor screen body
        void renderTransition(artboard::IRenderTarget &target, double nowMs);  // the open-project transition
        void renderReturn(artboard::IRenderTarget &target, double nowMs);      // the editor→home (reverse) transition
        void beginReveal();                      // start the reveal (editor fades in, loading dissolves)
        void drawWordmark(artboard::IRenderTarget &target, double p, double alpha = 1.0) const;  // p: 0=home(big) .. 1=top-bar(small)

        double mW, mH;
        double mNowMs = 0.0;
        // One source of truth for the preset-rail open state; the toggle
        // highlight and the rail width both observe() it so they can't desync.
        artboard::Observable<bool> mRailOpen{true};
        cosmo::EditSession mSession;
        EditParams mClipboard;       // Develop ▸ Copy/Paste Settings clipboard
        bool mHasClipboard = false;
        RenderService::Frame mLastAfterFrame;
        artboard::Theme mTheme;
        artboard::Color mAccent;
        artboard::GestureRecognizer mRecognizer;
        std::shared_ptr<artboard::Segment> mRoot;
        std::shared_ptr<TopBar> mTopBar;
        std::shared_ptr<LeftRail> mLeftRail;
        std::shared_ptr<CenterStage> mCenterStage;
        std::shared_ptr<RightColumn> mRightColumn;
        std::shared_ptr<HistoryView> mHistoryView;
        std::shared_ptr<ContextMenu> mContextMenu;
        int mRenameTargetNode = -1;   // group node the in-app rename currently targets
        std::shared_ptr<PresetDialog> mPresetDialog;      // modal category picker (overlay)
        std::shared_ptr<SettingsDialog> mSettingsDialog;  // modal engine settings (overlay)
        std::shared_ptr<ExportDialog> mExportDialog;      // modal batch export (overlay, R-EXPORT)
        std::shared_ptr<ConfirmDialog> mConfirmDialog;    // modal save/discard prompt (overlay)

        Screen mScreen = Screen::Home;                    // app starts on the launcher
        std::shared_ptr<HomeScreen> mHome;
        artboard::AnimatedProperty mScreenFade{0.0};      // cross-fade scrim on screen switch (1->0)
        std::vector<cosmo::RecentEntry> mRecents;         // backing the home grid (open-by-index)

        // ── open-project transition (R-LOADING) ──
        // Open:   Intro → Loading → Reveal.   Return: ReturnEnter → ReturnLoad → ReturnExit.
        enum class Phase { None, Intro, Loading, Reveal, ReturnEnter, ReturnLoad, ReturnExit };
        Phase mPhase = Phase::None;
        double mPhaseT0 = 0.0;               // start time of the current phase
        std::string mLoadName;               // project name (also mLoadCard.name)
        std::string mLoadStatus;             // "what is loading" line above the progress bar
        ProjectCardData mOpenCard;           // clicked recent card info (consumed at begin)
        ProjectCardData mLoadCard;           // the whole item drawn centred during loading
        Starfield mStars;                    // twinkling loading backdrop
        std::shared_ptr<artboard::ImageView> mCover;  // project cover (centre -> photo stage)
        bool mCoverReady = false;
        bool mLoadComplete = false;  // host signalled the decode finished (reveal gate)
        int mLoadDone = 0, mLoadTotal = 0;
        artboard::Rect mOpenFromRect{0, 0, 0, 0};  // pending: clicked-card rect (set by onOpenRecent)
        artboard::Rect mCoverFrom{0, 0, 0, 0};     // active: cover fly-in start (consumed at begin)
        artboard::AnimatedProperty mIntro{0.0};      // 0..1 intro (wordmark fly + name grow + backdrop)
        artboard::AnimatedProperty mReveal{0.0};     // 0..1 reveal (cover expands into the editor)
        artboard::AnimatedProperty mProgress{0.0};   // eased loading progress bar (0..1)
        artboard::AnimatedProperty mCoverFade{0.0};  // cover fade-in once it is ready
        artboard::AnimatedProperty mBarFade{0.0};    // progress-bar fade-in on entering part 2
        bool mLoadingStarted = false;                // onLoadingReady fired for this open
        // return-to-home (reverse) transition: editor fades to the star-sky, then the
        // home fades in; the wordmark flies from the top-bar slot back to its home spot.
        bool mReturning = false;
        artboard::AnimatedProperty mReturn{0.0};     // 1=top-bar(small) .. 0=home(big)
        artboard::AnimatedProperty mEnterFade{0.0};  // ReturnEnter: editor -> star-sky (0..1)
        artboard::AnimatedProperty mExitFade{0.0};   // ReturnExit: star-sky -> home (0..1)
    };
}
}
