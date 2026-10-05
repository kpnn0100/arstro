/*
 *  interstellar_v1 — PluginList: the Grade tab's IMAGE PROCESSING list — the selected rack node's
 *  plugin stack (R-FX-5).
 *
 *  Row one is always COSMO (the colour; its switch is the node's bypass, its mix the node's weight);
 *  the node's effects follow in their order. Each row: a switch, the plugin's name, its mix as a
 *  percentage, and — on hover — a × that removes an effect. A click on a row SELECTS it (which
 *  panel shows below — presentation, not a command); a right-click asks the screen for a menu
 *  (Move Up / Move Down / Remove). "+ Add" in the header asks the screen for the catalog menu.
 *
 *  Every change is one command line: `set <bind>.bypass=0|1`, `set <ef>.enabled=0|1`,
 *  `effect remove <ef>`. Rows travel on insert/remove (AnimatedRows); switches slide and the
 *  selection wash moves, both eased; the list's height eases with its row count so the panel
 *  under it never jumps.
 */
#pragma once
#include "../Theme.h"
#include "../AppHooks.h"
#include "AnimatedRows.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <map>
#include <string>

namespace arstro
{
namespace interstellar_v1
{
    class PluginList : public artboard::Segment
    {
    public:
        static constexpr double kHeaderH = 26.0;
        static constexpr double kRowH = 26.0;

        PluginList();
        void bind(const interstellar::AppModel &m);

        /** The row selected: "" = Cosmo, else an effect id. Presentation state. */
        const std::string &selected() const { return mSelected; }
        void select(const std::string &id);
        /** The height the list wants now (eased with its row count) — the inspector lays out by it. */
        double wantedHeight() const { return mHeight.value(); }
        int rowCount() const { return (int)mItems.size(); }

        // geometry a test aims at (local)
        artboard::Rect rowRect(int i) const;
        artboard::Rect switchRect(int i) const;
        artboard::Rect removeRect(int i) const;
        artboard::Rect addRect() const;
        /** The LIVE eased knob position of row i's switch (0 off … 1 on). */
        double switchAmount(int i) const;

        std::function<void(const std::string &line)> onCommand;
        /** The selection moved ("" = Cosmo). */
        std::function<void(const std::string &id)> onSelect;
        /** "+ Add" at a WORLD rect: the screen opens the catalog menu. */
        std::function<void(artboard::Rect world)> onAdd;
        /** Right-click on row `id` ("" = Cosmo) at a WORLD point. */
        std::function<void(const std::string &id, artboard::Point world)> onRowContext;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        struct Item
        {
            std::string id;           // "" = Cosmo
            std::string label, sub;   // "Gaussian Blur", "radius 8 px"
            bool enabled = true;
            double mix = 1.0;
        };
        int rowAt(const artboard::Point &p) const;
        void emit(const std::string &l) { if (onCommand) onCommand(l); }

        std::vector<Item> mItems;
        AnimatedRows<Item> mRows;
        std::string mBind, mSelected;
        bool mHasTarget = false;
        std::map<std::string, artboard::AnimatedProperty> mKnob;   // keyed by row id ("" = Cosmo)
        std::map<std::string, bool> mKnobApplied;
        cosmo_v2::HoverFade mHover, mSel;
        artboard::AnimatedProperty mHeight{kHeaderH + kRowH};
        double mHeightTarget = kHeaderH + kRowH, mHeightApplied = -1.0;
    };
}
}
