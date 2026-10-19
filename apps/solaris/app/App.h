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
#include <functional>
#include <memory>
#include <string>

namespace arstro
{
namespace solaris_ui
{
    class App
    {
    public:
        App(AppHooks hooks, double width, double height);

        /** The window's PHYSICAL size. Divided by the drawn UI scale into the LOGICAL size every widget lays out in
         *  (R-ACCESS-1, cosmo R-SCALE-2), so nothing below this call knows a scale exists. */
        void setSize(double width, double height);
        /** R-ACCESS-1: the shell drawn at the model's `settings.uiScale`, EASED — every frame the logical size is the
         *  window's divided by the drawn scale and every pointer is divided by it, so the shell zooms, never a cut then
         *  a reflow (cosmo's kScaleAnimMs). High contrast likewise: one eased amount on cosmo's tokens. */
        static constexpr double kScaleAnimMs = 260.0;
        double drawnUiScale() const { return mScale.value(); }        // LIVE
        double contrastAmount() const { return mContrast.value(); }   // LIVE: 0 normal … 1 high
        double logicalWidth() const { return mW; }
        double logicalHeight() const { return mH; }

        /** R-ACCESS-1: every control the window shows as an accessible node, in reading order — the song bar (its menus'
         *  titles after Settings), the browser, the lanes' headers, the dock, the open windows; a modal's — a menu open,
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
        void wheel(double x, double y, double notches, bool ctrl = false, bool shift = false);
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
        void openSongPicked(const std::string &path);
        void newSongPicked(const std::string &path);
        /** R-PRESET-2: a menu of the song templates at `world`; one picked, `onPickSongToCreate` asks where. */
        void pickTemplate(artboard::Point world);
        void openSettings();
        /** R-HIST-3: Edit ▸ Show History Tree… — cosmo's history view over the model's tree. */
        void openHistory();
        cosmo_v2::HistoryView &historyView() { return *mHistory; }
        /** Home, behind cosmo's ConfirmDialog when the song has unsaved changes. */
        void requestHome();

        // presentation, for the host and the tests
        const std::string &screen() const { return mScreen; }
        double screenOpacity(const std::string &screen) const;
        std::string toastText() const { return shown(mToastText); } // in the language drawn now
        bool toastIsRefusal() const { return mToastRefusal; }
        double toastAmount() const { return mToast.value(); }
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
        void showToast(const std::string &text, bool refusal = true);
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
        static std::string keyHint(const solaris::AppModel &m, const std::string &action); // "   (Ctrl+Z)", or "" — the keymap's first key
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
        artboard::Transform rootTransform() const { return artboard::Transform::scaling(mScale.value(), mScale.value()); }
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
        std::string mToastText;
        bool mToastRefusal = true;           // a refusal is outlined in the destructive red; a notice in the accent
        artboard::AnimatedProperty mToast{0.0};
        bool mToastWanted = false;
        double mToastShownAt = -1e9;
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
    };
}
}
