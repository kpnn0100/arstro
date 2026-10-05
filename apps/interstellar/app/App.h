/*
 *  interstellar_v1 — App: Interstellar's front end, a platform-free Artboard Segment tree.
 *
 *  Draws ONLY from `AppHooks::model()` and reports intent ONLY as text command lines through
 *  `AppHooks::dispatch` (project-format.md §8) — so the GUI can do nothing a script cannot (R-G-4),
 *  and a second front end needs nothing from this one. What IS this class's own is presentation:
 *  which tab is open, hover, scroll, every tween. The host (the integrator's GTK window) owns the
 *  window, the clock and the native file dialogs; it forwards input here and asks for frames.
 *
 *  Screens follow `AppModel::screen` (Home / Loading / Edit) — the service derives it — and the
 *  App CROSS-FADES between them (260 ms, the shell cross-fade), never cuts. The view re-reads the
 *  model once per frame, guarded by `revision`, so a change reaches the screen whoever caused it.
 *
 *  Host seam (beyond AppHooks): `pointer` (kind 0 = down, 2 = up, anything else = move), `wheel`
 *  (notches, + = up), `key`, `setSize`, `render(target, nowMs)`, `needsRedraw`. The three native
 *  pickers are `onPickProjectToOpen` / `onPickProjectToCreate` / `onPickFootage`: the host shows its
 *  dialog and answers with `openProjectPicked(path)` / `newProjectPicked(path)` /
 *  `footagePicked(paths)`, which become `project open|new <path>` / `rack add <paths…>`.
 *
 *  `installInterstellarAccent()` is called FIRST in the constructor, before any widget exists:
 *  several cosmo widgets read the accent once at construction (ConfirmDialog's accent, the
 *  segmented pickers' highlight), and the contract for the slot is "before the first frame".
 */
#pragma once
#include "Theme.h"
#include "AppHooks.h"
#include "widgets/HomeScreen.h"
#include "widgets/LoadingView.h"
#include "widgets/EditScreen.h"
#include "../../cosmo/widgets/SettingsDialog.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar_v1
{
    class App
    {
    public:
        App(AppHooks hooks, double width, double height);

        void setSize(double width, double height);
        void render(artboard::IRenderTarget &target, double nowMs);
        /** Conservative: true while anything could be moving (a fade, playback, a model change not
         *  yet painted), and for a short window after any input. Never truncates a tween. */
        bool needsRedraw(double nowMs) const;

        void pointer(int kind, double x, double y, int button, double timeMs, bool alt = false, bool shift = false, bool ctrl = false);
        void wheel(double x, double y, double notches, bool ctrl = false);
        bool key(const artboard::KeyEvent &e);

        /** Send one command line through the hooks. A refusal is SAID (the toast), never dropped. */
        bool dispatch(const std::string &line);

        // ── native pickers: the host shows its dialog, then answers ──
        std::function<void()> onPickProjectToOpen, onPickProjectToCreate, onPickFootage;
        /** File › Save As…, Preset › Import Preset…, File › Export Still… (cosmo's File/Preset). */
        std::function<void()> onPickSaveAs, onPickPresetToImport, onPickStillToExport;
        /** Host pickers for LUTs (R-COLOR-5/6): a .cube to read, a .cube to write (named `suggested`);
         *  each answers through `done`, never on cancel. */
        std::function<void(std::function<void(const std::string &)> done)> onPickLutToOpen;
        std::function<void(const std::string &suggested, std::function<void(const std::string &)> done)> onPickLutToSave;
        /** R-XCH: an EDL / FCPXML / OTIO to read, or one to write (the extension picks the format). */
        std::function<void(std::function<void(const std::string &)> done)> onPickTimelineToImport;
        std::function<void(const std::string &suggested, std::function<void(const std::string &)> done)> onPickTimelineToExport;
        // R-DLV-1: an .srt to read onto the open timeline, and where to write one
        std::function<void(std::function<void(const std::string &)> done)> onPickCaptionsToImport;
        std::function<void(const std::string &suggested, std::function<void(const std::string &)> done)> onPickCaptionsToExport;
        /** R-MEDIA-3: the file a missing source is now (named `name`, as it was), or a folder to search. */
        std::function<void(const std::string &name, std::function<void(const std::string &)> done)> onPickMediaToRelink;
        std::function<void(std::function<void(const std::string &)> done)> onPickFolder;
        void openRelinkMenu(artboard::Point at);
        void grabStill();                                        // R-CLR-4
        void openNodeContext(const NodeGraph::Node &n, artboard::Point at);   // R-CLR-3
        void openStillContext(const std::string &stillId, artboard::Point at);
        void openProjectPicked(const std::string &path);
        void newProjectPicked(const std::string &path);
        void footagePicked(const std::vector<std::string> &paths);
        void saveAsPicked(const std::string &path);          // → project save <path>
        void presetImportPicked(const std::string &path);    // → preset import <path>
        void stillExportPicked(const std::string &path);     // → export-still of the open timeline at the playhead
        /** The capture button's "Save Frame…" (R-UI-11): the host asks for a .png and answers here,
         *  which becomes `capture --out <path> [--source <bind>]`. */
        std::function<void()> onPickFrameToSave;
        void frameSavePicked(const std::string &path);
        /** Ask to go Home: `project close`, behind cosmo's ConfirmDialog when there are unsaved edits. */
        void requestHome();

        // ── presentation state ──
        void setTab(int tab) { mEdit->setTab(tab); }
        int tab() const { return mEdit->tab(); }
        /** The screen being shown (the cross-fade's target) and each screen layer's LIVE opacity. */
        interstellar::Screen screen() const { return mScreen; }
        double screenOpacity(interstellar::Screen s) const;
        void setHomeClock(long long nowUnix) { mHome->setNowUnix(nowUnix); }

        /** The LOGICAL minimum — the design size the layout needs. */
        static double minWidth() { return std::max(EditScreen::minWidth(), HomeScreen::kSidebarW + 2 * HomeScreen::kPad + HomeScreen::kMinCard); }
        static double minHeight() { return std::max(EditScreen::minHeight(), 560.0); }
        /** The window minimum at the TARGET screen scale — what the host asks the window for. */
        double minPhysicalWidth() const { return minWidth() * mUiScale / 100.0; }
        double minPhysicalHeight() const { return minHeight() * mUiScale / 100.0; }

        // ── screen scale (cosmo R-SCALE): followed from `settings.uiScale`, EASED ──
        int uiScale() const { return mUiScale; }
        /** The scale being drawn now; differs from uiScale() only mid-tween. */
        double drawnUiScale() const { return mScaleAnim.value(); }
        /** The largest scale this display can give a window for (the host measures it); the
         *  settings dialog draws larger ones disabled. */
        void setMaxUiScale(int percent) { mMaxUiScale = percent; }

        /** Engine Settings… — cosmo's own dialog, one instance for Home and Edit. */
        void openSettings();
        double width() const { return mW; }
        double height() const { return mH; }

        // ── the trees, for the host's debugging and for tests ──
        HomeScreen &home() { return *mHome; }
        LoadingView &loading() { return *mLoading; }
        EditScreen &edit() { return *mEdit; }
        cosmo_v2::SettingsDialog &settings() { return *mSettings; }
        artboard::Segment *activeRoot();
        const AppHooks &hooks() const { return mHooks; }

    private:
        void bindIfStale(double nowMs);
        void buildMenus();
        void refreshPresetMenu(const interstellar::AppModel &m);
        /** The Colour menu: the working space, the current one marked (R-COLOR-3). */
        void refreshColourMenu(const interstellar::AppModel &m);
        void refreshWorkspaceMenu(const interstellar::AppModel &m);
        void refreshSettingsMenu(const interstellar::AppModel &m);   // R-DLV-5
        /** The rack row's input-colour list, opened in place of its menu (R-COLOR-2). */
        void openInputColourMenu(const std::string &bind, artboard::Point at);
        void setUiScale(int percent, bool animate);
        void applyLogicalSize();
        artboard::Transform rootTransform() const;
        std::string selectedBind() const;
        bool editKey(const artboard::KeyEvent &e);
        void openRackContext(int rackIndex, artboard::Point at);
        void openCaptureMenu(artboard::Rect at);
        void openAddEffectMenu(artboard::Rect at);
        void openKeyContext(const std::string &address, double t, artboard::Point at);
        void openKeyPlotContext(double t, artboard::Point at);
        /** The key lane's graph, when the Cut tab shows it (else null). */
        KeyGraph *keyGraphShown();
        void openPluginContext(const std::string &id, artboard::Point at);
        void dropSource(const std::string &src, const std::string &track, double at);
        void openClipContext(const std::string &clipId, artboard::Point at);
        void openLaneContext(const std::string &trackId, double t, artboard::Point at);
        void openPlaceTimelineMenu(const std::string &trackId, double t, artboard::Point at);
        std::string freshMarkerName() const;
        std::string captureBind() const;
        std::string mCaptureBind;                       // what the open capture menu captures ("" = the timeline)
        std::string mRenameTarget;                      // the bind name the context menu is renaming
        void fetchFrame(const interstellar::AppModel &m, bool force);
        void showScopes(const interstellar::Raster &frame);
        void fetchSource(const interstellar::AppModel &m, const interstellar::RackNodeModel &n, int edge, bool force);
        void fetchViewer(const interstellar::AppModel &m, int edge, bool force);   // R-EDT-1: the source viewer
        void layoutAll();
        void noteActivity() { mLastActivityMs = mNowMs; }
        bool textEditing() const;
        static const interstellar::AppModel &emptyModel();

        AppHooks mHooks;
        double mW, mH;
        double mNowMs = 0.0, mLastActivityMs = -1e9;
        std::shared_ptr<HomeScreen> mHome;
        std::shared_ptr<LoadingView> mLoading;
        std::shared_ptr<EditScreen> mEdit;
        std::shared_ptr<cosmo_v2::SettingsDialog> mSettings;
        double mPhysW = 0.0, mPhysH = 0.0;             // the host's size; mW/mH are logical
        int mUiScale = 100, mMaxUiScale = 10000;
        bool mScaleBound = false;
        artboard::AnimatedProperty mScaleAnim{1.0};
        std::vector<std::string> mPresetNames;          // what the Preset menu lists now
        std::string mColourMenuFor;                     // the working space the Colour menu marks
        int mWorkspaceMenuFor = -1;                     // R-MEDIA-2: 0 no project · 1 originals · 2 proxies
        int mSettingsMenuFor = -2;                      // R-DLV-5: the autosave interval the Settings menu marks
        std::string mRecoveryAsked;                     // R-DLV-6: the autosave already offered (project|time)
        unsigned mThumbEpoch = 0;                       // the host's thumbnail epoch last seen
        artboard::GestureRecognizer mRecognizer;

        interstellar::Screen mScreen = interstellar::Screen::Home;
        bool mScreenInit = false;
        bool mBound = false;
        unsigned mSeenRevision = 0, mPaintedRevision = 0, mPaintedFrameSeq = 0;
        bool mPointerDown = false;

        // frame fetch state
        unsigned mFetchedSeq = ~0u;
        int mFetchedEdge = 0;
        double mFetchedAt = -1.0;
        std::string mFetchedTimeline;
        unsigned mFetchedRevision = ~0u;
        bool mFetchedSource = false;                    // the monitor shows one source (Grade), not the timeline
        bool mKDown = false;                            // R-EDT-2: K held — J/L step a frame
        std::string mFetchedBind;
        // the ref-frame slider's live preview (R-RACK-3): the source and the time it is dragged to;
        // mPreviewAt < 0 = not previewing (the monitor shows the committed reference frame)
        std::string mPreviewBind;
        double mPreviewAt = -1.0;
        interstellar::Raster mFrame;
    };
}
}
