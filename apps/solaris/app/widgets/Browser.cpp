#include "Browser.h"
#include "../../../interstellar/app/widgets/Glyphs.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include "../../../cosmo/widgets/Icons.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
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
        const char *kTabNames[Browser::kTabs] = {"Samples", "Instruments", "Song"};
        std::string q(const std::string &s) { return s.find_first_of(" \t\"") == std::string::npos && !s.empty() ? s : "\"" + s + "\""; }
        std::string baseName(const std::string &p)
        {
            const auto s = p.find_last_of('/');
            return s == std::string::npos ? p : p.substr(s + 1);
        }
        Color fade(Color c, double a) { c.a *= a; return c; }
        void waveGlyph(IRenderTarget &t, const Rect &b, const Color &c)
        {
            const double h[5] = {0.35, 0.8, 1.0, 0.6, 0.3};
            for (int i = 0; i < 5; ++i)
            {
                const double bh = b.h * h[i];
                drawRoundedRect(t, Rect{b.x + i * b.w / 5.0 + 0.5, b.y + (b.h - bh) * 0.5, b.w / 5.0 - 1.0, bh}, radius::control(), Paint::filled(c));
            }
        }
    }

    Browser::Browser() { clipToBounds = true; }

    void Browser::bind(const solaris::AppModel &m)
    {
        mFolders = m.settings.folders;
        mTypes = m.deviceTypes;
        mAudFile = m.audition.file;
        mAudPlaying = m.audition.playing;
        mAudProgress = m.audition.progress;
        // the folder browsed last is what the Samples tab shows, once the user went into one
        if (!mPath.empty() && m.browser.path == mPath) mEntries = m.browser.entries;
        mSongSounds.clear();
        std::set<std::string> seen;
        for (const auto &c : m.clips)
            if (c.kind == "audio" && seen.insert(c.src).second) mSongSounds.emplace_back(baseName(c.src), c.src);
        rebuild();
    }

    void Browser::rebuild()
    {
        mRows.clear();
        if (mTab == kSamples)
        {
            if (mPath.empty())
                for (const auto &f : mFolders) mRows.push_back(Item{"folder", baseName(f), f, f});
            else
            {
                const auto slash = mPath.find_last_of('/');
                mRows.push_back(Item{"up", baseName(mPath), slash == std::string::npos ? std::string() : mPath.substr(0, slash), ""});
                for (const auto &e : mEntries)
                    if (e.kind == "dir" || e.kind == "audio") mRows.push_back(Item{e.kind, e.name, e.path, ""});
            }
        }
        else if (mTab == kInstruments)
        {
            for (const char *kind : {"instrument", "effect"})
            {
                mRows.push_back(Item{"header", std::string(kind) == "instrument" ? "INSTRUMENTS" : "EFFECTS", "", ""});
                for (const auto &t : mTypes)
                    if (t.kind == kind) mRows.push_back(Item{kind, t.label, t.name, t.name});
            }
        }
        else
            for (const auto &s : mSongSounds) mRows.push_back(Item{"audio", s.first, s.second, ""});

        // what is drawn: keyed by generation and content; an empty list says so in words, as a row
        const std::string gen = std::to_string(mGen) + "|";
        std::vector<std::pair<std::string, Item>> keyed;
        for (const auto &it : mRows) keyed.emplace_back(gen + it.kind + "|" + it.value + "|" + it.label, it);
        if (mRows.empty())
        {
            Item e{"empty", "", "", ""};
            if (mTab == kSamples && mPath.empty()) { e.label = "No sample folders yet."; e.note = "Add yours in Settings \xE2\x80\x94 click here."; }
            else if (mTab == kSamples) { e.label = "Nothing to play here."; e.note = "Audio files and folders show up in this list."; }
            else if (mTab == kSong) { e.label = "This song plays no samples yet."; e.note = "Drag one in from Samples."; }
            keyed.emplace_back(gen + "empty|" + e.label, e);
        }
        mMotion.sync(keyed, space::rowH());
    }

    void Browser::navigate()
    {
        mGhostScroll = mScroll.value();
        ++mGen;
        mScroll.reset(); // a new list: nowhere to travel from (the old one fades out where it was)
    }

    double Browser::rowY(int i) const
    {
        const auto *r = mMotion.byIndex(i);
        if (!r) return i * space::rowH();
        return r->placed ? r->liveY() : r->yTarget;
    }

    double Browser::rowAlpha(int i) const
    {
        const auto *r = mMotion.byIndex(i);
        return r && r->placed ? r->liveAlpha() : 0.0;
    }

    void Browser::setTab(int t)
    {
        if (t < 0 || t >= kTabs || t == mTab) return;
        mTab = t;
        navigate();
        rebuild();
    }

    Rect Browser::tabRect(int t) const
    {
        const double w = (width.value() - 2 * space::padX()) / kTabs;
        return Rect{space::padX() + t * w, 4.0, w, kTabsH - 8.0};
    }

    double Browser::auditionAmount(const std::string &file) const
    {
        const auto it = mAud.find(file);
        return it == mAud.end() ? 0.0 : it->second.amt.value();
    }

    Rect Browser::rowRect(int i) const
    {
        return Rect{0.0, listTop() + rowY(i) - mScroll.value(), width.value(), space::rowH()};
    }

    void Browser::layout()
    {
        mScroll.setExtent(listTop(), std::max(0.0, height.value() - listTop()), mRows.size() * space::rowH() + space::padX());
    }

    int Browser::rowAt(const Point &p) const
    {
        if (p.y < listTop()) return -1;
        for (int i = 0; i < (int)mRows.size(); ++i)
            if (rowRect(i).contains(p)) return mRows[(size_t)i].kind == "header" ? -1 : i;
        return -1;
    }

    void Browser::advance(double nowMs)
    {
        mNowMs = nowMs;
        const double want = tabRect(mTab).x;
        if (!mTabInit) { mTabX.set(want); mTabInit = true; }
        else if (std::fabs(mTabX.value() - want) > 0.01 && !mTabX.isAnimating()) mTabX.animateTo(want, motion::kSlideMs, Easing::EaseOutCubic, nowMs);
        mTabX.update(nowMs);
        mMotion.advance(nowMs);
        // the preview's fill: on the row heard, its progress following the audio; eased in and out
        if (mAudPlaying && !mAudFile.empty()) mAud[mAudFile];
        for (auto it = mAud.begin(); it != mAud.end();)
        {
            Aud &a = it->second;
            a.want = mAudPlaying && it->first == mAudFile;
            if (a.want) a.progress = mAudProgress;
            if (!a.placed) { a.amt.set(0.0); a.placed = true; }
            if (a.want != a.last)
            {
                a.amt.animateTo(a.want ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
                a.last = a.want;
            }
            a.amt.update(nowMs);
            if (!a.want && !a.amt.isAnimating() && a.amt.value() <= 0.001) it = mAud.erase(it);
            else ++it;
        }
        mScroll.advance(nowMs);
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    bool Browser::handleGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Move:
        {
            int h = -1;
            for (int t = 0; t < kTabs; ++t)
                if (tabRect(t).contains(local)) h = 100 + t;
            if (h < 0) h = rowAt(local);
            mHover.setHovered(h);
            return true;
        }
        case Gesture::Type::Down:
            mPressRow = rowAt(local);
            return true;
        case Gesture::Type::DragStart:
            if (mPressRow >= 0 && draggable(mRows[(size_t)mPressRow]))
            {
                mDragging = true;
                mDragItem = mRows[(size_t)mPressRow];
                if (onDragMove) onDragMove(mDragItem, g.pos);
            }
            return true;
        case Gesture::Type::Drag:
            if (mDragging && onDragMove) onDragMove(mDragItem, g.pos);
            return true;
        case Gesture::Type::Drop:
        case Gesture::Type::Up:
            if (mDragging)
            {
                mDragging = false;
                if (onDrop) onDrop(mDragItem, g.pos);
            }
            mPressRow = -1;
            return true;
        case Gesture::Type::Scroll:
            return mScroll.scrollBy(g.delta.y);
        case Gesture::Type::Click:
        {
            for (int t = 0; t < kTabs; ++t)
                if (tabRect(t).contains(local)) { setTab(t); return true; }
            if (mTab == kSamples && mFolders.empty() && onOpenSettings) { onOpenSettings(); return true; }
            const int r = rowAt(local);
            if (r < 0) return true;
            const Item it = mRows[(size_t)r];
            if (it.kind == "folder" || it.kind == "dir" || it.kind == "up")
            {
                std::string target = it.value;
                // "up" from a top-level sample folder returns to the folder list
                if (it.kind == "up" && std::find(mFolders.begin(), mFolders.end(), mPath) != mFolders.end()) target.clear();
                mPath = target;
                mEntries.clear();
                navigate();
                if (!mPath.empty() && onCommand) onCommand("browse " + q(mPath));
                rebuild();
            }
            else if (it.kind == "audio" && onCommand)
                // R-EDM-9: a click hears it now; a click on the one being heard stops it
                onCommand(mAudPlaying && mAudFile == it.value ? std::string("audition stop") : "audition " + q(it.value));
            return true;
        }
        case Gesture::Type::DoubleClick:
        {
            const int r = rowAt(local);
            if (r >= 0 && draggable(mRows[(size_t)r]) && onActivate) onActivate(mRows[(size_t)r]);
            return true;
        }
        default:
            return Segment::handleGesture(g, local);
        }
    }

    void Browser::onPaint(IRenderTarget &t) const
    {
        const double W = width.value(), H = height.value();
        drawRoundedRect(t, Rect{0, 0, W, H}, 0.0, Paint::filled(surface::headerBg()));
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(W - 0.5, 0); t.lineTo(W - 0.5, H); t.strokePath();
        t.beginPath(); t.moveTo(0, kTabsH); t.lineTo(W, kTabsH); t.strokePath();

        // tabs: the highlight SLIDES
        drawRoundedRect(t, Rect{space::padX(), 4.0, W - 2 * space::padX(), kTabsH - 8.0}, radius::hairline(), Paint::filled(palette::segmentedBg()));
        const Rect tr0 = tabRect(0);
        drawRoundedRect(t, Rect{mTabX.value() + 1.0, tr0.y + 1.0, tr0.w - 2.0, tr0.h - 2.0}, radius::hairline(), Paint::filled(palette::secondary()));
        for (int k = 0; k < kTabs; ++k)
        {
            const Rect r = tabRect(k);
            const double hv = mHover.amount(100 + k);
            t.setFill(k == mTab ? palette::foreground() : lerpColor(palette::mutedForeground(), palette::foreground(), 0.5 * hv));
            t.drawText(kTabNames[k], r.x + (r.w - t.measureText(kTabNames[k], 10.0, font::sans())) * 0.5, interstellar_v1::textfit::baseline(r.y + r.h * 0.5, 10.0), 10.0,
                       k == mTab ? font::sansMedium() : font::sans());
        }

        const double top = listTop();
        t.save();
        t.clipRect(0, top, W, H - top);
        for (const auto &row : mMotion.rows())
        {
            const double a = row.placed ? row.liveAlpha() : 0.0;
            if (a <= 0.001) continue;
            // a row the last navigation left fades where that list was scrolled to; any other moves with this one
            const bool left = row.gone && std::atoi(row.key.c_str()) != mGen;
            const Rect r{0.0, top + row.liveY() - (left ? mGhostScroll : mScroll.value()), W, space::rowH()};
            if (r.bottom() < top || r.y > H) continue;
            const Item &it = row.data;
            const double cy = r.y + r.h * 0.5;
            if (it.kind == "empty")
            {
                t.setFill(fade(palette::mutedForeground(), a));
                t.drawText(textfit::ellipsize(t, it.label, W - 2 * space::padX(), 11.0, font::sans()), space::padX(), r.y + 28.0, 11.0, font::sans());
                t.setFill(fade(palette::mutedForeground(), 0.75 * a));
                t.drawText(textfit::ellipsize(t, it.note, W - 2 * space::padX(), 10.0, font::sans()), space::padX(), r.y + 44.0, 10.0, font::sans());
                continue;
            }
            if (it.kind == "header")
            {
                t.setFill(fade(palette::mutedForeground(), a));
                t.drawText(it.label, space::padX(), textfit::baseline(cy + 3.0, 9.0), 9.0, font::sansSemiBold(), 0.13 * 9.0);
                continue;
            }
            if (it.kind == "audio")
                if (const auto au = mAud.find(it.value); au != mAud.end() && au->second.amt.value() > 0.001)
                {
                    // R-EDM-9: being heard — a fill across the row as far as the preview has played
                    const double fa = au->second.amt.value() * a, done = std::clamp(au->second.progress, 0.0, 1.0);
                    drawRoundedRect(t, Rect{4.0, r.y + 1.0, (W - 8.0) * done, r.h - 2.0}, radius::control(), Paint::filled(palette::primaryAlpha(0.3 * fa)));
                    drawRoundedRect(t, Rect{4.0, r.bottom() - 3.0, (W - 8.0) * done, 2.0}, radius::pill(), Paint::filled(palette::primaryAlpha(fa)));
                }
            const double hv = row.gone || row.index < 0 ? 0.0 : mHover.amount(row.index);
            if (hv > 0.001) drawRoundedRect(t, Rect{4.0, r.y + 1.0, W - 8.0, r.h - 2.0}, radius::control(), Paint::filled(palette::hoverWash(hv * a)));
            const Rect ib{space::padX(), cy - 6.0, 12.0, 12.0};
            const Color ic = fade(lerpColor(palette::mutedForeground(), palette::foreground(), 0.4 * hv), a);
            if (it.kind == "folder" || it.kind == "dir") cosmo_v2::icon::folder(t, ib, ic, 1.1);
            else if (it.kind == "up") glyph::line(t, ib.x + 8.0, ib.y + 2.0, ib.x + 3.0, ib.y + 6.0, ic, 1.3), glyph::line(t, ib.x + 3.0, ib.y + 6.0, ib.x + 8.0, ib.y + 10.0, ic, 1.3);
            else if (it.kind == "audio") waveGlyph(t, ib, ic);
            else if (it.kind == "instrument") glyph::speaker(t, ib, ic, 1.1);
            else drawCircle(t, ib.x + 6.0, ib.y + 6.0, 4.0, Paint::stroked(ic, 1.1));
            const bool mono = it.kind == "audio";   // a filename is mono (the type ramp's rule)
            const double px = mono ? 10.0 : 11.0;
            const char *fam = mono ? font::mono() : font::sans();
            t.setFill(fade(it.kind == "up" ? palette::mutedForeground() : palette::foreground(), a));
            t.drawText(textfit::ellipsize(t, it.label, W - ib.right() - 2 * space::padX(), px, fam), ib.right() + 8.0, textfit::baseline(cy, px), px, fam);
        }
        t.restore();
        mScroll.drawBar(t, W - 5.0);
    }
}
}
