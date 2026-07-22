/*
 *  cosmo — phone (touch) app shell.
 *
 *  The touch counterpart of cosmo_v2::App: a single-column, ~6-inch phone UI built
 *  from Artboard Segments, driving the SAME UI-free core (arstro::cosmo::EditSession)
 *  and the same project model as the desktop app — so a cosmo project is identical on
 *  desktop and mobile. Home / Loading / Editor screens with animated transitions.
 *
 *  The Android host (android_main.cpp) owns the platform seam and calls render/pointer/
 *  wheel/setSize + addProjectImage, mirroring what linux_main.cpp does for the desktop App.
 */
#pragma once
#include "artboard/artboard.h"
#include "core/EditSession.h"

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

    enum class Screen { Home, Loading, Editor };

    class PhoneApp
    {
    public:
        PhoneApp(double width, double height);
        ~PhoneApp();

        void render(artboard::IRenderTarget &t, double nowMs);
        void pointer(int kind, double x, double y, int button, double timeMs,
                     bool alt = false, bool shift = false, bool ctrl = false);
        void wheel(double x, double y, double delta, bool ctrl);
        void longPress(double x, double y);   // synthesized by the host; -> multi-select toggle
        void setSize(double width, double height);

        // text entry (host feeds soft-keyboard events; app requests show/hide via onKeyboard)
        void charInput(unsigned int codepoint);
        void backspace();
        std::function<void(bool)> onKeyboard;   // set by the host: show(true)/hide(false) the IME

        // Build the project: add each decoded image (straight RGBA8). The first becomes
        // the project root's first image; the rest are added to the same group. Call
        // finishProject() once all are added. The app starts on the Home screen.
        void addProjectImage(const uint8_t *rgba, int w, int h, const std::string &name);
        void finishProject(const std::string &projectName);

        bool gpuAvailable() const;

    private:
        void poll();
        void setScreen(Screen s, double nowMs);
        artboard::Segment *activeRoot() const;
        void buildSession(const std::string &name, bool empty);  // (re)build the EditSession
        void enterProject(const std::string &name, bool empty);  // build + push recent + loading
        void newProject();      // fresh empty project
        void openProject();     // open the existing project
        void importCatalog();   // import the source images as a new catalog
        void openRecent(int index);
        void pushRecent(const std::string &name, int count, bool empty);
        void refreshHome();

        struct SrcImage { std::vector<uint8_t> rgba; int w = 0, h = 0; std::string name; };
        std::vector<SrcImage> mImgs;   // decoded source images (kept so New/Import can rebuild)
        struct Recent { std::string name; int count = 0; bool empty = false; std::vector<uint8_t> thumb; int tw = 0, th = 0; };
        std::vector<Recent> mRecents;  // newest first (DR-HOME-4)

        cosmo::EditSession mSession;
        artboard::GestureRecognizer mRecognizer;
        std::shared_ptr<HomeScreen> mHome;
        std::shared_ptr<LoadingScreen> mLoading;
        std::shared_ptr<EditorScreen> mEditor;
        Screen mScreen = Screen::Home;
        artboard::Property mFade{0.0};   // cross-fade scrim on screen change
        double mW, mH, mNowMs = 0.0;
        int mImageCount = 0;
    };
}
}
