/*
 *  arstro-android-shell — SurfaceHost implementation (M1.2). See SurfaceHost.h.
 */
#include "SurfaceHost.h"

#ifdef HAVE_GTK_LAYER_SHELL
#include <gtk-layer-shell/gtk-layer-shell.h>
#endif

namespace arstro
{
namespace androidshell
{
    const std::vector<SurfaceConfig> &defaultSurfaces()
    {
        using L = SurfaceConfig::Layer;
        // Colours are placeholders (dark M3-ish surfaces) so each surface is distinguishable
        // before the theme lands. Geometry follows plan §3.1.
        static const std::vector<SurfaceConfig> surfaces = {
            // 0 launcher — full-screen background (wallpaper + home + drawer + overview later)
            {"launcher", L::Background, true, true, true, true, 0, 0, 0,
             artboard::Color::rgba(20, 18, 24)},
            // 1 status bar — anchored top, height 24, reserves 24px so apps sit below it
            {"statusbar", L::Top, true, true, true, false, 0, 24, 24,
             artboard::Color::rgba(33, 31, 38)},
            // 2 notification shade — full-screen overlay (input-transparent except the panel, M4)
            {"shade-notifications", L::Overlay, true, true, true, true, 0, 0, 0,
             artboard::Color::rgba(29, 27, 32, 235)},
            // 3 quick-settings shade — full-screen overlay (M5)
            {"shade-quicksettings", L::Overlay, true, true, true, true, 0, 0, 0,
             artboard::Color::rgba(43, 41, 48, 235)},
            // 4 gesture edge strips — overlay; the real 3 strips (L/R/bottom) split out in M1.6/M7
            {"edge-strips", L::Overlay, true, true, false, true, 0, 0, 0,
             artboard::Color::rgba(0, 0, 0, 40)},
        };
        return surfaces;
    }

    namespace
    {
        artboard::PointerButton mapGdkButton(guint b)
        {
            switch (b)
            {
            case 2:  return artboard::PointerButton::Middle;
            case 3:  return artboard::PointerButton::Right;
            default: return artboard::PointerButton::Left;
            }
        }

        // True when the event's source device is a touchscreen. GDK reports the real source
        // device even on the pointer events it EMULATES from touch, so reading it here gives a
        // correct .touch flag for both mouse and finger without separately handling touch events
        // (which would double-report). v1 is single-pointer (plan: no multi-finger needed).
        bool isTouchSource(GdkEvent *ev)
        {
            GdkDevice *d = gdk_event_get_source_device(ev);
            return d && gdk_device_get_source(d) == GDK_SOURCE_TOUCHSCREEN;
        }
    }

    SurfaceHost::SurfaceHost(SurfaceConfig config) : mConfig(std::move(config))
    {
        // Route every synthesized gesture to the current root and mark the surface dirty so
        // input causes exactly one redraw. Reads mRoot at call time (set later via setRoot).
        mRecognizer.setSink([this](const artboard::Gesture &g) {
            if (mRoot) mRoot->onGesture(g);
            markDirty();
        });
    }

    void SurfaceHost::create(bool forceWindowed)
    {
        mWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(mWindow),
                             ("Arstro Android Shell \xE2\x80\x94 " + mConfig.name).c_str());

#ifdef HAVE_GTK_LAYER_SHELL
        if (!forceWindowed)
        {
            gtk_layer_init_for_window(GTK_WINDOW(mWindow));
            GtkLayerShellLayer layer = GTK_LAYER_SHELL_LAYER_TOP;
            switch (mConfig.layer)
            {
            case SurfaceConfig::Layer::Background: layer = GTK_LAYER_SHELL_LAYER_BACKGROUND; break;
            case SurfaceConfig::Layer::Top:        layer = GTK_LAYER_SHELL_LAYER_TOP;        break;
            case SurfaceConfig::Layer::Overlay:    layer = GTK_LAYER_SHELL_LAYER_OVERLAY;    break;
            }
            gtk_layer_set_layer(GTK_WINDOW(mWindow), layer);
            gtk_layer_set_anchor(GTK_WINDOW(mWindow), GTK_LAYER_SHELL_EDGE_LEFT,   mConfig.anchorLeft);
            gtk_layer_set_anchor(GTK_WINDOW(mWindow), GTK_LAYER_SHELL_EDGE_RIGHT,  mConfig.anchorRight);
            gtk_layer_set_anchor(GTK_WINDOW(mWindow), GTK_LAYER_SHELL_EDGE_TOP,    mConfig.anchorTop);
            gtk_layer_set_anchor(GTK_WINDOW(mWindow), GTK_LAYER_SHELL_EDGE_BOTTOM, mConfig.anchorBottom);
            if (mConfig.exclusiveZone != 0)
                gtk_layer_set_exclusive_zone(GTK_WINDOW(mWindow), mConfig.exclusiveZone);
        }
#else
        (void)forceWindowed;  // no layer-shell compiled in: always a plain window
#endif

        // A concrete default size so a plain window (or a not-yet-mapped layer surface) has
        // sensible bounds; a phone-portrait shape for the launcher, the configured band otherwise.
        const int defW = mConfig.width > 0 ? mConfig.width : 1080;
        const int defH = mConfig.height > 0 ? mConfig.height : 2160;
        gtk_window_set_default_size(GTK_WINDOW(mWindow), defW, defH);

        mArea = gtk_drawing_area_new();
        gtk_widget_set_can_focus(mArea, TRUE);
        // Pointer events only: GTK emulates button/motion for touch too (with the touchscreen
        // as source device), so we do NOT add GDK_TOUCH_MASK and get both from one path (M1.4).
        gtk_widget_add_events(mArea, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                         GDK_POINTER_MOTION_MASK);
        gtk_container_add(GTK_CONTAINER(mWindow), mArea);
        g_signal_connect(mArea, "draw", G_CALLBACK(onDraw), this);
        g_signal_connect(mArea, "button-press-event", G_CALLBACK(onButton), this);
        g_signal_connect(mArea, "button-release-event", G_CALLBACK(onButton), this);
        g_signal_connect(mArea, "motion-notify-event", G_CALLBACK(onMotion), this);
        g_signal_connect(mWindow, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    }

    void SurfaceHost::show() { gtk_widget_show_all(mWindow); }

    gboolean SurfaceHost::onDraw(GtkWidget *area, cairo_t *cr, gpointer self)
    {
        auto *h = static_cast<SurfaceHost *>(self);
        h->paint(cr, gtk_widget_get_allocated_width(area), gtk_widget_get_allocated_height(area));
        return FALSE;
    }

    gboolean SurfaceHost::onButton(GtkWidget *, GdkEventButton *e, gpointer self)
    {
        artboard::RawPointer rp;
        rp.kind = (e->type == GDK_BUTTON_PRESS) ? artboard::RawPointer::Kind::Down
                                                : artboard::RawPointer::Kind::Up;
        rp.pos = {e->x, e->y};
        rp.button = mapGdkButton(e->button);
        rp.timeMs = (double)e->time;
        rp.alt = (e->state & GDK_MOD1_MASK) != 0;
        rp.shift = (e->state & GDK_SHIFT_MASK) != 0;
        rp.ctrl = (e->state & GDK_CONTROL_MASK) != 0;
        rp.touch = isTouchSource(reinterpret_cast<GdkEvent *>(e));
        static_cast<SurfaceHost *>(self)->feedPointer(rp);
        return TRUE;
    }

    gboolean SurfaceHost::onMotion(GtkWidget *, GdkEventMotion *e, gpointer self)
    {
        artboard::RawPointer rp;
        rp.kind = artboard::RawPointer::Kind::Move;
        rp.pos = {e->x, e->y};
        rp.button = artboard::PointerButton::Left;  // motion carries no button; recognizer uses the press's
        rp.timeMs = (double)e->time;
        rp.alt = (e->state & GDK_MOD1_MASK) != 0;
        rp.shift = (e->state & GDK_SHIFT_MASK) != 0;
        rp.ctrl = (e->state & GDK_CONTROL_MASK) != 0;
        rp.touch = isTouchSource(reinterpret_cast<GdkEvent *>(e));
        static_cast<SurfaceHost *>(self)->feedPointer(rp);
        return TRUE;
    }

    void SurfaceHost::paint(cairo_t *cr, int w, int h)
    {
        mTarget.setContext(cr);

        // Placeholder background fill (real content: theme M2, surfaces M1.6+).
        mTarget.save();
        mTarget.setTransform(artboard::Transform::identity());
        artboard::drawRoundedRect(mTarget, artboard::Rect{0, 0, (double)w, (double)h}, 0.0,
                                  artboard::Paint::filled(mConfig.background));
        mTarget.restore();

        if (mRoot)
        {
            mRoot->width.set((double)w);
            mRoot->height.set((double)h);
            mRoot->render(mTarget);
            mRoot->renderOverlay(mTarget);
        }

        mDirty = false;   // this frame is now on screen; go idle unless something re-dirties us
        ++mPaintCount;
    }

    bool SurfaceHost::frameTick(double nowMs)
    {
        // Advance animation + input timing every frame (cheap; no drawing).
        if (mRoot) mRoot->advance(nowMs);
        mRecognizer.advance(nowMs);   // drives the long-press timeout (FR-28); sink wired in M1.4

        // Stay/become dirty while content reports it is animating.
        if (mAnimating && mAnimating(nowMs))
            mDirty = true;

        // Redraw ONLY when dirty — a static surface does zero redraws once painted.
        if (mDirty && mArea)
            gtk_widget_queue_draw(mArea);

        return mDirty;
    }

    void SurfaceHost::markDirty()
    {
        mDirty = true;
        if (mArea)
            gtk_widget_queue_draw(mArea);
    }

} // namespace androidshell
} // namespace arstro
