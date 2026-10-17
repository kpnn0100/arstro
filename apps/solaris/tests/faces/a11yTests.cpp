/*
 *  R-ACCESS-1 — the window's controls as AT-SPI reads them (docs/discussion.md Round 20, part 3): the host's bridge built
 *  from a list, read back ONLY through ATK's own calls — what a screen reader's AT-SPI bridge calls — with no display.
 *  Each control a child of the root in the list's order, with its role, its name, its value (the description; AtkValue
 *  for a slider, the minus the UI draws read as one), where it is (the logical bounds times the UI scale; on the screen
 *  through the window's origin), its states, and its actions coming back as the App's own (`onActivate`, `onStep`). A
 *  control that stays keeps its object across lists; one that goes is said gone and is defunct to whoever still holds
 *  it; one that comes is said added; the keyboard's focus is a state and an event.
 */
#ifdef NDEBUG
#undef NDEBUG // a test asserts in every build
#endif
#include "Accessibility.h"
#include <atk/atk.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace arstro::solaris_host;

namespace
{
    int passed = 0;
    void pass(const char *what)
    {
        std::printf("[PASS] %s\n", what);
        ++passed;
    }

    A11yItem item(const std::string &key, const std::string &role, const std::string &name, double x, double y, double w,
                  double h, const std::string &value = std::string())
    {
        A11yItem i;
        i.key = key;
        i.role = role;
        i.name = name;
        i.value = value;
        i.x = x;
        i.y = y;
        i.w = w;
        i.h = h;
        return i;
    }

    std::vector<A11yItem> songList()
    {
        std::vector<A11yItem> v;
        v.push_back(item("bar.play", "button", "Play", 10, 4, 28, 20));
        A11yItem loop = item("bar.loop", "toggle", "Loop", 40, 4, 28, 20);
        loop.checked = true;
        v.push_back(loop);
        A11yItem fader = item("dock.ch_1.fader", "slider", "Drums fader", 100.25, 300, 30, 120, "\xE2\x88\x92" "4.5 dB");
        fader.steps = true;
        v.push_back(fader);
        A11yItem undo = item("bar.undo", "button", "Undo", 70, 4, 28, 20);
        undo.enabled = false;
        v.push_back(undo);
        return v;
    }

    AtkObject *nth(AtkObject *root, int i)
    {
        AtkObject *o = atk_object_ref_accessible_child(root, i);
        if (o) g_object_unref(o); // the bridge keeps its own reference
        return o;
    }

    bool has(AtkObject *o, AtkStateType s)
    {
        AtkStateSet *set = atk_object_ref_state_set(o);
        const bool r = atk_state_set_contains_state(set, s);
        g_object_unref(set);
        return r;
    }

    struct Heard
    {
        std::vector<std::string> lines; // "add 2", "remove 1", "state focused 1", "focus 1"
    };
    void onChildren(AtkObject *, guint index, gpointer, gpointer data, const char *what)
    {
        ((Heard *)data)->lines.push_back(std::string(what) + " " + std::to_string(index));
    }
    void onAdd(AtkObject *o, guint i, gpointer c, gpointer data) { onChildren(o, i, c, data, "add"); }
    void onRemove(AtkObject *o, guint i, gpointer c, gpointer data) { onChildren(o, i, c, data, "remove"); }
    void onState(AtkObject *o, gchar *name, gboolean on, gpointer data)
    {
        ((Heard *)data)->lines.push_back(std::string("state ") + name + " " + (on ? "1" : "0") + " " + atk_object_get_name(o));
    }
    void onFocus(AtkObject *o, gboolean on, gpointer data)
    {
        ((Heard *)data)->lines.push_back(std::string("focus ") + (on ? "1" : "0") + " " + atk_object_get_name(o));
    }
    bool heard(const Heard &h, const std::string &line)
    {
        for (const auto &l : h.lines)
            if (l == line) return true;
        return false;
    }
}

int main()
{
    // ── the list as the window's children ────────────────────────────────────────────────────────────────────────────
    {
        AtkBridge b;
        AtkObject *root = b.root();
        assert(atk_object_get_n_accessible_children(root) == 0);
        b.update(songList(), 1.5);
        assert(b.count() == 4 && atk_object_get_n_accessible_children(root) == 4);
        const char *names[] = {"Play", "Loop", "Drums fader", "Undo"};
        const AtkRole roles[] = {ATK_ROLE_PUSH_BUTTON, ATK_ROLE_TOGGLE_BUTTON, ATK_ROLE_SLIDER, ATK_ROLE_PUSH_BUTTON};
        for (int i = 0; i < 4; ++i)
        {
            AtkObject *o = nth(root, i);
            assert(o && std::strcmp(atk_object_get_name(o), names[i]) == 0);
            assert(atk_object_get_role(o) == roles[i]);
            assert(atk_object_get_parent(o) == root && atk_object_get_index_in_parent(o) == i);
        }
        assert(nth(root, 4) == nullptr && nth(root, -1) == nullptr);
        assert(std::strcmp(atk_object_get_description(nth(root, 2)), "\xE2\x88\x92" "4.5 dB") == 0);
        pass("each control is a child of the root in the list's order — its name, its role, its value said");

        // where: logical × the scale, in the window; on the screen through the window's origin
        gint x, y, w, h;
        atk_component_get_extents(ATK_COMPONENT(nth(root, 2)), &x, &y, &w, &h, ATK_XY_WINDOW);
        assert(x == 150 && y == 450 && w == 45 && h == 180); // 100.25 × 1.5 = 150.375 → 150
        b.setOrigin(200, 50);
        atk_component_get_extents(ATK_COMPONENT(nth(root, 2)), &x, &y, &w, &h, ATK_XY_SCREEN);
        assert(x == 350 && y == 500 && w == 45 && h == 180);
        atk_component_get_extents(ATK_COMPONENT(nth(root, 2)), &x, &y, &w, &h, ATK_XY_WINDOW);
        assert(x == 150 && y == 450);
        b.update(songList(), 2.0); // the UI scale changed
        atk_component_get_extents(ATK_COMPONENT(nth(root, 0)), &x, &y, &w, &h, ATK_XY_WINDOW);
        assert(x == 20 && y == 8 && w == 56 && h == 40);
        pass("where each is: its bounds times the UI scale in the window, and through the window's origin on the screen");

        // the value: the slider's — the U+2212 minus read as one; a button has none to read
        assert(ATK_IS_VALUE(nth(root, 2)) && !ATK_IS_VALUE(nth(root, 0)) && !ATK_IS_VALUE(nth(root, 1)));
        gdouble v = 0;
        gchar *text = nullptr;
        atk_value_get_value_and_text(ATK_VALUE(nth(root, 2)), &v, &text);
        assert(std::fabs(v + 4.5) < 1e-9 && text && std::strcmp(text, "\xE2\x88\x92" "4.5 dB") == 0);
        g_free(text);
        pass("a slider answers its value — the words, and the number they begin with (−4.5); a button is no value");

        // states
        AtkObject *play = nth(root, 0), *loop = nth(root, 1), *undo = nth(root, 3);
        assert(has(play, ATK_STATE_ENABLED) && has(play, ATK_STATE_SENSITIVE) && has(play, ATK_STATE_FOCUSABLE));
        assert(has(play, ATK_STATE_VISIBLE) && has(play, ATK_STATE_SHOWING) && !has(play, ATK_STATE_FOCUSED));
        assert(!has(undo, ATK_STATE_ENABLED) && !has(undo, ATK_STATE_SENSITIVE));
        assert(has(loop, ATK_STATE_CHECKABLE) && has(loop, ATK_STATE_CHECKED) && !has(play, ATK_STATE_CHECKED));
        pass("its states: enabled or not, focusable, a toggle checkable and checked");
    }

    // ── its actions are the App's ────────────────────────────────────────────────────────────────────────────────────
    {
        AtkBridge b;
        std::vector<std::string> sent;
        b.onActivate = [&](const std::string &k) {
            sent.push_back("activate " + k);
            return k != "bar.undo";
        };
        b.onStep = [&](const std::string &k, int dir) {
            sent.push_back("step " + k + " " + std::to_string(dir));
            return true;
        };
        b.update(songList(), 1.0);
        AtkAction *play = ATK_ACTION(b.child("bar.play")), *fader = ATK_ACTION(b.child("dock.ch_1.fader"));
        assert(atk_action_get_n_actions(play) == 1 && std::strcmp(atk_action_get_name(play, 0), "click") == 0);
        assert(atk_action_get_name(play, 1) == nullptr);
        assert(atk_action_get_n_actions(fader) == 3);
        assert(std::strcmp(atk_action_get_name(fader, 1), "increment") == 0 && std::strcmp(atk_action_get_name(fader, 2), "decrement") == 0);
        assert(atk_action_do_action(play, 0));
        assert(atk_action_do_action(fader, 1) && atk_action_do_action(fader, 2));
        assert(!atk_action_do_action(play, 1) && !atk_action_do_action(fader, 3));
        assert(!atk_action_do_action(ATK_ACTION(b.child("bar.undo")), 0)); // the App said no
        assert((sent == std::vector<std::string>{"activate bar.play", "step dock.ch_1.fader 1", "step dock.ch_1.fader -1", "activate bar.undo"}));
        pass("its actions come back as the App's: a click by its key, a value's increment (+1) and decrement (−1)");
    }

    // ── a control that stays keeps its object; one that goes, goes; one that comes, comes ────────────────────────────
    {
        AtkBridge b;
        AtkObject *root = b.root();
        Heard h;
        g_signal_connect(root, "children-changed::add", G_CALLBACK(onAdd), &h);
        g_signal_connect(root, "children-changed::remove", G_CALLBACK(onRemove), &h);
        b.update(songList(), 1.0);
        assert((h.lines == std::vector<std::string>{"add 0", "add 1", "add 2", "add 3"}));
        AtkObject *play = b.child("bar.play"), *fader = b.child("dock.ch_1.fader"), *loop = b.child("bar.loop");
        g_object_ref(loop); // a screen reader holding it
        h.lines.clear();

        auto next = songList();
        next.erase(next.begin() + 1);                       // the loop button gone
        next[0].name = "Pause";                             // playing now
        next[1].value = "\xE2\x88\x92" "3.0 dB";            // the fader moved
        next.push_back(item("bar.record", "toggle", "Record", 100, 4, 28, 20));
        b.update(next, 1.0);
        assert(b.child("bar.play") == play && b.child("dock.ch_1.fader") == fader); // the same objects
        assert(std::strcmp(atk_object_get_name(play), "Pause") == 0);
        assert(std::strcmp(atk_object_get_description(fader), "\xE2\x88\x92" "3.0 dB") == 0);
        assert(b.count() == 4 && b.child("bar.loop") == nullptr && nth(root, 3) == b.child("bar.record"));
        assert(atk_object_get_index_in_parent(fader) == 1);
        assert((h.lines == std::vector<std::string>{"remove 1", "add 3"}));
        // the one let go: defunct to whoever still holds it, its actions nothing
        assert(has(loop, ATK_STATE_DEFUNCT) && atk_object_get_index_in_parent(loop) == -1);
        assert(!atk_action_do_action(ATK_ACTION(loop), 0));
        g_object_unref(loop);
        pass("a control that stays keeps its object (its name and value follow); one gone is said gone and is defunct; one new is said added");

        // two gone at once: each said where it was, the last first — the places still meaning what they say
        h.lines.clear();
        auto fewer = next;
        fewer.erase(fewer.begin() + 3);
        fewer.erase(fewer.begin() + 1);
        b.update(fewer, 1.0);
        assert((h.lines == std::vector<std::string>{"remove 3", "remove 1"}));
        assert(b.count() == 2 && nth(root, 0) == play && nth(root, 1) == b.child("bar.undo"));
        pass("several gone at once are said last first, each where it was");

        // a control that becomes a value is a new object (it answers AtkValue now)
        A11yItem knob = item("bar.play", "slider", "Tempo", 10, 4, 28, 20, "120 BPM");
        b.update({knob}, 1.0);
        assert(b.count() == 1 && b.child("bar.play") != play && ATK_IS_VALUE(b.child("bar.play")));
    }

    // ── the keyboard's focus ─────────────────────────────────────────────────────────────────────────────────────────
    {
        AtkBridge b;
        b.focus("dock.ch_1.fader"); // before the list: it is focused when it arrives
        b.update(songList(), 1.0);
        AtkObject *play = b.child("bar.play"), *fader = b.child("dock.ch_1.fader");
        assert(has(fader, ATK_STATE_FOCUSED) && !has(play, ATK_STATE_FOCUSED));
        Heard h;
        for (int i = 0; i < b.count(); ++i)
        {
            g_signal_connect(nth(b.root(), i), "state-change", G_CALLBACK(onState), &h);
            g_signal_connect(nth(b.root(), i), "focus-event", G_CALLBACK(onFocus), &h);
        }
        b.focus("bar.play");
        assert(has(play, ATK_STATE_FOCUSED) && !has(fader, ATK_STATE_FOCUSED));
        assert(heard(h, "state focused 0 Drums fader") && heard(h, "state focused 1 Play") && heard(h, "focus 1 Play"));
        h.lines.clear();
        b.focus("bar.play"); // no move, nothing said
        assert(h.lines.empty());
        b.focus("");
        assert(!has(play, ATK_STATE_FOCUSED) && heard(h, "state focused 0 Play"));
        // the focus moved onto a control the list did not have yet: said focused when it comes
        Heard added;
        g_signal_connect(b.root(), "children-changed::add", G_CALLBACK(onAdd), &added);
        b.focus("bar.record");
        auto more = songList();
        more.push_back(item("bar.record", "toggle", "Record", 100, 4, 28, 20));
        AtkObject *record = nullptr;
        struct Watch
        {
            Heard *h;
            static void onAdded(AtkObject *, guint, gpointer child, gpointer data)
            {
                g_signal_connect(child, "focus-event", G_CALLBACK(onFocus), ((Watch *)data)->h);
            }
        } watch{&h};
        g_signal_connect(b.root(), "children-changed::add", G_CALLBACK(Watch::onAdded), &watch);
        h.lines.clear();
        b.update(more, 1.0);
        record = b.child("bar.record");
        assert(record && has(record, ATK_STATE_FOCUSED) && heard(added, "add 4") && heard(h, "focus 1 Record"));
        pass("the keyboard's focus is a FOCUSED state and a focus event — said once, when it moves");
    }

    // ── a checked toggle and a shown tab, as they change ─────────────────────────────────────────────────────────────
    {
        AtkBridge b;
        A11yItem tab = item("settings.tab.audio", "tab", "Audio", 0, 0, 60, 24);
        tab.checked = true;
        b.update({tab, item("bar.loop", "toggle", "Loop", 40, 4, 28, 20)}, 1.0);
        AtkObject *t = b.child("settings.tab.audio"), *loop = b.child("bar.loop");
        assert(atk_object_get_role(t) == ATK_ROLE_PAGE_TAB && has(t, ATK_STATE_SELECTED) && !has(t, ATK_STATE_CHECKED));
        Heard h;
        g_signal_connect(loop, "state-change", G_CALLBACK(onState), &h);
        A11yItem on = item("bar.loop", "toggle", "Loop", 40, 4, 28, 20);
        on.checked = true;
        tab.checked = false;
        b.update({tab, on}, 1.0);
        assert(has(loop, ATK_STATE_CHECKED) && !has(t, ATK_STATE_SELECTED) && heard(h, "state checked 1 Loop"));
        pass("a tab shown is SELECTED; a toggle switched on is CHECKED, and said");
    }

    // ── the bridge gone, a held control is defunct, not a crash ──────────────────────────────────────────────────────
    {
        AtkObject *held = nullptr;
        {
            AtkBridge b;
            bool called = false;
            b.onActivate = [&](const std::string &) { return called = true; };
            b.update(songList(), 1.0);
            held = (AtkObject *)g_object_ref(b.child("bar.play"));
        }
        assert(has(held, ATK_STATE_DEFUNCT) && !atk_action_do_action(ATK_ACTION(held), 0));
        g_object_unref(held);
        pass("a control held past the bridge is defunct, and its click does nothing");
    }

    std::printf("solaris_a11y_tests: %d passed, 0 failed\n", passed);
    return 0;
}
