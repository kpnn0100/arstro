#include "TopBar.h"
#include "Icons.h"
#include "TextMetrics.h"
#include "TextFit.h"
#include "Wordmark.h"
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
        // R-UX-4: the Export button. Height matches the rail toggle; the width is the label's
        // estimate plus the glyph and px-2.5 each side (the paint centres with real metrics).
        constexpr double kExportH = 20.5, kExportW = 61.75, kExportIcon = 10.0, kExportGap = 4.875;
        constexpr double kGroupGap = 9.75;      // gap-3 between the bar's members
        constexpr double kTitleMinW = 26.0;     // below this a name is one letter and a dot: draw none
        constexpr double kTitleKeepW = 120.0;   // R-FAMILY-9: how much of the project name the filename must leave
        constexpr double kFileMinW = 36.0;      // below this a filename is an ellipsis: draw "(i/n)" alone
        constexpr double kSaveBox = 20.5;       // R-FAMILY-9: the save icon button, the rail toggle's size
        constexpr double kSaveIcon = 12.0;
        // Room kept after the name for the unsaved dot: its gap, its radius, and a breath.
        constexpr double kDotRoom = DirtyDot::kGap + DirtyDot::kRadius + 3.25;
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
        if (mExportHovered && !isHovered())
        {
            mExportHovered = false; mExportPressed = false;
            mExportHover.animateTo(0.0, 120.0, Easing::EaseOutCubic, nowMs);
        }
        if (mExportWanted != mExportApplied)   // a setter has no clock: the tween starts here
        {
            mExportApplied = mExportWanted;
            mExportEnable.animateTo(mExportWanted ? 1.0 : 0.0, 180.0, Easing::EaseOutCubic, nowMs);
        }
        mExportHover.update(nowMs);
        mExportEnable.update(nowMs);
        if (mSaveHovered && !isHovered())
        {
            mSaveHovered = false; mSavePressed = false;
            mSaveHover.animateTo(0.0, motion::kHoverMs, Easing::EaseOutCubic, nowMs);
        }
        mSaveHover.update(nowMs);
        mDirty.advance(nowMs);   // R-FAMILY-9: the dot and the save icon's light, one eased value
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

        // Right group, from the right edge in: Export (R-UX-4), the save icon (R-FAMILY-9), the
        // rail toggle, the filename.
        mRailToggle->x.set(w - kPad - kExportW - kGroupGap - kSaveBox - space::gap() - mRailToggle->width.value());
        mRailToggle->y.set((kHeight - mRailToggle->height.value()) * 0.5);
    }

    Rect TopBar::exportRect() const
    {
        return Rect{width.value() - kPad - kExportW, (kHeight - kExportH) * 0.5, kExportW, kExportH};
    }

    Rect TopBar::saveRect() const
    {
        return Rect{width.value() - kPad - kExportW - kGroupGap - kSaveBox, (kHeight - kSaveBox) * 0.5, kSaveBox, kSaveBox};
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
        // R-UX-4: the Export button. Dimmed (no photo) it neither hovers nor fires.
        {
            const bool live = mExportWanted;
            const bool over = live && exportRect().contains(local);
            if (g.type == Gesture::Type::Move && over != mExportHovered)
            {
                mExportHovered = over;
                mExportHover.animateTo(over ? 1.0 : 0.0, 120.0, Easing::EaseOutCubic, mNowMs);
            }
            if (g.type == Gesture::Type::Down) mExportPressed = over;
            if (g.type == Gesture::Type::Up) mExportPressed = false;
            if (g.type == Gesture::Type::Click && exportRect().contains(local))
            {
                mExportPressed = false;
                if (live && onExport) onExport();
                return true;
            }
        }
        // R-FAMILY-9: the save icon. With nothing to save it neither hovers nor fires.
        {
            const bool live = mDirty.target();
            const bool over = live && saveRect().contains(local);
            if (g.type == Gesture::Type::Move && over != mSaveHovered)
            {
                mSaveHovered = over;
                mSaveHover.animateTo(over ? 1.0 : 0.0, motion::kHoverMs, Easing::EaseOutCubic, mNowMs);
            }
            if (g.type == Gesture::Type::Down) mSavePressed = over;
            if (g.type == Gesture::Type::Up) mSavePressed = false;
            if (g.type == Gesture::Type::Click && saveRect().contains(local))
            {
                mSavePressed = false;
                if (live && onSave) onSave();
                return true;
            }
        }
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

        // Wordmark (R-FAMILY-5/9): the one helper, the accent dot at the MEASURED end of the name.
        // (It was placed at `estimateTextWidth`, which no real font matches - the dot detached.)
        Wordmark::draw(t, "cosmo", kPad, h * 0.5 + 13.0 * 0.35, 13.0);

        // ── the right-hand group's text: the group name, or the filename readout ──
        // Drawn first because the project name (below) is fitted to what these LEAVE.
        const double slotRight = mRailToggle->x.value() - kGroupGap;
        double slotLeft = slotRight;   // left edge of whatever the right slot draws
        // Group name (clickable, hovered) in the right slot while editing a group (DR-TOPBAR).
        if (!mGroupName.empty())
        {
            const Rect nr = groupNameRect();
            slotLeft = nr.x;
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
            // Measured (R5): the estimate is 0.6 em a glyph and the mono face is not exactly that.
            // R-FAMILY-9: in a narrow bar the FILENAME gives way before the project name does -
            // the name carries the unsaved dot, and the filename is also in the breadcrumb and
            // under its filmstrip cell. So the readout is capped to what is left once the name
            // has its room (up to kTitleKeepW of it); with nothing left only "(i/n)" is drawn.
            double fileMax = kNameMaxW;
            if (!mProjectName.empty())
            {
                const double menusEnd = mMenuStrip->x.value() + mMenuStrip->width.value() + kGroupGap;
                const double want = std::min(kTitleKeepW, t.measureText(mProjectName, 12.0, font::sansMedium())) + kDotRoom + kGroupGap;
                fileMax = std::max(0.0, std::min(kNameMaxW, slotRight - t.measureText(suffix, 11.0, font::mono()) - menusEnd - want));
            }
            const std::string name = fileMax >= kFileMinW ? fitTextEnd(t, mFilename, fileMax, 11.0, font::mono()) : std::string();
            const double nameW = t.measureText(name, 11.0, font::mono());
            const double suffixW = t.measureText(suffix, 11.0, font::mono());
            const double nameX = slotRight - nameW - suffixW;
            slotLeft = nameX;
            const double fy = h * 0.5 + 11.0 * 0.35;
            t.setFill(palette::mutedForeground());
            t.drawText(name, nameX, fy, 11.0, font::mono());
            t.setFill(Color{palette::foreground().r, palette::foreground().g, palette::foreground().b, 0.3});
            t.drawText(suffix, nameX + nameW, fy, 11.0, font::mono());
        }

        // ── project name, fitted (R-UX-6), LEFT after the menus (R-FAMILY-9) ──
        // Ellipsized with real metrics to what the right-hand group leaves, less the room the
        // unsaved dot needs. It was centred; the family reads name-then-dot at the left.
        mTitleRect = Rect{0, 0, 0, 0};
        mTitleDrawn.clear();
        mDotCentre = Point{0, 0};
        if (!mProjectName.empty())
        {
            const double spanL = mMenuStrip->x.value() + mMenuStrip->width.value() + kGroupGap;
            const double spanR = slotLeft - kGroupGap - kDotRoom;
            const double span = spanR - spanL;
            if (span >= kTitleMinW)
            {
                const std::string name = fitTextEnd(t, mProjectName, span, 12.0, font::sansMedium());
                if (!name.empty())
                {
                    const double nameW = t.measureText(name, 12.0, font::sansMedium());
                    t.setFill(palette::foreground());
                    t.drawText(name, spanL, h * 0.5 + 12.0 * 0.35, 12.0, font::sansMedium());
                    mTitleRect = Rect{spanL, (h - 12.0) * 0.5, nameW, 12.0};
                    mTitleDrawn = name;
                    // The unsaved dot: foreground colour, after the measured end of the name.
                    mDotCentre = Point{spanL + nameW + DirtyDot::kGap, h * 0.5};
                    mDirty.draw(t, mDotCentre);
                }
            }
        }

        // ── Save (R-FAMILY-9): an icon that LIGHTS while there is something to save ──
        {
            const Rect b = saveRect();
            const double lit = mDirty.amount();
            const double hv = std::max(mSaveHover.value(), mSavePressed ? 1.0 : 0.0) * lit;
            if (hv > 0.001) drawRoundedRect(t, b, radius::control(), Paint::filled(palette::hoverWash(hv)));
            // Dim = the muted colour at 45% (nothing to save, plainly not pressable); lit = the
            // foreground, the dot's own colour. Derived every frame from the eased amount (R-G-1).
            Color dim = palette::mutedForeground(); dim.a *= 0.45;
            const Color c = lerpColor(dim, palette::foreground(), lit);
            icon::save(t, Rect{b.x + (b.w - kSaveIcon) * 0.5, b.y + (b.h - kSaveIcon) * 0.5, kSaveIcon, kSaveIcon}, c);
        }

        // ── Export (R-UX-4): the editor's one accent-filled button ──
        {
            const Rect b = exportRect();
            const double en = mExportEnable.value();
            const double hv = std::max(mExportHover.value(), mExportPressed ? 1.0 : 0.0) * en;
            // Disabled is the same button at 35% - still recognisably the Export button, plainly
            // not pressable - and the fade between the two is eased (R-G-1).
            const double a = 0.35 + 0.65 * en;
            const Color acc = palette::primary();
            const Color fill = lerpColor(acc, palette::white(), 0.14 * hv);
            drawRoundedRect(t, b, radius::control(), Paint::filled(Color{fill.r, fill.g, fill.b, a}));
            const Color fg{palette::primaryForeground().r, palette::primaryForeground().g, palette::primaryForeground().b, a};
            const double labelW = t.measureText("Export", 10.0, font::sansMedium());
            const double gx = b.x + (b.w - (kExportIcon + kExportGap + labelW)) * 0.5;
            icon::download(t, Rect{gx, b.y + (b.h - kExportIcon) * 0.5, kExportIcon, kExportIcon}, fg);
            t.setFill(fg);
            t.drawText("Export", gx + kExportIcon + kExportGap, b.y + b.h * 0.5 + 10.0 * 0.35, 10.0, font::sansMedium());
        }
    }
}
}
