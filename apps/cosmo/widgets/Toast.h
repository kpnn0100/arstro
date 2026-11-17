/*
 *  cosmo_v2 by arstro — Toast: the family's ONE transient message (R-FAMILY-2).
 *
 *  A short sentence that says something happened, shown top-centre under an app's top bar, held
 *  3200 ms, then gone. Three kinds, told apart by SHAPE and COLOUR (a colour-blind eye gets the
 *  shape): every kind has the popover's ground and a neutral border — the accent is not a toast
 *  colour; a WARNING adds a 3 px bar at its left and a triangle in `palette::warning()`; an ERROR a
 *  3 px bar and a crossed circle in `palette::destructive()`; an INFO adds nothing.
 *
 *  Motion (arstro.rule.animation): it fades in WHILE sliding down 12 px into place (EaseOut, 150 ms),
 *  holds, and fades out WHILE sliding back up (EaseInOut, 300 ms) — never "stop, then fade". A toast
 *  shown while one is up CROSS-FADES with it (200 ms): the old words fade out as the new fade in,
 *  never a cut.
 *
 *  Why it is here and not in an app: Solaris had exactly this toast drawn inside its App
 *  (R-UI-15), Cosmo and Interstellar each said the same things a different way. The behaviour and
 *  every number are Solaris's, so Solaris can swap to this class with no visible change.
 *
 *  THE CALLER PLACES IT — it does not know any top bar. Give it the band it may use: `x`, `y` (the
 *  toast's resting top edge) and `width` (the band it is centred in; its text is ellipsized to
 *  min(kMaxWidth, width − 2·kSideMargin)). It paints in the overlay pass, so as a child it sits
 *  over its siblings; a host that draws it over everything renders it standalone
 *  (`toast.renderOverlay(target, rootTransform)`). It never takes input.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include <functional>
#include <string>

namespace arstro
{
namespace cosmo_v2
{
    class Toast : public artboard::Segment
    {
    public:
        /** Something was done · it worked and something is owed · it was refused. */
        enum class Kind { Info, Warning, Error };

        static constexpr double kHeight = 28.0;
        static constexpr double kBarW = 3.0;         // a warning's and an error's bar, at its left
        static constexpr double kSlide = 12.0;       // how far it travels while it fades
        static constexpr double kMaxWidth = 640.0;   // the widest its TEXT may be
        static constexpr double kSideMargin = 32.0;  // kept clear at each end of the band
        static constexpr double kGlyph = 12.0;       // the kind's glyph box

        Toast();

        /** Show a toast now. One already showing cross-fades into this one. */
        void show(Kind kind, const std::string &text, double nowMs);
        /** The same with no clock (a setter has none): it begins on the next `advance`. */
        void show(Kind kind, const std::string &text);

        /** Where the toast was last DRAWN, in this Segment's own coordinates (measured in the
         *  paint: it follows the text). Empty before the first paint. */
        artboard::Rect rect() const { return mRect; }
        /** LIVE: 0 hidden … 1 at rest. Its opacity, and (1 − amount)·kSlide is how far it still is
         *  from its resting place. */
        double amount() const { return mAmount.value(); }
        /** LIVE: 0 the toast it replaced … 1 this one alone (1 when nothing was replaced). */
        double swapAmount() const { return mSwap.value(); }
        Kind kind() const { return mKind; }
        /** The words as drawn (through `translate` when a host set one). */
        std::string text() const { return words(mText); }
        /** True while anything of it is on screen or still to start — a host's "needs a frame". */
        bool isShowing() const { return mWanted || mSwapWanted || mAmount.value() > 0.0 || mAmount.isAnimating() || mSwap.isAnimating(); }

        /** Opt-in: a host that draws each paint pass in its own language (Solaris R-ACCESS-2) turns
         *  the stored words into the shown ones here; unset = as given. */
        std::function<std::string(const std::string &)> translate;

        void advance(double nowMs) override;

    protected:
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool hitTestSelf(const artboard::Point &p) const override { (void)p; return false; }  // never takes input

    private:
        void begin(double nowMs);   // start whatever `show` asked for
        std::string words(const std::string &s) const { return translate ? translate(s) : s; }
        void paintOne(artboard::IRenderTarget &t, const std::string &words, Kind kind, double alpha,
                      double slide, bool keepRect) const;

        std::string mText, mPrevText;                 // the toast; the one it replaced while showing
        Kind mKind = Kind::Info, mPrevKind = Kind::Info;
        artboard::AnimatedProperty mAmount{0.0}, mSwap{1.0};
        bool mWanted = false, mSwapWanted = false;
        double mShownAt = -1e9;
        mutable artboard::Rect mRect{0.0, 0.0, 0.0, 0.0};
    };
}
}
