#include "UiDemoApp.h"
#include "../../Artboard/src/adapter/native/CairoTarget.h"
#include <gtk/gtk.h>

using arstro::examples::UiDemoApp;

namespace
{
    struct LinuxUiDemo
    {
        GtkWidget *window = nullptr;
        GtkWidget *drawingArea = nullptr;
        UiDemoApp app{960.0, 560.0};
        artboard::CairoTarget target;
        gint64 startUs = 0;
    };

    double nowMs(const LinuxUiDemo &state)
    {
        if (state.startUs == 0)
            return 0.0;
        return (g_get_monotonic_time() - state.startUs) / 1000.0;
    }

    int mapButton(guint button)
    {
        switch (button)
        {
        case 1: return 0;
        case 2: return 1;
        case 3: return 2;
        default: return 0;
        }
    }

    gboolean onDraw(GtkWidget *, cairo_t *cr, gpointer userData)
    {
        auto *state = static_cast<LinuxUiDemo *>(userData);
        state->target.setContext(cr);
        state->app.render(state->target, nowMs(*state));
        return FALSE;
    }

    gboolean onTick(gpointer userData)
    {
        auto *state = static_cast<LinuxUiDemo *>(userData);
        gtk_widget_queue_draw(state->drawingArea);
        return G_SOURCE_CONTINUE;
    }

    gboolean onButtonPress(GtkWidget *widget, GdkEventButton *event, gpointer userData)
    {
        auto *state = static_cast<LinuxUiDemo *>(userData);
        gtk_widget_grab_focus(widget);
        state->app.pointer(0, event->x, event->y, mapButton(event->button), nowMs(*state));
        gtk_widget_queue_draw(widget);
        return TRUE;
    }

    gboolean onButtonRelease(GtkWidget *widget, GdkEventButton *event, gpointer userData)
    {
        auto *state = static_cast<LinuxUiDemo *>(userData);
        state->app.pointer(2, event->x, event->y, mapButton(event->button), nowMs(*state));
        gtk_widget_queue_draw(widget);
        return TRUE;
    }

    gboolean onMotion(GtkWidget *widget, GdkEventMotion *event, gpointer userData)
    {
        auto *state = static_cast<LinuxUiDemo *>(userData);
        state->app.pointer(1, event->x, event->y, 0, nowMs(*state));
        gtk_widget_queue_draw(widget);
        return TRUE;
    }

    gboolean onKeyPress(GtkWidget *widget, GdkEventKey *event, gpointer userData)
    {
        auto *state = static_cast<LinuxUiDemo *>(userData);
        const bool shift = (event->state & GDK_SHIFT_MASK) != 0;
        const bool ctrl = (event->state & GDK_CONTROL_MASK) != 0;
        const bool alt = (event->state & GDK_MOD1_MASK) != 0;
        state->app.keyDown(static_cast<int>(event->keyval), shift, ctrl, alt);

        const gunichar unicode = gdk_keyval_to_unicode(event->keyval);
        if (!ctrl && !alt && unicode >= 0x20 && unicode != 0x7f)
        {
            char buffer[8] = {0};
            const int written = g_unichar_to_utf8(unicode, buffer);
            state->app.textInput(std::string(buffer, static_cast<size_t>(written)));
        }

        gtk_widget_queue_draw(widget);
        return TRUE;
    }

    gboolean onKeyRelease(GtkWidget *widget, GdkEventKey *event, gpointer userData)
    {
        auto *state = static_cast<LinuxUiDemo *>(userData);
        const bool shift = (event->state & GDK_SHIFT_MASK) != 0;
        const bool ctrl = (event->state & GDK_CONTROL_MASK) != 0;
        const bool alt = (event->state & GDK_MOD1_MASK) != 0;
        state->app.keyUp(static_cast<int>(event->keyval), shift, ctrl, alt);
        gtk_widget_queue_draw(widget);
        return TRUE;
    }
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);

    LinuxUiDemo state;
    state.startUs = g_get_monotonic_time();

    state.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(state.window), "Artboard UI Demo");
    gtk_window_set_default_size(GTK_WINDOW(state.window), 960, 560);
    gtk_window_set_resizable(GTK_WINDOW(state.window), FALSE);

    state.drawingArea = gtk_drawing_area_new();
    gtk_widget_set_size_request(state.drawingArea, 960, 560);
    gtk_widget_set_can_focus(state.drawingArea, TRUE);
    gtk_widget_add_events(state.drawingArea, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK);

    g_signal_connect(state.window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    g_signal_connect(state.drawingArea, "draw", G_CALLBACK(onDraw), &state);
    g_signal_connect(state.drawingArea, "button-press-event", G_CALLBACK(onButtonPress), &state);
    g_signal_connect(state.drawingArea, "button-release-event", G_CALLBACK(onButtonRelease), &state);
    g_signal_connect(state.drawingArea, "motion-notify-event", G_CALLBACK(onMotion), &state);
    g_signal_connect(state.window, "key-press-event", G_CALLBACK(onKeyPress), &state);
    g_signal_connect(state.window, "key-release-event", G_CALLBACK(onKeyRelease), &state);

    gtk_container_add(GTK_CONTAINER(state.window), state.drawingArea);
    gtk_widget_show_all(state.window);
    gtk_widget_grab_focus(state.drawingArea);
    g_timeout_add(16, onTick, &state);
    gtk_main();
    return 0;
}