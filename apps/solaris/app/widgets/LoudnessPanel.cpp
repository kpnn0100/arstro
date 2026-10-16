#include "LoudnessPanel.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;

    namespace
    {
        constexpr double kSilence = -143.0;    // the model says −144 for nothing measured
        constexpr double kFloorDb = -70.0;     // a reading's number eases no lower (BS.1770's absolute gate)
        constexpr double kSpecFloor = -72.0, kSpecTop = 0.0; // the spectrum's scale, dB
        constexpr double kAttackMs = 40.0;     // a band rises as a meter does (MixerDock's kMeterAttackMs)
        constexpr double kDbW = 22.75;         // space::u(7): the spectrum's dB labels
        const char *kLabels[] = {"MOMENTARY", "SHORT-TERM", "INTEGRATED", "TRUE PEAK", "CORRELATION"};
        const char *kUnits[] = {"LUFS", "LUFS", "LUFS", "dBTP", ""};
        Color fade(Color c, double a) { c.a *= a; return c; }
        std::string number(double v, int decimals)
        {
            char b[32];
            std::snprintf(b, sizeof b, "%.*f", decimals, v);
            std::string s = b;
            if (s == "-0.0" || s == "-0.00") s = s.substr(1);
            return s;
        }
        /** A band's x on the spectrum: 1/6 octave each, band 34 = 1 kHz (the model's rule). */
        double bandOf(double hz) { return 34.0 + 6.0 * std::log2(hz / 1000.0); }
    }

    LoudnessPanel::LoudnessPanel()
    {
        for (auto &b : mBand) b.set(kSpecFloor);
        for (auto &b : mBandTarget) b = kSpecFloor;
        for (auto &b : mBandLast) b = kSpecFloor;
        for (int r = 0; r < kReadings; ++r) mLive[(size_t)r].set(r == Correlation ? 0.0 : kFloorDb);
    }

    Rect LoudnessPanel::resetRect() const
    {
        const double w = 58.5; // space::u(18)
        return Rect{width.value() - space::padX() - w, (kHeaderH - 19.5) * 0.5, w, 19.5};
    }

    Rect LoudnessPanel::spectrumRect() const
    {
        const double pad = space::padX(), top = kHeaderH + space::gap() + 2.0 * kCellH + space::gap();
        return Rect{pad + kDbW, top, std::max(0.0, width.value() - 2.0 * pad - kDbW), std::max(0.0, height.value() - top - pad - 13.0)};
    }

    void LoudnessPanel::bind(const solaris::AppModel &m)
    {
        const auto &l = m.loudness;
        const double v[kReadings] = {l.momentary, l.shortTerm, l.integrated, l.truePeak, l.correlation};
        for (int r = 0; r < kReadings; ++r)
        {
            // correlation has a number whenever anything is measured; the others when they are
            mHas[(size_t)r] = r == Correlation ? l.momentary > kSilence : v[r] > kSilence;
            mTarget[(size_t)r] = r == Correlation ? v[r] : std::max(kFloorDb, v[r]);
        }
        for (int b = 0; b < kBands; ++b)
            mBandTarget[(size_t)b] = b < (int)l.spectrum.size() ? std::clamp(l.spectrum[(size_t)b], kSpecFloor, kSpecTop) : kSpecFloor;
    }

    void LoudnessPanel::advance(double nowMs)
    {
        for (int r = 0; r < kReadings; ++r)
        {
            AnimatedProperty &v = mLive[(size_t)r], &on = mOn[(size_t)r];
            const bool has = mHas[(size_t)r];
            if (!mPlaced)
            {
                if (has) v.set(mTarget[(size_t)r]); // the first placement: nowhere to travel from
                on.set(has ? 1.0 : 0.0);
            }
            else
            {
                // a number the meter, not the user, moves: it catches up (220 ms); nothing measured fades to "—"
                if (has && mTarget[(size_t)r] != mLast[(size_t)r]) v.animateTo(mTarget[(size_t)r], motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
                if (has != mHasLast[(size_t)r]) on.animateTo(has ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            }
            mHasLast[(size_t)r] = has;
            if (has) mLast[(size_t)r] = mTarget[(size_t)r];
            v.update(nowMs);
            on.update(nowMs);
        }
        for (int b = 0; b < kBands; ++b)
        {
            const double tg = mBandTarget[(size_t)b];
            AnimatedProperty &bp = mBand[(size_t)b];
            if (!mPlaced) bp.set(tg);
            else if (tg != mBandLast[(size_t)b])
                bp.animateTo(tg, tg > bp.value() ? kAttackMs : motion::kMeterFallMs, Easing::EaseOutCubic, nowMs);
            mBandLast[(size_t)b] = tg;
            bp.update(nowMs);
        }
        mPlaced = true;
        mHover.advance(nowMs);
    }

    bool LoudnessPanel::handleGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Move:
            mHover.setHovered(resetRect().contains(local) ? 0 : -1);
            return true;
        case Gesture::Type::Click:
            if (resetRect().contains(local) && onCommand) onCommand("meters clear"); // R-METER-2: one line
            return true;
        default:
            return true;
        }
    }

    void LoudnessPanel::onPaint(IRenderTarget &t) const
    {
        const double W = width.value(), pad = space::padX();
        // the header: what is measured, and Reset
        t.setFill(palette::mutedForeground());
        const Rect rs = resetRect();
        t.drawText(textfit::ellipsize(t, "ITU-R BS.1770-4 \xC2\xB7 the master", std::max(0.0, rs.x - 2.0 * pad), 10.0, font::mono()), pad,
                   textfit::baseline(rs.y + rs.h * 0.5, 10.0), 10.0, font::mono());
        drawRoundedRect(t, rs, radius::control(), Paint::filled(lerpColor(palette::secondary(), palette::popover(), mHover.amount(0))));
        t.setFill(palette::secondaryForeground());
        t.drawText("Reset", rs.x + (rs.w - t.measureText("Reset", 10.0, font::sans())) * 0.5, textfit::baseline(rs.y + rs.h * 0.5, 10.0), 10.0, font::sans());
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0, kHeaderH - 0.5); t.lineTo(W, kHeaderH - 0.5); t.strokePath();

        // the readings: three a row, a label over its number; the third cell of the second row is the needle
        const double cw = std::max(0.0, (W - 2.0 * pad) / 3.0), y0 = kHeaderH + space::gap();
        for (int r = 0; r < kReadings; ++r)
        {
            const double x = pad + (r % 3) * cw, y = y0 + (r / 3) * kCellH;
            t.setFill(palette::mutedForeground());
            t.drawText(textfit::ellipsize(t, kLabels[r], cw - 6.5, 9.0, font::sansSemiBold()), x, textfit::baseline(y + 9.75, 9.0), 9.0,
                       font::sansSemiBold(), 0.13 * 9.0);
            const double vy = textfit::baseline(y + 27.0, 13.0), on = mOn[(size_t)r].value();
            const std::string num = number(mLive[(size_t)r].value(), r == Correlation ? 2 : 1);
            if (on > 0.001)
            {
                t.setFill(fade(palette::foreground(), on));
                t.drawText(num, x, vy, 13.0, font::monoMedium());
                if (*kUnits[r])
                {
                    t.setFill(fade(palette::mutedForeground(), on));
                    t.drawText(kUnits[r], x + t.measureText(num, 13.0, font::monoMedium()) + 4.0, vy, 10.0, font::sans());
                }
            }
            if (on < 0.999)
            {
                t.setFill(fade(palette::mutedForeground(), 1.0 - on));
                t.drawText("\xE2\x80\x94", x, vy, 13.0, font::monoMedium()); // nothing measured
            }
        }
        {
            // the correlation's needle: −1 … +1, red left of centre, green right
            const double x = pad + 2.0 * cw, y = y0 + kCellH + 21.0, w = std::max(0.0, cw - 6.5);
            drawRoundedRect(t, Rect{x, y - 2.0, w, 4.0}, radius::pill(), Paint::filled(palette::input()));
            t.setStroke(palette::whiteAlpha(0.3), 1.0);
            t.beginPath(); t.moveTo(std::round(x + w * 0.5) + 0.5, y - 5.0); t.lineTo(std::round(x + w * 0.5) + 0.5, y + 5.0); t.strokePath();
            const double c = std::clamp(mLive[Correlation].value(), -1.0, 1.0), on = mOn[Correlation].value();
            const Color col = c >= 0.0 ? lerpColor(palette::secondaryForeground(), palette::success(), c) : lerpColor(palette::secondaryForeground(), palette::destructive(), -c);
            if (on > 0.001)
                drawRoundedRect(t, Rect{x + (c + 1.0) * 0.5 * w - 1.5, y - 5.0, 3.0, 10.0}, radius::pill(), Paint::filled(fade(col, on)));
            t.setFill(palette::mutedForeground());
            t.drawText("-1", x, y + 15.0, 8.0, font::mono());
            t.drawText("+1", x + w - t.measureText("+1", 8.0, font::mono()), y + 15.0, 8.0, font::mono());
        }

        // the spectrum: 61 bars of 1/6 octave on −72 … 0 dB, the grid every 12 dB, a few frequencies named
        const Rect sp = spectrumRect();
        if (sp.w <= 1.0 || sp.h <= 1.0) return;
        drawRoundedRect(t, sp, radius::control(), Paint::filled(palette::curvePlotBg()));
        auto yOf = [&](double db) { return sp.bottom() - (db - kSpecFloor) / (kSpecTop - kSpecFloor) * sp.h; };
        for (double db = -12.0; db >= -60.0; db -= 12.0)
        {
            const double y = std::round(yOf(db)) + 0.5;
            t.setStroke(palette::whiteAlpha(0.06), 1.0);
            t.beginPath(); t.moveTo(sp.x, y); t.lineTo(sp.right(), y); t.strokePath();
            const std::string s = number(db, 0);
            t.setFill(palette::mutedForeground());
            t.drawText(s, sp.x - 4.0 - t.measureText(s, 8.0, font::mono()), textfit::baseline(y, 8.0), 8.0, font::mono());
        }
        const double bw = sp.w / kBands;
        t.save();
        t.clipRect(sp.x, sp.y, sp.w, sp.h);
        for (int b = 0; b < kBands; ++b)
        {
            const double db = mBand[(size_t)b].value();
            if (db <= kSpecFloor + 0.01) continue;
            const double y = yOf(db);
            drawRoundedRect(t, Rect{sp.x + b * bw + 0.5, y, std::max(1.0, bw - 1.0), sp.bottom() - y}, radius::hairline(), Paint::filled(palette::primaryAlpha(0.75)));
        }
        t.restore();
        const struct { double hz; const char *name; } marks[] = {{50, "50"}, {100, "100"}, {500, "500"}, {1000, "1k"}, {5000, "5k"}, {10000, "10k"}};
        t.setFill(palette::mutedForeground());
        for (const auto &mk : marks)
        {
            const double x = sp.x + (bandOf(mk.hz) + 0.5) * bw;
            t.drawText(mk.name, x - t.measureText(mk.name, 8.0, font::mono()) * 0.5, sp.bottom() + 10.0, 8.0, font::mono());
        }
    }
}
}
