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
#include <memory>
#include <string>

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
        void setSize(double width, double height);

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
