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

    SurfaceHost::SurfaceHost(SurfaceConfig config) : mConfig(std::move(config)) {}

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
        gtk_container_add(GTK_CONTAINER(mWindow), mArea);
        g_signal_connect(mArea, "draw", G_CALLBACK(onDraw), this);
        g_signal_connect(mWindow, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    }

    void SurfaceHost::show() { gtk_widget_show_all(mWindow); }

    gboolean SurfaceHost::onDraw(GtkWidget *area, cairo_t *cr, gpointer self)
    {
        auto *h = static_cast<SurfaceHost *>(self);
        h->paint(cr, gtk_widget_get_allocated_width(area), gtk_widget_get_allocated_height(area));
        return FALSE;
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
    }

} // namespace androidshell
} // namespace arstro
