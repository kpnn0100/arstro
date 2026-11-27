/*
 *  arstro kit — the touch controls the shell and every app share (R-UI-2, R-UI-3).
 *
 *  One file, a handful of small self-drawn Segments: a press surface, a quick-settings tile, a
 *  thick slider, a list row, a scroller, a title bar. They are sized for a finger (nothing
 *  tappable is under 48 dp) and every one of them answers a touch in the frame it lands.
 *
 *  The rules they all keep (arstro.rule.design §1, arstro.rule.animation):
 *    - a setter records intent, advance() owns the clock and starts the tween;
 *    - nothing is stretched: a press shrinks a glyph uniformly, a fill grows as a layout box;
 *    - a setter never fires its own callback.
 */
#pragma once
#include "Draw.h"
#include "GeneratedIcons.h"
#include <cmath>
#include <functional>
#include <memory>
#include <string>

namespace arstro
{
namespace kit
{
    /**
     *  Pressable — anything a finger taps. Owns the animated press amount (in fast, out slower, so
     *  a quick tap is still seen) and the tap itself. A drag that starts on it un-presses it: the
     *  finger turned out to be scrolling, not tapping.
     */
    class Pressable : public artboard::Segment
    {
    public:
        std::function<void()> onTap;

        /** 0 at rest, 1 fully pressed — eased; scale a glyph or lift a fill by it. */
        double pressAmount() const { return mPress.value(); }

        void advance(double nowMs) override
        {
            mNow = nowMs;
            if (mDownWanted != mDownApplied)
            {
                mDownApplied = mDownWanted;
                mPress.animateTo(mDownWanted ? 1.0 : 0.0, mDownWanted ? motion::press() : motion::fade(),
                                 artboard::Easing::EaseOutCubic, nowMs);
            }
            mPress.update(nowMs);
            artboard::Segment::advance(nowMs);
        }

    protected:
        double now() const { return mNow; }
        virtual void tapped() {}

        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override
        {
            (void)local;
            using T = artboard::Gesture::Type;
            if (!enabled) return false;
            switch (g.type)
            {
            case T::Down: mDownWanted = true; return true;
            case T::DragStart: mDownWanted = false; return false;
            case T::Up:
            case T::Drop: mDownWanted = false; return true;
            case T::Click:
            case T::DoubleClick:  // a second quick tap is still a tap: a toggle must not eat it
                tapped();
                if (onTap) onTap();
                return true;
            default: return false;
            }
        }

    private:
        artboard::Property mPress{0.0};
        bool mDownWanted = false, mDownApplied = false;
        double mNow = 0.0;
    };

    /** A quick-settings tile: icon and label on a rounded tile that fills with the accent when on. */
    class Tile : public Pressable
    {
    public:
        Tile(const icons::IconPath &icon, std::string label) : mIcon(&icon), mLabel(std::move(label)) {}

        std::function<void(bool)> onToggle;
        std::string sub;  // the second line: the state in words

        void setOn(bool on) { mOnWanted = on; }
        bool on() const { return mOnWanted; }
        /** For the first placement only: there is nowhere to travel from. */
        void setOnImmediate(bool on) { mOnWanted = mOnApplied = on; mOn.set(on ? 1.0 : 0.0); }
        double onAmount() const { return mOn.value(); }

        void advance(double nowMs) override
        {
            if (mOnWanted != mOnApplied)
            {
                mOnApplied = mOnWanted;
                mOn.animateTo(mOnWanted ? 1.0 : 0.0, motion::fade(), artboard::Easing::EaseInOutCubic, nowMs);
            }
            mOn.update(nowMs);
            Pressable::advance(nowMs);
        }

    protected:
        void tapped() override
        {
            mOnWanted = !mOnWanted;
            if (onToggle) onToggle(mOnWanted);
        }

        void onPaint(artboard::IRenderTarget &t) const override
        {
            const double on = mOn.value(), press = pressAmount();
            const artboard::Rect box = localBounds();
            artboard::Color fill = mix(surface::tile(), palette::primary(), on);
            fill = mix(fill, palette::white(), 0.10 * press);
            fillRounded(t, box, radius::tile(), fill);
            const artboard::Color ink = mix(palette::foreground(), palette::white(), on);
            const double g = metric::glyph() * (1.0 - 0.08 * press);  // uniform: the icon is never stretched
            const double cy = box.h * 0.5;
            drawIcon(t, *mIcon, {space::pad() + (metric::glyph() - g) * 0.5, cy - g * 0.5, g, g}, ink);
            const double tx = space::pad() + metric::glyph() + 12.0, tw = box.w - tx - space::pad();
            if (sub.empty())
                drawLabel(t, mLabel, tx, cy, tw, type::label(), font::sansMedium(), ink);
            else
            {
                drawLabel(t, mLabel, tx, cy - 8.0, tw, type::label(), font::sansMedium(), ink);
                drawLabel(t, sub, tx, cy + 9.0, tw, type::caption(), font::sans(), withAlpha(ink, 0.62));
            }
        }

    private:
        const icons::IconPath *mIcon;
        std::string mLabel;
        artboard::Property mOn{0.0};
        bool mOnWanted = false, mOnApplied = false;
    };

    /**
     *  A thick pill slider (brightness, volume). The finger moves the value by how far it travels,
     *  never by where it landed — a press on the far end of the track must not blast the volume —
     *  and the fill follows the finger exactly; only a value set from outside glides.
     */
    class TouchSlider : public artboard::Segment
    {
    public:
        explicit TouchSlider(const icons::IconPath &icon) : mIcon(&icon) {}

        std::function<void(double)> onChange;

        double value() const { return mValue; }
        bool dragging() const { return mDragging; }
        /** From the model: ignored while the finger holds the slider (a gesture outranks the model). */
        void setValue(double v)
        {
            if (mDragging) return;
            mValue = std::clamp(v, 0.0, 1.0);
            if (!mPlaced) { mShown.reset(mValue); mPlaced = true; }
            mShown.setTarget(mValue);
        }
        double shownValue() const { return mShown.value(); }

        void advance(double nowMs) override
        {
            const double dt = mLast < 0 ? 0.0 : std::min(0.05, (nowMs - mLast) / 1000.0);
            mLast = nowMs;
            if (mDragging) mShown.reset(mValue);
            else mShown.advance(dt, motion::springFast());
            if (mHeldWanted != mHeldApplied)
            {
                mHeldApplied = mHeldWanted;
                mHeld.animateTo(mHeldWanted ? 1.0 : 0.0, motion::fade(), artboard::Easing::EaseOutCubic, nowMs);
            }
            mHeld.update(nowMs);
            artboard::Segment::advance(nowMs);
        }

    protected:
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override
        {
            using T = artboard::Gesture::Type;
            switch (g.type)
            {
            case T::Down:
                mGrabX = local.x;
                mGrabValue = mValue;
                mHeldWanted = true;
                return true;
            case T::DragStart: mDragging = true; return true;
            case T::Drag:
            {
                const double v = std::clamp(mGrabValue + (local.x - mGrabX) / std::max(1.0, width.value()), 0.0, 1.0);
                if (v != mValue)
                {
                    mValue = v;
                    if (onChange) onChange(v);
                }
                return true;
            }
            case T::Up:
            case T::Drop:
                mDragging = false;
                mHeldWanted = false;
                mShown.reset(mValue);
                return true;
            default: return false;
            }
        }

        void onPaint(artboard::IRenderTarget &t) const override
        {
            const artboard::Rect box = localBounds();
            const double h = box.h, v = mShown.value();
            fillRounded(t, box, radius::pill(), surface::tile());
            // The fill is a layout box that grows; its left cap stays a full half-circle, so it is
            // never thinner than the track is tall — below that it would fold into a sliver.
            const double fw = h + (box.w - h) * v;
            fillRounded(t, {0, 0, fw, h}, radius::pill(), mix(palette::primary(), palette::white(), 0.08 * mHeld.value()));
            const double g = metric::glyph();
            drawIcon(t, *mIcon, {(h - g) * 0.5, (h - g) * 0.5, g, g}, palette::white());
        }

    private:
        const icons::IconPath *mIcon;
        double mValue = 0.0, mGrabX = 0.0, mGrabValue = 0.0, mLast = -1.0;
        bool mDragging = false, mPlaced = false, mHeldWanted = false, mHeldApplied = false;
        artboard::Spring mShown{0.0};
        artboard::Property mHeld{0.0};
    };

    /**
     *  A row in a settings-style list: optional icon, a title, an optional second line, and a
     *  trailing switch, value or chevron. The whole row is the touch target.
     */
    class ListRow : public Pressable
    {
    public:
        enum class Trailing { None, Chevron, Switch, Value };

        explicit ListRow(std::string title, Trailing trailing = Trailing::None)
            : mTitle(std::move(title)), mTrailing(trailing) {}

        const icons::IconPath *icon = nullptr;
        std::string subtitle;
        std::string value;       // Trailing::Value
        bool divider = true;     // a hairline under the row; the last row of a group turns it off
        std::function<void(bool)> onToggle;

        void setTitle(std::string s) { mTitle = std::move(s); }
        void setOn(bool on) { mOnWanted = on; }
        void setOnImmediate(bool on) { mOnWanted = mOnApplied = on; mOn.set(on ? 1.0 : 0.0); }
        bool on() const { return mOnWanted; }
        double onAmount() const { return mOn.value(); }

        void advance(double nowMs) override
        {
            if (mOnWanted != mOnApplied)
            {
                mOnApplied = mOnWanted;
                mOn.animateTo(mOnWanted ? 1.0 : 0.0, motion::fade(), artboard::Easing::EaseInOutCubic, nowMs);
            }
            mOn.update(nowMs);
            Pressable::advance(nowMs);
        }

    protected:
        void tapped() override
        {
            if (mTrailing != Trailing::Switch) return;
            mOnWanted = !mOnWanted;
            if (onToggle) onToggle(mOnWanted);
        }

        void onPaint(artboard::IRenderTarget &t) const override
        {
            const artboard::Rect box = localBounds();
            const double cy = box.h * 0.5, press = pressAmount();
            if (press > 0.0) fillRounded(t, box, 0.0, palette::whiteAlpha(0.05 * press));
            double x = space::pad();
            if (icon)
            {
                const double g = metric::glyph();
                drawIcon(t, *icon, {x, cy - g * 0.5, g, g}, palette::secondaryForeground());
                x += g + space::pad();
            }
            // The trailing control's BOX, not its glyph, is what the text must stop before.
            double trailW = 0.0;
            if (mTrailing == Trailing::Switch) trailW = kSwitchW;
            else if (mTrailing == Trailing::Chevron) trailW = 16.0;
            else if (mTrailing == Trailing::Value) trailW = std::min(box.w * 0.4, t.measureText(value, type::label(), font::sans()));
            const double right = box.w - space::pad();
            const double tw = std::max(0.0, right - trailW - (trailW > 0 ? space::pad() : 0.0) - x);
            if (subtitle.empty())
                drawLabel(t, mTitle, x, cy, tw, type::body(), font::sans(), palette::foreground());
            else
            {
                drawLabel(t, mTitle, x, cy - 9.0, tw, type::body(), font::sans(), palette::foreground());
                drawLabel(t, subtitle, x, cy + 10.0, tw, type::caption(), font::sans(), palette::mutedForeground());
            }
            if (mTrailing == Trailing::Switch)
            {
                const double on = mOn.value();
                const artboard::Rect track{right - kSwitchW, cy - kSwitchH * 0.5, kSwitchW, kSwitchH};
                fillRounded(t, track, radius::pill(), mix(palette::switchBackground(), palette::primary(), on));
                const double d = kSwitchH - 6.0;
                fillRounded(t, {track.x + 3.0 + (kSwitchW - d - 6.0) * on, track.y + 3.0, d, d}, radius::pill(), palette::white());
            }
            else if (mTrailing == Trailing::Chevron)
                drawIcon(t, icons::kChevronRight, {right - 18.0, cy - 9.0, 18.0, 18.0}, palette::mutedForeground());
            else if (mTrailing == Trailing::Value)
                drawLabel(t, value, right - trailW, cy, trailW, type::label(), font::sans(), palette::mutedForeground(), Align::Right);
            if (divider) fillRounded(t, {x, box.h - 1.0, box.w - x, 1.0}, 0.0, surface::hairline());
        }

    private:
        static constexpr double kSwitchW = 50.0, kSwitchH = 30.0;
        std::string mTitle;
        Trailing mTrailing;
        artboard::Property mOn{0.0};
        bool mOnWanted = false, mOnApplied = false;
    };

    /**
     *  TouchScroll — a vertical scroller a finger can start on ANY child.
     *
     *  Artboard captures a press on the deepest child and delivers the whole drag there; nothing
     *  bubbles, so a list of tappable rows inside its ScrollView cannot be scrolled by touching a
     *  row (ledger: AB-7). Until the library can do it, this container keeps its content OUT of
     *  the child list: to Artboard it is one leaf, so every gesture inside it arrives here first.
     *  It passes them on to the content — translated into the content's own space — until a drag
     *  turns out to be vertical; then the content is released as if the finger had left, and the
     *  drag becomes the scroll.
     *
     *  The drag follows the finger exactly; the fling decays; past either end it gives way at half
     *  speed and a spring brings it back. One viewport rectangle (this segment's bounds) is used
     *  by the clamp, the clip and the hit test.
     */
    class TouchScroll : public artboard::Segment
    {
    public:
        void setContent(std::shared_ptr<artboard::Segment> content) { mContent = std::move(content); }
        void setContentHeight(double h) { mContentH = h; }
        double offset() const { return mOffset; }
        double maxOffset() const { return std::max(0.0, mContentH - height.value()); }
        bool moving() const { return mHeld || mReturning || mVelocity != 0.0; }
        /** Back to the top with no travel — only for a scroller nobody can see (a page about to
         *  fade in): on screen it would be a jump. */
        void resetToTop() { mOffset = 0.0; mVelocity = 0.0; mReturning = false; }

        void render(artboard::IRenderTarget &t, const artboard::Transform &parent) const override
        {
            if (isFadedOut()) return;
            mWorld = parent.mul(localTransform());
            if (!mContent) return;
            t.save();
            t.setTransform(mWorld);
            t.clipRect(0, 0, width.value(), height.value());
            artboard::Transform inner = mWorld;
            inner.translate(0.0, -mOffset);
            mContent->render(t, inner);
            t.restore();
        }

        void advance(double nowMs) override
        {
            const double dt = mLast < 0 ? 0.0 : std::min(0.05, (nowMs - mLast) / 1000.0);
            mLast = nowMs;
            const double hi = maxOffset();
            if (!mHeld)
            {
                if (mOffset < -0.01 || mOffset > hi + 0.01)
                {
                    if (!mReturning) { mBack.reset(mOffset); mReturning = true; }
                    mBack.setTarget(mOffset < 0.0 ? 0.0 : hi);
                    mOffset = mBack.advance(dt, motion::springFast());
                    mVelocity = 0.0;
                    if (!mBack.isMoving(0.3)) { mOffset = mBack.target(); mReturning = false; }
                }
                else
                {
                    mReturning = false;
                    if (std::fabs(mVelocity) > 4.0)
                    {
                        mOffset += mVelocity * dt;
                        mVelocity *= std::exp(-3.6 * dt);
                    }
                    else mVelocity = 0.0;
                }
            }
            else mReturning = false;
            if (mContent) mContent->advance(nowMs);
            artboard::Segment::advance(nowMs);
        }

    protected:
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override
        {
            using T = artboard::Gesture::Type;
            if (g.type == T::Down)
            {
                mScrolling = false;
                mVelocity = 0.0;  // a touch stops a fling, like a finger on a spinning wheel
            }
            else if (g.type == T::DragStart && !mScrolling &&
                     std::fabs(g.pos.y - g.start.y) >= std::fabs(g.pos.x - g.start.x) && maxOffset() > 0.0)
            {
                // Take the gesture from the content: an Up far outside releases its pressed look,
                // and no Click follows because the recogniser already knows this press became a drag.
                artboard::Gesture release = g;
                release.type = T::Up;
                release.pos = artboard::Point{-1e7, -1e7};
                if (mContent) mContent->onGesture(release);
                mScrolling = true;
                mGrabOffset = mOffset;
                mGrabY = g.start.y;
            }
            if (!mScrolling)
            {
                if (mContent)
                {
                    // Into the content's own space: it is its own root, drawn `mOffset` up.
                    artboard::Gesture inner = g;
                    inner.pos = artboard::Point{local.x, local.y + mOffset};
                    const artboard::Point s = toLocal(g.start);
                    inner.start = artboard::Point{s.x, s.y + mOffset};
                    mContent->onGesture(inner);
                }
                return true;
            }
            if (g.type == T::Drag || g.type == T::DragStart)
            {
                const double raw = mGrabOffset - (g.pos.y - mGrabY), hi = maxOffset();
                mOffset = raw < 0.0 ? raw * 0.5 : raw > hi ? hi + (raw - hi) * 0.5 : raw;
                mHeld = true;
            }
            else if (g.type == T::Fling)
                mVelocity = -g.velocity.y;
            else if (g.type == T::Drop || g.type == T::Up)
                mHeld = false;
            return true;
        }

        void onOverlay(artboard::IRenderTarget &t) const override
        {
            if (mContent)
            {
                // The content's own overlay pass (open menus, rings), clipped to the viewport.
                t.save();
                t.clipRect(0, 0, width.value(), height.value());
                artboard::Transform inner = mWorld;
                inner.translate(0.0, -mOffset);
                mContent->renderOverlay(t, inner);
                t.restore();
                t.setTransform(mWorld);
            }
            // The bar is drawn only when there is something to scroll: a permanent track would
            // claim there is more when there is not.
            const double hi = maxOffset(), h = height.value();
            if (hi <= 0.0 || h <= 0.0) return;
            const double barH = std::max(32.0, h * h / mContentH);
            const double k = std::clamp(mOffset / hi, 0.0, 1.0);
            fillRounded(t, {width.value() - 5.0, 4.0 + (h - 8.0 - barH) * k, 3.0, barH}, radius::pill(), palette::whiteAlpha(0.16));
        }

    private:
        std::shared_ptr<artboard::Segment> mContent;
        mutable artboard::Transform mWorld;
        double mContentH = 0.0, mOffset = 0.0, mVelocity = 0.0;
        double mGrabOffset = 0.0, mGrabY = 0.0, mLast = -1.0;
        bool mScrolling = false, mHeld = false, mReturning = false;
        artboard::Spring mBack{0.0};
    };

    /** A round icon button (back, close). Its touch target is the full 48 dp box. */
    class IconButton : public Pressable
    {
    public:
        explicit IconButton(const icons::IconPath &icon) : mIcon(&icon) {}

    protected:
        void onPaint(artboard::IRenderTarget &t) const override
        {
            const artboard::Rect box = localBounds();
            const double press = pressAmount();
            fillRounded(t, box, radius::pill(), palette::whiteAlpha(0.10 * press));
            const double g = metric::glyph() * (1.0 - 0.10 * press);
            drawIcon(t, *mIcon, {(box.w - g) * 0.5, (box.h - g) * 0.5, g, g}, palette::foreground());
        }

    private:
        const icons::IconPath *mIcon;
    };

    /** A quiet text button on a tinted pill ("Clear all"). */
    class TextButton : public Pressable
    {
    public:
        explicit TextButton(std::string label) : mLabel(std::move(label)) {}
        void setLabel(std::string s) { mLabel = std::move(s); }
        /** The width this label needs, valid during a render only. */
        static double widthFor(artboard::IRenderTarget &t, const std::string &label)
        {
            return t.measureText(label, type::label(), font::sansMedium()) + 2.0 * space::pad();
        }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override
        {
            const artboard::Rect box = localBounds();
            fillRounded(t, box, radius::pill(), palette::whiteAlpha(0.07 + 0.07 * pressAmount()));
            drawLabel(t, mLabel, 0, box.h * 0.5, box.w, type::label(), font::sansMedium(), palette::foreground(), Align::Center);
        }

    private:
        std::string mLabel;
    };
}
}
