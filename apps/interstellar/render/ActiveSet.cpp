/*
 *  interstellar_render — ActiveSet implementation. See ActiveSet.h for the contract and R-TL-4.
 *
 *  Per call it is O(clips + transitions) with one hash map; a timeline has hundreds of clips, not
 *  millions, and this runs once per output frame, so nothing cleverer than that is warranted.
 */
#include "ActiveSet.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace arstro
{
namespace interstellar
{
namespace render
{
    namespace
    {
        /** A clip the video path can use at all: not audio, a real source range, a forward speed,
         *  and no NaN anywhere (a NaN `at` would make every comparison false and the clip silently
         *  vanish — better that it is dropped by a rule that says so). */
        bool usable(const ClipSpan &c)
        {
            return !c.audio && std::isfinite(c.at) && std::isfinite(c.in) && std::isfinite(c.out) &&
                   std::isfinite(c.speed) && c.speed > 0 && c.out > c.in;
        }

        double timelineDuration(const ClipSpan &c) { return (c.out - c.in) / c.speed; }

        /** Frame boundaries are the authority (R-TL-5). t = k / fps computed in floating point can
         *  land a hair BELOW k / fps, and a bare floor then reads frame k-1 — a cut between frames.
         *  A millionth of a frame is far below any real timing and far above double rounding. */
        constexpr double kFrameEpsilon = 1e-6;
    }

    std::vector<Active> activeAt(const std::vector<ClipSpan> &clips,
                                 const std::vector<TransitionSpan> &transitions, double t, double fps)
    {
        std::vector<Active> out;
        if (!std::isfinite(t) || !(fps > 0)) return out;

        const size_t n = clips.size();
        std::unordered_map<std::string, size_t> byId;
        byId.reserve(n);
        std::vector<char> live(n, 0), held(n, 0);
        std::vector<double> weight(n, 1.0);
        for (size_t i = 0; i < n; ++i)
        {
            const ClipSpan &c = clips[i];
            if (!usable(c)) continue;
            byId.emplace(c.id, i);   // first wins on a duplicate id; the model forbids them anyway
            live[i] = t >= c.at && t < c.at + timelineDuration(c);
        }

        // A transition covering t sets both weights and — the half that was missing once — keeps
        // the OUTGOING clip live past its own out-point.
        struct Pair { size_t a, b; };
        std::vector<Pair> pairs;
        for (const auto &x : transitions)
        {
            if (!(x.dur > 0) || !std::isfinite(x.dur)) continue;
            const auto ia = byId.find(x.clipA), ib = byId.find(x.clipB);
            if (ia == byId.end() || ib == byId.end() || ia->second == ib->second) continue;
            const double start = clips[ib->second].at;
            if (t < start || t >= start + x.dur) continue;
            double f = (t - start) / x.dur;
            if (!x.linear) f = f * f * (3.0 - 2.0 * f);
            weight[ib->second] *= f;           // incoming 0 -> 1
            weight[ia->second] *= 1.0 - f;     // outgoing 1 -> 0, so the pair sums to 1
            if (!live[ia->second]) live[ia->second] = held[ia->second] = 1;
            pairs.push_back({ia->second, ib->second});
        }

        std::vector<size_t> order;
        for (size_t i = 0; i < n; ++i)
            if (live[i]) order.push_back(i);
        // Bottom track first — the composite order. Within a track, earlier first, so an outgoing
        // clip lies under its incoming partner and the pair ends up adjacent.
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            const ClipSpan &ca = clips[a], &cb = clips[b];
            if (ca.trackOrder != cb.trackOrder) return ca.trackOrder < cb.trackOrder;
            if (ca.at != cb.at) return ca.at < cb.at;
            return ca.id < cb.id;
        });

        std::vector<int> slot(n, -1);
        out.reserve(order.size());
        for (size_t i : order)
        {
            const ClipSpan &c = clips[i];
            Active a;
            a.id = c.id;
            a.localTime = (t - c.at) * c.speed + c.in;
            a.sourceFrame = (long long)std::floor(a.localTime * fps + kFrameEpsilon);
            a.weight = weight[i];
            a.held = held[i] != 0;
            a.progress = (t - c.at) / timelineDuration(c);
            slot[i] = (int)out.size();
            out.push_back(std::move(a));
        }

        // Only an ADJACENT pair can be mixed against one base by the composite; a pair split by
        // another layer falls back to ordinary stacking, which is what a user would expect of
        // clips on different tracks with something between them.
        for (const auto &p : pairs)
            if (slot[p.a] >= 0 && slot[p.b] == slot[p.a] + 1) out[slot[p.b]].dissolveWithPrevious = true;
        return out;
    }
}
}
}
