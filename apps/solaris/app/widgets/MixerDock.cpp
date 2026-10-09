#include "MixerDock.h"
#include "../../../interstellar/app/widgets/Glyphs.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;
    namespace glyph = interstellar_v1::glyph;

    namespace
    {
        constexpr int kSlots = 4;              // rack chips a card shows
        constexpr double kChipStep = 22.75;    // space::u(7)
        constexpr double kChipH = 19.5;        // space::u(6)
        constexpr double kSendH = 16.25;       // space::u(5)
        constexpr double kResizeH = 4.875;     // space::u(1.5): the dock's top edge, a grip
        constexpr double kMeterAttackMs = 40.0;

        Color fade(Color c, double a) { c.a *= a; return c; }
        std::string q(const std::string &s) { return s.find_first_of(" \t\"") == std::string::npos && !s.empty() ? s : "\"" + s + "\""; }
        std::string num(double v, int d)
        {
            char b[32];
            std::snprintf(b, sizeof b, "%.*f", d, v);
            std::string s = b;
            if (s == "-0.0" || s == "-0") s = s.substr(1);
            return s;
        }
        std::string dbText(double db) { return db <= -119.95 ? "-inf" : (db > 0.049 ? "+" : "") + num(db, 1); }
        double meterLevel(float peak)
        {
            if (peak <= 1e-6f) return 0.0;
            return std::clamp((20.0 * std::log10((double)peak) + 60.0) / 66.0, 0.0, 1.0); // −60 … +6 dBFS
        }
        double rnd(double v, double step) { return std::round(v / step) * step; }

        /** Where everything on a card is, from its left edge, width and the body's top and height. */
        struct Geo
        {
            Rect chips[kSlots], sends[2], pan, zone, mute, solo, out;
            double readoutY = 0;
        };
        Geo geoOf(double x, double w, double by, double bh)
        {
            Geo g;
            for (int k = 0; k < kSlots; ++k) g.chips[k] = Rect{x + 6.5, by + 38.0 + k * kChipStep, w - 13.0, kChipH};
            for (int k = 0; k < 2; ++k) g.sends[k] = Rect{x + 6.5, by + 132.0 + k * kSendH, w - 13.0, kSendH};
            g.pan = Rect{x + 9.75, by + 170.0, w - 19.5, 13.0};
            g.zone = Rect{x + 6.5, by + 192.0, w - 13.0, std::max(40.0, bh - 192.0 - 78.0)};
            g.readoutY = g.zone.bottom() + 14.0;
            const double bw = (w - 13.0 - 3.25) * 0.5;
            g.mute = Rect{x + 6.5, g.zone.bottom() + 22.0, bw, 19.5};
            g.solo = Rect{g.mute.right() + 3.25, g.mute.y, bw, 19.5};
            g.out = Rect{x + 6.5, g.zone.bottom() + 45.5, w - 13.0, 19.5};
            return g;
        }
        /** "→ text": the arrow DRAWN — the embedded Roboto has no U+2192 (app/NOTES.md). */
        void arrowText(IRenderTarget &t, const std::string &text, double x, double cy, double maxW, double px, const char *fam, const Color &c)
        {
            const double ay = std::round(cy) + 0.5;
            glyph::line(t, x, ay, x + 7.0, ay, c, 1.1);
            glyph::line(t, x + 4.5, ay - 2.5, x + 7.0, ay, c, 1.1);
            glyph::line(t, x + 4.5, ay + 2.5, x + 7.0, ay, c, 1.1);
            t.setFill(c);
            t.drawText(textfit::ellipsize(t, text, std::max(0.0, maxW - 11.0), px, fam), x + 11.0, textfit::baseline(cy, px), px, fam);
        }
        int hoverId(int part, const std::string &id, int index)
        {
            return (int)((std::hash<std::string>{}(id) * 31u + (size_t)part * 1009u + (size_t)(index + 7)) % 1000003u);
        }
        void chevron(IRenderTarget &t, double cx, double cy, double turn, const Color &c)
        {
            // a ">" turned by `turn` (0 = pointing right, 1 = pointing down): eased with the fold
            const double a = turn * M_PI * 0.5, ca = std::cos(a), sa = std::sin(a);
            auto P = [&](double x, double y) { return Point{cx + x * ca - y * sa, cy + x * sa + y * ca}; };
            const Point p0 = P(-2.0, -4.0), p1 = P(2.5, 0.0), p2 = P(-2.0, 4.0);
            glyph::line(t, p0.x, p0.y, p1.x, p1.y, c, 1.3);
            glyph::line(t, p1.x, p1.y, p2.x, p2.y, c, 1.3);
        }
    }

    double MixerDock::faderPos(double dB) { return dB <= -119.95 ? 0.0 : std::clamp(std::pow(10.0, (dB - 6.0) / 40.0), 0.0, 1.0); }
    double MixerDock::faderDb(double pos) { return pos <= 0.001 ? -120.0 : std::clamp(6.0 + 40.0 * std::log10(pos), -120.0, 6.0); }

    MixerDock::MixerDock()
    {
        clipToBounds = true;
    }

    // ── the model ────────────────────────────────────────────────────────────────────────────

    const solaris::StripModel *MixerDock::strip(const std::string &id) const
    {
        for (const auto &s : mModel.strips)
            if (s.id == id) return &s;
        return nullptr;
    }

    const MixerDock::Live *MixerDock::liveOf(const std::string &id) const
    {
        const auto it = mLive.find(id);
        return it == mLive.end() ? nullptr : &it->second;
    }

    std::string MixerDock::labelOf(const std::string &target) const
    {
        if (target == "master" || target.empty()) return "Master";
        if (const auto *s = strip(target)) return s->name;
        for (const auto &p : mModel.ports)
            if (p.id == target) return p.name + " port";
        return target;
    }

    std::string MixerDock::pageKey(int t) const { return t >= 0 && t < (int)mMixers.size() ? mMixers[(size_t)t].id : std::string(); }

    void MixerDock::bind(const solaris::AppModel &m, bool interacting)
    {
        if (m.projectPath != mModel.projectPath)
        {
            // another song: nothing of the last one travels into it
            mLive.clear();
            mPages.clear();
            mTabs.clear();
            mFold.clear();
            mCells.clear();
            mTab = 0;
            mHiInit = false;
            mEver = mBound = false;
            mScrollX.reset();
            mMxX.reset();
            mMxY.reset();
        }
        mModel = m;
        mMixers = m.mixers;
        mInteracting = interacting;
        mTab = std::clamp(mTab, 0, (int)mMixers.size());
        syncCards();
        syncTabs();
        mBound = true;
    }

    void MixerDock::syncCards()
    {
        static int ghostSerial = 0;
        std::set<std::string> live;
        for (const auto &mx : mMixers)
        {
            live.insert(mx.id);
            // wanted: processing order, strips feeding the same bus (two or more) gathered under its header
            std::map<std::string, std::vector<std::string>> byBus;
            for (const auto &id : mx.strips)
                if (const auto *s = strip(id))
                    if (strip(s->out)) byBus[s->out].push_back(id);
            struct Want { std::string key, id; bool header; };
            std::vector<Want> want;
            std::set<std::string> emitted;
            for (const auto &id : mx.strips)
            {
                const auto *s = strip(id);
                if (!s) continue;
                const auto g = byBus.find(s->out);
                if (g != byBus.end() && g->second.size() >= 2)
                {
                    if (emitted.insert(s->out).second)
                    {
                        want.push_back(Want{"fold:" + s->out, s->out, true});
                        for (const auto &mid : g->second) want.push_back(Want{"strip:" + mid, mid, false});
                    }
                    continue;
                }
                want.push_back(Want{"strip:" + id, id, false});
            }
            Page &pg = mPages[mx.id];
            // a card whose place among the others changed shrinks where it was and grows where it is
            std::vector<std::string> oldOrder, newOrder;
            std::set<std::string> wantKeys, oldKeys;
            for (const auto &w : want) wantKeys.insert(w.key);
            for (const auto &c : pg.cards)
                if (!c.gone) oldKeys.insert(c.key);
            for (const auto &c : pg.cards)
                if (!c.gone && wantKeys.count(c.key)) oldOrder.push_back(c.key);
            for (const auto &w : want)
                if (oldKeys.count(w.key)) newOrder.push_back(w.key);
            std::set<std::string> moved;
            for (size_t i = 0; i < oldOrder.size(); ++i)
                if (oldOrder[i] != newOrder[i]) moved.insert(oldOrder[i]);
            std::vector<Card> next;
            std::vector<bool> reused(pg.cards.size(), false);
            for (const auto &w : want)
            {
                bool found = false;
                for (size_t i = 0; i < pg.cards.size() && !found; ++i)
                    if (!reused[i] && !pg.cards[i].gone && pg.cards[i].key == w.key && !moved.count(w.key))
                    {
                        reused[i] = true;
                        next.push_back(pg.cards[i]);
                        found = true;
                    }
                if (!found)
                {
                    Card c;
                    c.key = w.key;
                    c.id = w.id;
                    c.header = w.header;
                    next.push_back(c);
                }
            }
            for (size_t i = 0; i < pg.cards.size(); ++i)
            {
                if (reused[i]) continue;
                Card c = pg.cards[i];
                if (!c.gone) { c.gone = true; c.key += "#" + std::to_string(++ghostSerial); }
                next.insert(next.begin() + (long)std::min(i, next.size()), c);
            }
            for (auto &c : next)
                if (!c.gone && !c.header)
                    if (const auto *s = strip(c.id)) c.snap = *s;
            pg.cards = std::move(next);
        }
        for (auto &kv : mPages)
            if (!kv.first.empty() && !live.count(kv.first))
                for (auto &c : kv.second.cards) c.gone = true; // a mixer deleted: its page empties as it fades
        mPages[""]; // the matrix
    }

    void MixerDock::syncTabs()
    {
        std::vector<std::pair<std::string, std::string>> want;
        for (const auto &mx : mMixers) want.emplace_back(mx.id, mx.name);
        want.emplace_back("+", "+");
        want.emplace_back("", "Matrix");
        for (auto &t : mTabs) t.gone = true;
        for (const auto &w : want)
        {
            Tab *found = nullptr;
            for (auto &t : mTabs)
                if (t.key == w.first) found = &t;
            if (!found) { mTabs.emplace_back(); found = &mTabs.back(); found->key = w.first; }
            found->label = w.second;
            found->gone = false;
        }
        // keep them in the wanted order (a ghost stays where it was)
        std::vector<Tab> ordered;
        for (const auto &w : want)
            for (auto &t : mTabs)
                if (t.key == w.first && !t.gone) ordered.push_back(t);
        for (size_t i = 0; i < mTabs.size(); ++i)
            if (mTabs[i].gone) ordered.insert(ordered.begin() + (long)std::min(i, ordered.size()), mTabs[i]);
        mTabs = std::move(ordered);
    }

    // ── geometry ─────────────────────────────────────────────────────────────────────────────

    double MixerDock::stripsRight() const { return std::max(0.0, width.value() - kMasterW); }
    double MixerDock::bodyH() const { return std::max(height.value() - kTabsH, kMinBodyH); }
    double MixerDock::bodyTop() const { return kTabsH - mScrollY.value(); }

    const MixerDock::Page *MixerDock::shownPage() const
    {
        const auto it = mPages.find(pageKey(mTab));
        return it == mPages.end() ? nullptr : &it->second;
    }

    double MixerDock::cardWidth(const Card &c) const
    {
        const double in = c.placed ? c.in.value() : 0.0;
        if (c.header)
        {
            const auto f = mFold.find(c.id);
            const double fold = f == mFold.end() ? 0.0 : f->second.a.value();
            return in * (kFoldW + (kStripW - kFoldW) * fold);
        }
        const auto *s = strip(c.id);
        double fold = 0.0;
        if (s)
        {
            const auto f = mFold.find(s->out);
            if (f != mFold.end())
            {
                // folded only if it is in that bus's group on this page (two or more feeding it)
                int n = 0;
                for (const auto &o : mModel.strips) n += o.out == s->out && o.mixer == s->mixer ? 1 : 0;
                if (n >= 2) fold = f->second.a.value();
            }
        }
        return in * kStripW * (1.0 - fold);
    }

    bool MixerDock::cardX(const std::string &key, double &x, double &w) const
    {
        const Page *pg = shownPage();
        if (!pg || onMatrix()) return false;
        double cx = -mScrollX.value();
        for (const auto &c : pg->cards)
        {
            const double cw = cardWidth(c);
            if (c.key == key && !c.gone) { x = cx; w = cw; return cw > 0.5; }
            cx += cw;
        }
        return false;
    }

    Rect MixerDock::cardRect(const std::string &id) const
    {
        if (id == "master") return Rect{stripsRight(), bodyTop(), kMasterW, bodyH()};
        double x, w;
        return cardX("strip:" + id, x, w) ? Rect{x, bodyTop(), w, bodyH()} : Rect{};
    }

    Rect MixerDock::addLineRect() const
    {
        const Page *pg = shownPage();
        if (!pg || onMatrix() || mTab >= (int)mMixers.size()) return Rect{};
        double x = -mScrollX.value();
        for (const auto &c : pg->cards) x += cardWidth(c); // after the LIVE cards: it slides as they grow and shrink
        return addLineAt(x, bodyTop());
    }

    Rect MixerDock::faderRect(const std::string &id) const
    {
        const Rect c = cardRect(id);
        return c.w > 0 ? geoOf(c.x, c.w, c.y, c.h).zone : Rect{};
    }
    Rect MixerDock::panRect(const std::string &id) const
    {
        const Rect c = cardRect(id);
        return c.w > 0 && id != "master" ? geoOf(c.x, c.w, c.y, c.h).pan : Rect{};
    }
    Rect MixerDock::muteRect(const std::string &id) const
    {
        const Rect c = cardRect(id);
        return c.w > 0 && id != "master" ? geoOf(c.x, c.w, c.y, c.h).mute : Rect{};
    }
    Rect MixerDock::soloRect(const std::string &id) const
    {
        const Rect c = cardRect(id);
        return c.w > 0 && id != "master" ? geoOf(c.x, c.w, c.y, c.h).solo : Rect{};
    }
    Rect MixerDock::outRect(const std::string &id) const
    {
        const Rect c = cardRect(id);
        return c.w > 0 && id != "master" ? geoOf(c.x, c.w, c.y, c.h).out : Rect{};
    }
    Rect MixerDock::sendRect(const std::string &id, int i) const
    {
        const Rect c = cardRect(id);
        return c.w > 0 && id != "master" && i >= 0 && i < 2 ? geoOf(c.x, c.w, c.y, c.h).sends[i] : Rect{};
    }
    int MixerDock::chipSlots(const std::string &id) const
    {
        const auto *devs = id == "master" ? &mModel.masterDevices : (strip(id) ? &strip(id)->devices : nullptr);
        if (!devs) return 0;
        const int n = (int)devs->size();
        return n + 1 <= kSlots ? n + 1 : kSlots; // the devices and "+ Effect", or two, "+N", "+ Effect"
    }
    Rect MixerDock::chipRect(const std::string &id, int slot) const
    {
        const Rect c = cardRect(id);
        return c.w > 0 && slot >= 0 && slot < chipSlots(id) ? geoOf(c.x, c.w, c.y, c.h).chips[slot] : Rect{};
    }
    Rect MixerDock::foldRect(const std::string &bus) const
    {
        double x, w;
        return cardX("fold:" + bus, x, w) ? Rect{x, bodyTop(), w, bodyH()} : Rect{};
    }
    double MixerDock::foldAmount(const std::string &bus) const
    {
        const auto it = mFold.find(bus);
        return it == mFold.end() ? 0.0 : it->second.a.value();
    }
    double MixerDock::faderLive(const std::string &id) const
    {
        const Live *l = liveOf(id);
        return l ? l->fader.value() : 0.0;
    }
    double MixerDock::panLive(const std::string &id) const
    {
        const Live *l = liveOf(id);
        return l ? l->pan.value() : 0.0;
    }
    double MixerDock::muteAmount(const std::string &id) const
    {
        const Live *l = liveOf(id);
        return l ? l->mute.value() : 0.0;
    }
    double MixerDock::meterLive(const std::string &id, int ch) const
    {
        const Live *l = liveOf(id);
        return l && ch >= 0 && ch < 2 ? l->meter[ch].value() : 0.0;
    }

    Rect MixerDock::tabRect(int t) const
    {
        const std::string key = t == (int)mMixers.size() ? std::string() : pageKey(t);
        if (t < 0 || t > (int)mMixers.size()) return Rect{};
        for (const auto &tb : mTabs)
            if (tb.key == key && !tb.gone)
            {
                const auto w = mTabTextW.find(tb.label);
                const double tw = (w == mTabTextW.end() ? tb.label.size() * 6.0 : w->second) + 2.0 * space::padX();
                return Rect{tb.placed ? tb.x.value() : tb.xWant, 4.0, tw, kTabsH - 8.0};
            }
        return Rect{};
    }
    Rect MixerDock::addMixerRect() const
    {
        for (const auto &tb : mTabs)
            if (tb.key == "+" && !tb.gone) return Rect{tb.placed ? tb.x.value() : tb.xWant, 4.0, kTabsH - 8.0, kTabsH - 8.0};
        return Rect{};
    }
    Rect MixerDock::toggleRect() const { return Rect{width.value() - 6.5 - 21.125, 4.0, 21.125, kTabsH - 8.0}; }

    double MixerDock::pageAmount(int t) const
    {
        const auto it = mPages.find(t == (int)mMixers.size() ? std::string() : pageKey(t));
        return it == mPages.end() ? 0.0 : it->second.alpha.value();
    }

    Rect MixerDock::cellRect(const std::string &from, const std::string &to) const
    {
        if (!onMatrix()) return Rect{};
        int row = 0, rowOf = -1;
        for (const auto &mx : mMixers)
        {
            ++row; // its section row
            for (const auto &id : mx.strips)
            {
                if (id == from) rowOf = row;
                ++row;
            }
        }
        int col = 0, colOf = -1;
        for (const auto &s : mModel.strips)
            if (!mMixers.empty() && s.mixer != mMixers.front().id) { if (s.id == to) colOf = col; ++col; }
        if (to == "master") colOf = col;
        ++col;
        for (const auto &p : mModel.ports)
            if (p.dir == "out") { if (p.id == to) colOf = col; ++col; }
        if (rowOf < 0 || colOf < 0) return Rect{};
        return Rect{kRowHeadW + colOf * kCellW - mMxX.value(), kTabsH + kColHeadH + rowOf * kCellH - mMxY.value(), kCellW, kCellH};
    }

    void MixerDock::layout()
    {
        const double W = width.value(), H = height.value();
        // tabs: measured widths (from the last paint), laid out left to right
        double x = space::padX();
        for (auto &tb : mTabs)
        {
            if (tb.gone) continue;
            const auto w = mTabTextW.find(tb.label);
            const double tw = tb.key == "+" ? kTabsH - 8.0 : (w == mTabTextW.end() ? tb.label.size() * 6.0 : w->second) + 2.0 * space::padX();
            tb.xWant = x;
            x += tw + (tb.key == "+" ? 13.0 : 3.25); // the matrix stands a little apart
        }
        // the shown page's horizontal extent
        double content = 0.0;
        if (const Page *pg = shownPage())
            for (const auto &c : pg->cards) content += cardWidth(c);
        if (!onMatrix()) content += kAddLineW;
        mScrollX.setExtent(0.0, stripsRight(), content);
        mScrollY.setExtent(kTabsH, std::max(0.0, H - kTabsH), bodyH());
        // the matrix's extents
        int rows = 0, cols = 1;
        for (const auto &mx : mMixers) rows += 1 + (int)mx.strips.size();
        for (const auto &s : mModel.strips)
            if (!mMixers.empty() && s.mixer != mMixers.front().id) ++cols;
        for (const auto &p : mModel.ports) cols += p.dir == "out" ? 1 : 0;
        mMxX.setExtent(kRowHeadW, std::max(0.0, W - kRowHeadW), cols * kCellW);
        mMxY.setExtent(kTabsH + kColHeadH, std::max(0.0, H - kTabsH - kColHeadH), rows * kCellH);
    }

    // ── time ─────────────────────────────────────────────────────────────────────────────────

    void MixerDock::advance(double nowMs)
    {
        mNowMs = nowMs;
        const bool fadeIn = mEver && !reducedMotion();
        // every strip's eased picture, and the master's
        auto drive = [&](const std::string &id, double gain, double pan, bool mute, bool solo, bool audible, const float *peak, int colour) {
            Live &l = live(id);
            const bool draggingFader = mDragging && mPress.part == Part::Fader && mPress.id == id;
            const bool draggingPan = mDragging && mPress.part == Part::Pan && mPress.id == id;
            const double f = faderPos(gain);
            const Color col = colour >= 0 ? surface::track(colour) : palette::secondaryForeground();
            if (!l.placed)
            {
                l.fader.set(f); l.faderLast = f;
                l.pan.set(pan); l.panLast = pan;
                l.mute.set(mute ? 1.0 : 0.0); l.muteLast = mute;
                l.solo.set(solo ? 1.0 : 0.0); l.soloLast = solo;
                l.dim.set(audible ? 0.0 : 1.0); l.dimLast = !audible;
                l.colFrom = l.colTo = col; l.colT.set(1.0); l.colLast = colour;
                l.placed = true;
            }
            if (!draggingFader && f != l.faderLast) { l.fader.animateTo(f, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs); l.faderLast = f; }
            if (!draggingPan && pan != l.panLast) { l.pan.animateTo(pan, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs); l.panLast = pan; }
            if (mute != l.muteLast) { l.mute.animateTo(mute ? 1.0 : 0.0, motion::kSelectMs, Easing::EaseOutCubic, nowMs); l.muteLast = mute; }
            if (solo != l.soloLast) { l.solo.animateTo(solo ? 1.0 : 0.0, motion::kSelectMs, Easing::EaseOutCubic, nowMs); l.soloLast = solo; }
            if (audible == l.dimLast) { l.dim.animateTo(audible ? 0.0 : 1.0, motion::kSelectMs, Easing::EaseOutCubic, nowMs); l.dimLast = !audible; }
            if (colour != l.colLast)
            {
                l.colFrom = lerpColor(l.colFrom, l.colTo, l.colT.value()); // from what is SHOWN: a retarget never jumps
                l.colTo = col;
                l.colT.set(0.0);
                l.colT.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
                l.colLast = colour;
            }
            for (int c = 0; c < 2; ++c)
            {
                const double m = meterLevel(peak[c]);
                if (m != l.meterLast[c])
                {
                    l.meter[c].animateTo(m, m > l.meter[c].value() ? kMeterAttackMs : motion::kMeterFallMs, Easing::EaseOutCubic, nowMs);
                    l.meterLast[c] = m;
                }
                l.meter[c].update(nowMs);
            }
            l.fader.update(nowMs);
            l.pan.update(nowMs);
            l.mute.update(nowMs);
            l.solo.update(nowMs);
            l.dim.update(nowMs);
            l.colT.update(nowMs);
        };
        for (const auto &s : mModel.strips) drive(s.id, s.gain, s.pan, s.mute, s.solo, s.audible, s.peak, s.colour);
        drive("master", mModel.masterGain, 0.0, false, false, true, mModel.transport.masterPeak, -1);

        // cards: grow in, shrink out
        for (auto &kv : mPages)
        {
            Page &pg = kv.second;
            for (auto &c : pg.cards)
            {
                const double want = c.gone ? 0.0 : 1.0;
                if (!c.placed) { c.in.set(fadeIn ? 0.0 : 1.0); c.inLast = c.in.value(); c.placed = true; }
                if (want != c.inLast) { c.in.animateTo(want, motion::kReflowMs, Easing::EaseOutCubic, nowMs); c.inLast = want; }
                c.in.update(nowMs);
            }
            pg.cards.erase(std::remove_if(pg.cards.begin(), pg.cards.end(),
                                          [](const Card &c) { return c.gone && !c.in.isAnimating() && c.in.value() <= 0.001; }),
                           pg.cards.end());
            pg.want = kv.first == pageKey(mTab) || (kv.first.empty() && onMatrix());
            if (!kv.first.empty() && onMatrix()) pg.want = false;
            if (!pg.placed) { pg.alpha.set(pg.want ? 1.0 : 0.0); pg.last = pg.want; pg.placed = true; }
            if (pg.want != pg.last) { pg.alpha.animateTo(pg.want ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs); pg.last = pg.want; }
            pg.alpha.update(nowMs);
        }
        for (auto it = mPages.begin(); it != mPages.end();)
        {
            bool live = it->first.empty();
            for (const auto &mx : mMixers) live |= mx.id == it->first;
            if (!live && it->second.alpha.value() <= 0.001 && !it->second.alpha.isAnimating()) it = mPages.erase(it);
            else ++it;
        }
        // folds, and the matrix's dots
        for (auto *map : {&mFold, &mCells})
            for (auto &kv : *map)
            {
                Toggle &tg = kv.second;
                tg.a.update(nowMs);
            }
        std::set<std::string> cellsNow;
        for (const auto &s : mModel.strips)
        {
            cellsNow.insert("m:" + s.id + ">" + (s.out.empty() ? std::string("master") : s.out));
            for (const auto &sd : s.sends) cellsNow.insert("s:" + s.id + ">" + sd.to);
        }
        for (const auto &k : cellsNow) mCells[k];
        for (auto &kv : mCells)
        {
            Toggle &tg = kv.second;
            const bool on = cellsNow.count(kv.first) > 0;
            if (!tg.placed) { tg.a.set(on && !fadeIn ? 1.0 : 0.0); tg.last = !fadeIn && on; tg.placed = true; }
            if (on != tg.last) { tg.a.animateTo(on ? 1.0 : 0.0, motion::kSelectMs, Easing::EaseOutCubic, nowMs); tg.last = on; }
        }
        // tabs: slide to their places, fade in and out; the highlight follows the shown one
        for (auto &tb : mTabs)
        {
            if (!tb.placed) { tb.x.set(tb.xWant); tb.xLast = tb.xWant; tb.in.set(fadeIn ? 0.0 : 1.0); tb.inLast = tb.in.value(); tb.placed = true; }
            if (tb.xWant != tb.xLast && !tb.gone) { tb.x.animateTo(tb.xWant, motion::kSlideMs, Easing::EaseOutCubic, nowMs); tb.xLast = tb.xWant; }
            const double want = tb.gone ? 0.0 : 1.0;
            if (want != tb.inLast) { tb.in.animateTo(want, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs); tb.inLast = want; }
            tb.x.update(nowMs);
            tb.in.update(nowMs);
        }
        mTabs.erase(std::remove_if(mTabs.begin(), mTabs.end(), [](const Tab &t) { return t.gone && !t.in.isAnimating() && t.in.value() <= 0.001; }), mTabs.end());
        const Rect hr = tabRect(mTab);
        double hx = space::padX(), hw = 0.0;
        for (const auto &tb : mTabs)
            if (!tb.gone && tb.key == (onMatrix() ? std::string() : pageKey(mTab)))
            {
                hx = tb.xWant;
                hw = hr.w;
            }
        if (!mHiInit) { mHiX.set(hx); mHiW.set(hw); mHiXLast = hx; mHiWLast = hw; mHiInit = true; }
        if (hx != mHiXLast) { mHiX.animateTo(hx, motion::kSlideMs, Easing::EaseOutCubic, nowMs); mHiXLast = hx; }
        if (hw != mHiWLast) { mHiW.animateTo(hw, motion::kSlideMs, Easing::EaseOutCubic, nowMs); mHiWLast = hw; }
        mHiX.update(nowMs);
        mHiW.update(nowMs);

        mScrollX.advance(nowMs);
        mScrollY.advance(nowMs);
        mMxX.advance(nowMs);
        mMxY.advance(nowMs);
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        if (mBound) mEver = true;
        Segment::advance(nowMs);
    }

    void MixerDock::setTab(int t)
    {
        t = std::clamp(t, 0, (int)mMixers.size());
        if (t == mTab) return;
        if (Page *pg = mPages.count(pageKey(mTab)) ? &mPages[pageKey(mTab)] : nullptr) pg->sx = mScrollX.value();
        mTab = t;
        mScrollX.reset(); // a new page: nowhere to travel from (the old one fades where it was)
    }

    // ── input ────────────────────────────────────────────────────────────────────────────────

    MixerDock::Hit MixerDock::hitAt(const Point &p) const
    {
        Hit h;
        if (p.y < kResizeH) { h.part = Part::Resize; return h; }
        if (p.y < kTabsH)
        {
            if (toggleRect().contains(p)) { h.part = Part::ToggleDock; return h; }
            if (addMixerRect().contains(p)) { h.part = Part::AddMixer; return h; }
            for (int t = 0; t < tabCount(); ++t)
                if (tabRect(t).contains(p)) { h.part = Part::Tab; h.index = t; return h; }
            return h;
        }
        if (onMatrix())
        {
            if (p.x < kRowHeadW || p.y < kTabsH + kColHeadH) return h;
            for (const auto &s : mModel.strips)
            {
                std::vector<std::string> cols;
                for (const auto &o : mModel.strips)
                    if (!mMixers.empty() && o.mixer != mMixers.front().id) cols.push_back(o.id);
                cols.push_back("master");
                for (const auto &pt : mModel.ports)
                    if (pt.dir == "out") cols.push_back(pt.id);
                for (const auto &c : cols)
                    if (cellRect(s.id, c).contains(p)) { h.part = Part::Cell; h.id = s.id; h.to = c; return h; }
            }
            return h;
        }
        // the master
        if (p.x >= stripsRight())
        {
            h.id = "master";
            const Rect c = cardRect("master");
            const Geo g = geoOf(c.x, c.w, c.y, c.h);
            if (g.zone.contains(p)) h.part = Part::Fader;
            for (int k = 0; k < chipSlots("master"); ++k)
                if (g.chips[k].contains(p)) { h.part = Part::Chip; h.index = k; }
            return h;
        }
        const Page *pg = shownPage();
        if (!pg) return h;
        double cx = -mScrollX.value();
        for (const auto &c : pg->cards)
        {
            const double cw = cardWidth(c);
            if (!c.gone && cw > 0.5 && p.x >= cx && p.x < cx + cw)
            {
                h.id = c.id;
                if (c.header) { h.part = Part::Fold; return h; }
                const Geo g = geoOf(cx, cw, bodyTop(), bodyH());
                h.part = Part::Header;
                for (int k = 0; k < chipSlots(c.id); ++k)
                    if (g.chips[k].contains(p)) { h.part = Part::Chip; h.index = k; }
                for (int k = 0; k < 2; ++k)
                    if (g.sends[k].contains(p)) { h.part = Part::Send; h.index = k; }
                if (g.pan.contains(p)) h.part = Part::Pan;
                if (g.zone.contains(p)) h.part = Part::Fader;
                if (g.mute.contains(p)) h.part = Part::Mute;
                if (g.solo.contains(p)) h.part = Part::Solo;
                if (g.out.contains(p)) h.part = Part::Out;
                return h;
            }
            cx += cw;
        }
        if (addLineRect().contains(p)) h.part = Part::AddLine;
        return h;
    }

    void MixerDock::openOut(const std::string &id, Point world)
    {
        const auto *s = strip(id);
        if (!s || !onMenu) return;
        std::vector<cosmo_v2::ContextMenu::Item> items;
        const std::string cur = s->out.empty() ? std::string("master") : s->out;
        for (const auto &t : s->targets) // exactly what the service says it may feed (R-MIX-4)
            items.push_back({labelOf(t) + (t == cur ? "  \xC2\xB7 now" : ""), [this, id, t] { send("route " + id + " --to " + t); }});
        onMenu(items, world);
    }

    void MixerDock::openAddLine(Point world)
    {
        // a line on THIS mixer: an audio line, a bus, or any instrument the registry has — one `strip add` each
        if (!onMenu || mTab >= (int)mMixers.size()) return;
        const std::string mx = mMixers[(size_t)mTab].id;
        std::vector<cosmo_v2::ContextMenu::Item> items = {
            {"Audio line", [this, mx] { send("strip add --kind audio --mixer " + mx); }},
            {"Bus", [this, mx] { send("strip add --kind bus --mixer " + mx); }}};
        for (const auto &t : mModel.deviceTypes)
            if (t.kind == "instrument")
            {
                const std::string type = t.name;
                items.push_back({t.label, [this, mx, type] { send("strip add --kind instrument --instrument " + type + " --mixer " + mx); }});
            }
        onMenu(std::move(items), world);
    }

    void MixerDock::openAddEffect(const std::string &id, Point world)
    {
        if (!onMenu) return;
        std::vector<cosmo_v2::ContextMenu::Item> items;
        for (const auto &t : mModel.deviceTypes)
            if (t.kind == "effect")
                items.push_back({t.label, [this, id, t] { send("device add " + id + " --type " + t.name); }});
        onMenu(items, world);
    }

    void MixerDock::openSend(const solaris::SendModel &sd, const std::string &from, Point world)
    {
        if (!onMenu) return;
        std::vector<cosmo_v2::ContextMenu::Item> items;
        const std::string id = sd.id, to = sd.to;
        const bool pre = sd.pre;
        items.push_back({pre ? "Post-fader" : "Pre-fader", [this, id, pre] { send("set " + id + ".pre=" + (pre ? "false" : "true")); }});
        items.push_back({"Make it the main output", [this, from, to, id] {
                             if (send("route " + from + " --to " + to)) send("send delete " + id);
                         }});
        items.push_back({"Remove send", [this, id] { send("send delete " + id); }});
        onMenu(items, world);
    }

    bool MixerDock::handleGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Move:
        {
            const Hit h = hitAt(local);
            mHover.setHovered(h.part == Part::None || h.part == Part::Header ? -1 : hoverId((int)h.part, h.id + ">" + h.to, h.index));
            return true;
        }
        case Gesture::Type::Down:
            mPress = hitAt(local);
            mDragging = false;
            mLastSent.clear();
            if (mPress.part == Part::Fader)
            {
                const Rect z = faderRect(mPress.id);
                const double pos = std::clamp((z.bottom() - 6.0 - local.y) / std::max(1.0, z.h - 12.0), 0.0, 1.0);
                mGrab = std::fabs(pos - faderLive(mPress.id)) < 0.06 ? pos - faderLive(mPress.id) : 0.0; // on the thumb: keep the grab
            }
            if (mPress.part == Part::Send)
            {
                const auto *s = strip(mPress.id);
                mStart = s && mPress.index < (int)s->sends.size() ? s->sends[(size_t)mPress.index].gain : 0.0;
            }
            if (mPress.part == Part::Cell)
            {
                mStart = 0.0;
                if (const auto *s = strip(mPress.id))
                    for (const auto &sd : s->sends)
                        if (sd.to == mPress.to) mStart = sd.gain;
            }
            return true;
        case Gesture::Type::DragStart:
            mDragging = true;
            [[fallthrough]];
        case Gesture::Type::Drag:
        {
            if (!mDragging) return true;
            switch (mPress.part)
            {
            case Part::Resize:
                if (onResize) onResize(g.pos.y);
                break;
            case Part::Fader:
            {
                const Rect z = faderRect(mPress.id);
                const double pos = std::clamp((z.bottom() - 6.0 - local.y) / std::max(1.0, z.h - 12.0) - mGrab, 0.0, 1.0);
                Live &l = live(mPress.id);
                l.fader.set(pos); // the pointer is the animation
                const double db = rnd(faderDb(pos), 0.1);
                l.faderLast = faderPos(db);
                const std::string line = mPress.id == "master" ? "set project.masterGain=" + num(db, 1) : "set " + mPress.id + ".gain=" + num(db, 1);
                if (line != mLastSent) { mLastSent = line; send(line); }
                break;
            }
            case Part::Pan:
            {
                const Rect r = panRect(mPress.id);
                double v = std::clamp((local.x - r.x) / std::max(1.0, r.w) * 2.0 - 1.0, -1.0, 1.0);
                if (std::fabs(v) < 0.03) v = 0.0; // the centre catches
                v = rnd(v, 0.01);
                Live &l = live(mPress.id);
                l.pan.set(v);
                l.panLast = v;
                const std::string line = "set " + mPress.id + ".pan=" + num(v, 2);
                if (line != mLastSent) { mLastSent = line; send(line); }
                break;
            }
            case Part::Send:
            case Part::Cell:
            {
                const auto *s = strip(mPress.id);
                if (!s) break;
                std::string sid;
                for (size_t k = 0; k < s->sends.size(); ++k)
                    if ((mPress.part == Part::Send && (int)k == mPress.index) || (mPress.part == Part::Cell && s->sends[k].to == mPress.to)) sid = s->sends[k].id;
                if (sid.empty()) break;
                const double d = mPress.part == Part::Send ? (g.pos.x - g.start.x) : (g.start.y - g.pos.y);
                const double gain = std::clamp(rnd(mStart + d * 0.1, 0.1), -60.0, 12.0);
                const std::string line = "set " + sid + ".gain=" + num(gain, 1);
                if (line != mLastSent) { mLastSent = line; send(line); }
                break;
            }
            default:
                break;
            }
            return true;
        }
        case Gesture::Type::Drop:
        case Gesture::Type::Up:
            mDragging = false;
            mPress = Hit{};
            return true;
        case Gesture::Type::Click:
        {
            const Hit h = hitAt(local);
            const Point world = g.pos;
            switch (h.part)
            {
            case Part::Tab: setTab(h.index); break;
            case Part::AddMixer: send("mixer add"); break;
            case Part::AddLine: openAddLine(world); break;
            case Part::ToggleDock: if (onToggle) onToggle(); break;
            case Part::Mute:
                if (const auto *s = strip(h.id)) send("set " + h.id + ".mute=" + (s->mute ? "false" : "true"));
                break;
            case Part::Solo:
                if (const auto *s = strip(h.id)) send("set " + h.id + ".solo=" + (s->solo ? "false" : "true"));
                break;
            case Part::Out: openOut(h.id, world); break;
            case Part::Fold:
            {
                Toggle &tg = mFold[h.id];
                const bool folded = !tg.last;
                tg.a.animateTo(folded ? 1.0 : 0.0, motion::kReflowMs, Easing::EaseOutCubic, mNowMs);
                tg.last = folded;
                tg.placed = true;
                break;
            }
            case Part::Chip:
            {
                const auto *devs = h.id == "master" ? &mModel.masterDevices : (strip(h.id) ? &strip(h.id)->devices : nullptr);
                if (!devs) break;
                const int n = (int)devs->size(), slots = chipSlots(h.id);
                if (h.index == slots - 1) { openAddEffect(h.id, world); break; }
                if (n + 1 > kSlots && h.index == slots - 2 && onMenu) // "+N": the rest of the rack
                {
                    std::vector<cosmo_v2::ContextMenu::Item> items;
                    for (const auto &d : *devs)
                    {
                        const std::string id = d.id;
                        items.push_back({d.label, [this, id] { if (onOpenDevice) onOpenDevice(id); }});
                    }
                    onMenu(items, world);
                    break;
                }
                const std::string dv = (*devs)[(size_t)h.index].id;
                if (onOpenDevice) onOpenDevice(dv); // its window (R-WIN-1): opened, or brought forward
                break;
            }
            case Part::Send:
            {
                const auto *s = strip(h.id);
                if (!s) break;
                const int n = (int)s->sends.size();
                if (h.index == 1 && n > 2) { setTab((int)mMixers.size()); break; } // "+N more": the matrix has them all
                if (h.index < n) openSend(s->sends[(size_t)h.index], h.id, world);
                break;
            }
            case Part::Cell:
            {
                const auto *s = strip(h.id);
                if (!s) break;
                const std::string out = s->out.empty() ? std::string("master") : s->out;
                if (out == h.to) break; // its main output: every strip has exactly one
                for (const auto &sd : s->sends)
                    if (sd.to == h.to) { openSend(sd, h.id, world); return true; }
                if (std::find(s->targets.begin(), s->targets.end(), h.to) != s->targets.end()) send("send add " + h.id + " --to " + h.to);
                break;
            }
            default:
                break;
            }
            return true;
        }
        case Gesture::Type::DoubleClick:
        {
            const Hit h = hitAt(local);
            if (h.part == Part::Fader) send(h.id == "master" ? "set project.masterGain=0" : "set " + h.id + ".gain=0");
            if (h.part == Part::Header) // an instrument strip's name: its instrument's window ("a window of that synth")
                if (const auto *s = strip(h.id))
                    if (s->kind == "instrument" && !s->devices.empty() && onOpenDevice) onOpenDevice(s->devices[0].id);
            if (h.part == Part::Pan) send("set " + h.id + ".pan=0");
            if (h.part == Part::Cell)
                if (const auto *s = strip(h.id))
                    if (std::find(s->targets.begin(), s->targets.end(), h.to) != s->targets.end()) send("route " + h.id + " --to " + h.to);
            return true;
        }
        case Gesture::Type::RightClick:
        {
            const Hit h = hitAt(local);
            if (h.part == Part::Tab && h.index < (int)mMixers.size() && onMenu)
            {
                const std::string id = mMixers[(size_t)h.index].id, name = mMixers[(size_t)h.index].name;
                const Point world = g.pos;
                onMenu({{"Rename\xE2\x80\xA6", [this, id, name, world] {
                             if (onRename) onRename(name, world, [this, id](const std::string &n) { send("set " + id + ".name=" + q(n)); });
                         }},
                        {"Delete mixer", [this, id] { send("mixer delete " + id); }}},
                       world);
            }
            else if (h.part != Part::None && h.part != Part::Fold && h.id != "master" && strip(h.id) && onMenu)
            {
                const solaris::StripModel &s = *strip(h.id);
                const std::string id = s.id, name = s.name;
                const Point world = g.pos;
                std::vector<cosmo_v2::ContextMenu::Item> items = {
                    {"Rename\xE2\x80\xA6", [this, id, name, world] {
                         if (onRename) onRename(name, world, [this, id](const std::string &n) { send("set " + id + ".name=" + q(n)); });
                     }}};
                // its clips re-linked to another line of its kind, all in one (R-MIX-14)
                std::vector<cosmo_v2::ContextMenu::Item> to;
                if (s.clipCount > 0)
                    for (const auto &o : mModel.strips)
                        if (o.id != id && o.kind == s.kind)
                        {
                            const std::string oid = o.id;
                            to.push_back({o.name, [this, id, oid] { send("strip relink " + id + " --to " + oid); }});
                        }
                if (!to.empty())
                    items.push_back({"Move its clips to \xE2\x96\xB8", [this, to, world] { if (onMenu) onMenu(to, world); }});
                // a key for a later strip's compressor — exactly the service's keyTargets (R-MIX-15)
                std::vector<cosmo_v2::ContextMenu::Item> keys;
                for (const auto &k : s.keyTargets)
                    if (const auto *ks = strip(k))
                        keys.push_back({ks->name, [this, id, k] { send("send add " + id + " --to " + k + " --sidechain"); }});
                if (!keys.empty())
                    items.push_back({"Sidechain to \xE2\x96\xB8", [this, keys, world] { if (onMenu) onMenu(keys, world); }});
                items.push_back({"Delete strip", [this, id] { send("strip delete " + id); }});
                onMenu(std::move(items), world);
            }
            return true;
        }
        case Gesture::Type::Scroll:
            if (onMatrix()) return g.shift ? mMxX.scrollBy(g.delta.y) : mMxY.scrollBy(g.delta.y);
            if (mScrollX.scrollable() && !g.shift) return mScrollX.scrollBy(g.delta.y);
            return mScrollY.scrollBy(g.delta.y);
        default:
            return Segment::handleGesture(g, local);
        }
    }

    // ── paint ────────────────────────────────────────────────────────────────────────────────

    void MixerDock::paintFader(IRenderTarget &t, const Rect &z, const Live &l, double a) const
    {
        const double trackX = z.x + z.w * 0.36, top = z.y + 6.0, bottom = z.bottom() - 6.0, span = bottom - top;
        // the scale: 0 dB a little brighter
        for (double db : {6.0, 0.0, -6.0, -12.0, -24.0, -48.0})
        {
            const double y = bottom - faderPos(db) * span;
            t.setStroke(fade(palette::whiteAlpha(db == 0.0 ? 0.3 : 0.12), a), 1.0);
            t.beginPath(); t.moveTo(trackX - 9.0, std::round(y) + 0.5); t.lineTo(trackX - 4.0, std::round(y) + 0.5); t.strokePath();
        }
        drawRoundedRect(t, Rect{trackX - 2.0, top, 4.0, span}, radius::pill(), Paint::filled(fade(palette::input(), a)));
        // the meters, L and R: green, amber above −12 dBFS, red above −3
        for (int c = 0; c < 2; ++c)
        {
            const double mx = z.x + z.w * 0.62 + c * 5.0;
            drawRoundedRect(t, Rect{mx, top, 3.0, span}, radius::hairline(), Paint::filled(fade(palette::whiteAlpha(0.05), a)));
            const double lv = l.meter[c].value();
            if (lv <= 0.001) continue;
            const double y12 = (48.0 / 66.0), y3 = (57.0 / 66.0);
            auto seg = [&](double from, double to, const Color &col) {
                const double f = std::min(lv, to);
                if (f <= from) return;
                drawRoundedRect(t, Rect{mx, bottom - f * span, 3.0, (f - from) * span}, radius::hairline(), Paint::filled(fade(col, a)));
            };
            seg(0.0, y12, surface::meterLow());
            seg(y12, y3, surface::meterMid());
            seg(y3, 1.0, surface::meterHigh());
        }
        // the thumb
        const double ty = bottom - l.fader.value() * span;
        drawRoundedRect(t, Rect{trackX - 10.0, ty - 4.5, 20.0, 9.0}, radius::control(), Paint::filledStroked(fade(palette::secondary(), a), fade(palette::foreground(), 0.8 * a), 1.0));
        t.setStroke(fade(palette::foreground(), a), 1.0);
        t.beginPath(); t.moveTo(trackX - 6.0, std::round(ty) + 0.5); t.lineTo(trackX + 6.0, std::round(ty) + 0.5); t.strokePath();
    }

    void MixerDock::paintCard(IRenderTarget &t, const solaris::StripModel &sm, double x, double w, double a) const
    {
        const solaris::StripModel *s = &sm;
        const std::string &id = sm.id;
        const Live *l = liveOf(id);
        if (!l || w < 1.0) return;
        const double by = bodyTop(), bh = bodyH();
        const Geo g = geoOf(x, w, by, bh);
        t.save();
        t.clipRect(x, kTabsH, w, height.value() - kTabsH); // a card narrowing (a fold, a removal) clips, never spills
        drawRoundedRect(t, Rect{x, by, w, bh}, 0.0, Paint::filled(fade(palette::card(), a)));
        t.setStroke(fade(palette::border(), a), 1.0);
        t.beginPath(); t.moveTo(x + w - 0.5, by); t.lineTo(x + w - 0.5, by + bh); t.strokePath();
        const double ca = a * (1.0 - 0.55 * l->dim.value()); // silenced by a solo, or muted: it recedes
        const Color col = lerpColor(l->colFrom, l->colTo, l->colT.value());
        drawRoundedRect(t, Rect{x + 6.5, by + 5.0, w - 13.0, 3.0}, radius::pill(), Paint::filled(fade(col, ca)));
        t.setFill(fade(palette::foreground(), ca));
        t.drawText(textfit::ellipsize(t, s->name, w - 13.0, 11.0, font::sansMedium()), x + 6.5, by + 21.0, 11.0, font::sansMedium());
        std::string sub = s->kind;
        if (s->kind == "audio" || s->kind == "instrument") sub += " \xC2\xB7 " + std::to_string(s->clipCount) + (s->clipCount == 1 ? " clip" : " clips");
        else sub += " \xC2\xB7 fed by " + std::to_string(s->fromStrips.size());
        t.setFill(fade(palette::mutedForeground(), ca));
        t.drawText(textfit::ellipsize(t, sub, w - 13.0, 9.0, font::sans()), x + 6.5, by + 33.0, 9.0, font::sans());

        // the rack
        const int n = (int)s->devices.size(), slots = chipSlots(id);
        for (int k = 0; k < slots; ++k)
        {
            const Rect r = g.chips[k];
            const double hv = mHover.amount(hoverId((int)Part::Chip, id + ">", k));
            std::string label;
            bool add = k == slots - 1, more = n + 1 > kSlots && k == slots - 2, bypass = false, open = false;
            if (add) label = "+ Effect";
            else if (more) label = "+" + std::to_string(n - (kSlots - 2)) + " more";
            else
            {
                const auto &d = s->devices[(size_t)k];
                label = d.label;
                bypass = d.bypass;
                open = isDeviceOpen && isDeviceOpen(d.id);
            }
            if (add)
                drawRoundedRect(t, r, radius::control(), Paint::stroked(fade(palette::whiteAlpha(0.14 + 0.2 * hv), ca), 1.0));
            else
                drawRoundedRect(t, r, radius::control(), Paint::filledStroked(fade(lerpColor(palette::secondary(), palette::popover(), hv), ca),
                                                                               fade(open ? palette::primary() : palette::whiteAlpha(0.0), ca), 1.0));
            t.setFill(fade(add || more || bypass ? palette::mutedForeground() : palette::foreground(), ca));
            t.drawText(textfit::ellipsize(t, label, r.w - 10.0, 10.0, font::sans()), r.x + 5.0, textfit::baseline(r.y + r.h * 0.5, 10.0), 10.0, font::sans());
        }
        // sends
        const int ns = (int)s->sends.size();
        for (int k = 0; k < std::min(ns, 2); ++k)
        {
            const Rect r = g.sends[k];
            const double hv = mHover.amount(hoverId((int)Part::Send, id + ">", k));
            if (hv > 0.001) drawRoundedRect(t, r, radius::control(), Paint::filled(palette::hoverWash(hv * ca)));
            if (k == 1 && ns > 2)
            {
                t.setFill(fade(palette::mutedForeground(), ca));
                t.drawText("+" + std::to_string(ns - 1) + " in the matrix", r.x + 3.0, textfit::baseline(r.y + r.h * 0.5, 9.0), 9.0, font::sans());
                continue;
            }
            const auto &sd = s->sends[(size_t)k];
            const std::string gain = num(sd.gain, 1) + (sd.pre ? " P" : "");
            const double gw = t.measureText(gain, 9.0, font::mono());
            // a sidechain key says so, in the solo amber: it is heard by a detector, not in the mix (R-MIX-15)
            arrowText(t, (sd.sidechain ? "key " : "") + labelOf(sd.to), r.x + 3.0, r.y + r.h * 0.5, r.w - gw - 10.0, 9.0, font::sans(),
                      fade(sd.sidechain ? surface::solo() : palette::secondaryForeground(), ca));
            t.setFill(fade(palette::mutedForeground(), ca));
            t.drawText(gain, r.right() - 3.0 - gw, textfit::baseline(r.y + r.h * 0.5, 9.0), 9.0, font::mono());
        }
        // pan: from the centre
        {
            const Rect r = g.pan;
            const double cy = r.y + r.h * 0.5, cx = r.x + r.w * 0.5, px = cx + l->pan.value() * r.w * 0.5;
            drawRoundedRect(t, Rect{r.x, cy - 1.5, r.w, 3.0}, radius::pill(), Paint::filled(fade(palette::input(), ca)));
            drawRoundedRect(t, Rect{std::min(cx, px), cy - 1.5, std::fabs(px - cx), 3.0}, radius::pill(), Paint::filled(fade(palette::primary(), ca)));
            t.setStroke(fade(palette::whiteAlpha(0.3), ca), 1.0);
            t.beginPath(); t.moveTo(std::round(cx) + 0.5, cy - 4.0); t.lineTo(std::round(cx) + 0.5, cy + 4.0); t.strokePath();
            drawCircle(t, px, cy, 4.0, Paint::filled(fade(palette::foreground(), ca)));
        }
        paintFader(t, g.zone, *l, ca);
        const std::string rd = dbText(faderDb(l->fader.value()));
        t.setFill(fade(palette::secondaryForeground(), ca));
        t.drawText(rd, x + (w - t.measureText(rd, 10.0, font::mono())) * 0.5, g.readoutY, 10.0, font::mono());
        // mute and solo: the fill eases whoever changed them; solo is amber, never the accent
        auto toggle = [&](const Rect &r, const char *label, double on, const Color &onCol, int part) {
            const double hv = mHover.amount(hoverId(part, id + ">", -1));
            drawRoundedRect(t, r, radius::control(), Paint::filled(fade(lerpColor(lerpColor(palette::secondary(), palette::popover(), hv), onCol, on), a)));
            t.setFill(fade(lerpColor(palette::secondaryForeground(), palette::background(), on), a));
            t.drawText(label, r.x + (r.w - t.measureText(label, 10.0, font::sansMedium())) * 0.5, textfit::baseline(r.y + r.h * 0.5, 10.0), 10.0, font::sansMedium());
        };
        toggle(g.mute, "M", l->mute.value(), palette::whiteAlpha(0.82), (int)Part::Mute);
        toggle(g.solo, "S", l->solo.value(), surface::solo(), (int)Part::Solo);
        // where it goes
        {
            const Rect r = g.out;
            const double hv = mHover.amount(hoverId((int)Part::Out, id + ">", -1));
            drawRoundedRect(t, r, radius::control(), Paint::filled(fade(lerpColor(palette::input(), palette::popover(), hv), ca)));
            arrowText(t, labelOf(s->out), r.x + 5.0, r.y + r.h * 0.5, r.w - 10.0, 10.0, font::sans(), fade(palette::secondaryForeground(), ca));
        }
        t.restore();
    }

    void MixerDock::paintHeader(IRenderTarget &t, const std::string &bus, const std::string &mixer, double x, double w, double a) const
    {
        if (w < 1.0) return;
        const double by = bodyTop(), bh = bodyH();
        const double f = foldAmount(bus);
        const auto *b = strip(bus);
        const Live *l = liveOf(bus);
        int n = 0; // the group: the strips on THIS page feeding the bus
        for (const auto &s : mModel.strips)
            if (s.out == bus && s.mixer == mixer) ++n;
        t.save();
        t.clipRect(x, kTabsH, w, height.value() - kTabsH);
        const double hv = mHover.amount(hoverId((int)Part::Fold, bus + ">", -1));
        drawRoundedRect(t, Rect{x, by, w, bh}, 0.0, Paint::filled(fade(lerpColor(palette::muted(), palette::card(), hv), a)));
        t.setStroke(fade(palette::border(), a), 1.0);
        t.beginPath(); t.moveTo(x + w - 0.5, by); t.lineTo(x + w - 0.5, by + bh); t.strokePath();
        const Color col = l ? lerpColor(l->colFrom, l->colTo, l->colT.value()) : palette::secondaryForeground();
        drawRoundedRect(t, Rect{x + 6.5, by + 5.0, w - 13.0, 3.0}, radius::pill(), Paint::filled(fade(col, a)));
        chevron(t, x + kFoldW * 0.5, by + 22.0, 1.0 - f, fade(palette::secondaryForeground(), a));
        const std::string count = std::to_string(n);
        t.setFill(fade(palette::mutedForeground(), a));
        t.drawText(count, x + (kFoldW - t.measureText(count, 10.0, font::mono())) * 0.5, by + 44.0, 10.0, font::mono());
        if (f > 0.01) // folded: it says what it holds
        {
            const double tx = x + kFoldW - 4.0, tw = std::max(0.0, w - kFoldW - 2.0);
            arrowText(t, b ? b->name : bus, tx, by + 22.0, tw, 11.0, font::sansMedium(), fade(palette::foreground(), a * f));
            t.setFill(fade(palette::mutedForeground(), a * f));
            t.drawText(textfit::ellipsize(t, std::to_string(n) + " strips", tw, 9.0, font::sans()), tx, by + 39.0, 9.0, font::sans());
        }
        t.restore();
    }

    void MixerDock::paintMaster(IRenderTarget &t, double a) const
    {
        const Live *l = liveOf("master");
        if (!l || a <= 0.001) return;
        const double x = stripsRight(), w = kMasterW, by = bodyTop(), bh = bodyH();
        const Geo g = geoOf(x, w, by, bh);
        t.save();
        t.clipRect(x, kTabsH, w, height.value() - kTabsH);
        drawRoundedRect(t, Rect{x, by, w, bh}, 0.0, Paint::filled(fade(palette::popover(), a)));
        t.setStroke(fade(palette::border(), a), 1.0);
        t.beginPath(); t.moveTo(x + 0.5, by); t.lineTo(x + 0.5, by + bh); t.strokePath();
        t.setFill(fade(palette::foreground(), a));
        t.drawText("Master", x + 6.5, by + 21.0, 11.0, font::sansMedium());
        t.setFill(fade(palette::mutedForeground(), a));
        const size_t nd = mModel.masterDevices.size();
        t.drawText(nd ? std::to_string(nd) + (nd == 1 ? " device" : " devices") : std::string("the mix"), x + 6.5, by + 33.0, 9.0, font::sans());
        const int n = (int)mModel.masterDevices.size(), slots = chipSlots("master");
        for (int k = 0; k < slots; ++k)
        {
            const Rect r = g.chips[k];
            const double hv = mHover.amount(hoverId((int)Part::Chip, "master>", k));
            const bool add = k == slots - 1, more = n + 1 > kSlots && k == slots - 2;
            const std::string label = add ? "+ Effect" : more ? "+" + std::to_string(n - (kSlots - 2)) + " more" : mModel.masterDevices[(size_t)k].label;
            if (add) drawRoundedRect(t, r, radius::control(), Paint::stroked(fade(palette::whiteAlpha(0.14 + 0.2 * hv), a), 1.0));
            else drawRoundedRect(t, r, radius::control(), Paint::filled(fade(lerpColor(palette::secondary(), palette::card(), hv), a)));
            t.setFill(fade(add || more ? palette::mutedForeground() : palette::foreground(), a));
            t.drawText(textfit::ellipsize(t, label, r.w - 10.0, 10.0, font::sans()), r.x + 5.0, textfit::baseline(r.y + r.h * 0.5, 10.0), 10.0, font::sans());
        }
        paintFader(t, g.zone, *l, a);
        const std::string rd = dbText(faderDb(l->fader.value()));
        t.setFill(fade(palette::secondaryForeground(), a));
        t.drawText(rd, x + (w - t.measureText(rd, 10.0, font::mono())) * 0.5, g.readoutY, 10.0, font::mono());
        std::string outs;
        for (const auto &o : mModel.masterOut) outs += (outs.empty() ? "" : ", ") + labelOf(o);
        arrowText(t, outs.empty() ? std::string("no port") : outs, x + 6.5, g.out.y + g.out.h * 0.5, w - 13.0, 10.0, font::sans(), fade(palette::mutedForeground(), a));
        t.restore();
    }

    void MixerDock::paintPage(IRenderTarget &t, const std::string &key, double alpha) const
    {
        const auto it = mPages.find(key);
        if (it == mPages.end()) return;
        const Page &pg = it->second;
        const bool shown = key == pageKey(mTab) && !onMatrix();
        double cx = -(shown ? mScrollX.value() : pg.sx);
        t.save();
        t.clipRect(0, kTabsH, stripsRight(), height.value() - kTabsH);
        for (const auto &c : pg.cards)
        {
            const double cw = cardWidth(c);
            if (cx + cw > 0.0 && cx < stripsRight() && cw >= 1.0)
            {
                if (c.header) paintHeader(t, c.id, key, cx, cw, alpha);
                else paintCard(t, c.snap, cx, cw, alpha);
            }
            cx += cw;
        }
        // "+ Line": after the last card, sliding with the cards as they grow and shrink (R-MIX-13)
        const Rect add = addLineAt(cx, bodyTop());
        if (add.x < stripsRight() && alpha > 0.001)
        {
            const double hv = shown ? mHover.amount(hoverId((int)Part::AddLine, ">", -1)) : 0.0;
            drawRoundedRect(t, add, radius::control(), Paint::stroked(fade(palette::whiteAlpha(0.14 + 0.2 * hv), alpha), 1.0));
            t.setFill(fade(lerpColor(palette::secondaryForeground(), palette::foreground(), hv), alpha));
            t.drawText("+ Line", add.x + (add.w - t.measureText("+ Line", 10.0, font::sans())) * 0.5, textfit::baseline(add.y + add.h * 0.5, 10.0), 10.0,
                       font::sans());
        }
        if (pg.cards.empty() && alpha > 0.001)
        {
            const double tx = add.right() + space::padX() * 2.0;
            t.setFill(fade(palette::mutedForeground(), alpha));
            t.drawText("No strips on this mixer.", tx, kTabsH + 34.0, 11.0, font::sans());
            t.setFill(fade(palette::mutedForeground(), 0.75 * alpha));
            t.drawText(textfit::ellipsize(t, "\"+ Line\" adds one here; a strip moved to this mixer shows up too.", std::max(0.0, stripsRight() - tx - 13.0), 10.0,
                                          font::sans()),
                       tx, kTabsH + 52.0, 10.0, font::sans());
        }
        t.restore();
    }

    void MixerDock::paintMatrix(IRenderTarget &t, double a) const
    {
        if (a <= 0.001) return;
        const double W = width.value(), H = height.value();
        std::vector<std::string> cols;
        for (const auto &s : mModel.strips)
            if (!mMixers.empty() && s.mixer != mMixers.front().id) cols.push_back(s.id);
        cols.push_back("master");
        for (const auto &p : mModel.ports)
            if (p.dir == "out") cols.push_back(p.id);
        const double x0 = kRowHeadW - mMxX.value(), y0 = kTabsH + kColHeadH - mMxY.value();
        // the cells
        t.save();
        t.clipRect(kRowHeadW, kTabsH + kColHeadH, W - kRowHeadW, H - kTabsH - kColHeadH);
        int row = 0;
        for (const auto &mx : mMixers)
        {
            const double sy = y0 + row * kCellH;
            drawRoundedRect(t, Rect{kRowHeadW, sy, W - kRowHeadW, kCellH}, 0.0, Paint::filled(fade(palette::muted(), a)));
            ++row;
            for (const auto &id : mx.strips)
            {
                const auto *s = strip(id);
                const double y = y0 + row * kCellH;
                ++row;
                if (!s || y + kCellH < kTabsH + kColHeadH || y > H) continue;
                for (size_t c = 0; c < cols.size(); ++c)
                {
                    const Rect r{x0 + c * kCellW, y, kCellW, kCellH};
                    if (r.right() < kRowHeadW || r.x > W) continue;
                    const std::string to = cols[c];
                    const bool allowed = std::find(s->targets.begin(), s->targets.end(), to) != s->targets.end();
                    t.setStroke(fade(palette::border(), a), 1.0);
                    t.beginPath(); t.moveTo(r.right() - 0.5, r.y); t.lineTo(r.right() - 0.5, r.bottom()); t.moveTo(r.x, r.bottom() - 0.5); t.lineTo(r.right(), r.bottom() - 0.5); t.strokePath();
                    if (!allowed)
                    {
                        // hatched: routing cannot point backward (R-MIX-4)
                        t.save();
                        t.clipRect(r.x, r.y, r.w - 1.0, r.h - 1.0);
                        t.setStroke(fade(palette::whiteAlpha(0.035), a), 1.0);
                        for (double k = -r.h; k < r.w; k += 6.5) { t.beginPath(); t.moveTo(r.x + k, r.bottom()); t.lineTo(r.x + k + r.h, r.y); t.strokePath(); }
                        t.restore();
                        continue;
                    }
                    const double hv = mHover.amount(hoverId((int)Part::Cell, id + ">" + to, -1));
                    if (hv > 0.001) drawRoundedRect(t, Rect{r.x + 1.0, r.y + 1.0, r.w - 3.0, r.h - 3.0}, radius::control(), Paint::filled(palette::hoverWash(hv * a)));
                    const auto mi = mCells.find("m:" + id + ">" + to), si = mCells.find("s:" + id + ">" + to);
                    const double main = mi == mCells.end() ? 0.0 : mi->second.a.value(), snd = si == mCells.end() ? 0.0 : si->second.a.value();
                    if (main > 0.001) drawCircle(t, r.x + r.w * 0.5, r.y + r.h * 0.5, 3.0 + 1.0 * main, Paint::filled(fade(palette::primary(), a * main)));
                    if (snd > 0.001)
                        for (const auto &sd : s->sends)
                            if (sd.to == to)
                            {
                                const std::string txt = num(sd.gain, 1) + (sd.pre ? " P" : "");
                                t.setFill(fade(palette::foreground(), a * snd));
                                t.drawText(txt, r.x + (r.w - t.measureText(txt, 10.0, font::mono())) * 0.5, textfit::baseline(r.y + r.h * 0.5, 10.0), 10.0, font::mono());
                            }
                }
            }
        }
        t.restore();
        // the row headers: mixer sections, then strips
        drawRoundedRect(t, Rect{0, kTabsH, kRowHeadW, H - kTabsH}, 0.0, Paint::filled(fade(surface::headerBg(), a)));
        t.save();
        t.clipRect(0, kTabsH + kColHeadH, kRowHeadW, H - kTabsH - kColHeadH);
        row = 0;
        for (const auto &mx : mMixers)
        {
            const double sy = y0 + row * kCellH;
            ++row;
            std::string label = mx.name;
            for (auto &ch : label) ch = (char)std::toupper((unsigned char)ch);
            t.setFill(fade(palette::mutedForeground(), a));
            t.drawText(textfit::ellipsize(t, label, kRowHeadW - 2.0 * space::padX(), 9.0, font::sansSemiBold()), space::padX(),
                       textfit::baseline(sy + kCellH * 0.5, 9.0), 9.0, font::sansSemiBold(), 0.13 * 9.0);
            for (const auto &id : mx.strips)
            {
                const auto *s = strip(id);
                const Live *l = liveOf(id);
                const double y = y0 + row * kCellH;
                ++row;
                if (!s) continue;
                if (l) drawRoundedRect(t, Rect{space::padX(), y + 7.0, 3.0, kCellH - 14.0}, radius::pill(), Paint::filled(fade(lerpColor(l->colFrom, l->colTo, l->colT.value()), a)));
                t.setFill(fade(palette::foreground(), a));
                t.drawText(textfit::ellipsize(t, s->name, kRowHeadW - 2.0 * space::padX() - 8.0, 11.0, font::sans()), space::padX() + 8.0,
                           textfit::baseline(y + kCellH * 0.5, 11.0), 11.0, font::sans());
            }
        }
        t.restore();
        // the column headers
        drawRoundedRect(t, Rect{kRowHeadW, kTabsH, W - kRowHeadW, kColHeadH}, 0.0, Paint::filled(fade(surface::headerBg(), a)));
        t.save();
        t.clipRect(kRowHeadW, kTabsH, W - kRowHeadW, kColHeadH);
        for (size_t c = 0; c < cols.size(); ++c)
        {
            const double x = x0 + c * kCellW;
            if (x + kCellW < kRowHeadW || x > W) continue;
            const std::string label = labelOf(cols[c]);
            t.setFill(fade(cols[c] == "master" ? palette::foreground() : palette::secondaryForeground(), a));
            t.drawText(textfit::ellipsize(t, label, kCellW - 8.0, 10.0, font::sans()), x + 4.0, kTabsH + kColHeadH - 10.0, 10.0, font::sans());
        }
        t.restore();
        t.setStroke(fade(palette::border(), a), 1.0);
        t.beginPath(); t.moveTo(0, kTabsH + kColHeadH - 0.5); t.lineTo(W, kTabsH + kColHeadH - 0.5); t.strokePath();
        t.beginPath(); t.moveTo(kRowHeadW - 0.5, kTabsH); t.lineTo(kRowHeadW - 0.5, H); t.strokePath();
        mMxY.drawBar(t, W - 5.0, a);
    }

    void MixerDock::onPaint(IRenderTarget &t) const
    {
        const double W = width.value(), H = height.value();
        drawRoundedRect(t, Rect{0, 0, W, H}, 0.0, Paint::filled(surface::stageBg()));
        // the pages: each fades as one layer, so a cross-fade never double-draws a half-visible card
        for (const auto &kv : mPages)
        {
            const double a = kv.second.alpha.value();
            if (a <= 0.001) continue;
            t.pushLayer(a);
            if (kv.first.empty()) paintMatrix(t, 1.0);
            else paintPage(t, kv.first, 1.0);
            t.popLayer();
        }
        const auto mp = mPages.find("");
        const double masterA = 1.0 - (mp == mPages.end() ? 0.0 : mp->second.alpha.value());
        paintMaster(t, masterA);
        // the strips' horizontal bar, only when there is somewhere to scroll (R6)
        if (!onMatrix() && mScrollX.scrollable())
        {
            const double view = stripsRight(), content = mScrollX.contentH();
            const double tw = std::max(24.0, view * view / content), tx = (view - tw) * (mScrollX.value() / mScrollX.maxScroll());
            drawRoundedRect(t, Rect{tx + 2.0, H - 5.0, tw - 4.0, 3.0}, radius::pill(), Paint::filled(palette::whiteAlpha(0.18)));
        }

        // the tab bar
        drawRoundedRect(t, Rect{0, 0, W, kTabsH}, 0.0, Paint::filled(surface::headerBg()));
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0, 0.5); t.lineTo(W, 0.5); t.strokePath();
        t.beginPath(); t.moveTo(0, kTabsH - 0.5); t.lineTo(W, kTabsH - 0.5); t.strokePath();
        if (mHiW.value() > 0.5)
            drawRoundedRect(t, Rect{mHiX.value(), 4.0, mHiW.value(), kTabsH - 8.0}, radius::hairline(), Paint::filled(palette::secondary()));
        for (const auto &tb : mTabs)
        {
            const double ia = tb.in.value();
            if (ia <= 0.001) continue;
            const double x = tb.placed ? tb.x.value() : tb.xWant;
            if (tb.key == "+")
            {
                const Rect r{x, 4.0, kTabsH - 8.0, kTabsH - 8.0};
                const double hv = mHover.amount(hoverId((int)Part::AddMixer, "", -1));
                if (hv > 0.001) drawRoundedRect(t, r, radius::hairline(), Paint::filled(palette::hoverWash(hv * ia)));
                const Color c = fade(lerpColor(palette::mutedForeground(), palette::foreground(), hv), ia);
                glyph::line(t, r.x + r.w * 0.5, r.y + 6.0, r.x + r.w * 0.5, r.bottom() - 6.0, c, 1.3);
                glyph::line(t, r.x + 6.0, r.y + r.h * 0.5, r.right() - 6.0, r.y + r.h * 0.5, c, 1.3);
                continue;
            }
            const bool cur = !tb.gone && tb.key == (onMatrix() ? std::string() : pageKey(mTab));
            // measured in the WIDER weight whichever is shown: a tab's place must not depend on which tab is
            // chosen, or choosing one moves its neighbours and retargets the highlight mid-slide
            mTabTextW[tb.label] = t.measureText(tb.label, 10.0, font::sansMedium());
            int ti = -1;
            for (int k = 0; k < tabCount(); ++k)
                if ((k == (int)mMixers.size() ? std::string() : pageKey(k)) == tb.key) ti = k;
            const double hv = ti >= 0 ? mHover.amount(hoverId((int)Part::Tab, "", ti)) : 0.0;
            t.setFill(fade(cur ? palette::foreground() : lerpColor(palette::mutedForeground(), palette::foreground(), 0.5 * hv), ia));
            t.drawText(tb.label, x + space::padX(), textfit::baseline(kTabsH * 0.5, 10.0), 10.0, cur ? font::sansMedium() : font::sans());
        }
        // the chevron: fold the dock away / bring it back
        {
            const Rect r = toggleRect();
            const double hv = mHover.amount(hoverId((int)Part::ToggleDock, "", -1));
            if (hv > 0.001) drawRoundedRect(t, r, radius::hairline(), Paint::filled(palette::hoverWash(hv)));
            const bool low = H < kTabsH + 40.0; // folded away: it points up
            const Color c = lerpColor(palette::mutedForeground(), palette::foreground(), hv);
            const double cx = r.x + r.w * 0.5, cy = r.y + r.h * 0.5, d = low ? -1.0 : 1.0;
            glyph::line(t, cx - 4.0, cy - 2.0 * d, cx, cy + 2.0 * d, c, 1.3);
            glyph::line(t, cx, cy + 2.0 * d, cx + 4.0, cy - 2.0 * d, c, 1.3);
        }
        if (!onMatrix()) mScrollY.drawBar(t, stripsRight() - 5.0);
    }
}
}
