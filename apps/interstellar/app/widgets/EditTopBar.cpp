#include "EditTopBar.h"
#include "TextFit.h"
#include "../../../cosmo/widgets/Icons.h"
#include <algorithm>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        constexpr double kPad = 9.75;            // cosmo's TopBar kPad
        constexpr double kWordPx = 13.0;
        constexpr double kNamePx = 12.0;
        constexpr double kGroupGap = 19.5;       // u(6) between the bar's groups
        constexpr double kMenuH = 21.0;          // cosmo's TopBar kMenuHeight
        constexpr double kMenuGap = 13.0;        // mr-1 + gap-3, as cosmo's TopBar places its strip
        const char *kWord = "interstellar";
    }

    EditTopBar::EditTopBar()
    {
        height.set(shell::topBarH());
        mTabs = std::make_shared<TabSwitcher>(std::vector<std::string>{"Grade", "Cut", "Deliver"});
        addChild(mTabs);
        mVersions = std::make_shared<VersionSwitcher>();
        addChild(mVersions);
        mSave = std::make_shared<cosmo_v2::IconButton>(
            [](IRenderTarget &t, const Rect &r, const Color &c) { cosmo_v2::icon::save(t, r, c); });
        mSave->activeColor = palette::primary();
        mSave->idleColor = palette::mutedForeground();
        mSave->hoverBg = palette::whiteAlpha(0.05);
        mSave->width.set(20.5);
        mSave->height.set(20.5);
        mSave->onClick = [this] { if (onSave) onSave(); };
        addChild(mSave);
        mMenus = std::make_shared<cosmo_v2::MenuStrip>();
        mMenus->height.set(kMenuH);
        // An open dropdown overlaps the pages below; raise the bar so the dropdown wins
        // hit-testing as well as the draw (cosmo's App does the same for its TopBar).
        mMenus->onOpenChanged = [this](int open) { if (open >= 0) raise(); };
        addChild(mMenus);
    }

    void EditTopBar::bind(const interstellar::AppModel &m)
    {
        mProjectName = m.projectName;
        mDirty = m.dirty;
        mBound = true;
        mVersions->bind(m);
    }

    void EditTopBar::layout()
    {
        const double w = width.value(), h = height.value();
        // Cosmo's order: wordmark, then the menu strip; the project name after it.
        mMenus->x.set(kPad + mWordmarkW + kMenuGap);
        mMenus->y.set((h - kMenuH) * 0.5);
        mMenus->width.set(mMenus->contentWidth());
        const double menusRight = mMenus->x.value() + mMenus->width.value();
        // The tab switcher is centred on the window; the version switcher follows it.
        const double tabsW = mTabs->preferredWidth();
        constexpr double kVerMin = 180.0, kVerMax = 296.0;   // the chrome's floor and its comfortable width
        double tabsX = (w - tabsW) * 0.5;
        const double saveX = w - kPad - mSave->width.value();
        const double maxRight = saveX - kGroupGap;
        // Never let the version switcher run into the save button at a narrow width: slide the
        // pair left first (the project name gives way, not the controls).
        if (tabsX + tabsW + 13.0 + kVerMin > maxRight) tabsX = std::max(menusRight + kGroupGap, maxRight - kVerMin - 13.0 - tabsW);
        tabsX = std::max(tabsX, menusRight + kGroupGap);   // the menus never sit under the tabs
        // ...and let the chrome take the room it has up to the save button, so a pinned version's
        // name and commit both fit before anything is ellipsized (R5)
        const double verW = std::clamp(maxRight - (tabsX + tabsW + 13.0), kVerMin, kVerMax);
        mTabs->x.set(tabsX);
        mTabs->y.set((h - TabSwitcher::kHeight) * 0.5);
        mTabs->width.set(tabsW);
        mVersions->x.set(tabsX + tabsW + 13.0);
        mVersions->y.set((h - VersionSwitcher::kHeight) * 0.5);
        mVersions->width.set(verW);
        mSave->x.set(saveX);
        mSave->y.set((h - mSave->height.value()) * 0.5);
    }

    Rect EditTopBar::nameRect() const
    {
        const double x = mMenus->x.value() + mMenus->width.value() + kGroupGap;
        return Rect{x, 0, std::max(0.0, mTabs->x.value() - kGroupGap - x), height.value()};
    }

    void EditTopBar::advance(double nowMs)
    {
        mNowMs = nowMs;
        if (mHomeHovered && !isHovered())
        {
            mHomeHovered = false;
            mHomeHover.animateTo(0.0, motion::kHoverMs, Easing::EaseOutCubic, nowMs);
        }
        mHomeHover.update(nowMs);
        if (!mDirtyInit) { mDirtyAmt.set(mDirty ? 1.0 : 0.0); mDirtyApplied = mDirty; mDirtyInit = mBound; }
        if (mDirty != mDirtyApplied)
        {
            mDirtyAmt.animateTo(mDirty ? 1.0 : 0.0, motion::kSelectMs, Easing::EaseOutCubic, nowMs);
            mDirtyApplied = mDirty;
        }
        mDirtyAmt.update(nowMs);
        // cosmo's IconButton switches `active` colours in one frame; driving its idle colour from
        // the eased dirty amount keeps "there are unsaved edits" a fade (R1) without forking it
        mSave->active = false;
        mSave->idleColor = lerpColor(palette::mutedForeground(), palette::primary(), mDirtyAmt.value());
        Segment::advance(nowMs);
    }

    bool EditTopBar::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type == Gesture::Type::Move)
        {
            const bool over = wordmarkRect().contains(local);
            if (over != mHomeHovered)
            {
                mHomeHovered = over;
                mHomeHover.animateTo(over ? 1.0 : 0.0, motion::kHoverMs, Easing::EaseOutCubic, mNowMs);
            }
            return true;
        }
        if (g.type == Gesture::Type::Down && wordmarkRect().contains(local)) return true;
        if (g.type == Gesture::Type::Click && wordmarkRect().contains(local))
        {
            if (onHome) onHome();
            return true;
        }
        return Segment::handleGesture(g, local);
    }

    void EditTopBar::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(palette::background()));
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0, h); t.lineTo(w, h); t.strokePath();

        // Wordmark — measured, so the dot sits at the real end of the word (R-G-2a's lesson).
        const double sp = -0.03 * kWordPx;
        const double base = textfit::baseline(h * 0.5, kWordPx);
        const double wordW = t.measureText(kWord, kWordPx, font::sansSemiBold(), sp);
        const double dotW = t.measureText(".", kWordPx, font::sansSemiBold(), sp);
        mWordmarkW = wordW + dotW;
        const double hv = mHomeHover.value();
        if (hv > 0.001)
            drawRoundedRect(t, Rect{kPad - 5.0, (h - 20.0) * 0.5, mWordmarkW + 10.0, 20.0}, radius::control(), Paint::filled(palette::hoverWash(hv)));
        t.setFill(palette::foreground());
        t.drawText(kWord, kPad, base, kWordPx, font::sansSemiBold(), sp);
        t.setFill(palette::primary());
        t.drawText(".", kPad + wordW, base, kWordPx, font::sansSemiBold(), sp);

        // Project name, ellipsized against what is actually left before the tabs (R5), with an
        // eased "edited" dot after it while the project has unsaved changes.
        const Rect nr = nameRect();
        const double dirty = mDirtyAmt.value();
        const double dotRoom = 12.0;
        const std::string name = textfit::ellipsize(t, mProjectName, nr.w - dotRoom, kNamePx, font::sansMedium());
        if (!name.empty())
        {
            t.setFill(palette::foreground());
            t.drawText(name, nr.x, textfit::baseline(h * 0.5, kNamePx), kNamePx, font::sansMedium());
            if (dirty > 0.001)
            {
                const double nx = nr.x + t.measureText(name, kNamePx, font::sansMedium()) + 7.0;
                drawCircle(t, nx, h * 0.5, 2.5, Paint::filled(palette::primaryAlpha(0.9 * dirty)));
            }
        }
    }
}
}
