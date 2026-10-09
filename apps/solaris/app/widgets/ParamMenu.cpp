#include "ParamMenu.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cctype>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;

    namespace
    {
        std::string q(const std::string &s) { return s.find_first_of(" \t\"") == std::string::npos && !s.empty() ? s : "\"" + s + "\""; }
        std::string body(const std::string &formula)
        {
            std::string b = formula.size() > 1 ? formula.substr(1) : std::string();
            const auto a = b.find_first_not_of(' '), z = b.find_last_not_of(' ');
            return a == std::string::npos ? std::string() : b.substr(a, z - a + 1);
        }
    }

    std::string automationOf(const std::string &formula)
    {
        const std::string b = body(formula);
        if (b.rfind("au_", 0) != 0 || b.size() < 4) return std::string();
        for (char c : b)
            if (!(std::isalnum((unsigned char)c) || c == '_')) return std::string();
        return b;
    }

    std::string bindingText(const std::string &formula, size_t maxChars)
    {
        const std::string au = automationOf(formula);
        if (!au.empty()) return "auto " + au; // an automation
        // a link, or a formula — cut to the column: the whole formula is one `get` away, and its menu shows it
        std::string out = "= " + body(formula);
        if (out.size() > maxChars) out = out.substr(0, maxChars - 1) + "\xE2\x80\xA6";
        return out;
    }

    std::vector<cosmo_v2::ContextMenu::Item> paramMenuItems(const ParamTarget &p, const PropertyHooks &h, Point world)
    {
        std::vector<cosmo_v2::ContextMenu::Item> items;
        const std::string address = p.address;
        auto cmd = h.command;
        if (p.bindable)
        {
            if (automationOf(p.formula).empty())
                items.push_back({"Create Automation", [cmd, address] { if (cmd) cmd("auto create " + address); }});
            const std::string current = p.formula.empty() ? std::string("=") : p.formula;
            auto rename = h.rename;
            items.push_back({"Formula\xE2\x80\xA6", [rename, cmd, address, current, world] {
                                 if (!rename) return;
                                 rename(current, world, [cmd, address](const std::string &typed) {
                                     std::string f = typed;
                                     if (f.empty()) return;
                                     if (f[0] != '=') f = "=" + f;
                                     if (cmd) cmd("set " + address + "=" + q(f));
                                 });
                             }});
            if (!p.formula.empty()) items.push_back({"Clear Binding", [cmd, address] { if (cmd) cmd("bind clear " + address); }});
        }
        if (!p.reset.empty())
        {
            const std::string def = p.reset;
            items.push_back({"Reset to Default", [cmd, address, def] { if (cmd) cmd("set " + address + "=" + def); }});
        }
        // R-UI-11: the host's clipboard — an address to paste into a formula or a script
        auto copy = h.copy;
        items.push_back({"Copy Address", [copy, address] { if (copy) copy(address); }});
        if (!p.value.empty())
        {
            const std::string value = p.value;
            items.push_back({"Copy Value", [copy, value] { if (copy) copy(value); }});
        }
        if (p.bindable) items.push_back({"Copy as Formula", [copy, address] { if (copy) copy("=" + address); }});
        return items;
    }

    double idPx(double namePx) { return std::max(8.5, namePx - 1.5); }

    void drawNameWithId(IRenderTarget &t, const std::string &name, const std::string &id, double x, double baseline, double maxW, double px,
                        const char *nameFont, const Color &nameColour, double ids, double alpha)
    {
        const double a = std::clamp(ids, 0.0, 1.0);
        double idW = 0.0;
        const double ip = idPx(px);
        if (a > 0.001 && !id.empty()) idW = std::min(t.measureText(id, ip, font::mono()), maxW * 0.6);
        // the name gives way as the id arrives — by the EASED amount, so it narrows, never jumps (§1)
        const double room = std::max(0.0, maxW - (idW + 5.0) * a);
        Color nc = nameColour;
        nc.a *= alpha;
        t.setFill(nc);
        t.drawText(textfit::ellipsize(t, name, room, px, nameFont), x, baseline, px, nameFont);
        if (idW <= 0.0) return;
        Color ic = palette::primary();
        ic.a *= alpha * a;
        t.setFill(ic);
        const std::string shown = textfit::ellipsize(t, id, idW, ip, font::mono());
        t.drawText(shown, x + maxW - t.measureText(shown, ip, font::mono()), baseline, ip, font::mono());
    }
}
}
