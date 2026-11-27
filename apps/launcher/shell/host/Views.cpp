#include "Views.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace shell
{
    using namespace kit;
    using artboard::Easing;
    using artboard::Rect;

    namespace
    {
        constexpr double kHeaderH = 40.0;  // the "Notifications" line above the list

        bool settingOn(const Model &m, const char *key)
        {
            auto it = m.settings.find(key);
            return it != m.settings.end() && it->second == "true";
        }
        double settingNumber(const Model &m, const char *key)
        {
            auto it = m.settings.find(key);
            return it == m.settings.end() ? 0.0 : std::atof(it->second.c_str());
        }
        std::string num(double v)
        {
            char b[32];
            std::snprintf(b, sizeof b, "%.3g", v);
            return b;
        }
    }

    void drawAppIcon(artboard::IRenderTarget &t, const AppInfo &app, int image, const Rect &r, double alpha)
    {
        if (image)
        {
            // The picture is clipped to the family's icon shape, so third-party artwork cannot
            // break the grid's rhythm.
            t.save();
            roundedRectPath(t, r, r.w * radius::icon());
            t.clipPath();
            if (alpha < 0.999) t.pushLayer(alpha);
            t.drawImage(image, r);
            if (alpha < 0.999) t.popLayer();
            t.restore();
            return;
        }
        drawMonogram(t, r, app.name, parseColor(app.accent, palette::primary()), alpha);
    }

    // ── AppIcon ──────────────────────────────────────────────────────────────────────────────

    Rect AppIcon::iconBox() const
    {
        const double s = metric::iconSize();
        return {(width.value() - s) * 0.5, 8.0, s, s};
    }

    void AppIcon::onPaint(artboard::IRenderTarget &t) const
    {
        const Rect box = iconBox();
        const double k = 1.0 - 0.08 * pressAmount();  // uniform press: the icon shrinks, it is never squashed
        const Rect r{box.x + box.w * (1 - k) * 0.5, box.y + box.h * (1 - k) * 0.5, box.w * k, box.h * k};
        drawAppIcon(t, app, image, r);
        drawLabel(t, app.name, 4.0, box.bottom() + 18.0, width.value() - 8.0, type::caption(), font::sans(),
                  palette::foreground(), Align::Center);
    }

    // ── HomeView ─────────────────────────────────────────────────────────────────────────────

    HomeView::HomeView(Dispatch dispatch, IconImage image) : mDispatch(std::move(dispatch)), mImage(std::move(image)) {}

    void HomeView::sync(const Model &m, const std::string &time, const std::string &date, double nowMs)
    {
        mNow = nowMs;
        if (time != mTime || date != mDate)
        {
            mTime = time;
            mDate = date;
            mBusy.touch(nowMs);
        }
        std::string key;
        for (const AppInfo &a : m.apps) key += a.id + "|" + a.name + "|" + a.icon + "|" + a.accent + "\n";
        if (key != mAppsKey || mIcons.size() != m.apps.size())
        {
            // A structure change: rebuild. New icons fade in; the grid is re-placed below.
            mAppsKey = key;
            clearChildren();
            mIcons.clear();
            for (const AppInfo &a : m.apps)
            {
                auto icon = std::make_shared<AppIcon>();
                icon->app = a;
                icon->image = mImage ? mImage(a) : 0;
                icon->opacity.set(0.0);
                icon->opacity.animateTo(1.0, motion::move(), Easing::EaseInOutSine, nowMs);
                const std::string id = a.id;
                icon->onTap = [this, id] { mDispatch("app launch " + id); };
                addChild(icon);
                mIcons.push_back(icon);
            }
            mPlaced = false;
            mBusy.touch(nowMs);
        }
        const bool empty = m.apps.empty();
        const double want = empty ? 1.0 : 0.0;
        if (!mEmptyAlpha.isAnimating() && std::fabs(mEmptyAlpha.value() - want) > 1e-3)
        {
            mEmptyAlpha.animateTo(want, motion::move(), Easing::EaseInOutSine, nowMs);
            mBusy.touch(nowMs);
        }
        mEmptyAlpha.update(nowMs);
    }

    void HomeView::layout(double w, double h)
    {
        width.set(w);
        height.set(h);
        const int n = (int)mIcons.size();
        if (!n) return;
        // The grid is a block of whole cells, centred, in the lower part of the screen; the clock
        // has the upper part. Columns come from the width that is actually there.
        const int cols = std::max(1, std::min(n, (int)((w - 2 * space::margin()) / kCellW)));
        const int rows = (n + cols - 1) / cols;
        const double gridW = cols * kCellW, gridH = rows * kCellH;
        const double top = std::max(h * 0.46, h - metric::navBar() - space::margin() - gridH);
        const bool moved = mPlaced && (w != mLaidW || h != mLaidH);
        for (int i = 0; i < n; ++i)
        {
            const double x = (w - gridW) * 0.5 + (i % cols) * kCellW, y = top + (i / cols) * kCellH;
            AppIcon &icon = *mIcons[i];
            icon.width.set(kCellW);
            icon.height.set(kCellH);
            if (moved)
            {
                // A resize re-flows the grid: every icon travels to its new cell.
                icon.x.animateTo(x, motion::move(), Easing::EaseInOutCubic, mNow);
                icon.y.animateTo(y, motion::move(), Easing::EaseInOutCubic, mNow);
            }
            else if (!mPlaced) { icon.x.set(x); icon.y.set(y); }
        }
        if (moved) mBusy.touch(mNow);
        mPlaced = true;
        mLaidW = w;
        mLaidH = h;
    }

    bool HomeView::iconRect(const std::string &appId, Rect &out) const
    {
        for (const auto &icon : mIcons)
            if (icon->app.id == appId)
            {
                const Rect b = icon->iconBox();
                out = {icon->x.value() + b.x, icon->y.value() + b.y, b.w, b.h};
                return true;
            }
        return false;
    }

    void HomeView::onPaint(artboard::IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        // The clock: large, light, and the only decoration the home screen has.
        const double size = std::min(type::display(), h * 0.16);
        const double cy = h * 0.22;
        drawLabel(t, mTime, space::margin(), cy, w - 2 * space::margin(), size, font::sans(), palette::foreground(),
                  Align::Center, -0.02 * size);
        drawLabel(t, mDate, space::margin(), cy + size * 0.62 + 10.0, w - 2 * space::margin(), type::body(), font::sans(),
                  palette::secondaryForeground(), Align::Center);
        const double e = mEmptyAlpha.value();
        if (e > 0.0)
        {
            drawLabel(t, "No apps installed", space::margin(), h * 0.62, w - 2 * space::margin(), type::title(),
                      font::sansMedium(), withAlpha(palette::foreground(), e), Align::Center);
            drawLabel(t, "Install a bundle with  arstro-pm install <bundle>", space::margin(), h * 0.62 + 30.0,
                      w - 2 * space::margin(), type::label(), font::sans(), withAlpha(palette::mutedForeground(), e), Align::Center);
        }
    }

    // ── StatusBarView ────────────────────────────────────────────────────────────────────────

    void StatusBarView::sync(const Model &m, const std::string &time, double nowMs)
    {
        if (time != mTime) { mTime = time; mBusy.touch(nowMs); }
        mWant[0] = settingOn(m, "wifi");
        mWant[1] = settingOn(m, "bluetooth");
        mWant[2] = settingOn(m, "dnd");
        if (!mPlaced)
        {
            for (int i = 0; i < 3; ++i) { mApplied[i] = mWant[i]; mAmount[i].set(mWant[i] ? 1.0 : 0.0); }
            mPlaced = true;
            mBusy.touch(nowMs);
        }
    }

    void StatusBarView::advance(double nowMs)
    {
        for (int i = 0; i < 3; ++i)
        {
            if (mWant[i] != mApplied[i])
            {
                mApplied[i] = mWant[i];
                mAmount[i].animateTo(mWant[i] ? 1.0 : 0.0, motion::fade(), Easing::EaseInOutSine, nowMs);
                mBusy.touch(nowMs);
            }
            mAmount[i].update(nowMs);
        }
        artboard::Segment::advance(nowMs);
    }

    void StatusBarView::onPaint(artboard::IRenderTarget &t) const
    {
        const double w = width.value(), cy = height.value() * 0.5 + 1.0;
        drawLabel(t, mTime, space::pad(), cy, 120.0, type::caption(), font::sansMedium(), palette::foreground());
        // Right to left. An icon that is switching on makes room as it fades in: its width in the
        // row is its eased amount, so its neighbours slide instead of jumping.
        const icons::IconPath *glyphs[3] = {&icons::kWifi, &icons::kBluetooth, &icons::kDndMoon};
        const double g = 15.0;
        double x = w - space::pad();
        for (int i = 0; i < 3; ++i)
        {
            const double a = mAmount[i].value();
            if (a <= 0.0) continue;
            x -= (g + 10.0) * a;
            drawIcon(t, *glyphs[i], {x, cy - g * 0.5, g, g}, withAlpha(palette::foreground(), a), 2.0);
        }
    }

    // ── NotificationList ─────────────────────────────────────────────────────────────────────

    void NotificationList::sync(const std::vector<Notification> &list, double nowMs)
    {
        for (Item &it : mItems)
        {
            const bool alive = std::any_of(list.begin(), list.end(), [&](const Notification &n) { return n.id == it.n.id; });
            if (!alive && !it.dying)
            {
                it.dying = true;
                it.alpha.animateTo(0.0, motion::fade(), Easing::EaseInOutSine, nowMs);
            }
        }
        // Newest first: a new one is inserted at its place in the model's order.
        for (size_t i = 0; i < list.size(); ++i)
        {
            const bool known = std::any_of(mItems.begin(), mItems.end(), [&](const Item &it) { return it.n.id == list[i].id && !it.dying; });
            if (known) continue;
            Item it;
            it.n = list[i];
            it.alpha.set(0.0);
            // The first fill of the list is a placement, not an arrival: nothing fades in on boot.
            if (mFirst) it.alpha.set(1.0);
            else it.alpha.animateTo(1.0, motion::move(), Easing::EaseInOutSine, nowMs);
            size_t at = 0, live = 0;
            for (; at < mItems.size() && live < i; ++at)
                if (!mItems[at].dying) ++live;
            mItems.insert(mItems.begin() + at, std::move(it));
        }
        mFirst = false;
        // Re-flow: each living item's slot; a dying one keeps its place until it has faded.
        double y = kHeaderH;
        for (Item &it : mItems)
        {
            if (it.dying) continue;
            if (it.fresh) { it.y.set(y); it.fresh = false; }
            else if (it.yTarget != y) it.y.animateTo(y, motion::move(), Easing::EaseInOutCubic, nowMs);
            it.yTarget = y;
            y += kItemH + kGap;
        }
        const bool empty = list.empty();
        if (empty != mEmptyApplied || (mEmpty.value() == 0.0 && empty && !mEmpty.isAnimating()))
        {
            mEmptyApplied = empty;
            mEmpty.animateTo(empty ? 1.0 : 0.0, motion::move(), Easing::EaseInOutSine, nowMs);
        }
    }

    void NotificationList::advance(double nowMs)
    {
        for (Item &it : mItems)
        {
            it.y.update(nowMs);
            it.alpha.update(nowMs);
        }
        mItems.erase(std::remove_if(mItems.begin(), mItems.end(),
                                    [](const Item &it) { return it.dying && !it.alpha.isAnimating() && it.alpha.value() <= 0.001; }),
                     mItems.end());
        mEmpty.update(nowMs);
        Pressable::advance(nowMs);
    }

    bool NotificationList::moving() const
    {
        if (mEmpty.isAnimating()) return true;
        for (const Item &it : mItems)
            if (it.y.isAnimating() || it.alpha.isAnimating()) return true;
        return false;
    }

    double NotificationList::contentHeight() const
    {
        double bottom = kHeaderH + 56.0;  // room for the "No notifications" line
        for (const Item &it : mItems)
            if (!it.dying) bottom = std::max(bottom, it.y.value() + kItemH + kGap);
        return bottom;
    }

    bool NotificationList::handleGesture(const artboard::Gesture &g, const artboard::Point &local)
    {
        if (g.type == artboard::Gesture::Type::Click || g.type == artboard::Gesture::Type::DoubleClick)
        {
            // The whole right end of a card is the dismiss target — 48 dp, not the 18 dp glyph.
            for (const Item &it : mItems)
                if (!it.dying && local.y >= it.y.value() && local.y <= it.y.value() + kItemH &&
                    local.x >= width.value() - metric::touchTarget() - 8.0)
                {
                    if (onDismiss) onDismiss(it.n.id);
                    return true;
                }
            return true;
        }
        return Pressable::handleGesture(g, local);
    }

    void NotificationList::onPaint(artboard::IRenderTarget &t) const
    {
        const double w = width.value();
        drawLabel(t, "NOTIFICATIONS", 4.0, kHeaderH * 0.5, w * 0.6, type::micro(), font::sansSemiBold(),
                  palette::mutedForeground(), Align::Left, 0.13 * type::micro());
        const double e = mEmpty.value();
        if (e > 0.0)
            drawLabel(t, "No notifications", 0, kHeaderH + 28.0, w, type::body(), font::sans(),
                      withAlpha(palette::mutedForeground(), e), Align::Center);
        for (const Item &it : mItems)
        {
            const double a = it.alpha.value(), y = it.y.value();
            if (a <= 0.0) continue;
            fillRounded(t, {0, y, w, kItemH}, radius::tile(), withAlpha(surface::tile(), a));
            const double tw = w - 2 * space::pad() - metric::touchTarget();
            if (it.n.body.empty())
                drawLabel(t, it.n.title, space::pad(), y + kItemH * 0.5, tw, type::body(), font::sansMedium(),
                          withAlpha(palette::foreground(), a));
            else
            {
                drawLabel(t, it.n.title, space::pad(), y + 24.0, tw, type::body(), font::sansMedium(), withAlpha(palette::foreground(), a));
                drawLabel(t, it.n.body, space::pad(), y + 46.0, tw, type::label(), font::sans(), withAlpha(palette::secondaryForeground(), a));
            }
            drawIcon(t, icons::kClose, {w - space::pad() - 16.0, y + kItemH * 0.5 - 8.0, 16.0, 16.0},
                     withAlpha(palette::mutedForeground(), a), 2.0);
        }
    }

    // ── ShadeView ────────────────────────────────────────────────────────────────────────────

    ShadeView::ShadeView(Dispatch dispatch) : mDispatch(std::move(dispatch))
    {
        mScroll = std::make_shared<TouchScroll>();
        mContent = std::make_shared<artboard::Segment>();
        mWifi = std::make_shared<Tile>(icons::kWifi, "Wi-Fi");
        mBluetooth = std::make_shared<Tile>(icons::kBluetooth, "Bluetooth");
        mDnd = std::make_shared<Tile>(icons::kDndMoon, "Do not disturb");
        mMotion = std::make_shared<Tile>(icons::kMotion, "Reduce motion");
        mBrightness = std::make_shared<TouchSlider>(icons::kBrightness);
        mVolume = std::make_shared<TouchSlider>(icons::kVolume);
        mList = std::make_shared<NotificationList>();
        mClear = std::make_shared<TextButton>("Clear all");

        const auto bind = [this](const std::shared_ptr<Tile> &tile, const char *key) {
            const std::string k = key;
            tile->onToggle = [this, k](bool on) { mDispatch("set " + k + "=" + (on ? "true" : "false")); };
        };
        bind(mWifi, "wifi");
        bind(mBluetooth, "bluetooth");
        bind(mDnd, "dnd");
        bind(mMotion, "reduced-motion");
        mBrightness->onChange = [this](double v) { mDispatch("set brightness=" + num(std::max(0.05, v))); };
        mVolume->onChange = [this](double v) { mDispatch("set volume=" + num(v)); };
        mList->onDismiss = [this](int id) { mDispatch("notify dismiss " + std::to_string(id)); };
        mClear->onTap = [this] { mDispatch("notify clear"); };
        mClear->opacity.set(0.0);

        for (const std::shared_ptr<artboard::Segment> &s :
             std::initializer_list<std::shared_ptr<artboard::Segment>>{mWifi, mBluetooth, mDnd, mMotion, mBrightness, mVolume, mList, mClear})
            mContent->addChild(s);
        mScroll->setContent(mContent);
        addChild(mScroll);
    }

    void ShadeView::sync(const Model &m, const std::string &time, const std::string &date, double nowMs)
    {
        if (time != mTime || date != mDate) { mTime = time; mDate = date; mBusy.touch(nowMs); }
        if (m.revision == mRevision && mPlaced) return;
        mRevision = m.revision;
        const struct { Tile *tile; const char *key; const char *on; const char *off; } tiles[] = {
            {mWifi.get(), "wifi", "On", "Off"}, {mBluetooth.get(), "bluetooth", "On", "Off"},
            {mDnd.get(), "dnd", "On", "Off"}, {mMotion.get(), "reduced-motion", "On", "Off"}};
        for (const auto &e : tiles)
        {
            const bool on = settingOn(m, e.key);
            if (!mPlaced) e.tile->setOnImmediate(on);
            else e.tile->setOn(on);
            e.tile->sub = on ? e.on : e.off;
        }
        mBrightness->setValue(settingNumber(m, "brightness"));
        mVolume->setValue(settingNumber(m, "volume"));
        mList->sync(m.notifications, nowMs);
        mClearWanted = !m.notifications.empty();
        if (mClearWanted != mClearApplied || !mPlaced)
        {
            mClearApplied = mClearWanted;
            // Opacity is the show/hide: at zero the button is also not hit-testable.
            if (!mPlaced) mClear->opacity.set(mClearWanted ? 1.0 : 0.0);
            else mClear->opacity.animateTo(mClearWanted ? 1.0 : 0.0, motion::fade(), Easing::EaseInOutSine, nowMs);
        }
        mPlaced = true;
        mBusy.touch(nowMs);
    }

    void ShadeView::layout(double w, double h)
    {
        width.set(w);
        height.set(h);
        // One column, as wide as the screen allows up to a comfortable reading width.
        mColW = std::min(w - 2 * space::pad(), 720.0);
        mColX = (w - mColW) * 0.5;
        const double top = metric::statusBar() + 76.0;  // under the status bar and the time header
        const double bottom = h - 36.0;                  // above the grab handle
        mScroll->x.set(mColX);
        mScroll->y.set(top);
        mScroll->width.set(mColW);
        mScroll->height.set(std::max(0.0, bottom - top));
        mContent->width.set(mColW);

        const double gap = 12.0, tileH = 64.0, sliderH = 48.0;
        const bool wide = mColW >= 708.0;  // four tiles in a row only when each still fits its label
        const int cols = wide ? 4 : 2;
        const double tileW = (mColW - gap * (cols - 1)) / cols;
        const std::shared_ptr<Tile> tiles[4] = {mWifi, mBluetooth, mDnd, mMotion};
        double y = 4.0;
        for (int i = 0; i < 4; ++i)
        {
            tiles[i]->x.set((i % cols) * (tileW + gap));
            tiles[i]->y.set(y + (i / cols) * (tileH + gap));
            tiles[i]->width.set(tileW);
            tiles[i]->height.set(tileH);
        }
        y += (4 / cols) * (tileH + gap) + 4.0;
        const double sliderW = wide ? (mColW - gap) * 0.5 : mColW;
        mBrightness->x.set(0);
        mBrightness->y.set(y);
        mBrightness->width.set(sliderW);
        mBrightness->height.set(sliderH);
        mVolume->x.set(wide ? sliderW + gap : 0);
        mVolume->y.set(wide ? y : y + sliderH + gap);
        mVolume->width.set(sliderW);
        mVolume->height.set(sliderH);
        y += (wide ? 1 : 2) * (sliderH + gap) + 8.0;
        mNotesY = y;
        mList->x.set(0);
        mList->y.set(y);
        mList->width.set(mColW);
        mList->height.set(mList->contentHeight());
        mClear->width.set(92.0);
        mClear->height.set(32.0);
        mClear->x.set(mColW - 92.0);
        mClear->y.set(y + 4.0);
        const double total = y + mList->contentHeight() + 8.0;
        mContent->height.set(total);
        mScroll->setContentHeight(total);
    }

    void ShadeView::onPaint(artboard::IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        fillRounded(t, {0, 0, w, h}, 0.0, surface::sheet());
        const double y = metric::statusBar() + 34.0;
        drawLabel(t, mTime, mColX + 4.0, y, mColW * 0.5, type::headline(), font::sans(), palette::foreground());
        drawLabel(t, mDate, mColX + mColW * 0.4, y + 4.0, mColW * 0.6 - 4.0, type::label(), font::sans(),
                  palette::mutedForeground(), Align::Right);
        // The handle: where a finger pushes the sheet back up.
        fillRounded(t, {(w - 40.0) * 0.5, h - 18.0, 40.0, 4.0}, radius::pill(), palette::whiteAlpha(0.28));
    }
}
}
