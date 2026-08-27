/*
 *  cosmo_v2 by arstro — ParamPanel: a scrollable list of SliderRows grouped
 *  into titled sections (App.tsx's Basic/Detail tab bodies: `px-3 pb-4` with
 *  repeated SectionHeader + SliderRow pairs). One reusable class serves both
 *  tabs (and the Mask tab's scoped tone/colour/presence sliders) -- only the
 *  Spec table differs -- mirroring the same "grouped Spec list" shape cosmo's
 *  own ParamPanel uses for the same reason (a sound generic design for "a
 *  panel of grouped sliders", not a copy of its visuals).
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "IconButton.h"
#include "SliderRow.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class ParamPanel : public artboard::Segment
    {
    public:
        struct Spec
        {
            std::string label;
            double min = -100.0, max = 100.0;
            std::function<void(double)> onChange;
            bool hasGradient = false;                 // paint the track as a colour ramp
            artboard::Color gradLeft, gradRight;      // ramp endpoints (min -> max)
        };
        struct Section
        {
            std::string title;
            std::vector<Spec> rows;
            /** An optional tool button on the section HEADER row, right-aligned — used by the
             *  white-balance picker (R-WB-1), and generic because a section is exactly the scope
             *  such a tool belongs to. `toggles` makes it latch (armed / not armed) rather than
             *  fire once. */
            enum class Action { None, Pipette };
            Action action = Action::None;
            bool toggles = false;
            bool armed = false;      // latched state, when `toggles`
            std::function<void(bool armed)> onAction;
        };

        explicit ParamPanel(std::vector<Section> sections);

        /** Push real values into the rows, in the same flattened section/row
         *  order the Specs were given in (mirrors cosmo's own setValues). Does
         *  not fire onChange. */
        void setValues(const std::vector<double> &values);
        /** Per-row stacked-reach offsets (ancestor-group contribution), same order as
         *  setValues; 0 hides a row's green reach (DR-EDIT-4). */
        void setSubValues(const std::vector<double> &offsets);
        void scrollBy(double delta);
        /** Set a section action's armed state from outside — the picker disarms itself once it
         *  has taken its sample, and the button has to follow. */
        void setSectionActionArmed(const std::string &sectionTitle, bool armed);
        void layout();  // call after width/height changes

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        void advance(double nowMs) override;  // eases the scroll toward its target (R-G-1)

    private:
        std::vector<Section> mSections;
        std::vector<std::shared_ptr<SliderRow>> mFlatRows;
        std::vector<double> mSectionHeaderY;  // cached per-section header y, from the last layout()
        std::vector<std::shared_ptr<IconButton>> mSectionActions;  // one per section, null when none
        // Scroll eases toward mScrollTarget rather than jumping per wheel notch.
        artboard::AnimatedProperty mScroll{0.0};
        double mScrollTarget = 0.0, mScrollLastTarget = 0.0;
        double mContentHeight = 0.0;
    };
}
}
