#include "NodeGraph.h"
#include "Glyphs.h"
#include "TextFit.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        constexpr double kNodeH = 24.0, kGap = 22.0, kPad = 9.75, kMixD = 14.0;
        Color fade(Color c, double a) { c.a *= a; return c; }
    }

    NodeGraph::NodeGraph() { clipToBounds = true; }

    void NodeGraph::bind(const interstellar::AppModel &m)
    {
        // the source the graph is of: the Grade target — or the source a parallel node belongs to, or
        // a group's first source
        int s = m.selectedRack;
        if (s >= 0 && s < (int)m.rack.size() && !m.rack[(size_t)s].parallelOf.empty())
            for (int i = 0; i < (int)m.rack.size(); ++i)
                if (m.rack[(size_t)i].rackObj == m.rack[(size_t)s].parallelOf) s = i;
        if (s >= 0 && s < (int)m.rack.size() && m.rack[(size_t)s].group)
        {
            int first = -1;
            std::vector<int> stack{s};
            while (!stack.empty() && first < 0)
            {
                const int g = stack.back();
                stack.pop_back();
                for (int i = 0; i < (int)m.rack.size(); ++i)
                    if (m.rack[(size_t)i].parent == g) { if (m.rack[(size_t)i].group) stack.push_back(i); else if (first < 0) first = i; }
            }
            s = first;
        }
        mSelected = m.selectedRack >= 0 && m.selectedRack < (int)m.rack.size() ? m.rack[(size_t)m.selectedRack].rackObj : std::string();
        std::vector<Node> nodes;
        if (s >= 0 && s < (int)m.rack.size())
        {
            const auto &src = m.rack[(size_t)s];
            const std::string media = src.media.substr(src.media.find_last_of("/\\") == std::string::npos ? 0 : src.media.find_last_of("/\\") + 1);
            nodes.push_back({"in", "", "INPUT \xC2\xB7 " + media, Kind::Input});
            nodes.push_back({src.rackObj, src.bindName, src.bindName, Kind::Source, src.bypass});
            bool parallel = false;
            for (const auto &n : m.rack)
                if (n.parallelOf == src.rackObj)
                {
                    // its share in its label, so the node is measured with it (R5)
                    const std::string share = n.parallelMix < 0.999 ? " \xC2\xB7 " + std::to_string((int)std::lround(n.parallelMix * 100)) + "%" : std::string();
                    nodes.push_back({n.rackObj, n.bindName, n.bindName + share, Kind::Parallel, n.bypass, n.parallelMix});
                    parallel = true;
                }
            if (parallel) nodes.push_back({"mix", "", "+", Kind::Mixer});
            for (int p = src.parent; p >= 0 && p < (int)m.rack.size(); p = m.rack[(size_t)p].parent)
                nodes.push_back({m.rack[(size_t)p].rackObj, m.rack[(size_t)p].bindName, m.rack[(size_t)p].bindName, Kind::Group, m.rack[(size_t)p].bypass});
            nodes.push_back({"out", "", "OUTPUT", Kind::Output});
        }
        std::string key;
        for (const auto &n : nodes) key += n.key + "|" + n.label + "|" + (n.bypassed ? "b" : "") + std::to_string((int)(n.mix * 100)) + ";";
        if (key != mLastKey) { mLastKey = key; mDirty = true; }
        mNodes = std::move(nodes);
    }

    void NodeGraph::place(double nowMs)
    {
        // columns: input · source (parallels stacked beside it) · mixer · groups … · output
        const double h = height.value();
        const double cy = h * 0.5;
        double x = kPad;
        int parallels = 0;
        for (const auto &n : mNodes) parallels += n.kind == Kind::Parallel;
        int pi = 0;
        std::map<std::string, bool> live;
        double sourceX = 0;
        auto widthOf = [&](size_t i) {
            const Node &n = mNodes[i];
            const double tw = i < mTextW.size() && mTextW[i] > 0 ? mTextW[i] : 7.0 * n.label.size();
            return n.kind == Kind::Mixer ? kMixD : std::clamp(tw + 20.0, 54.0, 150.0);
        };
        // the source and its parallels share one column, as wide as the widest of them, so the mixer
        // after it is after all of them
        double colW = 0;
        for (size_t i = 0; i < mNodes.size(); ++i)
            if (mNodes[i].kind == Kind::Source || mNodes[i].kind == Kind::Parallel) colW = std::max(colW, widthOf(i));
        for (size_t i = 0; i < mNodes.size(); ++i)
        {
            const Node &n = mNodes[i];
            double w = n.kind == Kind::Source || n.kind == Kind::Parallel ? colW : widthOf(i);
            double nx = x, ny = cy - kNodeH * 0.5;
            if (n.kind == Kind::Mixer) ny = cy - kMixD * 0.5;
            if (n.kind == Kind::Parallel)
            {
                // the parallels sit in the source's column, above and below its line
                const int side = pi % 2 == 0 ? -1 : 1, rank = pi / 2 + 1;
                nx = sourceX;
                ny = cy - kNodeH * 0.5 + side * rank * (kNodeH + 6.0);
                ++pi;
            }
            Anim &a = mAnims[n.key];
            a.gone = false;
            live[n.key] = true;
            a.tx = nx; a.ty = ny; a.tw = w;
            if (!a.placed)
            {
                a.x.set(nx); a.y.set(ny); a.w.set(w);
                a.alpha.set(0.0);
                a.alpha.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);   // a new node fades in where it belongs
                a.placed = true;
            }
            else
            {
                a.x.animateTo(nx, 220.0, Easing::EaseOutCubic, nowMs);
                a.y.animateTo(ny, 220.0, Easing::EaseOutCubic, nowMs);
                a.w.animateTo(w, 220.0, Easing::EaseOutCubic, nowMs);
                a.alpha.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            }
            if (n.kind == Kind::Source) sourceX = nx;
            if (n.kind != Kind::Parallel) x = nx + w + kGap;
        }
        (void)parallels;
        for (auto &kv : mAnims)
            if (!live.count(kv.first) && !kv.second.gone)
            {
                kv.second.gone = true;
                kv.second.alpha.animateTo(0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            }
    }

    void NodeGraph::advance(double nowMs)
    {
        if (mDirty || width.value() != mLastW || height.value() != mLastH)
        {
            place(nowMs);
            mDirty = false;
            mLastW = width.value();
            mLastH = height.value();
        }
        for (auto it = mAnims.begin(); it != mAnims.end();)
        {
            Anim &a = it->second;
            a.x.update(nowMs); a.y.update(nowMs); a.w.update(nowMs); a.alpha.update(nowMs);
            if (a.gone && a.alpha.value() <= 0.001 && !a.alpha.isAnimating()) it = mAnims.erase(it);
            else ++it;
        }
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    Rect NodeGraph::nodeRect(const std::string &key) const
    {
        const auto it = mAnims.find(key);
        if (it == mAnims.end()) return Rect{0, 0, 0, 0};
        const auto &a = it->second;
        const bool mixer = key == "mix";
        return Rect{a.x.value(), a.y.value(), a.w.value(), mixer ? kMixD : kNodeH};
    }

    double NodeGraph::nodeAlpha(const std::string &key) const
    {
        const auto it = mAnims.find(key);
        return it == mAnims.end() ? 0.0 : it->second.alpha.value();
    }

    int NodeGraph::nodeAt(const Point &local) const
    {
        for (int i = (int)mNodes.size() - 1; i >= 0; --i)
            if (nodeRect(mNodes[(size_t)i].key).contains(local) && nodeAlpha(mNodes[(size_t)i].key) > 0.5) return i;
        return -1;
    }

    bool NodeGraph::handleGesture(const Gesture &g, const Point &local)
    {
        const int i = nodeAt(local);
        switch (g.type)
        {
        case Gesture::Type::Move: mHover.setHovered(i); return true;
        case Gesture::Type::Click:
            if (i >= 0 && !mNodes[(size_t)i].bind.empty() && onSelect) onSelect(mNodes[(size_t)i].bind);
            return true;
        case Gesture::Type::RightClick:
            if (i >= 0 && !mNodes[(size_t)i].bind.empty() && onContext) onContext(mNodes[(size_t)i], Point{g.pos.x, g.pos.y});
            return true;
        default: break;
        }
        return Segment::handleGesture(g, local);
    }

    void NodeGraph::onPaint(IRenderTarget &t) const
    {
        // real metrics for the next layout (design rule R5): a width that changed re-places the nodes
        std::vector<double> widths(mNodes.size(), 0.0);
        for (size_t i = 0; i < mNodes.size(); ++i)
            widths[i] = t.measureText(mNodes[i].label, 10.0, mNodes[i].kind == Kind::Input || mNodes[i].kind == Kind::Output ? font::sansSemiBold() : font::sans());
        if (widths != mTextW) { mTextW = widths; mDirty = true; }
        if (mNodes.empty())
        {
            const std::string s = "Select a source to see its grade as nodes.";
            t.setFill(palette::mutedForeground());
            t.drawText(s, (width.value() - t.measureText(s, 11.0, font::sans())) * 0.5, height.value() * 0.5 + 4.0, 11.0, font::sans());
            return;
        }
        // links: each node to the next along the line; the input to every parallel, every parallel to the mixer
        auto centreR = [&](const std::string &k) { const Rect r = nodeRect(k); return Point{r.right(), r.y + r.h * 0.5}; };
        auto centreL = [&](const std::string &k) { const Rect r = nodeRect(k); return Point{r.x, r.y + r.h * 0.5}; };
        auto link = [&](const Point &a, const Point &b, double alpha) {
            const Color c = fade(palette::mutedForeground(), 0.8 * alpha);
            const double mx = (a.x + b.x) * 0.5;
            if (std::fabs(a.y - b.y) < 0.5) glyph::line(t, a.x, a.y, b.x, b.y, c, 1.2);
            else { glyph::line(t, a.x, a.y, mx, a.y, c, 1.2); glyph::line(t, mx, a.y, mx, b.y, c, 1.2); glyph::line(t, mx, b.y, b.x, b.y, c, 1.2); }
            glyph::line(t, b.x - 4.0, b.y - 3.0, b.x, b.y, c, 1.2);
            glyph::line(t, b.x - 4.0, b.y + 3.0, b.x, b.y, c, 1.2);
        };
        std::string prev = "in";
        const bool hasMix = std::any_of(mNodes.begin(), mNodes.end(), [](const Node &n) { return n.kind == Kind::Mixer; });
        for (const auto &n : mNodes)
        {
            if (n.kind == Kind::Input) continue;
            const double a = std::min(nodeAlpha(n.key), nodeAlpha(prev));
            if (n.kind == Kind::Parallel)
            {
                link(centreR("in"), centreL(n.key), std::min(nodeAlpha("in"), nodeAlpha(n.key)));
                if (hasMix) link(centreR(n.key), centreL("mix"), std::min(nodeAlpha(n.key), nodeAlpha("mix")));
                continue;
            }
            link(centreR(prev), centreL(n.key), a);
            prev = n.key;
        }
        // nodes
        for (size_t i = 0; i < mNodes.size(); ++i)
        {
            const Node &n = mNodes[i];
            const double a = nodeAlpha(n.key);
            if (a <= 0.001) continue;
            const Rect r = nodeRect(n.key);
            const double hv = mHover.amount((int)i);
            if (n.kind == Kind::Mixer)
            {
                drawRoundedRect(t, r, radius::pill(), Paint::filledStroked(fade(palette::card(), a), fade(palette::mutedForeground(), a), 1.0));
                glyph::line(t, r.x + 4.0, r.y + r.h * 0.5, r.right() - 4.0, r.y + r.h * 0.5, fade(palette::foreground(), a), 1.2);
                glyph::line(t, r.x + r.w * 0.5, r.y + 4.0, r.x + r.w * 0.5, r.bottom() - 4.0, fade(palette::foreground(), a), 1.2);
                continue;
            }
            const bool io = n.kind == Kind::Input || n.kind == Kind::Output;
            const bool sel = !n.key.empty() && n.key == mSelected;
            const Color fill = io ? palette::muted() : palette::secondary();
            const Color border = sel ? palette::primary() : palette::border();
            drawRoundedRect(t, r, radius::control(), Paint::filledStroked(fade(fill, a), fade(border, a), sel ? 1.5 : 1.0));
            if (hv > 0.001) drawRoundedRect(t, r, radius::control(), Paint::filled(palette::hoverWash(hv * a)));
            if (n.kind == Kind::Group) drawRoundedRect(t, Rect{r.x, r.y, 3.0, r.h}, radius::hairline(), Paint::filled(fade(palette::mutedForeground(), 0.6 * a)));
            if (n.kind == Kind::Parallel) drawRoundedRect(t, Rect{r.x, r.y, 3.0, r.h}, radius::hairline(), Paint::filled(fade(palette::primary(), 0.7 * a)));
            const std::string &label = n.label;
            const char *fam = io ? font::sansSemiBold() : font::sans();
            const double tx = r.x + 10.0;
            t.setFill(fade(n.bypassed ? palette::mutedForeground() : (io ? palette::mutedForeground() : palette::foreground()), a));
            t.drawText(textfit::ellipsize(t, label, std::max(0.0, r.w - 16.0), 10.0, fam), tx, textfit::baseline(r.y + r.h * 0.5, 10.0), 10.0, fam);
        }
    }
}
}
