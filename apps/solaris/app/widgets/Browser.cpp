#include "Browser.h"
#include "../../../interstellar/app/widgets/Glyphs.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include "../../../cosmo/widgets/Icons.h"
#include <cctype>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdio>
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
        const char *kTabNames[Browser::kTabs] = {N_("Samples"), N_("Instruments"), N_("Song"), N_("Presets")};
        std::string q(const std::string &s) { return s.find_first_of(" \t\"") == std::string::npos && !s.empty() ? s : "\"" + s + "\""; }
        std::string baseName(const std::string &p)
        {
            const auto s = p.find_last_of('/');
            return s == std::string::npos ? p : p.substr(s + 1);
        }
        Color fade(Color c, double a) { c.a *= a; return c; }
        // R-BROWSE-4: a pattern's glyph — a tiny piano roll, three notes at three pitches
        void notesGlyph(IRenderTarget &t, const Rect &b, const Color &c)
        {
            const double y[3] = {0.7, 0.25, 0.5}, x[3] = {0.0, 0.3, 0.62}, w[3] = {0.4, 0.4, 0.38};
            for (int i = 0; i < 3; ++i)
                drawRoundedRect(t, Rect{b.x + x[i] * b.w, b.y + y[i] * b.h - 1.0, w[i] * b.w, 2.2}, radius::control(), Paint::filled(c));
        }
        std::string lengthText(double beats, int perBar)
        {
            // a pattern's length as a musician says it: bars when it is whole bars, else beats — as words (Words.h)
            const double bars = beats / std::max(1, perBar);
            if (std::fabs(bars - std::round(bars)) < 1e-9 && bars >= 1) return sayn(std::lround(bars), "{1} bar", "{1} bars", std::lround(bars));
            if (std::fabs(beats - std::round(beats)) < 1e-9) return sayn(std::lround(beats), "{1} beat", "{1} beats", std::lround(beats));
            char buf[32];
            std::snprintf(buf, sizeof buf, "%g", beats);
            return say("{1} beats", buf);
        }
        /** R-PRESET-3: a five-pointed star in `b` — outlined, its fill `lit` (0 … 1) of the colour. */
        void starGlyph(IRenderTarget &t, const Rect &b, const Color &c, double lit)
        {
            const double cx = b.x + b.w * 0.5, cy = b.y + b.h * 0.53, ro = b.w * 0.5, ri = ro * 0.42;
            t.beginPath();
            for (int k = 0; k < 10; ++k)
            {
                const double ang = -M_PI / 2.0 + k * M_PI / 5.0, rr = k % 2 == 0 ? ro : ri;
                const double x = cx + rr * std::cos(ang), y = cy + rr * std::sin(ang);
                if (k == 0) t.moveTo(x, y);
                else t.lineTo(x, y);
            }
            t.closePath();
            if (lit > 0.001)
            {
                Color f = c;
                f.a *= lit;
                t.setFill(f);
                t.fillPath();
                t.beginPath();
                for (int k = 0; k < 10; ++k)
                {
                    const double ang = -M_PI / 2.0 + k * M_PI / 5.0, rr = k % 2 == 0 ? ro : ri;
                    const double x = cx + rr * std::cos(ang), y = cy + rr * std::sin(ang);
                    if (k == 0) t.moveTo(x, y);
                    else t.lineTo(x, y);
                }
                t.closePath();
            }
            t.setStroke(c, 1.1);
            t.strokePath();
        }
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
        mPresets = m.presets; // R-PRESET-1
        mAudFile = m.audition.file;
        mAudPlaying = m.audition.playing;
        mAudProgress = m.audition.progress;
        // the folder browsed last is what the Samples tab shows, once the user went into one — or the search it made
        if (!mSearch.empty()) { if (m.browser.search == mSearch) mEntries = m.browser.entries; }
        else if (!mPath.empty() && m.browser.path == mPath) mEntries = m.browser.entries;
        mLib.clear(); // R-PRESET-3
        for (const auto &e : m.library) mLib[e.path] = e;
        // R-BROWSE-4: the song's MIDI — each pattern with its length, its notes and who plays it
        mSongMidi.clear();
        std::map<std::string, std::string> stripName;
        for (const auto &s : m.strips) stripName[s.id] = s.name;
        const int perBar = std::max(1, std::atoi(m.sig.c_str()));
        for (const auto &pt : m.patterns)
        {
            std::string who;
            for (const auto &s : pt.strips) who += (who.empty() ? "" : ", ") + (stripName.count(s) ? stripName[s] : s);
            Item it{"pattern", pt.name, pt.id, ""};
            it.note = lengthText(pt.length, perBar) + " \xC2\xB7 " + sayn((long)pt.notes.size(), "{1} note", "{1} notes", (long)pt.notes.size()) +
                      " \xC2\xB7 " + (who.empty() ? say("unplayed") : who);
            it.strip = pt.lastStrip;
            mSongMidi.push_back(it);
        }
        mSongSounds.clear();
        std::set<std::string> seen;
        for (const auto &c : m.clips)
            if (c.kind == "audio" && seen.insert(c.src).second) mSongSounds.emplace_back(baseName(c.src), c.src);
        rebuild();
    }

    void Browser::rebuild()
    {
        mRows.clear();
        if (mTab == kSamples && !mSearch.empty())
        {
            // R-PRESET-3: what a search found — its row back to the folders first
            mRows.push_back(Item{"clearsearch", mSearch == "favourites" ? say("Favourites") : say("Search \xC2\xB7 {1}", mSearch), "", ""});
            for (const auto &e : mEntries)
                if (e.kind == "audio") mRows.push_back(Item{"audio", e.name, e.path, ""});
        }
        else if (mTab == kSamples)
        {
            if (mPath.empty())
            {
                for (const auto &f : mFolders) mRows.push_back(Item{"folder", baseName(f), f, f});
                if (!mFolders.empty() || !mLib.empty()) // R-PRESET-3: a search and the favourites, under the folders
                {
                    mRows.push_back(Item{"search", say("Search samples\xE2\x80\xA6"), "", ""});
                    mRows.push_back(Item{"favourites", say("Favourites"), "", ""});
                }
            }
            else
            {
                const auto slash = mPath.find_last_of('/');
                mRows.push_back(Item{"up", baseName(mPath), slash == std::string::npos ? std::string() : mPath.substr(0, slash), ""});
                for (const auto &e : mEntries)
                    if (e.kind == "dir" || e.kind == "audio" || e.kind == "midi") mRows.push_back(Item{e.kind, e.name, e.path, ""}); // R-MIDI-1
            }
        }
        else if (mTab == kInstruments)
        {
            for (const char *kind : {"instrument", "effect"})
            {
                mRows.push_back(Item{"header", std::string(kind) == "instrument" ? say("Instruments") : say("Effects"), "", ""}); // uppercased where drawn
                for (const auto &t : mTypes)
                    if (t.kind == kind) mRows.push_back(Item{kind, t.label, t.name, t.name});
            }
        }
        else if (mTab == kPresets)
        {
            // R-PRESET-1: each type that has presets, in the registry's order — a header, then its presets (the factory's
            // marked)
            for (const auto &t : mTypes)
            {
                bool any = false;
                for (const auto &x : mPresets)
                    if (x.type == t.name)
                    {
                        if (!any)
                        {
                            mRows.push_back(Item{"header", t.label, "", ""}); // the registry's word; in capitals where drawn, as the other tabs'
                        }
                        any = true;
                        Item it{"preset", x.name, x.type, x.factory ? say("factory") : std::string()};
                        it.role = t.kind;
                        mRows.push_back(it);
                    }
            }
        }
        else
        {
            // R-BROWSE-4: the song's MIDI first (a new one always to hand), then its samples
            mRows.push_back(Item{"header", say("MIDI"), "", ""});
            mRows.push_back(Item{"newmidi", say("New MIDI"), "", ""});
            for (const auto &it : mSongMidi) mRows.push_back(it);
            if (!mSongSounds.empty()) mRows.push_back(Item{"header", say("Samples"), "", ""});
            for (const auto &s : mSongSounds) mRows.push_back(Item{"audio", s.first, s.second, ""});
        }

        // what is drawn: keyed by generation and content; an empty list says so in words, as a row
        const std::string gen = std::to_string(mGen) + "|";
        std::vector<std::pair<std::string, Item>> keyed;
        for (const auto &it : mRows) keyed.emplace_back(gen + it.kind + "|" + it.value + "|" + it.label, it);
        if (mTab == kSamples && !mSearch.empty() && mRows.size() == 1)
        {
            Item e{"empty", mSearch == "favourites" ? "No favourites yet." : "Nothing found.", "", ""};
            e.note = mSearch == "favourites" ? "A sound's star makes it one." : "Try part of a name, or a tag.";
            keyed.emplace_back(gen + "empty|" + e.label, e);
        }
        if (mRows.empty())
        {
            Item e{"empty", "", "", ""};
            if (mTab == kSamples && mPath.empty()) { e.label = say("No sample folders yet."); e.note = say("Add yours in Settings \xE2\x80\x94 click here."); }
            else if (mTab == kSamples) { e.label = say("Nothing to play here."); e.note = say("Audio files and folders show up in this list."); }
            else if (mTab == kSong) { e.label = say("This song plays no samples yet."); e.note = say("Drag one in from Samples."); }
            else if (mTab == kPresets) { e.label = say("No presets yet."); e.note = say("Save one from a device's window \xE2\x80\x94 Presets."); }
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

    Rect Browser::starRect(int i) const
    {
        if (i < 0 || i >= (int)mRows.size() || mRows[(size_t)i].kind != "audio") return Rect{};
        const Rect r = rowRect(i);
        return Rect{width.value() - space::padX() - 13.0, r.y + (r.h - 13.0) * 0.5, 13.0, 13.0};
    }

    double Browser::starAmount(const std::string &file) const
    {
        const auto it = mStars.find(file);
        return it == mStars.end() ? 0.0 : it->second.amt.value();
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
        // each tab as wide as its name needs (four characters' room around it), so "Instruments" fits beside the rest (R5)
        double sum = 0.0, before = 0.0;
        for (int k = 0; k < kTabs; ++k)
        {
            const double weight = (double)std::strlen(kTabNames[k]) + 4.0;
            if (k < t) before += weight;
            sum += weight;
        }
        const double room = width.value() - 2 * space::padX();
        return Rect{space::padX() + room * before / sum, 4.0, room * ((double)std::strlen(kTabNames[t]) + 4.0) / sum, kTabsH - 8.0};
    }

    double Browser::auditionAmount(const std::string &file) const
    {
        const auto it = mAud.find(file);
        return it == mAud.end() ? 0.0 : it->second.amt.value();
    }

    void Browser::accessibles(std::vector<A11yNode> &out)
    {
        for (int t = 0; t < kTabs; ++t)
        {
            A11yNode n = a11yNode("browser.tab." + std::to_string(t), "tab", tr(kTabNames[t]), worldRect(*this, tabRect(t)));
            n.checked = t == mTab;
            out.push_back(n);
        }
        for (int i = 0; i < (int)mRows.size(); ++i)
        {
            const Rect r = rowRect(i);
            const auto &it = mRows[(size_t)i];
            if (r.bottom() <= listTop() || r.y >= height.value() || it.kind == "header" || it.kind == "empty") continue; // out of view; words
            out.push_back(a11yNode("browser.row." + it.kind + "." + (it.value.empty() ? english(it.label) : it.value), "listitem", ::arstro::solaris_ui::shown(it.label),
                                   worldRect(*this, r), ::arstro::solaris_ui::shown(it.note)));
        }
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
        const double want = tabRect(mTab).x, wantW = tabRect(mTab).w;
        if (!mTabInit) { mTabX.set(want); mTabW.set(wantW); mTabInit = true; }
        else
        {
            if (std::fabs(mTabX.value() - want) > 0.01 && !mTabX.isAnimating()) mTabX.animateTo(want, motion::kSlideMs, Easing::EaseOutCubic, nowMs);
            if (std::fabs(mTabW.value() - wantW) > 0.01 && !mTabW.isAnimating()) mTabW.animateTo(wantW, motion::kSlideMs, Easing::EaseOutCubic, nowMs);
        }
        mTabX.update(nowMs);
        mTabW.update(nowMs);
        mMotion.advance(nowMs);
        // R-PRESET-3: each shown sound's star — lit as the library says, eased whoever changed it
        for (const auto &it : mRows)
            if (it.kind == "audio")
            {
                Star &s = mStars[it.value];
                const auto lib = mLib.find(it.value);
                const double want = lib != mLib.end() && lib->second.favourite ? 1.0 : 0.0;
                if (s.last < 0.0) s.amt.set(want);
                else if (want != s.last) s.amt.animateTo(want, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
                s.last = want;
            }
        for (auto &s : mStars) s.second.amt.update(nowMs);
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
            const int r = rowAt(local);
            if (mTab == kSamples && mFolders.empty() && mSearch.empty() && (r < 0 || (mRows[(size_t)r].kind != "search" && mRows[(size_t)r].kind != "favourites")) && onOpenSettings)
            { onOpenSettings(); return true; }
            if (r < 0) return true;
            const Item it = mRows[(size_t)r];
            // R-PRESET-3: a star, a search, the favourites, back from a search — each ONE line (or none)
            if (it.kind == "audio" && starRect(r).contains(local))
            {
                const auto lib = mLib.find(it.value);
                const bool fav = lib != mLib.end() && lib->second.favourite;
                if (onCommand) onCommand("sample favourite " + q(it.value) + (fav ? " off" : " on"));
                return true;
            }
            if (it.kind == "search")
            {
                const Point world = g.pos;
                if (onRename)
                    onRename("", world, [this](const std::string &text) {
                        if (text.empty()) return;
                        mSearch = text;
                        mEntries.clear();
                        navigate();
                        if (onCommand) onCommand("browse --search " + q(text));
                        rebuild();
                    });
                return true;
            }
            if (it.kind == "favourites" || it.kind == "clearsearch")
            {
                mSearch = it.kind == "favourites" ? "favourites" : "";
                mPath.clear();
                mEntries.clear();
                navigate();
                if (!mSearch.empty() && onCommand) onCommand("browse --favourites");
                rebuild();
                return true;
            }
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
            else if (it.kind == "newmidi" && onCommand)
                onCommand("pattern new --name " + q("MIDI " + std::to_string(mSongMidi.size() + 1))); // R-BROWSE-4
            return true;
        }
        case Gesture::Type::DoubleClick:
        {
            const int r = rowAt(local);
            if (r < 0) return true;
            if (mRows[(size_t)r].kind == "pattern") { if (onOpenPattern) onOpenPattern(mRows[(size_t)r].value); return true; } // its piano roll
            if (draggable(mRows[(size_t)r]) && onActivate) onActivate(mRows[(size_t)r]);
            return true;
        }
        case Gesture::Type::RightClick:
        {
            const int r = rowAt(local);
            if (r >= 0 && mRows[(size_t)r].kind == "audio" && onMenu)
            {
                // R-PRESET-3: a sound's menu — a tag typed, a tag taken off, the star — each one line
                const std::string file = mRows[(size_t)r].value;
                const auto lib = mLib.find(file);
                const bool fav = lib != mLib.end() && lib->second.favourite;
                const Point world = g.pos;
                std::vector<cosmo_v2::ContextMenu::Item> items;
                items.push_back({say("Tag\xE2\x80\xA6"), [this, file, world] {
                                     if (onRename) onRename("", world, [this, file](const std::string &tag) { if (onCommand && !tag.empty()) onCommand("sample tag " + q(file) + " " + q(tag)); });
                                 }});
                if (lib != mLib.end())
                    for (const auto &tag : lib->second.tags)
                        items.push_back({say("Remove tag \xC2\xB7 {1}", tag), [this, file, tag] { if (onCommand) onCommand("sample untag " + q(file) + " " + tag); }});
                items.push_back({fav ? say("Not a favourite") : say("Favourite"), [this, file, fav] { if (onCommand) onCommand("sample favourite " + q(file) + (fav ? " off" : " on")); }});
                onMenu(std::move(items), world);
                return true;
            }
            // a pattern's menu (R-BROWSE-4): each one line
            if (r < 0 || mRows[(size_t)r].kind != "pattern" || !onMenu) return true;
            const std::string id = mRows[(size_t)r].value, name = mRows[(size_t)r].label; // a pattern's name: data
            const Point world = g.pos;
            onMenu({{say("Rename\xE2\x80\xA6"), [this, id, name, world] {
                         if (onRename) onRename(name, world, [this, id](const std::string &n) { if (onCommand) onCommand("set " + id + ".name=" + q(n)); });
                     }},
                    {say("Piano Roll"), [this, id] { if (onOpenPattern) onOpenPattern(id); }},
                    {say("Duplicate"), [this, id] { if (onCommand) onCommand("pattern duplicate " + id); }},
                    {say("Copy ID"), [this, id] { if (onCopy) onCopy(id); }},
                    {say("Delete"), [this, id] { if (onCommand) onCommand("pattern delete " + id); }}},
                   world);
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
        drawRoundedRect(t, Rect{mTabX.value() + 1.0, tr0.y + 1.0, mTabW.value() - 2.0, tr0.h - 2.0}, radius::hairline(), Paint::filled(palette::secondary()));
        for (int k = 0; k < kTabs; ++k)
        {
            const Rect r = tabRect(k);
            const double hv = mHover.amount(100 + k);
            t.setFill(k == mTab ? palette::foreground() : lerpColor(palette::mutedForeground(), palette::foreground(), 0.5 * hv));
            const char *fam = k == mTab ? font::sansMedium() : font::sans();
            const std::string name = textfit::ellipsize(t, tr(kTabNames[k]), r.w - 4.0, 10.0, fam); // a longer language fits its tab
            t.drawText(name, r.x + (r.w - t.measureText(name, 10.0, font::sans())) * 0.5, interstellar_v1::textfit::baseline(r.y + r.h * 0.5, 10.0), 10.0, fam);
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
                t.drawText(textfit::ellipsize(t, ::arstro::solaris_ui::shown(it.label), W - 2 * space::padX(), 11.0, font::sans()), space::padX(), r.y + 28.0, 11.0, font::sans());
                t.setFill(fade(palette::mutedForeground(), 0.75 * a));
                t.drawText(textfit::ellipsize(t, ::arstro::solaris_ui::shown(it.note), W - 2 * space::padX(), 10.0, font::sans()), space::padX(), r.y + 44.0, 10.0, font::sans());
                continue;
            }
            if (it.kind == "header")
            {
                t.setFill(fade(palette::mutedForeground(), a));
                t.drawText(textfit::ellipsize(t, solaris_strings::upper(::arstro::solaris_ui::shown(it.label)), W - 2 * space::padX(), 9.0, font::sansSemiBold()), space::padX(),
                           textfit::baseline(cy + 3.0, 9.0), 9.0, font::sansSemiBold(), 0.13 * 9.0);
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
            else if (it.kind == "up" || it.kind == "clearsearch") glyph::line(t, ib.x + 8.0, ib.y + 2.0, ib.x + 3.0, ib.y + 6.0, ic, 1.3), glyph::line(t, ib.x + 3.0, ib.y + 6.0, ib.x + 8.0, ib.y + 10.0, ic, 1.3);
            else if (it.kind == "search")
            {
                drawCircle(t, ib.x + 5.0, ib.y + 5.0, 3.6, Paint::stroked(ic, 1.2)); // a lens and its handle
                glyph::line(t, ib.x + 7.6, ib.y + 7.6, ib.x + 11.0, ib.y + 11.0, ic, 1.3);
            }
            else if (it.kind == "favourites") starGlyph(t, ib, ic, 0.0);
            else if (it.kind == "audio") waveGlyph(t, ib, ic);
            else if (it.kind == "instrument" || (it.kind == "preset" && it.role == "instrument")) glyph::speaker(t, ib, ic, 1.1);
            else if (it.kind == "pattern" || it.kind == "midi") notesGlyph(t, ib, ic);
            else if (it.kind == "newmidi")
            {
                glyph::line(t, ib.x + 6.0, ib.y + 2.0, ib.x + 6.0, ib.y + 10.0, ic, 1.3);
                glyph::line(t, ib.x + 2.0, ib.y + 6.0, ib.x + 10.0, ib.y + 6.0, ic, 1.3);
            }
            else drawCircle(t, ib.x + 6.0, ib.y + 6.0, 4.0, Paint::stroked(ic, 1.1));
            const bool mono = it.kind == "audio";   // a filename is mono (the type ramp's rule)
            const double px = mono ? 10.0 : 11.0;
            const char *fam = mono ? font::mono() : font::sans();
            double labelW = W - ib.right() - 2 * space::padX();
            if (it.kind == "audio")
            {
                // R-PRESET-3: its star at the right (filled as far as it is lit), its tags beside it — muted mono
                const Rect sr{W - space::padX() - 13.0, cy - 6.5, 13.0, 13.0};
                const auto st = mStars.find(it.value);
                const double lit = st == mStars.end() ? 0.0 : st->second.amt.value();
                starGlyph(t, sr, fade(lerpColor(palette::mutedForeground(), palette::primary(), lit), a * (0.45 + 0.55 * std::max(lit, hv))), lit);
                labelW = std::max(0.0, labelW - 19.0);
                const auto lib = mLib.find(it.value);
                if (lib != mLib.end() && !lib->second.tags.empty())
                {
                    std::string tags;
                    for (const auto &tg : lib->second.tags) tags += (tags.empty() ? "" : " \xC2\xB7 ") + tg;
                    const std::string meta = textfit::ellipsize(t, tags, W * 0.4, 9.0, font::mono());
                    const double mw = t.measureText(meta, 9.0, font::mono());
                    t.setFill(fade(palette::mutedForeground(), a));
                    t.drawText(meta, sr.x - 6.0 - mw, textfit::baseline(cy, 9.0), 9.0, font::mono());
                    labelW = std::max(0.0, labelW - mw - 8.0);
                }
            }
            if ((it.kind == "pattern" || it.kind == "preset") && !it.note.empty())
            {
                // its length, notes and who plays it, at the right — mono (numbers), muted; the name gets the rest
                const std::string meta = textfit::ellipsize(t, ::arstro::solaris_ui::shown(it.note), W * 0.62, 9.0, font::mono());
                const double mw = t.measureText(meta, 9.0, font::mono());
                t.setFill(fade(palette::mutedForeground(), a));
                t.drawText(meta, W - space::padX() - mw, textfit::baseline(cy, 9.0), 9.0, font::mono());
                labelW = std::max(0.0, labelW - mw - 8.0);
            }
            t.setFill(fade(it.kind == "up" || it.kind == "newmidi" ? lerpColor(palette::mutedForeground(), palette::foreground(), it.kind == "newmidi" ? hv : 0.0)
                                                                   : palette::foreground(), a));
            t.drawText(textfit::ellipsize(t, ::arstro::solaris_ui::shown(it.label), labelW, px, fam), ib.right() + 8.0, textfit::baseline(cy, px), px, fam);
        }
        t.restore();
        mScroll.drawBar(t, W - 5.0);
    }
}
}
