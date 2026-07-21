/*
 *  cosmo — phone (touch) app shell.
 *
 *  The touch counterpart of cosmo_v2::App: a single-column, ~6-inch phone UI built
 *  from Artboard Segments, driving the SAME UI-free core (arstro::cosmo::EditSession)
 *  and polling its RenderService for preview pixels — so edits run through the exact
 *  desktop engine, GPU-accelerated on Android (GLES 3.1 compute).
 *
 *  Namespace arstro::cosmo_touch (distinct from the desktop cosmo_v2 widgets). The
 *  Android host (android_main.cpp) owns the platform seam and calls render/pointer/
 *  wheel/setSize/openImage, mirroring what linux_main.cpp does for the desktop App.
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
    class EditorScreen;  // internal (PhoneApp.cpp)

    class PhoneApp
    {
    public:
        PhoneApp(double width, double height);
        ~PhoneApp();

        // Platform entry surface (mirrors cosmo_v2::App).
        void render(artboard::IRenderTarget &t, double nowMs);
        void pointer(int kind, double x, double y, int button, double timeMs,
                     bool alt = false, bool shift = false, bool ctrl = false);
        void wheel(double x, double y, double delta, bool ctrl);
        void setSize(double width, double height);

        // Load a decoded image (straight RGBA8) into the session and show the editor.
        int openImage(const uint8_t *rgba, int w, int h, const std::string &name);

        // True if a GPU compute backend (GLES 3.1) is available for the edit pipeline.
        bool gpuAvailable() const;

    private:
        void poll();  // move the latest completed preview into the photo canvas

        cosmo::EditSession mSession;
        artboard::GestureRecognizer mRecognizer;
        std::shared_ptr<EditorScreen> mEditor;
        std::shared_ptr<artboard::Segment> mRoot;
        double mW, mH;
        double mNowMs = 0.0;
    };
}
}
