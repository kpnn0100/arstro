/*
 *  solaris_ui — AutomationPanel: one automation's facts — the content of its window (R-AUTO-11, R-WIN).
 *
 *  Opened by a double-click on an automation row's header, or on its curve away from a point. Every
 *  line is the MODEL's, nothing re-derived: its name (Rename → ONE `set <au>.name=…`), its id, unit and
 *  range, the address it was made from, its points (beat, value, shape — a bezier point's handles),
 *  every formula that reads it (`usedBy`, each with its binding's formula) and its value at the
 *  playhead — `automations[].now`, which the service computes with the engine's own curve at the
 *  transport position, so the window says what the audio plays there.
 *
 *  The rows are Interstellar's `AnimatedRows`, keyed by what they say: a point added slides the rows
 *  under it down and fades in; an edit cross-fades its row; a rename cross-fades the name. The value
 *  at the playhead follows the transport continuously while playing and eases to a seek or an edit
 *  (§1) — `nowLive()` is the drawn value, `nowTarget()` the model's.
 */
#pragma once
#include "../Theme.h"
#include "AppModel.h"
#include "../../../interstellar/app/widgets/AnimatedRows.h"
#include "../../../interstellar/app/widgets/EasedScroll.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris_ui
{
    class AutomationPanel : public artboard::Segment
    {
    public:
        static constexpr double kHeaderH = 35.75; // space::u(11)
        static constexpr double kRowH = 22.75;    // space::u(7)

        explicit AutomationPanel(std::string au);
        const std::string &automation() const { return mAu; }
        bool present() const { return mPresent; }   // the model still has it
        std::string title() const;                  // "Automation — <name>"

        void bind(const solaris::AppModel &m);
        void layout();
        void advance(double nowMs) override;

        // what a test reads (LIVE, as drawn)
        std::string text() const;                   // every row that is there: "label  value  extra", one a line
        bool hasRow(const std::string &key) const;  // a row keyed `key`, not fading out
        std::vector<std::string> rowKeys() const;   // the rows that are there, in order ("pt:<beat>|<value>|<shape>", "rd:<address>|<formula>", …)
        double rowAlpha(const std::string &key) const;
        double nowLive() const { return mNow.value(); }
        double nowTarget() const { return mNowTarget; }
        artboard::Rect renameRect() const;

        std::function<bool(const std::string &line)> onCommand;
        std::function<void(const std::string &current, artboard::Point world, std::function<void(const std::string &)> done)> onRename;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        struct Row
        {
            enum Kind { Info, Now, Section, Point, Reader } kind = Info;
            std::string label, value, extra;
        };
        std::string format(double v) const;         // in the automation's unit

        std::string mAu;
        solaris::AutomationModel mModel;
        bool mPresent = false, mPlaying = false;
        double mPosition = 0.0;
        interstellar_v1::AnimatedRows<Row> mRows;
        double mContentH = 0.0;
        double mNowTarget = 0.0, mNowLast = 0.0, mPosLast = 0.0;
        bool mNowInit = false;
        artboard::AnimatedProperty mNow{0.0}, mPos{0.0}; // the value at the playhead, and the playhead's beat it names
        interstellar_v1::EasedScroll mScroll;
        cosmo_v2::HoverFade mHover;
    };
}
}
