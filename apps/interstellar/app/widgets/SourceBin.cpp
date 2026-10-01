#include "SourceBin.h"
#include "CommandLine.h"
#include "Glyphs.h"
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
        constexpr double kPadX = 9.75;
        Color fade(Color c, double a) { c.a *= a; return c; }
        Rect emptyChip(const Rect &vp) { return Rect{vp.x + (vp.w - 116.0) * 0.5, vp.y + vp.h * 0.42 + 14.0, 116.0, 24.0}; }
    }

    SourceBin::SourceBin() { clipToBounds = true; }

    void SourceBin::bind(const interstellar::AppModel &m)
    {
        mSources.clear();
        std::vector<std::pair<std::string, interstellar::RackNodeModel>> items;
        for (const auto &n : m.rack)
            if (!n.group)
            {
                mSources.push_back(n);
                items.emplace_back(n.rackObj.empty() ? n.bindName : n.rackObj, n);
            }
        mRows.sync(items, kRowH);
        bool anyVideoClip = false;
        for (const auto &c : m.clips) if (!c.audio) { anyVideoClip = true; break; }
        mHighlightWanted = !anyVideoClip && !mSources.empty() && !m.tracks.empty();
        mSelectedBind = (m.selectedRack >= 0 && m.selectedRack < (int)m.rack.size()) ? m.rack[m.selectedRack].bindName : std::string();
        int sel = -1;
        for (int i = 0; i < (int)mSources.size(); ++i) if (mSources[i].bindName == mSelectedBind) sel = i;
        mSel.setHovered(sel);   // the selection wash cross-fades between rows, like hover
    }

    Rect SourceBin::viewport() const { return Rect{0, kHeaderH, width.value(), std::max(0.0, height.value() - kHeaderH)}; }

    Rect SourceBin::rowRect(int i) const
    {
        const auto *r = mRows.byIndex(i);
        const double y = r ? r->liveY() : i * kRowH;
        return Rect{0, kHeaderH + y - mScroll.value(), width.value(), kRowH};
    }

    int SourceBin::rowAt(const Point &p) const
    {
        if (!viewport().contains(p)) return -1;
        for (int i = 0; i < (int)mSources.size(); ++i)
            if (rowRect(i).contains(p)) return i;
        return -1;
    }

    bool SourceBin::handleGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Move:
            if (mSources.empty() && emptyChip(viewport()).contains(local)) { mHover.setHovered(0); return true; }
            mHover.setHovered(rowAt(local) >= 0 ? 10 + rowAt(local) : -1);
            return true;
        case Gesture::Type::Down:
            return true;
        case Gesture::Type::Scroll:
            return mScroll.scrollBy(g.delta.y);
        case Gesture::Type::Click:
        {
            if (mSources.empty() && emptyChip(viewport()).contains(local)) { if (onAddFootage) onAddFootage(); return true; }
            const int i = rowAt(local);
            if (i >= 0 && mSources[i].bindName != mSelectedBind && onCommand)
                onCommand("rack select " + cmd::quote(mSources[i].bindName));
            return true;
        }
        default:
            break;
        }
        return Segment::handleGesture(g, local);
    }

    void SourceBin::advance(double nowMs)
    {
        mPhaseMs = nowMs;
        mRows.advance(nowMs);
        mScroll.setExtent(kHeaderH, std::max(0.0, height.value() - kHeaderH), (double)mSources.size() * kRowH);
        mScroll.advance(nowMs);
        if (!mInit) { mHighlight.set(mHighlightWanted ? 1.0 : 0.0); mHighlightApplied = mHighlightWanted; mInit = true; }
        if (mHighlightWanted != mHighlightApplied)
        {
            mHighlight.animateTo(mHighlightWanted ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            mHighlightApplied = mHighlightWanted;
        }
        mHighlight.update(nowMs);
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        mSel.advance(nowMs, motion::kSelectMs);
        Segment::advance(nowMs);
    }

    void SourceBin::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(palette::leftRailBg()));
        const double hl = mHighlight.value();
        if (hl > 0.001) drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(palette::primaryAlpha(0.05 * hl)));
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(w - 0.5, 0); t.lineTo(w - 0.5, h); t.strokePath();

        const double hy = kHeaderH * 0.5;
        t.setFill(palette::mutedForeground());
        t.drawText("SOURCE BIN", kPadX, textfit::baseline(hy, 9.0), 9.0, font::sansSemiBold(), 0.13 * 9.0);
        const double lw = t.measureText("SOURCE BIN", 9.0, font::sansSemiBold(), 0.13 * 9.0);
        const std::string count = std::to_string(mSources.size());
        t.setFill(fade(palette::mutedForeground(), 0.7));
        t.drawText(count, kPadX + lw + 6.0, textfit::baseline(hy, 9.0), 9.0, font::mono());
        const double rx = kPadX + lw + 6.0 + t.measureText(count, 9.0, font::mono()) + 6.5;
        if (w - kPadX > rx)
        {
            t.setStroke(palette::border(), 1.0);
            t.beginPath(); t.moveTo(rx, hy); t.lineTo(w - kPadX, hy); t.strokePath();
        }

        const Rect vp = viewport();
        t.save();
        t.clipRect(vp.x, vp.y, vp.w, vp.h);
        if (mSources.empty() && mRows.rows().empty())
        {
            const std::string s = "no footage yet \xE2\x80\x94 add some";
            t.setFill(palette::mutedForeground());
            t.drawText(s, vp.x + (vp.w - t.measureText(s, 11.0, font::sans())) * 0.5, vp.y + vp.h * 0.42, 11.0, font::sans());
            const Rect chip = emptyChip(vp);
            const double hv = mHover.amount(0);
            drawRoundedRect(t, chip, radius::control(),
                            Paint::filledStroked(palette::primaryAlpha(0.10 * hv), lerpColor(palette::border(), palette::primary(), 0.5 + 0.5 * hv), 1.0));
            const std::string cl = "Add footage\xE2\x80\xA6";
            t.setFill(palette::foreground());
            t.drawText(cl, chip.x + (chip.w - t.measureText(cl, 11.0, font::sans())) * 0.5, textfit::baseline(chip.y + chip.h * 0.5, 11.0), 11.0, font::sans());
        }
        for (const auto &row : mRows.rows())
        {
            const auto &n = row.data;
            const double a = row.liveAlpha();
            if (a <= 0.001 || !mScroll.bandVisible(row.liveY(), kRowH)) continue;
            const double top = kHeaderH + row.liveY() - mScroll.value();
            const double hv = row.index >= 0 ? mHover.amount(10 + row.index) : 0.0;
            const double sel = row.index >= 0 ? mSel.amount(row.index) : 0.0;
            if (sel > 0.001) drawRoundedRect(t, Rect{0, top, w - 1, kRowH}, 0.0, Paint::filled(palette::primaryAlpha(0.10 * a * sel)));
            if (hv > 0.001) drawRoundedRect(t, Rect{0, top, w - 1, kRowH}, 0.0, Paint::filled(palette::hoverWash(hv * a)));
            const double l1 = top + 11.5, l2 = top + 23.0;
            const Rect gb{kPadX, l1 - 6.0, 12.0, 12.0};
            const Color gc = fade(n.failed ? palette::destructive() : palette::mutedForeground(), a);
            if (n.pending) glyph::spinner(t, gb.x + 6.0, gb.y + 6.0, 4.5, mPhaseMs, a);
            else if (n.failed) glyph::warn(t, gb, gc);
            else if (n.video) glyph::film(t, gb, gc);
            else cosmo_v2::icon::image(t, gb, gc);
            const double x = kPadX + 18.0;
            const std::string uses = n.usedBy == 0 ? "unused" : (n.usedBy == 1 ? "1 clip" : std::to_string(n.usedBy) + " clips");
            const double uw = t.measureText(uses, 9.0, font::sans());
            const std::string nm = textfit::ellipsize(t, n.cosmoName.empty() ? n.bindName : n.cosmoName, w - kPadX - x, 11.0, font::sans());
            t.setFill(fade(n.failed ? palette::destructive() : palette::foreground(), a));
            t.drawText(nm, x, textfit::baseline(l1, 11.0), 11.0, font::sans());
            if (n.failed)
            {
                t.setFill(fade(palette::destructive(), a));
                t.drawText(textfit::ellipsize(t, "offline \xE2\x80\x94 media missing", w - kPadX - x, 9.0, font::sans()), x, textfit::baseline(l2, 9.0), 9.0, font::sans());
            }
            else
            {
                t.setFill(fade(palette::mutedForeground(), a));
                t.drawText(uses, w - kPadX - uw, textfit::baseline(l2, 9.0), 9.0, font::sans());
                const std::string meta = n.pending ? std::string("decoding\xE2\x80\xA6") : n.bindName;
                t.drawText(textfit::ellipsize(t, meta, w - kPadX - uw - 8.0 - x, 9.0, n.pending ? font::sans() : font::mono()), x,
                           textfit::baseline(l2, 9.0), 9.0, n.pending ? font::sans() : font::mono());
            }
        }
        t.restore();
        mScroll.drawBar(t, w - 2.0);
    }

    void SourceBin::onOverlay(IRenderTarget &t) const
    {
        const double hl = mHighlight.value();
        if (hl <= 0.001) return;
        // the "drag a source here" target is lit: an accent outline, eased in and out
        drawRoundedRect(t, Rect{1.5, 1.5, width.value() - 3.0, height.value() - 3.0}, radius::control(),
                        Paint::stroked(palette::primaryAlpha(0.55 * hl), 1.0));
    }
}
}
