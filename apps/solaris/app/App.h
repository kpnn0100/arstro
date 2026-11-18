/*
 *  solaris_ui — App: Solaris's front end, a platform-free Artboard Segment tree (R-UI-1, R-G-4).
 *
 *  Draws ONLY from `AppHooks::model()` and reports intent ONLY as text command lines through
 *  `AppHooks::dispatch` (docs/API.md) — the GUI can do nothing a script cannot, and a refusal is
 *  SAID (a toast with the service's sentence), never dropped. What is this class's own is
 *  presentation: which screen is showing, the settings sheet, hover, every tween. The host owns the
 *  window, the clock and the native file pickers.
 *
 *  Screens follow `AppModel::screen` (home | project) and CROSS-FADE (260 ms) — never cut. The view
 *  re-binds from the model once per frame when its `revision` moved, so a change reaches the
 *  screen whoever made it (a shell, a script, this window).
 *
 *  Host seam: `pointer` (kind 0 down, 2 up, else move; button 2 = right), `wheel` (notches, + = up),
 *  `key`, `setSize`, `render(target, nowMs)`, `needsRedraw`. Pickers: `onPickSongToOpen`,
 *  `onPickSongToCreate`, `onPickFolder` — the host shows its dialog and answers with
 *  `openSongPicked` / `newSongPicked` / the `done` callback.
 *
 *  `installSolarisAccent()` runs FIRST in the constructor, before any widget exists.
 */
#pragma once
#include "AppHooks.h"
#include "Words.h"
#include "A11y.h"
#include "Theme.h"
#include "widgets/HomeScreen.h"
#include "widgets/ProjectScreen.h"
#include "widgets/SettingsSheet.h"
#include "widgets/ExportSheet.h"
#include "widgets/RelinkSheet.h"
#include "widgets/ConflictsSheet.h"
#include "widgets/VideoPanel.h"
#include "../../cosmo/widgets/ConfirmDialog.h"
#include "../../cosmo/widgets/ContextMenu.h"
#include "../../cosmo/widgets/HistoryView.h"
#include "../../cosmo/widgets/Toast.h"
#include <functional>
#include <memory>
#include <string>

namespace arstro
{
namespace solaris_ui
{
    /** R-UI-15: the three voices of a toast — something was done · the song is open and something is owed · a line was
     *  refused. Told apart by shape as well as colour (a 3 px bar and a glyph), never by the accent. */
    using ToastKind = cosmo_v2::Toast::Kind; // R-FAM-4: the family's toast, and its three kinds

    class App
    {
    public:
        App(AppHooks hooks, double width, double height);

        /** The window's PHYSICAL size. Divided by the drawn UI scale into the LOGICAL size every widget lays out in
         *  (R-ACCESS-1, cosmo R-SCALE-2), so nothing below this call knows a scale exists. */
        void setSize(double width, double height);
        /** R-HOST-2: where the window's top-left is in the target, in device pixels — (0, 0) for a window of its own; the
         *  frame another app gives it when that app hosts Solaris (Interstellar's Song page). Every pointer is read from
         *  it and everything is drawn from it; the host clips to the frame. */
        void setOrigin(double x, double y) { mOx = x; mOy = y; }
        /** R-ACCESS-1: the shell drawn at the model's `settings.uiScale`, EASED — every frame the logical size is the
         *  window's divided by the drawn scale and every pointer is divided by it, so the shell zooms, never a cut then
         *  a reflow (cosmo's kScaleAnimMs). High contrast likewise: one eased amount on cosmo's tokens. */
        static constexpr double kScaleAnimMs = 260.0;
        double drawnUiScale() const { return mScale.value(); }        // LIVE
        double contrastAmount() const { return mContrast.value(); }   // LIVE: 0 normal … 1 high
        double logicalWidth() const { return mW; }
        double logicalHeight() const { return mH; }

        /** R-ACCESS-1: every control the window shows as an accessible node, in reading order — the song bar (its menus'
         *  titles after the wordmark), the browser, the lanes' headers, the dock, the open windows; a modal's — a menu open,
         *  a dialog, a sheet — ALONE while it is open. What the keyboard walks and the host gives AT-SPI. */
        std::vector<A11yNode> accessibles();
        /** A node by its key, as the keyboard does it: its activation (the click it stands for), its step. False when
         *  there is no such node, or it is not enabled. */
        bool activateNode(const std::string &key);
        bool stepNode(const std::string &key, int dir, bool fine);
        /** The keyboard's focus: Tab / Shift+Tab move it through the enabled nodes, Enter and Space activate, the arrows
         *  step (Shift: a tenth), Escape lets go. While it is held those keys are its; else they are the keymap's. */
        const std::string &focusKey() const { return mFocusKey; }
        bool focusHeld() const { return mFocusOn; }
        double focusAmount() const { return mRingA.value(); }        // LIVE: the ring's fade
        artboard::Rect focusRing() const { return artboard::Rect{mRingX.value(), mRingY.value(), mRingW.value(), mRingH.value()}; } // LIVE
        /** Said when the keyboard's focus moves (the host tells AT-SPI): the node's key. */
        std::function<void(const std::string &key)> onFocusMoved;
        void render(artboard::IRenderTarget &target, double nowMs);
        bool needsRedraw(double nowMs) const;

        void pointer(int kind, double x, double y, int button, double timeMs, bool alt = false, bool shift = false, bool ctrl = false);
        /** The wheel: `notches` up (+) or down (−); Shift scrolls sideways — and a sideways scroll (a tilted wheel,
         *  a touchpad) arrives as Shift (D-5). */
        void wheel(double x, double y, double notches, bool ctrl = false, bool shift = false, bool alt = false);
        bool key(const artboard::KeyEvent &e);
        /** R-UI-14: what the pointer should look like where it is now — the host shows it after every pointer event. */
        Cursor cursor() const;

        /** Send one command line. A refusal becomes the toast, with the service's sentence. */
        bool dispatch(const std::string &line);

        std::function<void()> onPickSongToOpen, onPickSongToCreate;
        std::function<void(std::function<void(const std::string &)> done)> onPickFolder;
        /** A file to write: Save As, Render, Render Stems (the host's save dialog; `suggested` names it). */
        std::function<void(const std::string &title, const std::string &suggested, std::function<void(const std::string &)> done)> onPickSave;
        /** R-COLLECT-1: a file to read (Relink's Locate…) and a folder to search (Search a Folder…) — the host's dialogs. */
        std::function<void(const std::string &title, std::function<void(const std::string &)> done)> onPickFile, onPickDirectory;
        /** The HOST's clipboard (R-UI-11; the core has none, R-SVC-4): Copy Address / Value / as Formula / ID put
         *  their text here, and the app says so ("Copied dv_1.filter.cutoff"). */
        std::function<void(const std::string &text)> onCopy;
        /** R-VIDEO-1: the HOST's decoder (the core carries no codec, R-SVC-4) — the frame showing `t` seconds into `file`,
         *  fitted to boxW × boxH. Without it the Video window says where the song is and shows no frame. */
        VideoFrameSource onVideoFrame;
        /** … and the lane's thumbnails: a decoder of their own, so their seeks never cost the window's playing frames
         *  (without it, `onVideoFrame`'s). */
        VideoFrameSource onVideoThumb;
        /** R-ACCESS-2: the OS's language ("vi_VN.UTF-8"; the host reads it once) — what `language=auto` draws in when
         *  Solaris has it. */
        void setOsLanguage(const std::string &os) { mOsLanguage = os; }
        /** R-ACCESS-2: a switch of language dissolves the whole window — the old language painted, the new one over it at
         *  this amount (180 ms, linear: a dissolve is a linear alpha ramp). */
        static constexpr double kLanguageDissolveMs = 180.0;
        const std::string &drawnLanguage() const { return mLangTo; }   // the language being drawn (the new one, mid-way)
        const std::string &fadingLanguage() const { return mLangFrom; } // … and the one it dissolves from
        double languageAmount() const { return mLangFade.value(); }    // LIVE: 0 the old language … 1 the new alone
        /** The OS's own "reduce motion" (the host reads it once); the app ORs it with its setting (R-SET-3). */
        void setOsReducedMotion(bool on) { mOsReducedMotion = on; artboard::setReducedMotion(on || mAppReducedMotion); }
        /** R-RENDER-4: a `render …` line from the window. It is NOT sent now: a "Rendering…" modal eases in first, and
         *  the line goes out at the start of the frame AFTER one that painted the modal whole — the service runs a
         *  render to its end before it answers, so that frame is what stands on screen meanwhile. `file` is named on it. */
        void beginRender(const std::string &line, const std::string &file);
        /** The service's `render.finished`, as the host heard it (the host subscribes; the app links no service):
         *  said in an info toast — the file and the master's peak. */
        void renderFinished(const std::string &out, const std::string &peakDb);
        bool renderPending() const { return !mRenderLine.empty(); }  // asked for, not yet sent
        double renderAmount() const { return mRenderAmt.value(); }   // LIVE: the modal's fade
        artboard::Rect renderCardRect() const;                       // its card (logical units)
        /** R-FAM-6: the card's progress bar — the family's 3 px accent bar. A render has no fraction to show, so a
         *  segment (kRenderSegment of the track) SWEEPS along it, eased in and out, entering and leaving through the
         *  track's ends so the loop has no seam; still, at the middle, under reduced motion. `renderSweep` is its LIVE
         *  place, 0 (about to enter) … 1 (gone out the far end). */
        static constexpr double kRenderBarH = 3.0, kRenderSegment = 0.3, kRenderSweepMs = 1400.0;
        double renderSweep() const;
        artboard::Rect renderBarRect() const;     // the track
        artboard::Rect renderSegmentRect() const; // the accent's segment, as drawn now (clipped to the track)
        void openSongPicked(const std::string &path);
        void newSongPicked(const std::string &path);
        /** R-PRESET-2: a menu of the song templates at `world`; one picked, `onPickSongToCreate` asks where. */
        void pickTemplate(artboard::Point world);
        void openSettings();
        /** R-HIST-3: Edit ▸ Show History Tree… — cosmo's history view over the model's tree. */
        void openHistory();
        cosmo_v2::HistoryView &historyView() { return *mHistory; }
        /** Home, behind cosmo's ConfirmDialog when the song has unsaved changes (R-FAM-3: the family's question). */
        void requestHome();
        /** R-FAM-2: the window's close button. With no unsaved edits `onQuit` runs at once; with some, the same dialog
         *  as Home's is raised and `onQuit` runs only on Save (after the save landed) or Don't save. */
        void requestQuit();
        std::function<void()> onQuit; // the host closes its window
        /** R-FAM-8: what the window's title bar says — `*<song> — Solaris` unsaved, `<song> — Solaris` saved, `Solaris`
         *  on Home. The host shows it when it changes. */
        std::string windowTitle() const;

        // presentation, for the host and the tests
        const std::string &screen() const { return mScreen; }
        double screenOpacity(const std::string &screen) const;
        // R-FAM-4: the toast is cosmo's `Toast`; these forward what it publishes
        std::string toastText() const { return mToast->text(); }           // in the language drawn now
        bool toastIsRefusal() const { return mToast->kind() == ToastKind::Error; }
        ToastKind toastKind() const { return mToast->kind(); }             // R-UI-15
        double toastAmount() const { return mToast->amount(); }            // LIVE: its fade (and its slide into place)
        double toastSwapAmount() const { return mToast->swapAmount(); }    // LIVE: 0 the toast it replaced … 1 this one alone
        artboard::Rect toastRect() const;                                  // as last DRAWN (logical units, the window's): under the song bar
        static constexpr double kToastBarW = cosmo_v2::Toast::kBarW;       // a warning's and an error's bar, at its left
        static constexpr double kToastTop = SongBar::kHeight + 4.0;        // where it rests: under the song bar, in the ruler's band
        static double minWidth() { return 960.0; }
        static double minHeight() { return 600.0; }

        HomeScreen &home() { return *mHome; }
        ProjectScreen &project() { return *mProject; }
        SettingsSheet &settings() { return *mSettings; }
        ExportSheet &exportSheet() { return *mExport; } // R-EXPORT-1: File › Export…
        RelinkSheet &relinkSheet() { return *mRelink; } // R-COLLECT-1: File › Relink…
        ConflictsSheet &conflictsSheet() { return *mConflicts; } // R-COLLAB-1: what a sync asked
        cosmo_v2::ConfirmDialog &confirm() { return *mConfirm; }
        cosmo_v2::ContextMenu &menu() { return *mMenu; }
        artboard::Segment *activeRoot();

    private:
        void bindIfStale();
        void buildMenus();
        void refreshMenus(const solaris::AppModel &m);
        std::string selectedClip() const;
        void layoutAll();
        /** Words to say (`say` — Words.h — or the service's line, as data). */
        void showToast(const std::string &text, ToastKind kind = ToastKind::Error);
        /** A menu at `world` — its labels kept as words, shown in each pass's language (R-ACCESS-2). */
        void openMenu(std::vector<cosmo_v2::ContextMenu::Item> items, artboard::Point world);
        /** cosmo's dialog, its words kept: the title and the message composed for the language drawn, each pass. */
        void showConfirm(std::function<std::string()> title, std::function<std::string()> message, std::vector<cosmo_v2::ConfirmDialog::Button> buttons);
        /** A song-bar menu's words (title, labels as `say` words or data): kept, and shown in the language drawn. */
        void setMenuWords(int menu, const std::string &title, std::vector<cosmo_v2::MenuStrip::Item> items);
        /** R-ACCESS-2: every stored word shown in the language drawn now — once a pass, when the language or the words changed. */
        void relabel();
        /** One paint of the whole window in a language (twice a frame while a switch dissolves). */
        void paintPass(artboard::IRenderTarget &target, const artboard::Transform &root, const std::string &language);
        void advanceLanguage(const solaris::AppModel &m, double nowMs);
        void copy(const std::string &text);
        static std::string quote(const std::string &s);
        static const solaris::AppModel &emptyModel();
        /** R-SET-4: the action a combo means on a keyboard ("song" or "roll") in the model's keymap, or "". */
        static std::string keyActionFor(const solaris::AppModel &m, const std::string &combo, const std::string &group);
        static std::string keyHint(const solaris::AppModel &m, const std::string &action); // "Ctrl+Z", or "" — the keymap's first key, as a menu's shortcut column shows it
        void askUnsaved(std::function<void()> then); // R-FAM-3: `then` at once when saved; else on Save (saved) or Don't save
        void syncHistory(const solaris::AppModel &m); // the open tree view follows the model's tree
        void askRecovery(const solaris::AppModel &m); // R-SAFE-2: Recover / Open the saved one
        bool songAction(const solaris::AppModel &m, const std::string &id); // true when it did something
        bool pasteClip(const solaris::AppModel &m);                         // R-CLIP-10

        AppHooks mHooks;
        double mW, mH, mNowMs = 0.0, mLastActivityMs = -1e9;
        artboard::Point mPointer{-1.0, -1.0};            // where the pointer last was (R-UI-14)
        std::shared_ptr<HomeScreen> mHome;
        std::shared_ptr<ProjectScreen> mProject;
        std::shared_ptr<SettingsSheet> mSettings;
        std::shared_ptr<ExportSheet> mExport;
        std::shared_ptr<RelinkSheet> mRelink;
        double mLoopFrom = 0.0, mLoopTo = 0.0; // R-TIME-4 amended: the song's loop as the window last saw it (what Loop turns back on)
        std::string mLoopSong;                 // … and the song it was in
        std::string mMissingSaidFor; // R-COLLECT-1: the song whose missing files a toast has said
        std::shared_ptr<ConflictsSheet> mConflicts; // R-COLLAB-1
        double mPhysW = 1.0, mPhysH = 1.0;          // R-ACCESS-1: the window, in device pixels
        std::string mFocusKey;                      // R-ACCESS-1: the keyboard's focus
        bool mFocusOn = false;
        artboard::AnimatedProperty mRingX{0.0}, mRingY{0.0}, mRingW{0.0}, mRingH{0.0}, mRingA{0.0};
        artboard::Rect mRingTarget{0.0, 0.0, 0.0, 0.0};
        double mRingWantA = 0.0, mKeyClickClock = 0.0;
        bool mRingPlaced = false;
        bool focusKeyPress(const artboard::KeyEvent &e);
        void advanceFocus(double nowMs);
        void clickAt(artboard::Point p);
        artboard::AnimatedProperty mScale{1.0}, mContrast{0.0};
        int mScaleWant = -1, mContrastWant = -1;
        void applyLogicalSize();
        artboard::Transform rootTransform() const
        {
            return artboard::Transform::translation(mOx, mOy).mul(artboard::Transform::scaling(mScale.value(), mScale.value()));
        }
        double mOx = 0.0, mOy = 0.0; // R-HOST-2: the frame's top-left, device pixels
        int mSyncSeen = 0;                          // the syncs a toast has said
        std::shared_ptr<cosmo_v2::ConfirmDialog> mConfirm;
        std::shared_ptr<cosmo_v2::ContextMenu> mMenu;  // cosmo's: a route, an effect, rename — modal while open
        std::shared_ptr<artboard::Segment> mModalRoot; // hosts cosmo's dialog: its advance is the tree's
        std::shared_ptr<cosmo_v2::HistoryView> mHistory; // R-HIST-3: cosmo's history tree, modal while open
        std::vector<int> mHistoryIds;                    // the view's node i is the model's history id mHistoryIds[i]
        std::vector<std::string> mHistoryLabels;
        // R-CLIP-10/11: the clips Ctrl+C copied (the window's clipboard), and where the last paste went — a paste with the
        // time bar unmoved lands right after it, so pastes run end to end
        std::vector<std::string> mClipClipboard;
        std::string mNewTemplate; // R-PRESET-2: the template the next new song starts from ('' none)
        double mPasteAt = -1.0, mPasteNext = 0.0;
        artboard::GestureRecognizer mRecognizer;
        artboard::GestureRecognizer mKeyClicks;     // R-ACCESS-1: the keyboard's clicks — their own, so they never pair
        std::string mScreen = "home";
        bool mScreenInit = false;
        bool mRollKeys = false; // R-ROLL-8: the pointer last pressed in the front piano roll — the keys go to it
        long long mBoundRevision = -1;
        bool mBoundAudition = false;
        std::shared_ptr<cosmo_v2::Toast> mToast; // R-FAM-4: the family's toast, placed under the song bar
        bool mOsReducedMotion = false, mAppReducedMotion = false;
        std::string mMenuKey;                // what the dynamic menu labels were built from
        // R-ACCESS-2: the language drawn, and the words stored in cosmo's widgets kept as words
        std::string mOsLanguage, mLangFrom, mLangTo;
        artboard::AnimatedProperty mLangFade{1.0};
        std::vector<std::pair<std::string, std::vector<std::string>>> mMenuWords; // each song-bar menu: its title, its labels
        std::vector<std::string> mContextWords;                                  // the open menu's labels
        std::function<std::string()> mConfirmTitle, mConfirmMessage;
        std::vector<std::string> mConfirmWords;                                  // its buttons' labels
        std::string mHistoryHint;                                                // the history view's line
        long mWordsGen = 0, mRelabelGen = -1;
        std::string mRelabelLang;
        std::string mRecoveryAsked;          // R-SAFE-2: the song whose autosave the window has asked about
        // R-RENDER-4: the line waiting to be sent, the file it names, the modal's fade; a frame painted it whole; the
        // service has answered and the modal closes on the next frame's clock (the one a render blocked is stale)
        std::string mRenderLine, mRenderFile;
        artboard::AnimatedProperty mRenderAmt{0.0};
        bool mRenderOpening = false, mRenderPainted = false, mRenderCloseWanted = false;
        double mRenderT0 = 0.0; // when the modal began to open: the sweep's clock (R-FAM-6)
        void paintRender(artboard::IRenderTarget &target) const;
    };
}
}
