/*
 *  Genesis — ReactionsPanel: the bottom panel, where motion is authored.
 *
 *  A component is not a 0–60s movie; it is a set of responses to events, any of which can
 *  fire at any time and interrupt any other. So the source of truth here is the EVENT
 *  GRAPH — signal → steps → tracks — and the scrubber is per reaction, not global. Beside
 *  each reaction sit the signals that can interrupt it, and a Fire button, so an author can
 *  interrupt a running reaction on purpose and watch what happens.
 */
#pragma once
#include "../Theme.h"
#include "Document.h"
#include "Panel.h"
#include <artboard/artboard.h>
#include <memory>
#include <string>
#include <vector>

namespace genesis
{
namespace ui
{
    class App;

    class ReactionsPanel : public artboard::Segment
    {
    public:
        explicit ReactionsPanel(App &app);
        void layout(double w, double h);
        void refresh();
        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &localPoint) override;

    private:
        struct TrackRow
        {
            int step = 0;
            int track = 0;
            double y = 0.0;      // laid-out row top, so the chips can be hit-tested
            std::shared_ptr<artboard::TextBox> target;
            std::shared_ptr<artboard::TextBox> from;
            std::shared_ptr<artboard::TextBox> to;
            std::shared_ptr<artboard::TextBox> ms;
            std::shared_ptr<artboard::TextBox> delay;
            std::shared_ptr<artboard::ComboBox> easing;
        };
        /** The chips at a row's right edge: repeat count, yoyo, delete. */
        enum class Chip { None, Repeat, Yoyo, Remove };
        Chip chipAt(const artboard::Point &p, int &rowIndex) const;
        /** Track-row column widths. A width of 0 means the column is DROPPED at this size:
         *  the panel sheds detail rather than letting columns run into each other. */
        struct Columns
        {
            double target = 0, from = 0, to = 0, ms = 0, delay = 0, easing = 0, gap = 6.0;
            bool showChips = true;
            double total() const;
        };
        Columns columns(double panelW) const;
        Reaction *current();
        const Reaction *current() const;
        double maxListScroll() const;
        void commit();
        void rebuildRows();
        /** Structure only (which rows exist), never their values — see Inspector. */
        std::string structureKey() const;
        void syncValues();

        App &mApp;
        std::vector<TrackRow> mRows;
        std::shared_ptr<artboard::ComboBox> mSignal;
        std::shared_ptr<artboard::ComboBox> mCancel;
        std::shared_ptr<artboard::Button> mAddReaction;
        std::shared_ptr<artboard::Button> mDeleteReaction;
        std::shared_ptr<artboard::Button> mAddStep;
        std::shared_ptr<artboard::Button> mAddTrack;
        std::shared_ptr<artboard::Button> mFire;
        RowHover mHover;
        double mNowMs = 0.0;
        double mScrubT = 0.0;         // 0..1 through the selected reaction, for the scrubber
        bool mScrubbing = false;
        double mListScroll = 0.0;
        std::string mStructure;
    };
}
}
