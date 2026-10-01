#include "RenderQueue.h"
#include "Glyphs.h"
#include "TextFit.h"
#include "../../../cosmo/widgets/Icons.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        constexpr double kPadX = 9.75;
        constexpr double kBarW = 140.0;
        constexpr double kStatusW = 120.0;
        Color fade(Color c, double a) { c.a *= a; return c; }
    }

    RenderQueue::RenderQueue() { clipToBounds = true; }

    int RenderQueue::stateIndex(const std::string &s)
    {
        if (s == "running") return 1;
        if (s == "done") return 2;
        if (s == "failed") return 3;
        return 0;   // queued, cancelled
    }

    double RenderQueue::stateAmount(const std::string &id, int k) const
    {
        auto it = mProgress.find(id);
        return it == mProgress.end() || k < 0 || k > 3 ? 0.0 : it->second.state[k].value();
    }

    void RenderQueue::bind(const interstellar::AppModel &m, double nowMs)
    {
        mJobs = m.renders;
        std::vector<std::pair<std::string, interstellar::RenderJobModel>> items;
        for (const auto &j : m.renders)
        {
            items.emplace_back(j.id, j);
            Progress &p = mProgress[j.id];
            p.target = j.total > 0 ? std::clamp((double)j.done / j.total, 0.0, 1.0) : (j.state == "done" ? 1.0 : 0.0);
            if (p.lastDone >= 0 && j.done > p.lastDone && nowMs > p.lastMs)
            {
                const double inst = (j.done - p.lastDone) / ((nowMs - p.lastMs) / 1000.0);
                p.rate = p.rate <= 0.0 ? inst : p.rate * 0.7 + inst * 0.3;   // smoothed, so it does not flicker
            }
            if (j.done != p.lastDone) { p.lastDone = j.done; p.lastMs = nowMs; }
            p.want = stateIndex(j.state);
        }
        mRows.sync(items, kRowH);
    }

    double RenderQueue::shownFraction(const std::string &id) const
    {
        auto it = mProgress.find(id);
        return it == mProgress.end() ? 0.0 : it->second.frac.value();
    }
    double RenderQueue::rate(const std::string &id) const
    {
        auto it = mProgress.find(id);
        return it == mProgress.end() ? 0.0 : it->second.rate;
    }

    Rect RenderQueue::rowRect(int i) const
    {
        const auto *r = mRows.byIndex(i);
        return Rect{0, kHeaderH + (r ? r->liveY() : i * kRowH) - mScroll.value(), width.value(), kRowH};
    }

    bool RenderQueue::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type == Gesture::Type::Scroll) return mScroll.scrollBy(g.delta.y);
        return Segment::handleGesture(g, local);
    }

    void RenderQueue::advance(double nowMs)
    {
        mRows.advance(nowMs);
        mScroll.setExtent(kHeaderH, std::max(0.0, height.value() - kHeaderH), (double)mJobs.size() * kRowH);
        mScroll.advance(nowMs);
        for (auto &kv : mProgress)
        {
            Progress &p = kv.second;
            if (p.last < 0.0) { p.frac.set(p.target); p.last = p.target; }
            else if (p.target != p.last)
            {
                p.frac.animateTo(p.target, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
                p.last = p.target;
            }
            p.frac.update(nowMs);
            for (int k = 0; k < 4; ++k)
            {
                if (p.applied < 0) p.state[k].set(k == p.want ? 1.0 : 0.0);
                else if (p.want != p.applied) p.state[k].animateTo(k == p.want ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
                p.state[k].update(nowMs);
            }
            p.applied = p.want;
        }
        Segment::advance(nowMs);
    }

    void RenderQueue::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(surface::deckBg()));
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0, 0.5); t.lineTo(w, 0.5); t.strokePath();
        const double hy = kHeaderH * 0.5;
        t.setFill(palette::mutedForeground());
        t.drawText("RENDER QUEUE", kPadX, textfit::baseline(hy, 9.0), 9.0, font::sansSemiBold(), 0.13 * 9.0);
        const double lw = t.measureText("RENDER QUEUE", 9.0, font::sansSemiBold(), 0.13 * 9.0);
        const std::string count = std::to_string(mJobs.size());
        t.setFill(fade(palette::mutedForeground(), 0.7));
        t.drawText(count, kPadX + lw + 6.0, textfit::baseline(hy, 9.0), 9.0, font::mono());
        const double rx = kPadX + lw + 6.0 + t.measureText(count, 9.0, font::mono()) + 6.5;
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(rx, hy); t.lineTo(w - kPadX, hy); t.strokePath();

        t.save();
        t.clipRect(0, kHeaderH, w, std::max(0.0, h - kHeaderH));
        if (mJobs.empty() && mRows.rows().empty())
        {
            const std::string s1 = "nothing queued";
            const std::string s2 = "Choose a version and press Render \xE2\x80\x94 the job names the version it renders.";
            const double cy = kHeaderH + (h - kHeaderH) * 0.42;
            t.setFill(palette::mutedForeground());
            t.drawText(s1, (w - t.measureText(s1, 12.0, font::sans())) * 0.5, cy, 12.0, font::sans());
            const std::string s2e = textfit::ellipsize(t, s2, w - 2 * kPadX, 10.0, font::sans());
            t.setFill(fade(palette::mutedForeground(), 0.75));
            t.drawText(s2e, (w - t.measureText(s2e, 10.0, font::sans())) * 0.5, cy + 17.0, 10.0, font::sans());
        }
        for (const auto &row : mRows.rows())
        {
            const double a = row.liveAlpha();
            if (a <= 0.001 || !mScroll.bandVisible(row.liveY(), kRowH)) continue;
            const auto &j = row.data;
            const double top = kHeaderH + row.liveY() - mScroll.value();
            const double l1 = top + 11.5, l2 = top + 23.0;
            if (row.index >= 0 && row.index % 2) drawRoundedRect(t, Rect{0, top, w, kRowH}, 0.0, Paint::filled(fade(surface::laneAltBg(), a)));
            auto pit = mProgress.find(j.id);
            double st[4] = {0, 0, 0, 0};
            for (int k = 0; k < 4; ++k) st[k] = pit == mProgress.end() ? (k == stateIndex(j.state) ? 1.0 : 0.0) : pit->second.state[k].value();
            const double barA = st[1] + st[2];   // the bar shows while running or done
            const Rect gb{kPadX, l1 - 5.5, 11.0, 11.0};
            if (st[3] > 0.001) glyph::warn(t, gb, fade(palette::destructive(), a * st[3]));
            if (st[2] > 0.001) cosmo_v2::icon::checkCircle(t, gb, fade(palette::success(), a * st[2]));
            if (st[1] > 0.001) glyph::film(t, gb, fade(palette::primary(), a * st[1]));
            if (st[0] > 0.001) glyph::film(t, gb, fade(palette::mutedForeground(), a * st[0]));

            // right block first (measured), so the name and the path get what is left (R5)
            const double right = w - kPadX;
            const double statusX = right - kStatusW;
            const double barX = statusX - 10.0 - kBarW;
            const double textRight = statusX - 12.0 - (kBarW + 10.0) * std::min(1.0, barA);
            // the version, NAMED (R-RENDER-1), then the format chip
            const double x = kPadX + 18.0;
            std::string fmt = j.format;
            for (char &c : fmt) c = (char)std::toupper((unsigned char)c);
            const double chipW = t.measureText(fmt, 8.0, font::mono()) + 8.0;
            const std::string name = textfit::ellipsize(t, j.timelineName.empty() ? j.timeline : j.timelineName, textRight - x - chipW - 8.0, 11.0, font::sansMedium());
            t.setFill(fade(palette::foreground(), a));
            t.drawText(name, x, textfit::baseline(l1, 11.0), 11.0, font::sansMedium());
            const double cx = x + t.measureText(name, 11.0, font::sansMedium()) + 8.0;
            const Rect chip{cx, l1 - 6.5, chipW, 13.0};
            drawRoundedRect(t, chip, radius::hairline(), Paint::filledStroked(fade(palette::secondary(), a), fade(palette::border(), a), 1.0));
            t.setFill(fade(palette::mutedForeground(), a));
            t.drawText(fmt, chip.x + 4.0, textfit::baseline(l1, 8.0), 8.0, font::mono());
            t.setFill(fade(palette::mutedForeground(), a));
            t.drawText(textfit::ellipsize(t, j.outPath, textRight - x, 9.0, font::mono()), x, textfit::baseline(l2, 9.0), 9.0, font::mono());

            // the bar: its colour slides from the accent to success as the job finishes
            const double frac = shownFraction(j.id);
            if (barA > 0.001)
            {
                const double by = top + kRowH * 0.5 - 1.5;
                drawRoundedRect(t, Rect{barX, by, kBarW, 3.0}, radius::pill(), Paint::filled(fade(palette::secondary(), a * barA)));
                if (frac * kBarW >= 3.0)
                {
                    const double doneMix = barA > 0 ? st[2] / barA : 0.0;
                    drawRoundedRect(t, Rect{barX, by, frac * kBarW, 3.0}, radius::pill(),
                                    Paint::filled(fade(lerpColor(palette::primary(), palette::success(), doneMix), a * barA)));
                }
            }
            // the status line, one per state, cross-faded
            char buf[64];
            for (int k = 0; k < 4; ++k)
            {
                if (st[k] <= 0.001) continue;
                std::string status;
                Color sc = palette::mutedForeground();
                const char *fam = font::sans();
                if (k == 1)
                {
                    const double r = rate(j.id);
                    if (r > 0.05) std::snprintf(buf, sizeof buf, "%d%% \xC2\xB7 %.0f fps", (int)std::round(frac * 100.0), r);
                    else std::snprintf(buf, sizeof buf, "%d%% \xC2\xB7 %d/%d", (int)std::round(frac * 100.0), j.done, j.total);
                    status = buf;
                    sc = palette::foreground();
                    fam = font::mono();
                }
                else if (k == 2) { std::snprintf(buf, sizeof buf, "done \xC2\xB7 %d frames", j.total); status = buf; sc = palette::success(); }
                else if (k == 3) { status = j.error.empty() ? std::string("failed") : "failed \xE2\x80\x94 " + j.error; sc = palette::destructive(); }
                else status = j.state == "cancelled" ? "cancelled" : "queued";
                const std::string se = textfit::ellipsize(t, status, kStatusW, 10.0, fam);
                t.setFill(fade(sc, a * st[k]));
                t.drawText(se, right - t.measureText(se, 10.0, fam), textfit::baseline(top + kRowH * 0.5, 10.0), 10.0, fam);
            }
        }
        t.restore();
        mScroll.drawBar(t, w - 2.0);
    }
}
}
