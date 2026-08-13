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
        constexpr double kMenuHeight = 21.0;
        constexpr double kNameFontPx = 11.0;
        constexpr double kNameBoxH = 18.0;
        constexpr double kNamePadX = 6.0;
        constexpr double kNameMaxW = 220.0;
        // "Group: <name>", ellipsized to fit the right slot (R5).
        std::string groupLabel(const std::string &name)
        {
            std::string label = "Group: " + name;
            if (estimateTextWidth(label, kNameFontPx) <= kNameMaxW) return label;
            while (label.size() > 8 && estimateTextWidth(label + "\xE2\x80\xA6", kNameFontPx) > kNameMaxW)
                label.pop_back();
            return label + "\xE2\x80\xA6";  // …
        }
    }

    TopBar::TopBar()
    {
        mMenuStrip = std::make_shared<MenuStrip>();
        mMenuStrip->height.set(kMenuHeight);
        addChild(mMenuStrip);

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

    void TopBar::setFilename(const std::string &name, int index, int total)
    {
        mFilename = name; mFileIndex = index; mFileTotal = total;
        mGroupName.clear();   // showing an image -> not a group
        layout();
    }

    void TopBar::setGroupName(const std::string &name)
    {
        mGroupName = name;
        mFileTotal = 0;       // hide the filename readout while a group is shown
        layout();
    }

    Rect TopBar::groupNameRect() const
    {
        if (mGroupName.empty() || !mRailToggle) return Rect{0, 0, 0, 0};
        const double tw = estimateTextWidth(groupLabel(mGroupName), kNameFontPx);
        const double w = tw + 2 * kNamePadX;
        const double rightEdge = mRailToggle->x.value() - 9.75;
        return Rect{rightEdge - w, (kHeight - kNameBoxH) * 0.5, w, kNameBoxH};
    }

    void TopBar::advance(double nowMs)
    {
        mNowMs = nowMs;
        if (mNameHovered && !isHovered())  // pointer left the bar -> release the name hover
        {
            mNameHovered = false;
            mNameHover.animateTo(0.0, 120.0, Easing::EaseOutCubic, nowMs);
        }
        mNameHover.update(nowMs);
        Segment::advance(nowMs);
    }

    void TopBar::setRailOpen(bool open)
    {
        mRailOpen = open;
        mRailToggle->active = open;
    }

    void TopBar::layout()
    {
        const double w = width.value();

        // Left group: wordmark (drawn directly in onPaint, not a child) + the menu strip.
        const double wordmarkW = estimateTextWidth("cosmo.", 13.0);
        const double cx = kPad + wordmarkW + 3.25 /*mr-1*/ + 9.75 /*gap-3*/;
        mMenuStrip->x.set(cx);
        mMenuStrip->y.set((kHeight - kMenuHeight) * 0.5);
        mMenuStrip->width.set(mMenuStrip->contentWidth());

        // Right group: filename + rail toggle, positioned from the right edge in.
        mRailToggle->x.set(w - kPad - mRailToggle->width.value());
        mRailToggle->y.set((kHeight - mRailToggle->height.value()) * 0.5);
    }

    Rect TopBar::wordmarkRect() const
    {
        const double wmW = estimateTextWidth("cosmo.", 13.0);
        return Rect{kPad - 4.0, 0.0, wmW + 8.0, kHeight};
    }

    bool TopBar::handleGesture(const Gesture &g, const Point &local)
    {
        // Clicking the "cosmo." wordmark returns to the launcher (R-HOME).
        if (g.type == Gesture::Type::Click && onHome && wordmarkRect().contains(local)) { onHome(); return true; }
        // The group name is a clickable, hovered affordance -> open rename (DR-TREE-5).
        if (!mGroupName.empty())
        {
            const bool over = groupNameRect().contains(local);
            if (g.type == Gesture::Type::Move && over != mNameHovered)
            {
                mNameHovered = over;
                mNameHover.animateTo(over ? 1.0 : 0.0, 120.0, Easing::EaseOutCubic, mNowMs);
            }
            if (g.type == Gesture::Type::Click && over && onNameClick) { onNameClick(); return true; }
        }
        return Segment::handleGesture(g, local);
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

        // Project name, centred in the bar (R-HOME item 2).
        if (!mProjectName.empty())
        {
            const double nameW = estimateTextWidth(mProjectName, 12.0);
            t.setFill(palette::foreground());
            t.drawText(mProjectName, (w - nameW) * 0.5, h * 0.5 + 12.0 * 0.35, 12.0, font::sansMedium());
        }

        // Group name (clickable, hovered) in the right slot while editing a group (DR-TOPBAR).
        if (!mGroupName.empty())
        {
            const Rect nr = groupNameRect();
            const double hv = mNameHover.value();
            if (hv > 0.001)
                drawRoundedRect(t, nr, radius::control(), Paint::filled(palette::whiteAlpha(0.07 * hv)));
            t.setFill(palette::foreground());
            t.drawText(groupLabel(mGroupName), nr.x + kNamePadX, h * 0.5 + kNameFontPx * 0.35, kNameFontPx, font::sans());
        }
        // Filename + "(i/n)", right-aligned against the rail toggle.
        else if (mFileTotal > 0)
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
