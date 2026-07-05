#include "TopBar.h"
#include "Icons.h"
#include "TextMetrics.h"
#include "../Theme.h"

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        const std::vector<std::string> kMenus = {"File", "Settings", "Develop", "History", "Preset"};
        constexpr double kMenuFontPx = 11.0;
        constexpr double kMenuPadX = 8.125;  // px-2.5
        constexpr double kMenuPadY = 3.25;   // py-1 (height derives from font + padding)
        constexpr double kMenuHeight = 21.0; // ~11px line height + 2*py-1, approximated
        constexpr double kMenuGap = 1.0;     // gap-[1px]
    }

    TopBar::TopBar()
    {
        for (size_t i = 0; i < kMenus.size(); ++i)
        {
            auto btn = std::make_shared<PillButton>(kMenus[i]);
            btn->idleBox = {Paint{}, 2.0};  // no fill/stroke -> PillButton skips drawing the box
            btn->activeBox = {Paint::filled(palette::primary()), 2.0};
            btn->idleText = {palette::mutedForeground(), kMenuFontPx, font::sansMedium()};
            btn->activeText = {palette::white(), kMenuFontPx, font::sansMedium()};
            btn->height.set(kMenuHeight);
            const int idx = (int)i;
            btn->onClick = [this, idx] { if (onMenuClick) onMenuClick(idx); };
            addChild(btn);
            mMenus.push_back(btn);
        }

        mRailToggle = std::make_shared<IconButton>(
            [](IRenderTarget &t, const Rect &r, const Color &c) { icon::panelLeft(t, r, c); });
        mRailToggle->activeColor = palette::primary();
        mRailToggle->idleColor = palette::mutedForeground();
        mRailToggle->hoverBg = palette::whiteAlpha(0.05);
        mRailToggle->width.set(20.5);
        mRailToggle->height.set(20.5);
        mRailToggle->onClick = [this] { if (onRailToggle) onRailToggle(); };
        addChild(mRailToggle);

        height.set(kHeight);
    }

    void TopBar::setActiveMenu(int index)
    {
        for (size_t i = 0; i < mMenus.size(); ++i)
            mMenus[i]->active = ((int)i == index);
    }

    void TopBar::setFilename(const std::string &name, int index, int total)
    {
        mFilename = name; mFileIndex = index; mFileTotal = total;
        layout();
    }

    void TopBar::setRailOpen(bool open)
    {
        mRailOpen = open;
        mRailToggle->active = open;
    }

    void TopBar::layout()
    {
        const double w = width.value();

        // Left group: wordmark (drawn directly in onPaint, not a child) + menus.
        const double wordmarkW = estimateTextWidth("cosmo.", 13.0);
        double cx = kPad + wordmarkW + 3.25 /*mr-1*/ + 9.75 /*gap-3*/;
        for (auto &btn : mMenus)
        {
            const double bw = estimateTextWidth(btn->label(), kMenuFontPx) + 2 * kMenuPadX;
            btn->x.set(cx);
            btn->y.set((kHeight - kMenuHeight) * 0.5);
            btn->width.set(bw);
            cx += bw + kMenuGap;
        }

        // Right group: filename + rail toggle, positioned from the right edge in.
        mRailToggle->x.set(w - kPad - mRailToggle->width.value());
        mRailToggle->y.set((kHeight - mRailToggle->height.value()) * 0.5);
    }

    void TopBar::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = kHeight;

        // Bottom hairline border.
        t.beginPath();
        t.moveTo(0, h); t.lineTo(w, h);
        t.setStroke(palette::border(), 1.0);
        t.strokePath();

        // Wordmark: "cosmo" (SemiBold, foreground) + "." (SemiBold, primary --
        // the dot's actual weight in Figma is Bold/700, which isn't vendored
        // separately; at this size a period's shape is visually identical
        // across 600/700 weight, so SemiBold is used for both runs).
        const double baseline = h * 0.5 + 13.0 * 0.35;
        t.setFill(palette::foreground());
        t.drawText("cosmo", kPad, baseline, 13.0, font::sansSemiBold(), -0.39);
        const double dotX = kPad + estimateTextWidth("cosmo", 13.0);
        t.setFill(palette::primary());
        t.drawText(".", dotX, baseline, 13.0, font::sansSemiBold(), -0.39);

        // Filename + "(i/n)", right-aligned against the rail toggle.
        if (mFileTotal > 0)
        {
            const std::string suffix = " (" + std::to_string(mFileIndex) + "/" + std::to_string(mFileTotal) + ")";
            const double nameW = estimateTextWidth(mFilename, 11.0);
            const double suffixW = estimateTextWidth(suffix, 11.0);
            const double rightEdge = mRailToggle->x.value() - 9.75;
            const double nameX = rightEdge - nameW - suffixW;
            const double fy = h * 0.5 + 11.0 * 0.35;
            t.setFill(palette::mutedForeground());
            t.drawText(mFilename, nameX, fy, 11.0, font::mono());
            t.setFill(Color{palette::foreground().r, palette::foreground().g, palette::foreground().b, 0.3});
            t.drawText(suffix, nameX + nameW, fy, 11.0, font::mono());
        }
    }
}
}
