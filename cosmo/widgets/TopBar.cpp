#include "TopBar.h"
#include "Icons.h"
#include "TextMetrics.h"
#include "../Theme.h"

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace { constexpr double kMenuHeight = 21.0; }

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
