/*
 *  interstellar_v1 — what a keyframe diamond shows, and the line it dispatches (R-ANIM-3).
 *
 *  A diamond beside a parameter: 0 = no curve, 1 = animated but no key at the current time,
 *  2 = a key here. "Here" is the curve's own clock — for a rack node or an effect the source's
 *  reference frame (where Grade stands), the same time the service keys a `set` at. A click adds a
 *  key there (`key add <address> --at <now>`), or removes the one that is there.
 */
#pragma once
#include "../AppHooks.h"
#include "CommandLine.h"
#include <cmath>
#include <string>

namespace arstro
{
namespace interstellar_v1
{
namespace keys
{
    inline const interstellar::AnimModel *animOf(const interstellar::AppModel &m, const std::string &node, const std::string &key)
    {
        for (const auto &a : m.anims)
            if (a.node == node && a.key == key) return &a;
        return nullptr;
    }

    /** "Now" on a source clock, as the service reckons it: the source's reference frame; a group
     *  stands where the Grade target does. */
    inline double sourceNow(const interstellar::AppModel &m, const std::string &rackObj)
    {
        for (const auto &r : m.rack)
            if (r.rackObj == rackObj && !r.media.empty()) return r.frame;
        if (m.selectedRack >= 0 && m.selectedRack < (int)m.rack.size() && !m.rack[(size_t)m.selectedRack].media.empty())
            return m.rack[(size_t)m.selectedRack].frame;
        return 0.0;
    }

    inline bool keyAt(const interstellar::AnimModel &a, double t)
    {
        for (const auto &k : a.keys)
            if (std::fabs(k.t - t) < 5e-4) return true;
        return false;
    }

    inline int state(const interstellar::AppModel &m, const std::string &node, const std::string &key, double now)
    {
        const interstellar::AnimModel *a = animOf(m, node, key);
        if (!a) return 0;
        return keyAt(*a, now) ? 2 : 1;
    }

    /** The line a diamond click dispatches. */
    inline std::string toggle(const std::string &address, int state, double now)
    {
        return std::string(state == 2 ? "key remove " : "key add ") + cmd::quote(address) + " --at " + cmd::num(now);
    }
}
}
}
