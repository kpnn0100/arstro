/*
 *  solaris_ui — ParamMenu: ONE menu for every number a formula can drive (R-WIN-3, R-MIX-16, R-UI-11).
 *
 *  A device parameter's row in its window, a fader, a pan, a send's level and the master fader in the
 *  dock — and, when hosting lands, a third-party VST3's parameter list (R-VST-6) — offer the same
 *  right-click: Create Automation (`auto create <address>`), Formula… (cosmo's rename field → `set
 *  <address>="=<typed>"`), Clear Binding (`bind clear <address>`), Reset to Default (`set
 *  <address>=<default>`), and Copy Address / Copy Value / Copy as Formula through the HOST's
 *  clipboard (the core has none, R-SVC-4). The widget says WHAT is under the pointer (a
 *  `ParamTarget`); this builds the items, so the menus cannot drift apart. Every edit is one line.
 *
 *  Also here, because every widget that shows an id draws it the same way: `drawNameWithId` — a name
 *  and, faded in by View › Show IDs (eased by the screen), its id beside it in the accent's mono.
 */
#pragma once
#include "../Theme.h"
#include "../../../cosmo/widgets/ContextMenu.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris_ui
{
    /** What a property's menu reaches: the grammar, cosmo's rename field, the host's clipboard. */
    struct PropertyHooks
    {
        std::function<bool(const std::string &line)> command;
        std::function<void(const std::string &current, artboard::Point world, std::function<void(const std::string &)> done)> rename;
        std::function<void(const std::string &text)> copy;
    };

    /** The property under the pointer. */
    struct ParamTarget
    {
        std::string address;   // dv_1.filter.cutoff · ch_2.gain · ch_2.pan · sd_1.gain · project.masterGain
        std::string formula;   // what drives it, with its "=" ("" = its own value plays)
        std::string value;     // Copy Value: as shown, with its unit ("1.20 kHz", "-6.0 dB")
        std::string reset;     // Reset to Default's value as `set` takes it ("" = no reset)
        bool bindable = true;  // a number; a choice or a switch is neither bound nor read by a formula (R-AUTO-1)
    };

    /** Create Automation, Formula…, Clear Binding, Reset to Default, Copy Address, Copy Value, Copy as Formula. */
    std::vector<cosmo_v2::ContextMenu::Item> paramMenuItems(const ParamTarget &p, const PropertyHooks &h, artboard::Point world);

    /** The automation a formula IS ("au_1" for "=au_1"); "" when it is a link or an expression. */
    std::string automationOf(const std::string &formula);
    /** What decides a bound value, said in a value column (R-WIN-2): `auto au_1`, `= ch_2.gain`, `= <formula>` cut
     *  to `maxChars` with an ellipsis. */
    std::string bindingText(const std::string &formula, size_t maxChars = 14);

    /** R-UI-11: `name` at (x, baseline) in `maxW`, and its `id` right-aligned in the same box, faded in by `ids`
     *  (0…1, the screen's eased Show IDs) — the name gives way as the id arrives. `alpha` fades both. */
    void drawNameWithId(artboard::IRenderTarget &t, const std::string &name, const std::string &id, double x, double baseline, double maxW,
                        double px, const char *nameFont, const artboard::Color &nameColour, double ids, double alpha);
    /** The id's own look: the accent, mono, a step smaller than the name it sits beside. */
    double idPx(double namePx);
}
}
