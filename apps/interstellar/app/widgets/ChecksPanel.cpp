#include "ChecksPanel.h"
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
        std::string plural(int n, const char *one, const char *many) { return std::to_string(n) + " " + (n == 1 ? one : many); }
    }

    ChecksPanel::ChecksPanel() { clipToBounds = true; }

    void ChecksPanel::bind(const interstellar::AppModel &m)
    {
        mChecks.clear();
        std::vector<std::pair<std::string, Check>> items;
        auto add = [&](const std::string &key, int sev, const std::string &text, const std::string &detail) {
            Check c{sev, text, detail};
            mChecks.push_back(c);
            items.emplace_back(key, c);
        };
        mProblems = 0;
        for (const auto &tl : m.timelines)
            if (tl.danglingDeltas > 0)
            {
                ++mProblems;
                add("dangling:" + tl.id, 2, (tl.name.empty() ? tl.id : tl.name) + ": " + plural(tl.danglingDeltas, "dangling delta", "dangling deltas"),
                    "The base deleted what they point at. Rebase to reconcile.");
            }
        for (const auto &n : m.rack)
        {
            if (n.failed)
            {
                ++mProblems;
                add("offline:" + n.rackObj, 2, (n.cosmoName.empty() ? n.bindName : n.cosmoName) + " is offline",
                    "Media missing" + (n.usedBy > 0 ? " \xE2\x80\x94 " + plural(n.usedBy, "clip uses", "clips use") + " it" : std::string()));
            }
            else if (n.pending)
                add("pending:" + n.rackObj, 1, (n.cosmoName.empty() ? n.bindName : n.cosmoName) + " is still decoding", "Renders wait for it.");
        }
        for (const auto &j : m.renders)
            if (j.state == "failed")
            {
                ++mProblems;
                add("render:" + j.id, 2, "Render of " + (j.timelineName.empty() ? j.timeline : j.timelineName) + " failed",
                    j.error.empty() ? std::string("No reason was given.") : j.error);
            }
        if (mProblems == 0 && !m.timelines.empty())
            add("clear", 3, "No problems found", "Every version can render.");
        for (const auto &tl : m.timelines)
        {
            if (tl.colourPinned)
                add("pin:" + tl.id, 0, (tl.name.empty() ? tl.id : tl.name) + " is pinned",
                    "Colour stopped at @" + (tl.pinCommit.size() > 7 ? tl.pinCommit.substr(0, 7) : tl.pinCommit) + "; base regrades do not reach it.");
            if (tl.cutFrozen)
                add("frozen:" + tl.id, 0, (tl.name.empty() ? tl.id : tl.name) + " is frozen", "Its arrangement no longer follows the base.");
        }
        mRows.sync(items, kRowH);
    }

    bool ChecksPanel::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type == Gesture::Type::Scroll) return mScroll.scrollBy(g.delta.y);
        return Segment::handleGesture(g, local);
    }

    void ChecksPanel::advance(double nowMs)
    {
        mRows.advance(nowMs);
        mScroll.setExtent(kHeaderH, std::max(0.0, height.value() - kHeaderH), (double)mChecks.size() * kRowH);
        mScroll.advance(nowMs);
        Segment::advance(nowMs);
    }

    void ChecksPanel::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(palette::leftRailBg()));
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(w - 0.5, 0); t.lineTo(w - 0.5, h); t.strokePath();
        const double hy = kHeaderH * 0.5;
        t.setFill(palette::mutedForeground());
        t.drawText("CHECKS", kPadX, textfit::baseline(hy, 9.0), 9.0, font::sansSemiBold(), 0.13 * 9.0);
        const double lw = t.measureText("CHECKS", 9.0, font::sansSemiBold(), 0.13 * 9.0);
        if (mProblems > 0)
        {
            const std::string n = std::to_string(mProblems);
            t.setFill(palette::destructive());
            t.drawText(n, kPadX + lw + 6.0, textfit::baseline(hy, 9.0), 9.0, font::mono());
        }
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(kPadX + lw + 22.0, hy); t.lineTo(w - kPadX, hy); t.strokePath();

        t.save();
        t.clipRect(0, kHeaderH, w, std::max(0.0, h - kHeaderH));
        if (mChecks.empty() && mRows.rows().empty())
        {
            const std::string s = "Open a project to check it.";
            t.setFill(palette::mutedForeground());
            t.drawText(s, (w - t.measureText(s, 11.0, font::sans())) * 0.5, kHeaderH + (h - kHeaderH) * 0.4, 11.0, font::sans());
        }
        for (const auto &row : mRows.rows())
        {
            const double a = row.liveAlpha();
            if (a <= 0.001 || !mScroll.bandVisible(row.liveY(), kRowH)) continue;
            const double top = kHeaderH + row.liveY() - mScroll.value();
            const auto &c = row.data;
            const Rect gb{kPadX, top + 8.0, 11.0, 11.0};
            Color gc = c.severity == 2 ? palette::destructive() : (c.severity == 3 ? palette::success() : palette::mutedForeground());
            if (c.severity == 2) glyph::warn(t, gb, fade(gc, a));
            else if (c.severity == 3) cosmo_v2::icon::checkCircle(t, gb, fade(gc, a));
            else if (c.severity == 1) glyph::spinner(t, gb.x + 5.5, gb.y + 5.5, 4.0, 0.0, a);
            else glyph::lock(t, Rect{gb.x + 1.5, gb.y, 8.0, 11.0}, fade(gc, a));
            const double x = kPadX + 18.0;
            t.setFill(fade(c.severity == 2 ? palette::foreground() : palette::foreground(), a));
            t.drawText(textfit::ellipsize(t, c.text, w - kPadX - x, 11.0, font::sans()), x, top + 16.5, 11.0, font::sans());
            t.setFill(fade(palette::mutedForeground(), a));
            t.drawText(textfit::ellipsize(t, c.detail, w - kPadX - x, 9.5, font::sans()), x, top + 30.0, 9.5, font::sans());
        }
        t.restore();
        mScroll.drawBar(t, w - 2.0);
    }
}
}
