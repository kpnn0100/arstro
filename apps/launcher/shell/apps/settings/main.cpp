/*
 *  arstro.settings — the system's settings, as a list of categories and one page for each.
 *
 *  Settings owns no state. Every value lives in the shell: the app asks for a change with one
 *  command line (`set brightness=0.4`) and shows whatever the shell then reports in a
 *  `setting.changed` event — its own change, or one made elsewhere (the quick-settings shade). So
 *  there is one source of truth and the two surfaces cannot disagree.
 *
 *  The tree: a root that paints the background and the title bar, six pages that are always
 *  there, and a back arrow. A page that is not showing is at zero opacity, which Artboard neither
 *  draws nor hit-tests. Changing page is two tweens per page (a fade and a short slide), so a
 *  change of mind half way through simply re-aims them.
 */
#include "App.h"
#include "Controls.h"
#include "GeneratedIcons.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <map>

using namespace arstro;
using namespace arstro::kit;
using namespace arstro::settings;

namespace
{
    enum PageId { kTop, kDisplay, kSound, kNetwork, kDateTime, kAbout, kPageCount };

    /** The display sizes offered. `scale` may hold any number (the shade or a script can set it);
     *  the nearest step is the one shown. */
    struct SizeStep
    {
        const char *name;
        double scale;
    };
    const std::array<SizeStep, 4> kSizes = {{{"Small", 0.85}, {"Default", 1.0}, {"Large", 1.15}, {"Larger", 1.3}}};

    /** One event or command line into words: blanks separate, "double quotes" keep blanks, a
     *  backslash keeps the next character. The same reading the shell gives a line. */
    std::vector<std::string> splitWords(const std::string &line)
    {
        std::vector<std::string> words;
        std::string cur;
        bool inQuote = false, any = false;
        for (size_t i = 0; i < line.size(); ++i)
        {
            const char c = line[i];
            if (inQuote)
            {
                if (c == '\\' && i + 1 < line.size()) cur += line[++i];
                else if (c == '"') inQuote = false;
                else cur += c;
            }
            else if (c == '"') { inQuote = true; any = true; }
            else if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                if (any) { words.push_back(cur); cur.clear(); any = false; }
            }
            else { cur += c; any = true; }
        }
        if (any) words.push_back(cur);
        return words;
    }

    std::string percent(double v)
    {
        char b[16];
        std::snprintf(b, sizeof b, "%d%%", (int)std::lround(std::clamp(v, 0.0, 1.0) * 100.0));
        return b;
    }

    std::string number(double v)
    {
        char b[32];
        std::snprintf(b, sizeof b, "%.3f", v);
        return b;
    }

    bool sameText(const std::string &a, const std::string &b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
        return true;
    }
}

/**
 *  One page: a scroller over a column of cards. The page itself is what fades and slides, as one
 *  group, so its scroll bar fades with it.
 */
class Page : public artboard::Segment
{
public:
    explicit Page(std::string title) : title(std::move(title))
    {
        mScroll = std::make_shared<TouchScroll>();
        mContent = std::make_shared<artboard::Segment>();
        mScroll->setContent(mContent);
        addChild(mScroll);
    }

    std::string title;

    void add(std::shared_ptr<artboard::Segment> item)
    {
        mContent->addChild(item);
    }

    /** How far it has scrolled — the title bar draws its hairline from this. */
    double scrolled() const { return mScroll->offset(); }
    double shift() const { return mShift.value(); }

    /** Fade in while arriving from `from` dp to the side. A page already partly showing (the user
     *  changed their mind) carries on from where it is. */
    void enter(double from, double nowMs)
    {
        if (isFadedOut())
        {
            // Unseen, so it may be placed: off to the side, and back at its top — a fresh visit.
            mShift.set(from);
            mScroll->resetToTop();
        }
        mShift.animateTo(0.0, motion::move(), artboard::Easing::EaseOutCubic, nowMs);
        opacity.animateTo(1.0, motion::move(), artboard::Easing::EaseInOutSine, nowMs);
    }

    /** Fade out WHILE leaving: both tweens end together, so it is never seen standing still. */
    void leave(double to, double nowMs)
    {
        mShift.animateTo(to, motion::move(), artboard::Easing::EaseInCubic, nowMs);
        opacity.animateTo(0.0, motion::move(), artboard::Easing::EaseInOutSine, nowMs);
    }

    void advance(double nowMs) override
    {
        mShift.update(nowMs);
        artboard::Segment::advance(nowMs);
    }

    /** Place the page under the title bar and its cards in the centred column. */
    void place(double top, double w, double h, double colX, double colW, double bottomPad)
    {
        x.set(mShift.value());
        y.set(top);
        width.set(w);
        height.set(h);
        mScroll->x.set(0.0);
        mScroll->y.set(0.0);
        mScroll->width.set(w);
        mScroll->height.set(h);
        double cy = space::gap();
        for (const auto &c : mContent->children())
        {
            double ch = metric::rowH();
            if (auto *laid = dynamic_cast<Laid *>(c.get())) ch = laid->arrange(colW);
            c->x.set(colX);
            c->y.set(cy);
            c->width.set(colW);
            c->height.set(ch);
            cy += ch + space::pad();
        }
        const double total = cy - space::pad() + bottomPad;
        mContent->width.set(w);
        mContent->height.set(total);
        mScroll->setContentHeight(total);
    }

private:
    std::shared_ptr<TouchScroll> mScroll;
    std::shared_ptr<artboard::Segment> mContent;
    artboard::Property mShift{0.0};
};

/**
 *  The root: background, title bar, the pages and the back arrow — and the one place that knows
 *  which page is showing. Navigation is asked for with `go()` from a tap or the Back gesture,
 *  neither of which has a frame clock; `advance` is where the move starts.
 */
class SettingsRoot : public artboard::Segment
{
public:
    std::array<std::shared_ptr<Page>, kPageCount> pages;
    std::shared_ptr<IconButton> backButton;
    double insetTop = 0.0, colX = 0.0, colW = 0.0;

    void go(PageId page) { mWanted = page; }
    PageId wanted() const { return mWanted; }

    /** The accent the shell chose. The palette is eased toward it here, every frame, so nothing
     *  drawn in the accent changes colour in one frame — the app is where it is seen. */
    void setAccent(const artboard::Color &c, bool animate)
    {
        if (!animate || !mAccentPlaced)
        {
            mAccentFrom = mAccentTo = c;
            mAccentMix.set(1.0);
            mAccentPlaced = true;
            palette::setAccent(c);
        }
        else
        {
            mAccentNext = c;
            mAccentPending = true;
        }
    }

    void advance(double nowMs) override
    {
        if (mWanted != mShown)
        {
            // Deeper pages arrive from the right; going back, everything travels the other way.
            // With reduced motion there is no travel at all — the fade alone carries it.
            const double d = artboard::reducedMotion() ? 0.0 : space::margin();
            const double dir = mWanted == kTop ? -1.0 : 1.0;
            pages[mShown]->leave(-dir * d, nowMs);
            pages[mWanted]->enter(dir * d, nowMs);
            mShown = mWanted;
        }
        if (mAccentPending)
        {
            mAccentPending = false;
            mAccentFrom = shownAccent();
            mAccentTo = mAccentNext;
            mAccentMix.set(0.0);
            mAccentMix.animateTo(1.0, motion::move(), artboard::Easing::EaseInOutSine, nowMs);
        }
        mAccentMix.update(nowMs);
        if (mAccentPlaced) palette::setAccent(shownAccent());
        // Only the page being shown takes touches: one fading out under it must not be toggled.
        for (int i = 0; i < kPageCount; ++i) pages[i]->enabled = i == mWanted;
        artboard::Segment::advance(nowMs);
    }

protected:
    void onPaint(artboard::IRenderTarget &t) const override
    {
        const double w = width.value(), h = height.value();
        fillRounded(t, {0, 0, w, h}, 0.0, surface::appBackground());
        // Each page's title is as visible as its page, so the titles cross-fade in step with the
        // pages and a change of mind half way needs no bookkeeping. Sub-page titles sit right of
        // the back arrow, on the same line as the row titles below; the top title sits where the
        // row icons are.
        const double cy = insetTop + metric::topBar() * 0.5;
        const double iconX = colX + space::pad(), textX = iconX + metric::glyph() + space::pad();
        double hairline = 0.0;
        for (int i = 0; i < kPageCount; ++i)
        {
            const double a = pages[i]->opacity.value();
            if (a <= 0.0) continue;
            const double tx = i == kTop ? iconX : textX;
            drawLabel(t, pages[i]->title, tx, cy, colX + colW - space::pad() - tx, type::title(), font::sansMedium(),
                      withAlpha(palette::foreground(), a));
            // The bar gets an edge only once something has scrolled under it.
            hairline += a * std::clamp(pages[i]->scrolled() / space::pad(), 0.0, 1.0);
        }
        fillRounded(t, {0, insetTop + metric::topBar() - 1.0, w, 1.0}, 0.0, withAlpha(surface::hairline(), std::min(1.0, hairline)));
    }

private:
    artboard::Color shownAccent() const { return mix(mAccentFrom, mAccentTo, mAccentMix.value()); }

    PageId mWanted = kTop, mShown = kTop;
    artboard::Color mAccentFrom, mAccentTo, mAccentNext;
    artboard::Property mAccentMix{1.0};
    bool mAccentPlaced = false, mAccentPending = false;
};

class SettingsApp : public app::App
{
public:
    void create(app::Context &ctx) override
    {
        mCtx = &ctx;
        mRoot = std::make_shared<SettingsRoot>();
        static const char *const titles[kPageCount] = {"Settings", "Display", "Sound", "Network", "Date & time", "About"};
        for (int i = 0; i < kPageCount; ++i)
        {
            mRoot->pages[i] = std::make_shared<Page>(titles[i]);
            if (i != kTop) mRoot->pages[i]->opacity.set(0.0);
            mRoot->addChild(mRoot->pages[i]);
        }
        mRoot->backButton = std::make_shared<IconButton>(icons::kBackArrow);
        mRoot->backButton->opacity.set(0.0);
        mRoot->backButton->onTap = [this] { mRoot->go(kTop); };
        mRoot->addChild(mRoot->backButton);
        mRoot->setAccent(ctx.accent, false);

        buildTop();
        buildDisplay();
        buildSound();
        buildNetwork();
        buildDateTime();
        buildAbout();
        refreshSummaries();
    }

    artboard::Segment &root() override { return *mRoot; }

    void layout(app::Context &ctx) override
    {
        const double w = ctx.width, h = ctx.height;
        const double colW = std::max(0.0, std::min(metric::contentMax(), w - 2.0 * space::margin()));
        const double colX = (w - colW) * 0.5;
        const double top = ctx.insetTop + metric::topBar();
        mRoot->insetTop = ctx.insetTop;
        mRoot->colX = colX;
        mRoot->colW = colW;
        // The page runs to the bottom edge and its content ends above the gesture strip: a list
        // scrolls under the pill, and its last row can always be brought clear of it.
        for (const auto &p : mRoot->pages) p->place(top, w, std::max(0.0, h - top), colX, colW, ctx.insetBottom + space::margin());

        // The back arrow is there exactly as much as the top page is not. Its glyph stands in the
        // column the row icons stand in; its 48 dp target is centred on it.
        const double t = metric::touchTarget();
        IconButton &back = *mRoot->backButton;
        back.x.set(colX + space::pad() + metric::glyph() * 0.5 - t * 0.5);
        back.y.set(ctx.insetTop + (metric::topBar() - t) * 0.5);
        back.width.set(t);
        back.height.set(t);
        back.opacity.set(1.0 - mRoot->pages[kTop]->opacity.value());
        back.enabled = mRoot->wanted() != kTop;
        mShown = true;
    }

    bool back() override
    {
        if (mRoot->wanted() == kTop) return false;
        mRoot->go(kTop);
        return true;
    }

    void shellEvent(const std::string &line) override
    {
        const std::vector<std::string> words = splitWords(line);
        if (words.empty() || words[0] != "setting.changed") return;
        std::string key, value;
        for (size_t i = 1; i < words.size(); ++i)
        {
            const size_t eq = words[i].find('=');
            if (eq == std::string::npos) continue;
            const std::string k = words[i].substr(0, eq), v = words[i].substr(eq + 1);
            if (k == "key") key = v;
            else if (k == "value") value = v;
        }
        if (!key.empty()) apply(key, value);
    }

private:
    // ── building ────────────────────────────────────────────────────────────────────────────
    std::shared_ptr<Card> card(PageId page)
    {
        auto c = std::make_shared<Card>();
        mRoot->pages[page]->add(c);
        return c;
    }

    /** A switch row bound to one bool setting. The row flips itself at the tap (the finger gets
     *  its answer at once); the shell's echo then finds it already there. */
    std::shared_ptr<ListRow> switchRow(Card &into, const std::string &key, const char *title, const char *subtitle,
                                       const icons::IconPath &icon, bool initial)
    {
        auto row = std::make_shared<ListRow>(title, ListRow::Trailing::Switch);
        row->icon = &icon;
        row->subtitle = subtitle;
        row->setOnImmediate(initial);
        row->onToggle = [this, key](bool on) {
            mBools[key] = on;
            refreshSummaries();
            mCtx->request("set " + key + "=" + (on ? "true" : "false"));
        };
        mBools[key] = initial;
        mSwitches[key] = row;
        into.add(row);
        return row;
    }

    void buildTop()
    {
        struct Entry
        {
            PageId page;
            const char *title;
            const icons::IconPath *icon;
        };
        const Entry entries[] = {{kDisplay, "Display", &icons::kDisplay},
                                 {kSound, "Sound", &icons::kVolume},
                                 {kNetwork, "Network", &icons::kWifi},
                                 {kDateTime, "Date & time", &icons::kClock},
                                 {kAbout, "About", &icons::kInfo}};
        auto c = card(kTop);
        for (const Entry &e : entries)
        {
            auto row = std::make_shared<ListRow>(e.title, ListRow::Trailing::Chevron);
            row->icon = e.icon;
            const PageId page = e.page;
            row->onTap = [this, page] { mRoot->go(page); };
            mCategory[e.page] = row;
            c->add(row);
        }
        mCategory[kAbout]->divider = false;
    }

    void buildDisplay()
    {
        mBrightness = std::make_shared<TouchSlider>(icons::kBrightness);
        mBrightness->setValue(1.0);
        // The shell refuses a black screen (0.05 is its floor), and so does the request.
        mBrightness->onChange = [this](double v) {
            refreshSummaries();
            mCtx->request("set brightness=" + number(std::max(0.05, v)));
        };
        auto brightness = std::make_shared<Block>("Brightness", mBrightness);
        brightness->readout = [this] { return Block::Readout{percent(mBrightness->shownValue()), "", 0.0}; };
        card(kDisplay)->add(brightness);

        std::vector<std::string> names;
        for (const SizeStep &s : kSizes) names.push_back(s.name);
        mSize = std::make_shared<Segmented>(names);
        mSize->select(1, false);
        mSize->onSelect = [this](int i) {
            refreshSummaries();
            mCtx->request("set scale=" + number(kSizes[(size_t)i].scale));
        };
        auto look = card(kDisplay);
        auto size = std::make_shared<Block>("Display size", mSize);
        size->divider = true;
        look->add(size);

        mAccent = std::make_shared<Swatches>();
        mAccent->select(0, false);
        mAccent->onSelect = [this](int i) { mCtx->request(std::string("set accent=") + accents::at(i).hex); };
        auto accent = std::make_shared<Block>("Accent colour", mAccent);
        // The colour's name cross-fades as the ring passes from one dot to the next.
        accent->readout = [this] {
            const double pos = mAccent->position();
            const int lo = std::clamp((int)std::floor(pos), 0, accents::count() - 1);
            Block::Readout r{accents::at(lo).name, accents::at(lo + 1).name, std::clamp(pos - lo, 0.0, 1.0)};
            // No ring, no name: a colour from elsewhere is not one of these.
            if (mAccent->ringAmount() < 1.0) { r.b.clear(); r.mixB = 1.0 - mAccent->ringAmount(); }
            return r;
        };
        accent->readoutFont = font::sans();
        look->add(accent);

        auto motionCard = card(kDisplay);
        switchRow(*motionCard, "reduced-motion", "Reduce motion", "Replace movement with short fades", icons::kMotion, false)->divider = false;
    }

    void buildSound()
    {
        mVolume = std::make_shared<TouchSlider>(icons::kVolume);
        mVolume->setValue(0.6);
        mVolume->onChange = [this](double v) {
            refreshSummaries();
            mCtx->request("set volume=" + number(v));
        };
        auto volume = std::make_shared<Block>("Volume", mVolume);
        volume->readout = [this] { return Block::Readout{percent(mVolume->shownValue()), "", 0.0}; };
        card(kSound)->add(volume);
        auto c = card(kSound);
        switchRow(*c, "dnd", "Do not disturb", "Notifications arrive silently", icons::kDndMoon, false)->divider = false;
    }

    void buildNetwork()
    {
        auto c = card(kNetwork);
        switchRow(*c, "wifi", "Wi-Fi", "", icons::kWifi, true);
        switchRow(*c, "bluetooth", "Bluetooth", "", icons::kBluetooth, false)->divider = false;
    }

    void buildDateTime()
    {
        auto c = card(kDateTime);
        switchRow(*c, "clock24", "24-hour clock", "13:00 instead of 1:00 pm", icons::kClock, true)->divider = false;
    }

    void buildAbout()
    {
        auto c = card(kAbout);
        // Read-only until the stack has an on-screen keyboard; a disabled row shows no press.
        mDeviceName = std::make_shared<ListRow>("Device name", ListRow::Trailing::Value);
        mDeviceName->value = "Arstro";
        mDeviceName->enabled = false;
        c->add(mDeviceName);
        auto version = std::make_shared<ListRow>("Arstro shell", ListRow::Trailing::Value);
        version->value = "Version 0.1";
        version->divider = false;
        version->enabled = false;
        c->add(version);
        mRoot->pages[kAbout]->add(std::make_shared<Note>(
            "On this build the radios, volume and brightness are not yet connected to hardware. "
            "The shell stores and shows these settings; the real backends are a later milestone."));
    }

    // ── following the shell ─────────────────────────────────────────────────────────────────
    /** One setting as the shell now has it. Before the first frame there is nowhere to travel
     *  from, so values are placed; after it, every change is eased by the control that shows it. */
    void apply(const std::string &key, const std::string &value)
    {
        const bool animate = mShown;
        const auto sw = mSwitches.find(key);
        if (sw != mSwitches.end())
        {
            const bool on = value == "true" || value == "1" || value == "on";
            mBools[key] = on;
            if (animate) sw->second->setOn(on);
            else sw->second->setOnImmediate(on);
        }
        // A slider the finger is holding ignores the model by itself (the kit's rule).
        else if (key == "brightness") mBrightness->setValue(std::atof(value.c_str()));
        else if (key == "volume") mVolume->setValue(std::atof(value.c_str()));
        else if (key == "scale")
        {
            const double s = std::atof(value.c_str());
            int best = 0;
            for (int i = 1; i < (int)kSizes.size(); ++i)
                if (std::fabs(kSizes[(size_t)i].scale - s) < std::fabs(kSizes[(size_t)best].scale - s)) best = i;
            mSize->select(best, animate);
        }
        else if (key == "accent")
        {
            int match = -1;
            for (int i = 0; i < accents::count(); ++i)
                if (sameText(value, accents::at(i).hex)) match = i;
            mAccent->select(match, animate);
            mRoot->setAccent(parseColor(value, mCtx->accent), animate);
        }
        else if (key == "device-name") mDeviceName->value = value;
        else return;
        refreshSummaries();
    }

    /** The one line under each category: what is set, read from the same controls the pages show. */
    void refreshSummaries()
    {
        mCategory[kDisplay]->subtitle = "Brightness " + percent(mBrightness->value()) + "  \xC2\xB7  " +
                                        kSizes[(size_t)mSize->selected()].name + " size";
        mCategory[kSound]->subtitle = "Volume " + percent(mVolume->value()) + (mBools["dnd"] ? "  \xC2\xB7  Do not disturb" : "");
        mCategory[kNetwork]->subtitle = std::string("Wi-Fi ") + (mBools["wifi"] ? "on" : "off") + "  \xC2\xB7  Bluetooth " +
                                        (mBools["bluetooth"] ? "on" : "off");
        mCategory[kDateTime]->subtitle = mBools["clock24"] ? "24-hour clock" : "12-hour clock";
        mCategory[kAbout]->subtitle = mDeviceName->value;
    }

    app::Context *mCtx = nullptr;
    std::shared_ptr<SettingsRoot> mRoot;
    std::map<PageId, std::shared_ptr<ListRow>> mCategory;
    std::map<std::string, std::shared_ptr<ListRow>> mSwitches;
    std::map<std::string, bool> mBools;
    std::shared_ptr<TouchSlider> mBrightness, mVolume;
    std::shared_ptr<Segmented> mSize;
    std::shared_ptr<Swatches> mAccent;
    std::shared_ptr<ListRow> mDeviceName;
    bool mShown = false;  // a frame has been laid out: from here on, changes are eased
};

int main()
{
    SettingsApp app;
    return app::run(app, "arstro.settings");
}
