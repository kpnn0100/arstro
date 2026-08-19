/*
 *  cosmo_v2 by arstro — the Figma-exact editor UI, wired to cosmo_core's
 *  EditSession for real editing behavior. Mirrors CosmoApp's public shape
 *  (host-callback seam, session/workspace passthroughs) so linux_main.cpp's
 *  native dialog glue is nearly identical to cosmo's -- but the Segment tree
 *  built here is an entirely new visual language, not a reference to cosmo's.
 */
#pragma once
#include "../../core/Artboard/include/artboard/artboard.h"
#include "core/EditSession.h"
#include "core/service/Command.h"
#include "core/service/CosmoService.h"
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
#include "core/AppSettings.h"
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
        /** S4c: the service is constructed FIRST and handed in — App is a view of it, not its
         *  owner. `mSession` below is a reference into the service's session, which is what
         *  lets the call sites still being migrated compile unchanged while the ownership has
         *  already moved. Every one of them is a line still to delete. */
        App(cosmo::CosmoService &svc, double width, double height);

        void render(artboard::IRenderTarget &target, double nowMs);
        void pointer(int kind, double x, double y, int button, double timeMs, bool alt = false, bool shift = false, bool ctrl = false);
        void wheel(double x, double y, double delta, bool ctrl);
        /** Forward a key/text event; returns true if consumed (host suppresses its
         *  own shortcut for that key). */
        bool key(const artboard::KeyEvent &e);
        /** PHYSICAL window size in device pixels. Divided by the UI scale into the LOGICAL
         *  units every widget lays out in (R-SCALE-2), so nothing below this call has to know
         *  a scale exists. */
        void setSize(double width, double height);

        // ── R-SCALE: one scale for the whole shell ────────────────────────────────────────
        /** Draw the shell at `percent` of the design size. EASES there over `kScaleAnimMs`
         *  (R-SCALE-2a / R-G-1): the drawn scale, the logical size derived from it and every
         *  widget's layout are recomputed each frame from the eased value, so the shell zooms
         *  rather than jumping and then reflowing. Snapped by `AppSettings::clampUiScale`.
         *
         *  `animate == false` for the startup apply, where there is no previous scale to
         *  travel from and a tween would be an entrance nobody asked for. */
        void setUiScale(int percent, bool animate = true);
        /** The scale that is SET — a preference is a number, not a motion. */
        int uiScale() const { return mUiScale; }
        /** The scale currently being DRAWN. Differs from `uiScale()` only mid-tween, and that
         *  difference is the only way a test can tell an eased change from a snapped one, which
         *  is why it is public (R-G-1's compliance clause). */
        double drawnUiScale() const { return mScaleAnim.value(); }
        static constexpr double kScaleAnimMs = 260.0;   // as the home grid's reflow (§1.6)
        /** The display the window is on, in physical px, so a scale whose minimum will not fit
         *  can be offered as disabled rather than as a trap (R-SCALE-3). Zero (the default, and
         *  what a headless harness leaves it at) means "unknown" — nothing is disabled. */
        void setDisplaySize(double w, double h) { mDispW = w; mDispH = h; }
        /** True when this scale's window minimum fits the reported display. */
        bool scaleFitsDisplay(int percent) const;
        /** The smallest LOGICAL size the shell can be laid out in without a column squeezing
         *  another to nothing — the larger of what the launcher needs and what the editor
         *  needs (R-SCALE-3). The host turns this into the window's minimum by multiplying by
         *  the scale, which is the whole enforcement mechanism: pick a bigger scale and the
         *  window cannot be made small enough to break. */
        /** The photo canvas's floor, in logical px — the size below which the editor stops
         *  being an editor. 260x220 is a legible thumbnail of a photo with the filmstrip and
         *  breadcrumb still under it, and it is what both the window minimum and the rail's
         *  self-collapse are derived from, so there is one number rather than two guesses. */
        static constexpr double kMinCanvasW = 260.0;
        static constexpr double kMinCanvasH = 220.0;
        static double minLogicalWidth();
        static double minLogicalHeight();
        /** Physical minimum at the scale currently in force — what the host asks GTK for. */
        double minPhysicalWidth() const { return minLogicalWidth() * (mUiScale / 100.0); }
        double minPhysicalHeight() const { return minLogicalHeight() * (mUiScale / 100.0); }

        // ── home screen / projects (R-HOME) ──
        /** The session the service drives (R-SVC-1). App still OWNS it in S2 — see
         *  CosmoService's header for why ownership moves in S4 rather than now. Every other
         *  use of this from outside App is a line S4 deletes. */
        cosmo::EditSession &session() { return mSession; }
        cosmo::CosmoService &service() { return mSvc; }

        /** Re-push every panel + the browse chrome from the session — for when a COMMAND
         *  changed the edit state from outside the widgets (a script, or an agent on the
         *  control socket, R-SVC-8). A click through the widgets has already done this for
         *  itself, so calling it again is idempotent. */
        void syncFromSession();
        /** R-SVC-2: the view's outbound channel. A widget or a shortcut that wants a
         *  behaviour the service owns emits a Command through this instead of calling the
         *  session, so the GUI, a script and the control socket all take the same path and
         *  cannot diverge. The host wires it to `CosmoService::dispatch`.
         *
         *  Not everything is here yet — the accessors below that still call `mSession`
         *  directly are S4's remaining work, and each one is a line to delete. */
        std::function<void(cosmo::Command)> onCommand;
        /** Emit `c` if a service is wired, and report whether it was. */
        bool emitCommand(const cosmo::Command &c) const { if (!onCommand) return false; onCommand(c); return true; }
        /** Feed the filmstrip's thumb pool, which is indexed BY SLOT and so must be fed in
         *  lockstep with the service attaching an image. Public because the load lives in the
         *  service now (R-SVC-1) and the host is what hears about each arrival. */
        void registerThumb(int slot);

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
        /** D-24: the bar's real target — finished entries PLUS the fraction of the ones being
         *  decoded. `done/total` could not move for nine seconds at a time, because one RAF is
         *  ~8.3 s and 90% of it is one `dcraw_process()` call. */
        void setLoadFraction(double fraction);
        /** R-LOADUX-4: entries a worker has CLAIMED, and the named stage. The bar draws
         *  `started-done` as work in progress rather than as emptiness, which is what stops a
         *  9-second first decode reading as a stall (D-22). */
        void setLoadInFlight(int started, const std::string &stage);
        /** Set the load-status line shown above the progress bar (what is being loaded,
         *  e.g. "Loading  IMG_1234.jpg"). The host feeds this per item as it decodes. */
        void setLoadStatus(const std::string &text);
        /** Loading finished: reveal the editor (the loading elements fade out in place). */
        void finishOpenTransition();
        /** At least one image has landed, so revealing would show a real photo. Gates the
         *  capped reveal for catalogs too big to wait for (R-LOADPERF-3). */
        void setLoadUsable() { mLoadUsable = true; }
        bool inOpenTransition() const { return mScreen == Screen::Loading; }
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
        /** Display name of a recent project, so the launch splash can say WHICH project
         *  it is currently reading a cover for (R-SPLASH-2a). Empty if out of range. */
        std::string recentName(int index) const
        { return (index >= 0 && index < (int)mRecents.size()) ? mRecents[index].name : std::string(); }
        int recentCount() const { return (int)mRecents.size(); }

        /** P0.6 — a named view root, for `ui dump` over the control socket. The window has
         *  two independent trees (the editor and the home screen are siblings, not one
         *  hierarchy), so a caller that wants "the UI" has to ask for both by name. Returns
         *  null for an unknown name. Read-only on purpose: an inspector that could mutate the
         *  tree would be a second, untested way to drive the app. */
        const artboard::Segment *uiRoot(const std::string &name) const;
        /** The names `uiRoot` accepts, so a client can dump everything without knowing what
         *  cosmo happens to be made of this month. */
        static const std::vector<std::string> &uiRootNames();

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

        /** R-SETTINGS-4: apply the persisted engine preferences at startup, before the
         *  first render, so the app runs with what the user last chose. */
        void applySettings(const cosmo::AppSettings &s);
        /** Fired whenever a setting changes, so the host can persist it. */
        std::function<void(cosmo::AppSettings)> onSettingsChanged;

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
        // ── R-SVC-12: the view binds to the view-model, it does not remember ──────────────
        /** Mark the view-model as possibly changed. Cheap: it sets a flag. The actual re-read
         *  happens once, in `bindIfStale()`, at the top of the frame.
         *
         *  It also tells the SERVICE to re-derive, because most callers got here after mutating
         *  `EditSession` directly rather than by sending a Command — see
         *  `CosmoService::refreshFromSession`. Without that the flag would cause the view to
         *  re-read a snapshot describing the previous edit target (D-35). */
        void syncControlsToSlot();
        /** Re-read every displayed value from the view-model, but only when it has actually
         *  changed. `AppModel::revision` increments on every change and its own header has said
         *  since S2 that "a view that has already drawn revision N can skip work" — nothing did.
         *  Called from `render()`, so a model change reaches the screen whatever caused it: a
         *  click, a script, an agent on the socket, a load finishing. There is no event to
         *  subscribe to and forget. */
        void bindIfStale();
        /** The bind itself: read the view-model, write the widgets. Was `syncControlsToSlot`'s
         *  body. Must stay a pure push — no callbacks fired, nothing read back — because it runs
         *  whenever the model moved, including under the user's hands mid-interaction. */
        void bindViewToModel();

    public:
        /** The revision the widgets currently show. Public so a test can assert the binding
         *  contract in both directions: it advances when the model moves, and does NOT when the
         *  model stood still — a bind that ran every frame would fight the user's hands. */
        unsigned boundRevision() const { return mBoundRevision; }

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

        double mW, mH;            // LOGICAL size the widgets lay out in = physical / scale
        double mPhysW = 0, mPhysH = 0;   // what the window last reported, kept so a scale
                                         // change can re-derive mW/mH without a resize event
        unsigned mBoundRevision = 0;     // the AppModel revision the widgets currently show
        bool mBindPending = true;        // force the first bind, before the first frame
        int mUiScale = 100;              // percent, the TARGET (R-SCALE-1)
        // R-SCALE-2a: the drawn scale eases to the target. Everything geometric reads this,
        // every frame — a logical size computed once from the target and then left alone is
        // exactly the snap R-G-1 forbids.
        artboard::AnimatedProperty mScaleAnim{1.0};
        double mDispW = 0, mDispH = 0;   // reported display size, 0 = unknown
        double targetScale() const { return mUiScale / 100.0; }
        double scale() const { return mScaleAnim.value(); }
        /** Re-derive the logical box from the physical one and the scale being drawn, then lay
         *  out. Called by setSize and once per frame while the scale is easing. */
        void applyLogicalSize();
        /** The view root's transform. EVERY place that used to install
         *  `Transform::identity()` installs this instead — `setTransform` is ABSOLUTE, so a
         *  scale applied by the host outside App would be wiped by the first reset inside it,
         *  which is exactly why the scale lives here and not in the GTK layer (R-SCALE-2). */
        artboard::Transform rootTransform() const
        { return artboard::Transform::scaling(scale(), scale()); }
        /** Physical pointer coordinates -> logical. The inverse of rootTransform, and the only
         *  other place the scale is allowed to appear. */
        artboard::Point toLogical(double x, double y) const
        { return artboard::Point{x / scale(), y / scale()}; }
        double mNowMs = 0.0;
        // One source of truth for the preset-rail open state; the toggle
        // highlight and the rail width both observe() it so they can't desync.
        /** Two flags, not one, and the distinction is the whole of R-SCALE-3's rail rule:
         *  `mRailWanted` is what the USER asked for and only `toggleRail` changes it;
         *  `mRailOpen` is what the window can currently afford and is DERIVED every layout.
         *  Collapsing the rail when the canvas would drop under its floor, without keeping the
         *  intent, means a window dragged narrow and then wide again has quietly thrown the
         *  user's choice away and they have to find the toggle to get it back. */
        bool mRailWanted = true;
        artboard::Observable<bool> mRailOpen{true};
        /** True when the rail can be open without pushing the canvas under its floor. */
        bool roomForRail() const
        { return mW - LeftRail::kOpenWidth - RightColumn::kWidth >= kMinCanvasW; }
        cosmo::CosmoService &mSvc;      // S4c: the application; App draws it
        cosmo::EditSession &mSession;   // = mSvc.session(), for the not-yet-migrated call sites
        cosmo::AppSettings mSettings;   // R-SETTINGS-4: what is in force + what gets saved
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
        artboard::AnimatedProperty mInFlight{0.0};        // eased target for started/total (R-LOADUX-4)
        int mLoadStarted = 0;
        std::string mLoadStage;
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
        bool mLoadUsable = false;    // at least one image landed (capped-reveal gate)
        double mLoadStartMs = 0.0;   // when the decode began — the min-visible time runs from here
        int mLoadDone = 0, mLoadTotal = 0;
        artboard::Rect mOpenFromRect{0, 0, 0, 0};  // pending: clicked-card rect (set by onOpenRecent)
        artboard::Rect mCoverFrom{0, 0, 0, 0};     // active: cover fly-in start (consumed at begin)
        artboard::AnimatedProperty mIntro{0.0};      // 0..1 intro (wordmark fly + name grow + backdrop)
        artboard::AnimatedProperty mReveal{0.0};     // 0..1 reveal (cover expands into the editor)
        artboard::AnimatedProperty mProgress{0.0};   // eased loading progress bar (0..1)
        artboard::AnimatedProperty mCoverFade{0.0};  // cover fade-in once it is ready
        artboard::AnimatedProperty mBarFade{0.0};    // progress-bar fade-in on entering part 2
        // return-to-home (reverse) transition: editor fades to the star-sky, then the
        // home fades in; the wordmark flies from the top-bar slot back to its home spot.
        bool mReturning = false;
        artboard::AnimatedProperty mReturn{0.0};     // 1=top-bar(small) .. 0=home(big)
        artboard::AnimatedProperty mEnterFade{0.0};  // ReturnEnter: editor -> star-sky (0..1)
        artboard::AnimatedProperty mExitFade{0.0};   // ReturnExit: star-sky -> home (0..1)
    };
}
}
