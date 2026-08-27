/*
 *  cosmo_v2 by arstro — InfoDialog: the "Image information" modal (R-INFO). A card with the
 *  photo's name, a two-column list of label/value rows exactly as the service produced them, and
 *  a Close button. No Figma element to map to: the design brief has no metadata panel, so this
 *  borrows ConfirmDialog's chrome (same card, same scrim, same 150/120 ms appear/close) rather
 *  than inventing a second dialog language.
 *
 *  It renders what it is GIVEN and interprets nothing. Deciding that a file has an ISO, and what
 *  to call it, is the service's job (R-SVC-4) — this widget only knows that a row has a label and
 *  a value, which is why it works unchanged for a JPEG's eleven rows and a RAW's sixteen.
 *
 *  The list scrolls (R-G-3 / R6): a RAW produces more rows than fit a sane card height, and a
 *  dialog that is as tall as its content is a dialog that runs off a 720p screen.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "HoverFade.h"
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class InfoDialog : public artboard::Segment
    {
    public:
        using Row = std::pair<std::string, std::string>;

        static constexpr double kCardW = 400.0;
        static constexpr double kRowH = 19.5;      // 6 spacing units
        static constexpr double kMaxBodyH = 292.5; // 15 rows; beyond that the body scrolls

        InfoDialog() = default;

        void show(const std::string &title, std::vector<Row> rows);
        bool isOpen() const { return mOpen && !mClosing; }
        void close() { mOpen = false; mClosing = false; mAppear.set(0.0); mScroll.set(0.0); }
        /** Escape or Enter closes; everything else is swallowed, as in every other modal. */
        bool handleKey(const artboard::KeyEvent &e);
        /** The wheel, routed by App while this modal owns the input. */
        void scrollBy(double delta);

        int rowCount() const { return (int)mRows.size(); }
        /** For a headless assertion: the value shown for `label`, or empty. */
        std::string valueOf(const std::string &label) const;
        /** Live scroll offset, so a test can tell an eased list from a snapping one (R-G-1). */
        double scrollOffset() const { return mScroll.value(); }

    protected:
        void advance(double nowMs) override;
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return mOpen && !mClosing; }

    private:
        void beginClose();
        double bodyHeight() const;
        artboard::Rect cardRect() const;
        artboard::Rect bodyRect() const;
        artboard::Rect closeRect() const;
        artboard::Rect buttonRect() const;

        bool mOpen = false;
        bool mClosing = false;
        double mLastMs = 0.0;
        artboard::AnimatedProperty mAppear{0.0};
        artboard::AnimatedProperty mScroll{0.0};
        double mScrollTarget = 0.0, mScrollLastTarget = 0.0;
        std::string mTitle;
        std::vector<Row> mRows;
        HoverFade mHover;   // 0 = the X, 1 = the Close button
    };
}
}
