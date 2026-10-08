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
#include "Theme.h"
#include "widgets/HomeScreen.h"
#include "widgets/ProjectScreen.h"
#include "widgets/SettingsSheet.h"
#include "../../cosmo/widgets/ConfirmDialog.h"
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

        void setSize(double width, double height);
        void render(artboard::IRenderTarget &target, double nowMs);
        bool needsRedraw(double nowMs) const;

        void pointer(int kind, double x, double y, int button, double timeMs, bool alt = false, bool shift = false, bool ctrl = false);
        void wheel(double x, double y, double notches, bool ctrl = false);
        bool key(const artboard::KeyEvent &e);

        /** Send one command line. A refusal becomes the toast, with the service's sentence. */
        bool dispatch(const std::string &line);

        std::function<void()> onPickSongToOpen, onPickSongToCreate;
        std::function<void(std::function<void(const std::string &)> done)> onPickFolder;
        void openSongPicked(const std::string &path);
        void newSongPicked(const std::string &path);
        void openSettings();
        /** Home, behind cosmo's ConfirmDialog when the song has unsaved changes. */
        void requestHome();

        // presentation, for the host and the tests
        const std::string &screen() const { return mScreen; }
        double screenOpacity(const std::string &screen) const;
        const std::string &toastText() const { return mToastText; }
        double toastAmount() const { return mToast.value(); }
        static double minWidth() { return 960.0; }
        static double minHeight() { return 600.0; }

        HomeScreen &home() { return *mHome; }
        ProjectScreen &project() { return *mProject; }
        SettingsSheet &settings() { return *mSettings; }
        cosmo_v2::ConfirmDialog &confirm() { return *mConfirm; }
        artboard::Segment *activeRoot();

    private:
        void bindIfStale();
        void layoutAll();
        void showToast(const std::string &text);
        static std::string quote(const std::string &s);
        static const solaris::AppModel &emptyModel();

        AppHooks mHooks;
        double mW, mH, mNowMs = 0.0, mLastActivityMs = -1e9;
        std::shared_ptr<HomeScreen> mHome;
        std::shared_ptr<ProjectScreen> mProject;
        std::shared_ptr<SettingsSheet> mSettings;
        std::shared_ptr<cosmo_v2::ConfirmDialog> mConfirm;
        std::shared_ptr<artboard::Segment> mModalRoot; // hosts cosmo's dialog: its advance is the tree's
        artboard::GestureRecognizer mRecognizer;
        std::string mScreen = "home";
        bool mScreenInit = false;
        long long mBoundRevision = -1;
        std::string mToastText;
        artboard::AnimatedProperty mToast{0.0};
        bool mToastWanted = false;
        double mToastShownAt = -1e9;
    };
}
}
