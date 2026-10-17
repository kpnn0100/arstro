#include "SessionView.h"
#include "../../../interstellar/app/widgets/Glyphs.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;
    namespace glyph = interstellar_v1::glyph;

    namespace
    {
        Color fade(Color c, double a) { c.a *= a; return c; }
        constexpr double kPulseMs = 500.0; // a queued slot's pulse: a timing (it waits on the bar), not a tween
        // hover ids: a cell, a scene's ▶, a column's ■, the two buttons
        constexpr int kBack = 1, kAddScene = 2, kCellBase = 1000, kSceneBase = 100000, kStopBase = 200000;
        std::string quoted(std::string v)
        {
            for (auto &c : v)
                if (c == '"') c = '\'';
            return v.find_first_of(" \t;=#") == std::string::npos && !v.empty() ? v : "\"" + v + "\"";
        }
    }

    SessionView::SessionView() {}

    void SessionView::bind(const solaris::AppModel &m)
    {
        mCols.clear();
        std::vector<const solaris::StripModel *> s;
        for (const auto &x : m.strips)
            if (x.kind == "instrument" || x.kind == "audio") s.push_back(&x);
        // processing order: the mixers' order, then each strip's
        std::stable_sort(s.begin(), s.end(), [&](const solaris::StripModel *a, const solaris::StripModel *b) {
            int ma = 0, mb = 0;
            for (size_t i = 0; i < m.mixers.size(); ++i)
            {
                if (m.mixers[i].id == a->mixer) ma = m.mixers[i].order;
                if (m.mixers[i].id == b->mixer) mb = m.mixers[i].order;
            }
            return ma != mb ? ma < mb : a->order < b->order;
        });
        for (const auto *x : s) mCols.push_back(*x);
        mScenes = m.scenes;
        mSlots = m.slots;
        mPatterns = m.patterns;
        mClips = m.clips;
        mLaunched = m.transport.launched;
    }

    int SessionView::colOf(const std::string &strip) const
    {
        for (size_t i = 0; i < mCols.size(); ++i)
            if (mCols[i].id == strip) return (int)i;
        return -1;
    }

    int SessionView::rowOf(const std::string &scene) const
    {
        for (size_t i = 0; i < mScenes.size(); ++i)
            if (mScenes[i].id == scene) return (int)i;
        return -1;
    }

    const solaris::SlotModel *SessionView::slotAt(const std::string &scene, const std::string &strip) const
    {
        for (const auto &x : mSlots)
            if (x.scene == scene && x.strip == strip) return &x;
        return nullptr;
    }

    Rect SessionView::cellRect(const std::string &scene, const std::string &strip) const
    {
        const int c = colOf(strip), r = rowOf(scene);
        if (c < 0 || r < 0) return Rect{};
        return Rect{kSceneW + c * kColW - mScrollX.value() + 3.0, kHeadH + r * kRowH - mScrollY.value() + 3.0, kColW - 6.0, kRowH - 6.0};
    }

    Rect SessionView::sceneLaunchRect(const std::string &scene) const
    {
        const int r = rowOf(scene);
        if (r < 0) return Rect{};
        return Rect{kSceneW - 32.0, kHeadH + r * kRowH - mScrollY.value() + (kRowH - 24.0) * 0.5, 24.0, 24.0};
    }

    Rect SessionView::stopRect(const std::string &strip) const
    {
        const int c = colOf(strip);
        if (c < 0) return Rect{};
        return Rect{kSceneW + c * kColW - mScrollX.value() + (kColW - 28.0) * 0.5, kHeadH + mScenes.size() * kRowH - mScrollY.value() + 6.0, 28.0, 24.0};
    }

    Rect SessionView::backRect() const { return Rect{8.0, 4.0, kSceneW - 16.0, kHeadH - 8.0}; }

    Rect SessionView::addSceneRect() const
    {
        return Rect{8.0, kHeadH + mScenes.size() * kRowH - mScrollY.value() + 6.0, kSceneW - 48.0, 24.0};
    }

    double SessionView::playAmount(const std::string &slot) const
    {
        const auto it = mLive.find(slot);
        return it == mLive.end() ? 0.0 : it->second.play.value();
    }

    double SessionView::queuedAmount(const std::string &slot) const
    {
        const auto it = mLive.find(slot);
        return it == mLive.end() ? 0.0 : it->second.queued.value();
    }

    void SessionView::advance(double nowMs)
    {
        mNowMs = nowMs;
        // each slot's state, from what the service says plays — eased in and out, keyed by its id
        for (const auto &sl : mSlots)
        {
            bool playing = false, queued = false;
            for (const auto &l : mLaunched)
                if (l.strip == sl.strip)
                {
                    playing = l.session && l.slot == sl.id; // until what is pending lands
                    queued = l.next == sl.id;
                }
            Live &v = mLive[sl.id];
            if (!v.placed)
            {
                v.play.set(playing ? 1.0 : 0.0);
                v.queued.set(queued ? 1.0 : 0.0);
                v.playWas = playing;
                v.queuedWas = queued;
                v.placed = true;
            }
            if (playing != v.playWas) { v.play.animateTo(playing ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs); v.playWas = playing; }
            if (queued != v.queuedWas) { v.queued.animateTo(queued ? 1.0 : 0.0, motion::kSelectMs, Easing::EaseOutCubic, nowMs); v.queuedWas = queued; }
            v.play.update(nowMs);
            v.queued.update(nowMs);
        }
        // the grid's extent, measured every frame (R6): what is beyond the view scrolls
        const double W = width.value(), H = height.value();
        mScrollX.setExtent(kSceneW, std::max(0.0, W - kSceneW), mCols.size() * kColW);
        mScrollY.setExtent(kHeadH, std::max(0.0, H - kHeadH), mScenes.size() * kRowH + kFootH);
        mScrollX.advance(nowMs);
        mScrollY.advance(nowMs);
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    int SessionView::hoverId(const Point &p) const
    {
        if (backRect().contains(p)) return kBack;
        if (addSceneRect().contains(p) && p.y > kHeadH) return kAddScene;
        for (size_t r = 0; r < mScenes.size(); ++r)
            if (p.y > kHeadH && sceneLaunchRect(mScenes[r].id).contains(p)) return kSceneBase + (int)r;
        for (size_t c = 0; c < mCols.size(); ++c)
        {
            if (p.x < kSceneW) break;
            if (p.y > kHeadH && stopRect(mCols[c].id).contains(p)) return kStopBase + (int)c;
            for (size_t r = 0; r < mScenes.size(); ++r)
                if (p.y > kHeadH && cellRect(mScenes[r].id, mCols[c].id).contains(p)) return kCellBase + (int)(r * mCols.size() + c);
        }
        return -1;
    }

    void SessionView::emptyCellMenu(const std::string &scene, const solaris::StripModel &strip, Point world)
    {
        // what a cell can hold: an instrument's — any pattern of the song; an audio strip's — its own clips' files
        std::vector<cosmo_v2::ContextMenu::Item> items;
        if (strip.kind == "instrument")
            for (const auto &pt : mPatterns)
                items.push_back({pt.name.empty() ? pt.id : pt.name, [this, scene, strip = strip.id, id = pt.id] { send("slot add " + scene + " " + strip + " --pattern " + id); }});
        else
            for (const auto &c : mClips)
                if (c.track == strip.id && c.kind == "audio")
                    items.push_back({c.name.empty() ? c.id : c.name, [this, scene, strip = strip.id, id = c.id] { send("slot add " + scene + " " + strip + " --clip " + id); }});
        if (items.empty()) items.push_back({strip.kind == "instrument" ? "(the song has no patterns yet)" : "(no clips on this strip yet)", [] {}});
        if (onMenu) onMenu(std::move(items), world);
    }

    bool SessionView::handleGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Move:
            mHover.setHovered(hoverId(local));
            return true;
        case Gesture::Type::Scroll:
            // the wheel: up and down; Shift: sideways — a grid that cannot move lets it go on
            if (g.shift) return mScrollX.scrollBy(g.delta.y != 0 ? g.delta.y : g.delta.x);
            return mScrollY.scrollBy(g.delta.y);
        case Gesture::Type::Click:
        case Gesture::Type::RightClick:
        {
            const bool right = g.type == Gesture::Type::RightClick;
            const int id = hoverId(local);
            if (id == kBack) { send("launch back"); return true; }
            if (id == kAddScene) { send("scene add"); return true; }
            if (id >= kStopBase) { send("launch stop " + mCols[(size_t)(id - kStopBase)].id); return true; }
            if (id >= kSceneBase)
            {
                const auto &sc = mScenes[(size_t)(id - kSceneBase)];
                if (!right) { send("launch " + sc.id); return true; }
            }
            // a scene's name, right-clicked: its menu
            if (local.x < kSceneW && local.y > kHeadH)
            {
                const int r = (int)std::floor((local.y + mScrollY.value() - kHeadH) / kRowH);
                if (right && r >= 0 && r < (int)mScenes.size())
                {
                    const auto sc = mScenes[(size_t)r];
                    std::vector<cosmo_v2::ContextMenu::Item> items;
                    items.push_back({say("Launch Scene"), [this, sc] { send("launch " + sc.id); }});
                    items.push_back({say("Rename\xE2\x80\xA6"), [this, sc, at = g.pos] {
                                         if (onRename) onRename(sc.name, at, [this, id = sc.id](const std::string &n) { if (!n.empty()) send("set " + id + ".name=" + quoted(n)); });
                                     }});
                    items.push_back({say("Delete Scene"), [this, sc] { send("scene delete " + sc.id); }});
                    if (onMenu) onMenu(std::move(items), g.pos);
                }
                return true;
            }
            if (id >= kCellBase)
            {
                const size_t k = (size_t)(id - kCellBase), r = k / mCols.size(), c = k % mCols.size();
                const auto &sc = mScenes[r];
                const auto &st = mCols[c];
                const solaris::SlotModel *sl = slotAt(sc.id, st.id);
                if (!sl) { emptyCellMenu(sc.id, st, g.pos); return true; }
                if (!right) { send("launch " + sl->id); return true; }
                const solaris::SlotModel s = *sl;
                std::vector<cosmo_v2::ContextMenu::Item> items;
                items.push_back({say("Launch"), [this, s] { send("launch " + s.id); }});
                items.push_back({say("Rename\xE2\x80\xA6"), [this, s, at = g.pos] {
                                     if (onRename) onRename(s.name, at, [this, id = s.id](const std::string &n) { if (!n.empty()) send("set " + id + ".name=" + quoted(n)); });
                                 }});
                items.push_back({say("Delete Slot"), [this, s] { send("slot delete " + s.id); }});
                if (onMenu) onMenu(std::move(items), g.pos);
                return true;
            }
            return true;
        }
        default:
            return Segment::handleGesture(g, local);
        }
    }

    void SessionView::onPaint(IRenderTarget &t) const
    {
        const double W = width.value(), H = height.value();
        drawRoundedRect(t, Rect{0, 0, W, H}, 0.0, Paint::filled(palette::canvasBg()));
        auto colourOf = [](const solaris::StripModel &s) { return s.colour >= 0 ? surface::track(s.colour) : palette::secondaryForeground(); };
        const double sx = mScrollX.value(), sy = mScrollY.value();

        // the cells — clipped to the grid, scrolled both ways
        t.save();
        t.clipRect(kSceneW, kHeadH, std::max(0.0, W - kSceneW), std::max(0.0, H - kHeadH));
        for (size_t c = 0; c < mCols.size(); ++c)
        {
            const auto &st = mCols[c];
            const double x = kSceneW + c * kColW - sx;
            if (x + kColW < kSceneW || x > W) continue;
            const Color col = colourOf(st);
            for (size_t r = 0; r < mScenes.size(); ++r)
            {
                const Rect cr = cellRect(mScenes[r].id, st.id);
                if (cr.bottom() < kHeadH || cr.y > H) continue;
                const double hv = mHover.amount(kCellBase + (int)(r * mCols.size() + c));
                const solaris::SlotModel *sl = slotAt(mScenes[r].id, st.id);
                if (!sl)
                {
                    drawRoundedRect(t, cr, radius::control(), Paint::filledStroked(palette::whiteAlpha(0.02 + 0.04 * hv), palette::border(), 1.0));
                    if (hv > 0.001)
                    {
                        t.setFill(fade(palette::mutedForeground(), hv));
                        t.drawText("+", cr.x + (cr.w - t.measureText("+", 12.0, font::sans())) * 0.5, textfit::baseline(cr.y + cr.h * 0.5, 12.0), 12.0, font::sans());
                    }
                    continue;
                }
                const auto lv = mLive.find(sl->id);
                const double play = lv == mLive.end() ? 0.0 : lv->second.play.value(), queued = lv == mLive.end() ? 0.0 : lv->second.queued.value();
                drawRoundedRect(t, cr, radius::control(),
                                Paint::filled(lerpColor(lerpColor(palette::secondary(), palette::card(), hv), fade(col, 0.45), play)));
                // queued: an outline in its colour, pulsing until it lands
                if (queued > 0.001)
                {
                    const double pulse = 0.5 + 0.5 * std::sin(mNowMs / kPulseMs * 2.0 * M_PI);
                    t.setStroke(fade(col, queued * (0.35 + 0.65 * pulse)), 1.5);
                    t.beginPath();
                    t.moveTo(cr.x + 0.75, cr.y + 0.75); t.lineTo(cr.right() - 0.75, cr.y + 0.75); t.lineTo(cr.right() - 0.75, cr.bottom() - 0.75);
                    t.lineTo(cr.x + 0.75, cr.bottom() - 0.75); t.lineTo(cr.x + 0.75, cr.y + 0.75);
                    t.strokePath();
                }
                // ▶, then its name
                glyph::play(t, Rect{cr.x + 8.0, cr.y + (cr.h - 9.0) * 0.5, 9.0, 9.0}, lerpColor(palette::mutedForeground(), palette::foreground(), std::max(play, hv)));
                std::string name = sl->name;
                if (name.empty())
                    for (const auto &pt : mPatterns)
                        if (pt.id == sl->pattern) name = pt.name;
                if (name.empty()) name = sl->pattern.empty() ? sl->src.substr(sl->src.find_last_of('/') == std::string::npos ? 0 : sl->src.find_last_of('/') + 1) : sl->pattern;
                t.setFill(palette::foreground());
                t.drawText(textfit::ellipsize(t, name, std::max(0.0, cr.w - 30.0), 11.0, font::sans()), cr.x + 24.0, textfit::baseline(cr.y + cr.h * 0.5, 11.0), 11.0, font::sans());
                // playing: its loop's progress along its foot — the model's, frame by frame, like the playhead
                if (play > 0.001)
                    for (const auto &l : mLaunched)
                        if (l.strip == st.id && l.slot == sl->id && l.session)
                            drawRoundedRect(t, Rect{cr.x + 3.0, cr.bottom() - 4.0, std::max(0.0, (cr.w - 6.0) * std::clamp(l.progress, 0.0, 1.0)), 2.0},
                                            radius::pill(), Paint::filled(fade(col, play)));
            }
            // its ■, under the last scene
            const Rect sr = stopRect(st.id);
            const double hv = mHover.amount(kStopBase + (int)c);
            drawRoundedRect(t, sr, radius::control(), Paint::filled(palette::hoverWash(0.4 + 0.6 * hv)));
            drawRoundedRect(t, Rect{sr.x + (sr.w - 8.0) * 0.5, sr.y + (sr.h - 8.0) * 0.5, 8.0, 8.0}, radius::control(),
                            Paint::filled(lerpColor(palette::mutedForeground(), palette::foreground(), hv)));
        }
        t.restore();

        // the strips' heads: colour and name, scrolled sideways only
        drawRoundedRect(t, Rect{0, 0, W, kHeadH}, 0.0, Paint::filled(palette::leftRailBg()));
        t.save();
        t.clipRect(kSceneW, 0, std::max(0.0, W - kSceneW), kHeadH);
        for (size_t c = 0; c < mCols.size(); ++c)
        {
            const double x = kSceneW + c * kColW - sx;
            drawRoundedRect(t, Rect{x + 3.0, 4.0, kColW - 6.0, 3.0}, radius::pill(), Paint::filled(colourOf(mCols[c])));
            t.setFill(palette::foreground());
            t.drawText(textfit::ellipsize(t, mCols[c].name, kColW - 12.0, 11.0, font::sansMedium()), x + 6.0, 23.0, 11.0, font::sansMedium());
        }
        t.restore();
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0, kHeadH - 0.5); t.lineTo(W, kHeadH - 0.5); t.strokePath();

        // the scenes' column: names and ▶, scrolled up and down only; Back to Arrangement above; "+ Scene" under
        drawRoundedRect(t, Rect{0, kHeadH, kSceneW, std::max(0.0, H - kHeadH)}, 0.0, Paint::filled(palette::leftRailBg()));
        t.save();
        t.clipRect(0, kHeadH, kSceneW, std::max(0.0, H - kHeadH));
        for (size_t r = 0; r < mScenes.size(); ++r)
        {
            const double y = kHeadH + r * kRowH - sy;
            t.setFill(palette::foreground());
            t.drawText(textfit::ellipsize(t, mScenes[r].name, kSceneW - 52.0, 11.0, font::sans()), 12.0, textfit::baseline(y + kRowH * 0.5, 11.0), 11.0, font::sans());
            const Rect lr = sceneLaunchRect(mScenes[r].id);
            const double hv = mHover.amount(kSceneBase + (int)r);
            drawRoundedRect(t, lr, radius::control(), Paint::filled(palette::hoverWash(0.3 + 0.7 * hv)));
            glyph::play(t, Rect{lr.x + 8.0, lr.y + 7.5, 9.0, 9.0}, lerpColor(palette::mutedForeground(), palette::foreground(), hv));
        }
        const Rect ar = addSceneRect();
        const double ah = mHover.amount(kAddScene);
        if (ah > 0.001) drawRoundedRect(t, ar, radius::control(), Paint::filled(palette::hoverWash(ah)));
        t.setFill(lerpColor(palette::mutedForeground(), palette::foreground(), ah));
        t.drawText(textfit::ellipsize(t, tr("+ Scene"), ar.w - 8.0, 11.0, font::sans()), ar.x + 6.0, textfit::baseline(ar.y + ar.h * 0.5, 11.0), 11.0, font::sans());
        t.restore();
        const Rect br = backRect();
        const double bh = mHover.amount(kBack);
        drawRoundedRect(t, br, radius::control(), Paint::filledStroked(lerpColor(palette::secondary(), palette::card(), bh), palette::border(), 1.0));
        t.setFill(lerpColor(palette::secondaryForeground(), palette::foreground(), bh));
        const std::string back = tr("Back to Arrangement");
        t.drawText(textfit::ellipsize(t, back, br.w - 12.0, 10.0, font::sans()), br.x + 6.0, textfit::baseline(br.y + br.h * 0.5, 10.0), 10.0, font::sans());
        if (mScenes.empty())
        {
            t.setFill(palette::mutedForeground());
            const std::string hint = tr("No scenes yet \xE2\x80\x94 \"+ Scene\" adds one; a cell takes a pattern or a clip.");
            t.drawText(textfit::ellipsize(t, hint, std::max(0.0, W - kSceneW - 24.0), 11.0, font::sans()), kSceneW + 12.0, kHeadH + 24.0, 11.0, font::sans());
        }
        // the bars, only when there is more to see (R6)
        if (mScrollY.scrollable())
        {
            const double vh = std::max(0.0, H - kHeadH), th = std::max(18.0, vh * vh / std::max(vh, mScrollY.contentH()));
            drawRoundedRect(t, Rect{W - 5.0, kHeadH + (vh - th) * (sy / std::max(1.0, mScrollY.maxScroll())), 3.0, th}, radius::pill(), Paint::filled(palette::whiteAlpha(0.18)));
        }
        if (mScrollX.scrollable())
        {
            const double vw = std::max(0.0, W - kSceneW), tw = std::max(18.0, vw * vw / std::max(vw, mScrollX.contentH()));
            drawRoundedRect(t, Rect{kSceneW + (vw - tw) * (sx / std::max(1.0, mScrollX.maxScroll())), H - 5.0, tw, 3.0}, radius::pill(), Paint::filled(palette::whiteAlpha(0.18)));
        }
    }
}
}
