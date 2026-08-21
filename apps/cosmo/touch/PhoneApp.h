/*
 *  cosmo — phone (touch) app shell.
 *
 *  The touch counterpart of cosmo_v2::App: a single-column, ~6-inch phone UI built
 *  from Artboard Segments over the SAME service the desktop shell binds to — one core,
 *  one view-model, two views (R-TOUCH-1). It owns no session and no engine: every change
 *  it makes leaves as a `Command` dispatched to `CosmoService`, and every preview frame
 *  arrives through `CosmoService::takeFrame`, so a project is identical on desktop and
 *  mobile and both shells are drivable by the same scripts and the same `cosmo-cc`.
 *
 *  Reads still go through `svc.session()`, the transitional accessor the desktop App also
 *  uses until S4 moves ownership in — every one of them is a line S4 deletes. What is NOT
 *  transitional is the write path: nothing here mutates an `EditParams`.
 *
 *  The Android host (android_main.cpp) owns the platform seam — it constructs the
 *  ThreadBudget + CosmoService, installs the decoder factory, pumps the service once per
 *  frame and calls render/pointer/setSize — mirroring what linux_main.cpp does for the
 *  desktop App.
 */
#pragma once
#include "artboard/artboard.h"
#include "core/service/CosmoService.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_touch
{
    class HomeScreen;
    class LoadingScreen;
    class EditorScreen;
    class FileBrowser;

    enum class Screen { Home, Loading, Editor };

    class PhoneApp
    {
    public:
        PhoneApp(cosmo::CosmoService &svc, double width, double height);
        ~PhoneApp();

        void render(artboard::IRenderTarget &t, double nowMs);
        void pointer(int kind, double x, double y, int button, double timeMs,
                     bool alt = false, bool shift = false, bool ctrl = false);
        void wheel(double x, double y, double delta, bool ctrl);
        void longPress(double x, double y);   // synthesized by the host; -> multi-select toggle
        void setSize(double width, double height);
        /** Where this shell sits inside the surface the host draws into (R-TOUCH-6). A desktop
         *  host that letterboxes the phone layout into a wide window sets this; a phone host
         *  leaves it at 0,0. Input arrives in SHELL coordinates either way — the host subtracts
         *  the same offset — so this is a drawing concern only, and it has to be the SHELL's
         *  because the tree sets the transform absolutely: a translate applied by the caller is
         *  wiped on the first node (which is what a shot of the desktop window showed). */
        void setOrigin(double x, double y) { mOriginX = x; mOriginY = y; }

        // text entry (host feeds soft-keyboard events; app requests show/hide via onKeyboard)
        void charInput(unsigned int codepoint);
        void backspace();
        std::function<void(bool)> onKeyboard;   // set by the host: show(true)/hide(false) the IME

        // Build the project from pixels the host already has (the Android sample, a harness):
        // the raw-pixel seam, because no Command carries pixels. `finishProject` selects the
        // first image and asks the service for the editor screen — it does NOT decide the screen
        // itself, since the screen is the model's (R-TOUCH-1).
        void addProjectImage(const uint8_t *rgba, int w, int h, const std::string &name);
        void finishProject(const std::string &projectName);

        // ── host seams for the things only a host can do (R-SVC-7) ──
        // A phone host may leave these unset, in which case the shell falls back to its own
        // built-in file browser. A desktop host wires them to its native dialogs, exactly as the
        // desktop shell does — one project-opening story, whichever shell is drawing.
        std::function<void()> onNewProjectRequested;
        std::function<void()> onOpenRequested;
        std::function<void()> onImportRequested;

        bool gpuAvailable() const;
        /** Which screen this shell is showing. Read-back for the harness (and for a host that
         *  wants to know), the same way the desktop App exposes its bound revision. */
        Screen screen() const { return mScreen; }

    private:
        void poll();
        /** Adopt the service's state: the screen, the project, the recents, the controls. Called
         *  whenever `AppModel::revision` moves, and once at construction — which is what makes a
         *  shell built over an already-open project show THAT project (R-TOUCH-1). */
        void syncFromModel();
        /** The only way out (R-TOUCH-1). False when the service rejected it — `model().lastError`
         *  says why, and a caller that cares can read it. */
        bool emit(const cosmo::Command &c);
        /** The transitional READ accessor, and the raw-pixel host seam. S4 deletes these. */
        cosmo::EditSession &sess() { return mSvc.session(); }
        void setScreen(Screen s, double nowMs);
        artboard::Segment *activeRoot() const;
        void loadImagesAsProject(const std::vector<std::string> &paths, const std::string &name);  // from the file browser

        cosmo::CosmoService &mSvc;      // borrowed: the host owns it (R-TOUCH-1)
        artboard::GestureRecognizer mRecognizer;
        std::shared_ptr<HomeScreen> mHome;
        std::shared_ptr<LoadingScreen> mLoading;
        std::shared_ptr<EditorScreen> mEditor;
        std::shared_ptr<FileBrowser> mBrowser;
        Screen mScreen = Screen::Home;
        artboard::Property mFade{0.0};   // cross-fade scrim on screen change
        double mW, mH, mNowMs = 0.0;
        double mOriginX = 0.0, mOriginY = 0.0;
        unsigned mBoundRevision = 0;   // the AppModel revision this view is currently showing
        int mImageCount = 0;
    };
}
}
