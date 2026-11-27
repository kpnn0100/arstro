/*
 *  arstro.settings — the few controls Settings needs that the kit does not have yet.
 *
 *  A card that groups rows, a titled block that holds one control, a segmented choice, a row of
 *  accent swatches and a wrapped note. They live here, not in the kit, because only one app uses
 *  them today; the moment a second one does they move to kit/Widgets.h unchanged.
 *
 *  They keep the kit's rules: a setter records intent and `advance` starts the tween, nothing is
 *  stretched, a setter never fires its own callback, and no control is under 48 dp.
 */
#pragma once
#include "Widgets.h"
#include <vector>

namespace arstro
{
namespace settings
{
    using namespace arstro::kit;

    /** Something a page stacks: told its width, it places its own insides and answers its height.
     *  Layout is a pass of its own (not done in `advance`) so the very first frame is already
     *  placed — `advance` runs before the app knows the screen. */
    struct Laid
    {
        virtual ~Laid() = default;
        virtual double arrange(double w) = 0;
    };

    /** A rounded group of rows. It clips its children to its own rounded shape, because a row's
     *  press wash is a plain rectangle and would otherwise poke out of the corners. */
    class Card : public artboard::Segment, public Laid
    {
    public:
        void add(std::shared_ptr<artboard::Segment> part)
        {
            addChild(part);
        }

        double arrange(double w) override
        {
            double y = 0.0;
            for (const auto &c : children())
            {
                double h = metric::rowH();
                if (auto *laid = dynamic_cast<Laid *>(c.get())) h = laid->arrange(w);
                c->x.set(0.0);
                c->y.set(y);
                c->width.set(w);
                c->height.set(h);
                y += h;
            }
            width.set(w);
            height.set(y);
            return y;
        }

        void render(artboard::IRenderTarget &t, const artboard::Transform &parent) const override
        {
            if (isFadedOut()) return;
            t.save();
            t.setTransform(parent.mul(localTransform()));
            roundedRectPath(t, localBounds(), radius::tile());
            t.clipPath();
            artboard::Segment::render(t, parent);
            t.restore();
        }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override
        {
            fillRounded(t, localBounds(), radius::tile(), surface::tile());
        }
    };

    /**
     *  A titled control inside a card: the name on the left, an optional readout on the right,
     *  and one finger-sized control under them. The readout is asked for every frame, so it can
     *  follow an eased value instead of jumping to the target.
     */
    class Block : public artboard::Segment, public Laid
    {
    public:
        Block(std::string title, std::shared_ptr<artboard::Segment> control)
            : mTitle(std::move(title)), mControl(std::move(control))
        {
            addChild(mControl);
        }

        /** Up to two readouts with their opacities — two, so a changing word can cross-fade. */
        struct Readout
        {
            std::string a, b;
            double mixB = 0.0;
        };
        std::function<Readout()> readout;
        const char *readoutFont = font::mono();  // a number by default; a word sets font::sans()
        bool divider = false;

        double arrange(double w) override
        {
            mControl->x.set(space::pad());
            mControl->y.set(space::pad() + kLine + space::gap());
            mControl->width.set(std::max(0.0, w - 2.0 * space::pad()));
            mControl->height.set(metric::touchTarget());
            return space::pad() + kLine + space::gap() + metric::touchTarget() + space::pad();
        }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override
        {
            const double w = width.value(), cy = space::pad() + kLine * 0.5, inner = w - 2.0 * space::pad();
            double readW = 0.0;
            if (readout)
            {
                const Readout r = readout();
                readW = std::min(inner * 0.5, std::max(t.measureText(r.a, type::label(), readoutFont),
                                                        t.measureText(r.b, type::label(), readoutFont)));
                const artboard::Color ink = palette::secondaryForeground();
                drawLabel(t, r.a, w - space::pad() - readW, cy, readW, type::label(), readoutFont, withAlpha(ink, 1.0 - r.mixB), Align::Right);
                drawLabel(t, r.b, w - space::pad() - readW, cy, readW, type::label(), readoutFont, withAlpha(ink, r.mixB), Align::Right);
            }
            drawLabel(t, mTitle, space::pad(), cy, inner - readW - (readW > 0 ? space::pad() : 0.0), type::body(), font::sans(), palette::foreground());
            if (divider) fillRounded(t, {space::pad(), height.value() - 1.0, w - space::pad(), 1.0}, 0.0, surface::hairline());
        }

    private:
        static constexpr double kLine = 24.0;  // the title line: three grid units
        std::string mTitle;
        std::shared_ptr<artboard::Segment> mControl;
    };

    /**
     *  One of a few named steps, on a pill track with a highlight that SLIDES to the chosen one.
     *  The labels take their ink from how close the highlight is, so they change with it rather
     *  than at the tap.
     */
    class Segmented : public artboard::Segment
    {
    public:
        explicit Segmented(std::vector<std::string> labels) : mLabels(std::move(labels)) {}

        std::function<void(int)> onSelect;

        int selected() const { return mWanted; }
        /** From the model. `animate` is false only for the first placement. */
        void select(int index, bool animate)
        {
            mWanted = std::clamp(index, 0, (int)mLabels.size() - 1);
            if (!animate || !mPlaced)
            {
                mApplied = mWanted;
                mPos.set(mWanted);
                mPlaced = true;
            }
        }
        double position() const { return mPos.value(); }

        void advance(double nowMs) override
        {
            if (mWanted != mApplied)
            {
                mApplied = mWanted;
                mPos.animateTo(mWanted, motion::move(), artboard::Easing::EaseInOutCubic, nowMs);
            }
            mPos.update(nowMs);
            artboard::Segment::advance(nowMs);
        }

    protected:
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override
        {
            using T = artboard::Gesture::Type;
            if (g.type == T::Down || g.type == T::Up || g.type == T::Drop) return true;
            if (g.type != T::Click && g.type != T::DoubleClick) return false;
            const int n = (int)mLabels.size();
            const int i = std::clamp((int)(local.x / std::max(1.0, width.value()) * n), 0, n - 1);
            if (i != mWanted)
            {
                mWanted = i;
                if (onSelect) onSelect(i);
            }
            return true;
        }

        void onPaint(artboard::IRenderTarget &t) const override
        {
            const artboard::Rect box = localBounds();
            const int n = (int)mLabels.size();
            if (n == 0) return;
            fillRounded(t, box, radius::pill(), surface::tile());
            const double inset = space::half(), segW = (box.w - 2.0 * inset) / n, pos = mPos.value();
            fillRounded(t, {inset + segW * pos, inset, segW, box.h - 2.0 * inset}, radius::pill(), palette::primary());
            for (int i = 0; i < n; ++i)
            {
                const double near = std::clamp(1.0 - std::fabs(pos - i), 0.0, 1.0);
                drawLabel(t, mLabels[i], inset + segW * i + space::half(), box.h * 0.5, segW - 2.0 * space::half(), type::label(),
                          font::sansMedium(), mix(palette::secondaryForeground(), palette::white(), near), Align::Center);
            }
        }

    private:
        std::vector<std::string> mLabels;
        artboard::Property mPos{0.0};
        int mWanted = 0, mApplied = 0;
        bool mPlaced = false;
    };

    /**
     *  The accent choices as a row of dots. The chosen one wears a ring, and the ring TRAVELS to a
     *  new choice — taking each dot's colour as it passes — while the tick cross-fades from the
     *  old dot to the new one. A colour that is not one of the choices (set from elsewhere) has no
     *  ring: it fades out rather than sitting on the wrong dot.
     */
    class Swatches : public artboard::Segment
    {
    public:
        std::function<void(int)> onSelect;

        /** -1: none of the choices. `animate` is false only for the first placement. */
        void select(int index, bool animate)
        {
            mWanted = std::clamp(index, -1, accents::count() - 1);
            if (!animate || !mPlaced)
            {
                mApplied = mWanted;
                if (mWanted >= 0) mPos.set(mWanted);
                mRing.set(mWanted >= 0 ? 1.0 : 0.0);
                mPlaced = true;
            }
        }
        double position() const { return mPos.value(); }
        double ringAmount() const { return mRing.value(); }

        void advance(double nowMs) override
        {
            if (mWanted != mApplied)
            {
                // A ring that is not showing has nowhere to travel from: it appears in place.
                if (mWanted >= 0)
                {
                    if (mRing.value() <= 0.0) mPos.set(mWanted);
                    else mPos.animateTo(mWanted, motion::move(), artboard::Easing::EaseInOutCubic, nowMs);
                }
                mRing.animateTo(mWanted >= 0 ? 1.0 : 0.0, motion::fade(), artboard::Easing::EaseInOutSine, nowMs);
                mApplied = mWanted;
            }
            mPos.update(nowMs);
            mRing.update(nowMs);
            artboard::Segment::advance(nowMs);
        }

    protected:
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override
        {
            using T = artboard::Gesture::Type;
            if (g.type == T::Down || g.type == T::Up || g.type == T::Drop) return true;
            if (g.type != T::Click && g.type != T::DoubleClick) return false;
            // The nearest dot wins: the gaps between dots belong to their neighbours, so every
            // target is at least as wide as the pitch.
            const int n = accents::count();
            const int i = std::clamp((int)std::lround((local.x - centre(0)) / std::max(1.0, pitch())), 0, n - 1);
            if (i != mWanted)
            {
                mWanted = i;
                if (onSelect) onSelect(i);
            }
            return true;
        }

        void onPaint(artboard::IRenderTarget &t) const override
        {
            const int n = accents::count();
            const double cy = height.value() * 0.5, d = metric::swatch(), pos = mPos.value(), ring = mRing.value();
            for (int i = 0; i < n; ++i)
            {
                const double cx = centre(i);
                fillRounded(t, {cx - d * 0.5, cy - d * 0.5, d, d}, radius::pill(), colour(i));
                const double near = std::clamp(1.0 - std::fabs(pos - i), 0.0, 1.0) * ring;
                const double g = d * 0.5;
                drawIcon(t, icons::kCheck, {cx - g * 0.5, cy - g * 0.5, g, g}, withAlpha(palette::white(), near));
            }
            if (ring <= 0.0) return;
            const int lo = std::clamp((int)std::floor(pos), 0, n - 1), hi = std::min(n - 1, lo + 1);
            const artboard::Color c = mix(colour(lo), colour(hi), std::clamp(pos - lo, 0.0, 1.0));
            // The ring sits half a unit off the dot, inside the 48 dp target.
            const double rd = ringSize() - metric::ring();
            const double cx = centre(0) + pitch() * pos;
            roundedRectPath(t, {cx - rd * 0.5, cy - rd * 0.5, rd, rd}, radius::pill());
            t.setStroke(withAlpha(c, ring), metric::ring());
            t.strokePath();
        }

    private:
        static artboard::Color colour(int i) { return parseColor(accents::at(i).hex, palette::primary()); }
        /** The ring's outer size: half a unit of air around the dot, then the stroke. */
        static double ringSize() { return metric::swatch() + 2.0 * (space::half() + metric::ring()); }
        /** The dots share the width evenly. It is the RING of the first dot that is flush with the
         *  left edge, so a chosen first colour lines up with the title above it. Never further
         *  apart than a touch target and a half. */
        double pitch() const
        {
            const int n = accents::count();
            return std::min(metric::touchTarget() * 1.5, (width.value() - ringSize()) / std::max(1, n - 1));
        }
        double centre(int i) const { return ringSize() * 0.5 + pitch() * i; }

        artboard::Property mPos{0.0}, mRing{0.0};
        int mWanted = -1, mApplied = -1;
        bool mPlaced = false;
    };

    /**
     *  A muted paragraph under the cards, wrapped to the column. Text can only be measured while
     *  drawing, so the line breaks are worked out in the paint and the height they need is used by
     *  the next layout — it is the last thing on its page, so only the scroll extent follows.
     */
    class Note : public artboard::Segment, public Laid
    {
    public:
        explicit Note(std::string text) : mText(std::move(text)) {}

        double arrange(double w) override
        {
            (void)w;
            return space::gap() + mLines * kLine;
        }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override
        {
            const double x = space::pad(), w = width.value() - 2.0 * space::pad();
            if (w <= 0.0) return;
            int line = 0;
            std::string cur;
            const auto flush = [&] {
                if (cur.empty()) return;
                drawLabel(t, cur, x, space::gap() + kLine * (line + 0.5), w, type::label(), font::sans(), palette::mutedForeground());
                ++line;
                cur.clear();
            };
            size_t i = 0;
            while (i < mText.size())
            {
                size_t j = mText.find(' ', i);
                if (j == std::string::npos) j = mText.size();
                const std::string word = mText.substr(i, j - i);
                const std::string trial = cur.empty() ? word : cur + " " + word;
                if (!cur.empty() && t.measureText(trial, type::label(), font::sans()) > w)
                {
                    flush();
                    cur = word;
                }
                else cur = trial;
                i = j + 1;
            }
            flush();
            mLines = std::max(1, line);
        }

    private:
        static constexpr double kLine = 20.0;  // two and a half grid units of leading
        std::string mText;
        mutable int mLines = 3;
    };
}
}
