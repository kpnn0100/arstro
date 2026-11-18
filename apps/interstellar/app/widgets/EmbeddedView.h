/*
 *  interstellar_v1 — EmbeddedView: another Arstro app's window, drawn inside a frame of this one
 *  (R-SONG-7: Solaris's own window on the Song page).
 *
 *  The front end knows nothing of what is behind it — no Solaris type, no service: the HOST makes one
 *  (AppHooks::songView) and binds it to what it draws from and where its lines go, as the host binds
 *  this app's own hooks. The App gives it a frame, its pointer, its wheel and its keys, in DEVICE
 *  pixels relative to the frame, and asks it to paint. So the embedded window is the real one — its
 *  own widgets, gestures, menus and tweens — and Interstellar draws none of it.
 *
 *  Only Artboard's types appear here, so the bridge that implements it (apps/interstellar/song) can
 *  include this file beside another app's headers.
 */
#pragma once
#include "render/RenderTarget.h"
#include "ui/base/InputController.h"
#include <string>

namespace arstro
{
namespace interstellar_v1
{
    class EmbeddedView
    {
    public:
        virtual ~EmbeddedView() = default;
        /** Paint into `target` within the frame at (x, y), w × h DEVICE pixels; `alpha` is the page's fade.
         *  The caller has clipped to the frame; the view sets its own transforms from it. */
        virtual void render(artboard::IRenderTarget &target, double x, double y, double w, double h, double alpha, double nowMs) = 0;
        virtual bool needsRedraw(double nowMs) const = 0;
        /** kind 0 down, 2 up, else move; button 2 = right; x, y device pixels relative to the frame. */
        virtual void pointer(int kind, double x, double y, int button, double timeMs, bool alt, bool shift, bool ctrl) = 0;
        virtual void wheel(double x, double y, double notches, bool ctrl, bool shift, bool alt) = 0;
        /** True when the view took the key. */
        virtual bool key(const artboard::KeyEvent &e) = 0;
        /** The pointer's shape where it last was, as a CSS/X cursor name ("default", "col-resize", …). */
        virtual std::string cursorName() const { return "default"; }
        /** What it shows — a song's name — for the page's header. */
        virtual std::string title() const = 0;
        /** The page is leaving: stop what should not go on unseen (its transport). */
        virtual void hidden() {}
    };
}
}
