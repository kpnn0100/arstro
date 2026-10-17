/*
 *  solaris_host — the window's controls for assistive technology (R-ACCESS-1; docs/discussion.md Round 20, part 3).
 *
 *  The App lists its controls (its accessible nodes — `App::accessibles`); the host turns the list into ATK objects that
 *  AT-SPI reads: a root (the window's content) whose children are the controls, each with its role, its name, its value
 *  (as its description, and through AtkValue for a slider), where it is (AtkComponent: the window's pixels — the logical
 *  bounds times the UI scale — and the screen's, through the window's origin), its states (enabled, focusable, focused,
 *  checked) and its actions (AtkAction: "click"; a value's "increment" and "decrement") — which come back to the App
 *  (`onActivate`, `onStep`), so a screen reader's click is the keyboard's, which is the pointer's. Updated from a fresh
 *  list (`update`): a control that stays keeps its object (by key); one that goes is removed, one that comes is added,
 *  each said to AT-SPI (children-changed). Plain ATK — no GTK — so a test builds it from a list and reads it back through
 *  ATK's own calls with no display; the window's host hangs the root under its drawing area's accessible.
 */
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

typedef struct _AtkObject AtkObject;

namespace arstro
{
namespace solaris_host
{
    struct A11yItem
    {
        std::string key, role, name, value; // role: button | toggle | slider | combo | tab | menu | menuitem | listitem
        double x = 0, y = 0, w = 0, h = 0;  // in the window, logical units
        bool enabled = true, checked = false, steps = false;
    };

    class AtkBridge
    {
    public:
        AtkBridge();
        ~AtkBridge();
        AtkBridge(const AtkBridge &) = delete;
        AtkBridge &operator=(const AtkBridge &) = delete;

        /** The window's content: its children are the controls. Owned by the bridge. */
        AtkObject *root() const;
        /** The list as it is now; `scale` turns logical units into the window's pixels. */
        void update(const std::vector<A11yItem> &items, double scale);
        /** The keyboard's focus moved there ("" = none): its FOCUSED state, and a focus event. */
        void focus(const std::string &key);
        /** Where the window is on the screen (for ATK_XY_SCREEN). */
        void setOrigin(int x, int y);
        /** The child for a key (null: none) — what a test reads. */
        AtkObject *child(const std::string &key) const;
        int count() const;

        std::function<bool(const std::string &key)> onActivate;           // "click"
        std::function<bool(const std::string &key, int dir)> onStep;       // "increment" (+1), "decrement" (−1)

        struct Impl;

    private:
        std::unique_ptr<Impl> d;
    };
}
}
