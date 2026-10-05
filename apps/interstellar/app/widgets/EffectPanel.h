/*
 *  interstellar_v1 — EffectPanel: the node's effects as COLLAPSIBLE sections (R-FX-5, amended
 *  2026-10-05: "the effect panel should be able to collapse an item").
 *
 *  When an effect is selected in the IMAGE PROCESSING list the panel shows EVERY effect of that node,
 *  in stack order, each as a section: a header (a disclosure arrow that turns with its section, the
 *  effect's name, its id — the address root a script types) over its parameters as cosmo's own
 *  `SliderRow`s — Mix first, then the plugin's parameters from the catalog the model publishes
 *  (label, range, unit; a 0..1 parameter shows 0..100 %). A click on a header opens or closes that
 *  section; the selected effect's section opens and is scrolled into view. Opening and closing ease
 *  (the section's height and its rows' opacity); it is presentation, never a command.
 *
 *  Every drag is one command line, `set <ef>.<key>=<value>` (engine units). Rows are built per
 *  effect INSTANCE the first time it is shown and kept (a Segment never drops a child); rows of
 *  effects not shown are culled. While the pointer is down in here, `bind` does not re-seed the
 *  sliders — a gesture in flight outranks the model. The body scrolls when the sections outgrow it.
 */
#pragma once
#include "../Theme.h"
#include "../AppHooks.h"
#include "EasedScroll.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include "../../../cosmo/widgets/SliderRow.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar_v1
{
    class EffectPanel : public artboard::Segment
    {
    public:
        static constexpr double kHeaderH = 29.25;    // a section's header

        EffectPanel();
        /** Show the node of effect `id`, every effect of it; `id`'s section opens. */
        void bind(const interstellar::AppModel &m, const std::string &id, bool interacting);
        void layout();
        void advance(double nowMs) override;
        const std::string &effect() const { return mId; }
        /** The slider for `key` ("mix" or a parameter key) of the SELECTED effect, or nullptr. */
        std::shared_ptr<cosmo_v2::SliderRow> slider(const std::string &key) const { return sliderOf(mId, key); }
        std::shared_ptr<cosmo_v2::SliderRow> sliderOf(const std::string &effectId, const std::string &key) const;

        // ── the sections ──
        int sectionCount() const { return (int)mShown.size(); }
        const std::string &sectionId(int i) const { return mShown[(size_t)i]; }
        artboard::Rect headerRect(int i) const;              // local, scrolled — where it is drawn
        /** Open or close a section (intent; it eases). */
        void setOpen(const std::string &effectId, bool open);
        bool isOpen(const std::string &effectId) const;
        double openAmount(const std::string &effectId) const;   // the LIVE eased amount
        double scrollTarget() const { return mScroll.target(); }

        std::function<void(const std::string &line)> onCommand;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;

    private:
        struct Row
        {
            std::string key;
            bool percent = false;                  // a 0..1 parameter, shown 0..100
            std::shared_ptr<cosmo_v2::SliderRow> slider;
        };
        struct Section
        {
            std::string label, type;
            std::vector<Row> rows;
            bool open = false, openApplied = false, placed = false;
            artboard::AnimatedProperty amount{0.0};
        };
        Section &sectionFor(const interstellar::EffectModel &e);
        double bodyH(const Section &s) const;
        double sectionTop(int i) const;            // content coords
        double contentH() const;

        std::map<std::string, Section> mSections;  // effect id → its section (kept)
        std::vector<std::string> mShown;           // the node's effects, in stack order
        std::string mId, mNode, mLastSelected;
        bool mRevealPending = false;
        EasedScroll mScroll;
        cosmo_v2::HoverFade mHover;
    };
}
}
