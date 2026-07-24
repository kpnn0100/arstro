/*
 *  arstro-android-shell — SurfaceHost (M1.2).
 *
 *  Wraps ONE GTK top-level (a gtk-layer-shell surface where available, else a plain
 *  window under --windowed) + its Artboard CairoTarget + one Artboard root segment,
 *  and renders the root on the GTK draw signal — mirroring the cosmo GTK->Cairo glue
 *  (cosmo/linux_main.cpp). The multi-surface orchestration, the shared frame clock,
 *  GDK->RawPointer input, and ShellState all arrive in M1.3–M1.6; M1.2 is just this
 *  single-surface draw path, plus a headless paint() the golden-image tests reuse.
 *
 *  See launcher/docs/android-theme-plan.md §3.1 for the surface model.
 */
#pragma once
#include <gtk/gtk.h>
#include <cairo/cairo.h>
#include "adapter/native/CairoTarget.h"
#include "artboard/artboard.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace androidshell
{
    struct ShellState;  // fwd — a surface reads shared state through it (bound per-surface at M3+)

    // Which compositor layer a surface lives on + how it is anchored/sized. Consumed only
    // when gtk-layer-shell is present; under plain --windowed it degrades to a normal
    // top-level of width x height (0 on an axis = "stretch / let the WM decide").
    struct SurfaceConfig
    {
        enum class Layer { Background, Top, Overlay };
        std::string name = "surface";
        Layer layer = Layer::Top;
        bool anchorLeft = false, anchorRight = false, anchorTop = false, anchorBottom = false;
        int width = 0, height = 0;            // px; 0 = stretch along an anchored axis / WM default
        int exclusiveZone = 0;                // reserve this many px so maximized apps avoid us
        artboard::Color background{0, 0, 0, 1};  // placeholder fill until real content (M2/M1.6)
    };

    // The M1 surface set (plan §3.1). Defined once here so --surface=N and the M1.6
    // multi-surface creation share one table. Placeholder background colours make each
    // surface visually distinct until the theme + real content land (M2+).
    const std::vector<SurfaceConfig> &defaultSurfaces();

    class SurfaceHost
    {
    public:
        explicit SurfaceHost(SurfaceConfig config);

        // Non-copyable / non-movable: the recognizer's sink (set in the ctor) captures `this`,
        // so the object must stay at a fixed address for its whole life. Hold them behind
        // unique_ptr (or on the stack) — never in a value container that could relocate them.
        SurfaceHost(const SurfaceHost &) = delete;
        SurfaceHost &operator=(const SurfaceHost &) = delete;

        // Build the GTK window: a layer-shell surface when gtk-layer-shell is compiled in
        // and !forceWindowed, otherwise a plain top-level window.
        void create(bool forceWindowed);
        void setRoot(std::shared_ptr<artboard::Segment> root) { mRoot = std::move(root); }
        // The shared cross-surface state (M1.5). Stored for surfaces to read as they are built
        // (M3+); the M1.6 placeholder roots do not consume it yet.
        void setShellState(ShellState *state) { mShellState = state; }
        ShellState *shellState() const { return mShellState; }
        void show();
        GtkWidget *window() const { return mWindow; }

        // Pure draw path (no GTK): fill the surface background, size the root to w x h, then
        // render it (and its overlay pass). Used by the GTK draw signal AND by headless
        // golden-image rendering, so both produce identical pixels. Clears the dirty flag and
        // bumps the paint counter.
        void paint(cairo_t *cr, int w, int h);

        // ---- frame clock (M1.3) ----
        // Called once per frame by the shared FrameClock. Advances this surface's Artboard root
        // and its GestureRecognizer to `nowMs`, then queues a GTK redraw ONLY if the surface is
        // dirty (an external invalidation via markDirty(), or the animating predicate reporting
        // it is mid-animation). A surface with static content therefore does zero redraws once it
        // has painted its first frame. Returns whether the surface was dirty (for headless tests).
        bool frameTick(double nowMs);

        // Force a one-shot redraw next frame (resize, and — from M1.4 — input). Also queues an
        // immediate GTK draw when a window exists.
        void markDirty();

        // Content opts into continuous redraw by supplying a predicate that returns true while it
        // is animating (so the clock keeps redrawing until it settles). Empty (default) = static
        // content = idle after the first paint. A future Artboard `Segment::isAnimating()`
        // aggregate could make this automatic; until then content declares it (see ledger).
        void setAnimatingQuery(std::function<bool(double nowMs)> q) { mAnimating = std::move(q); }

        artboard::GestureRecognizer &recognizer() { return mRecognizer; }
        long paintCount() const { return mPaintCount; }
        bool dirty() const { return mDirty; }

        // Feed one low-level pointer sample into this surface's recognizer (M1.4). The GDK
        // button/motion handlers translate into this; headless tests call it directly. The
        // recognizer's synthesized gestures are routed to the root (see the sink in the ctor).
        void feedPointer(const artboard::RawPointer &rp) { mRecognizer.feed(rp); }

    private:
        static gboolean onDraw(GtkWidget *area, cairo_t *cr, gpointer self);
        static gboolean onButton(GtkWidget *area, GdkEventButton *e, gpointer self);
        static gboolean onMotion(GtkWidget *area, GdkEventMotion *e, gpointer self);

        SurfaceConfig mConfig;
        GtkWidget *mWindow = nullptr;
        GtkWidget *mArea = nullptr;
        artboard::CairoTarget mTarget;
        std::shared_ptr<artboard::Segment> mRoot;
        artboard::GestureRecognizer mRecognizer;        // per-surface; sink wired in M1.4
        std::function<bool(double)> mAnimating;         // content's "am I animating?" predicate
        bool mDirty = true;                             // starts true so the first frame paints
        long mPaintCount = 0;
        ShellState *mShellState = nullptr;              // shared state (M1.5); consumed at M3+
    };

} // namespace androidshell
} // namespace arstro
