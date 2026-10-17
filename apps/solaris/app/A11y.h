/*
 *  solaris_ui — the accessible list (R-ACCESS-1; docs/discussion.md Round 20, part 2).
 *
 *  Every widget lists its controls as nodes: a stable key, a role, a name, a value, where it is in the window (logical
 *  units), whether it is on, and what it does. Activating a node is the CLICK it stands for — the App feeds a press and a
 *  release at its centre through the same recogniser a pointer uses, so the line sent is the one a click sends and no
 *  widget learns a second way to act — unless the node says otherwise (`activate`: a combo's next choice). A value's
 *  `step` is its keyboard step (a fader ±0.5 dB, a parameter by its registry step), ONE line each. The App gathers the
 *  widgets' nodes in reading order (`App::accessibles`) — a modal's alone while it is open — and walks them from the
 *  keyboard (Tab, Enter / Space, the arrows); the host gives the same list to AT-SPI.
 */
#pragma once
#include "Theme.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris_ui
{
    struct A11yNode
    {
        std::string key;     // stable while the control lives: "bar.play", "dock.ch_2.fader", "settings.uiScale"
        std::string role;    // button | toggle | slider | combo | tab | menu | menuitem | listitem
        std::string name;    // what it is called: "Play", "Drums fader"
        std::string value;   // what it says now: "−4.5 dB", "150 %" ("" for none)
        artboard::Rect bounds;                          // in the window (logical units)
        bool enabled = true, checked = false;           // checked: a toggle on, a tab shown
        std::function<void()> activate;                 // set: this, not the click at its centre
        std::function<void(int dir, bool fine)> step;   // a value's step (dir −1 / +1; fine: a tenth)
    };

    /** A widget's local rectangle, in the window. */
    inline artboard::Rect worldRect(const artboard::Segment &s, const artboard::Rect &r)
    {
        const artboard::Transform t = s.worldTransform();
        const artboard::Point a = t.apply(artboard::Point{r.x, r.y}), b = t.apply(artboard::Point{r.x + r.w, r.y + r.h});
        return artboard::Rect{a.x, a.y, b.x - a.x, b.y - a.y};
    }

    inline A11yNode a11yNode(const std::string &key, const char *role, const std::string &name, const artboard::Rect &bounds,
                             const std::string &value = std::string())
    {
        A11yNode n;
        n.key = key;
        n.role = role;
        n.name = name;
        n.value = value;
        n.bounds = bounds;
        return n;
    }
}
}
