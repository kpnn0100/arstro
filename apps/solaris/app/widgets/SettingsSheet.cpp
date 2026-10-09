#include "SettingsSheet.h"
#include "../../../interstellar/app/widgets/Glyphs.h"
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
    namespace glyph = interstellar_v1::glyph;

    namespace
    {
        constexpr double kFontPx = 11.0;
        constexpr double kRowLabelGap = 13.0;   // label baseline → its chips: a label belongs to what is BELOW it
        constexpr double kRowGap = 26.0;        // chips → the next label: wider, so the rows read as groups
        constexpr double kFolderRowH = 26.0;
        constexpr int kDoneId = 900, kAddId = 901, kRemoveBase = 1000;
        Color fade(Color c, double a) { c.a *= a; return c; }
        std::string q(const std::string &s) { return s.find_first_of(" \t\"") == std::string::npos && !s.empty() ? s : "\"" + s + "\""; }
        const char *kLabels[SettingsSheet::kRows] = {"OUTPUT", "INPUT", "SAMPLE RATE", "BUFFER", "METRONOME", "CLICK LEVEL",
                                                     "TEMPO", "METER", "REDUCED MOTION"};
        const char *kKeys[SettingsSheet::kRows] = {"output", "input", "sampleRate", "bufferSize", "metronome", "metronomeLevel",
                                                   "newBpm", "newSig", "reducedMotion"};
        // the sheet's sections (R-SET-3), in order; the sample folders sit before Interface
        struct Section { const char *title; std::vector<int> rows; bool folders; };
        const Section kSections[] = {
            {"Audio", {SettingsSheet::kOutput, SettingsSheet::kInput, SettingsSheet::kRate, SettingsSheet::kBuffer}, false},
            {"Playback", {SettingsSheet::kMetronome, SettingsSheet::kClickLevel}, false},
            {"New songs", {SettingsSheet::kNewBpm, SettingsSheet::kNewSig}, false},
            {"Sample folders", {}, true},
            {"Interface", {SettingsSheet::kMotion}, false},
        };
        constexpr double kSectionGap = 30.0; // a section title sits this far below the last chips above it
        std::string num(double v)
        {
            char b[32];
            std::snprintf(b, sizeof b, "%g", v);
            return b;
        }
        const int kRates[] = {44100, 48000, 88200, 96000};
        const int kBuffers[] = {64, 128, 256, 512, 1024, 2048};
        // a path too wide for its row keeps its END — the folder's own name is the part that identifies it
        std::string tailFit(const IRenderTarget &t, const std::string &s, double maxW, double px, const char *fam)
        {
            if (t.measureText(s, px, fam) <= maxW) return s;
            for (size_t cut = 1; cut < s.size(); ++cut)
            {
                if ((s[cut] & 0xC0) == 0x80) continue;
                const std::string tail = "\xE2\x80\xA6" + s.substr(cut);
                if (t.measureText(tail, px, fam) <= maxW) return tail;
            }
            return "\xE2\x80\xA6";
        }
    }

    SettingsSheet::SettingsSheet()
    {
        // rate and buffer choices are fixed; device chips come from the model
        for (int r : kRates)
        {
            char b[16];
            std::snprintf(b, sizeof b, "%g kHz", r / 1000.0);
            mChips[kRate].push_back(Chip{b, std::to_string(r)});
        }
        for (int b : kBuffers) mChips[kBuffer].push_back(Chip{std::to_string(b), std::to_string(b)});
        mChips[kMetronome] = {Chip{"Off", "off"}, Chip{"On", "on"}};
        for (int d : {-18, -12, -6, 0}) mChips[kClickLevel].push_back(Chip{std::to_string(d) + " dB", std::to_string(d)});
        for (int b : {100, 120, 124, 126, 128, 140, 150, 174}) mChips[kNewBpm].push_back(Chip{std::to_string(b), std::to_string(b)});
        for (const char *m : {"4/4", "3/4", "6/8", "7/8"}) mChips[kNewSig].push_back(Chip{m, m});
        mChips[kMotion] = {Chip{"Off", "off"}, Chip{"On", "on"}};
        for (int r = 0; r < kRows; ++r) mChosen[r].assign(mChips[r].size(), AnimatedProperty{0.0});
    }

    void SettingsSheet::bind(const solaris::AppModel &m)
    {
        const auto &s = m.settings;
        mOutput = s.output;
        mInput = s.input;
        mRate = s.sampleRate;
        mBuffer = s.bufferSize;
        mLatencyMs = s.latencyMs;
        mFolders = s.folders;
        mSettings = s;
        for (int r : {kOutput, kInput})
        {
            std::vector<Chip> chips = {Chip{"System default", ""}};
            const std::string dir = r == kOutput ? "out" : "in";
            for (const auto &d : m.devices)
                if (d.dir == dir) chips.push_back(Chip{d.name, d.id});
            // a device chosen on another day but not listed now still shows, so the choice is never invisible
            const std::string &cur = r == kOutput ? mOutput : mInput;
            bool listed = cur.empty();
            for (const auto &c : chips) listed = listed || c.value == cur;
            if (!listed) chips.push_back(Chip{cur + " (not connected)", cur});
            if (chips.size() != mChips[r].size()) mChosen[r].resize(chips.size(), AnimatedProperty{0.0});
            mChips[r] = chips;
        }
    }

    std::string SettingsSheet::current(int row) const
    {
        switch (row)
        {
        case kOutput: return mOutput;
        case kInput: return mInput;
        case kRate: return std::to_string(mRate);
        case kBuffer: return std::to_string(mBuffer);
        case kMetronome: return mSettings.metronome ? "on" : "off";
        case kClickLevel: return num(mSettings.metronomeLevel);
        case kNewBpm: return num(mSettings.newBpm);
        case kNewSig: return mSettings.newSig;
        case kMotion: return mSettings.reducedMotion ? "on" : "off";
        default: return std::string();
        }
    }

    int SettingsSheet::chosen(int row) const
    {
        const std::string v = current(row);
        for (size_t i = 0; i < mChips[row].size(); ++i)
            if (mChips[row][i].value == v) return (int)i;
        return -1;
    }

    double SettingsSheet::chosenAmount(int row, int chip) const
    {
        return row >= 0 && row < kRows && chip >= 0 && chip < (int)mChosen[row].size() ? mChosen[row][(size_t)chip].value() : 0.0;
    }

    Rect SettingsSheet::chipRect(int row, int chip) const
    {
        return row >= 0 && row < kRows && chip >= 0 && chip < (int)mChipRects[row].size() ? mChipRects[row][(size_t)chip] : Rect{};
    }

    void SettingsSheet::revealRect(const Rect &r)
    {
        const Rect c = cardRect();
        mScroll.reveal(r.y - (c.y - mScroll.value()), r.h + kPad);
    }

    void SettingsSheet::show() { mShowWanted = true; mCloseWanted = false; }
    void SettingsSheet::close() { mCloseWanted = true; mShowWanted = false; }

    void SettingsSheet::advance(double nowMs)
    {
        mNowMs = nowMs;
        if (mShowWanted)
        {
            mOpen = true;
            mClosing = false;
            mAppear.animateTo(1.0, motion::kModalOpenMs, Easing::EaseOutCubic, nowMs);
            mShowWanted = false;
            mScroll.reset();
        }
        if (mCloseWanted && mOpen && !mClosing)
        {
            mClosing = true;
            mAppear.animateTo(0.0, motion::kModalCloseMs, Easing::EaseOutCubic, nowMs);
            mCloseWanted = false;
        }
        mAppear.update(nowMs);
        if (mClosing && !mAppear.isAnimating()) { mOpen = false; mClosing = false; }
        for (int r = 0; r < kRows; ++r)
        {
            const int c = chosen(r);
            for (size_t i = 0; i < mChosen[r].size(); ++i)
            {
                auto &p = mChosen[r][i];
                const double want = (int)i == c ? 1.0 : 0.0;
                // a chip that becomes chosen fills over 200 ms, whoever chose it (§1)
                if (std::fabs(p.value() - want) > 1e-6 && !p.isAnimating()) p.animateTo(want, motion::kSelectMs, Easing::EaseOutCubic, nowMs);
                p.update(nowMs);
            }
        }
        mScroll.advance(nowMs);
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    Rect SettingsSheet::cardRect() const
    {
        const double W = width.value(), H = height.value();
        const double w = std::min(kCardW, W - 32.0);
        const double h = std::min(mContentH, H - 64.0);
        return Rect{(W - w) * 0.5, std::max(32.0, (H - h) * 0.5), w, std::max(120.0, h)};
    }

    int SettingsSheet::hoverId(const Point &p) const
    {
        if (mDone.contains(p)) return kDoneId;
        if (mAddFolder.contains(p)) return kAddId;
        for (size_t i = 0; i < mRemove.size(); ++i)
            if (mRemove[i].contains(p)) return kRemoveBase + (int)i;
        int id = 0;
        for (int r = 0; r < kRows; ++r)
            for (size_t i = 0; i < mChipRects[r].size(); ++i, ++id)
                if (mChipRects[r][i].contains(p)) return id;
        return -1;
    }

    bool SettingsSheet::handleGesture(const Gesture &g, const Point &local)
    {
        if (!mOpen || mClosing) return false;
        switch (g.type)
        {
        case Gesture::Type::Move:
            mHover.setHovered(hoverId(local));
            return true;
        case Gesture::Type::Scroll:
            mScroll.scrollBy(g.delta.y);
            return true;
        case Gesture::Type::Click:
        {
            if (!mCard.contains(local)) { close(); return true; }   // the scrim closes, as cosmo's does
            if (mDone.contains(local)) { close(); return true; }
            if (mAddFolder.contains(local)) { if (onAddFolder) onAddFolder(); return true; }
            for (size_t i = 0; i < mRemove.size() && i < mFolders.size(); ++i)
                if (mRemove[i].contains(local)) { if (onCommand) onCommand("folder remove " + q(mFolders[i])); return true; }
            for (int r = 0; r < kRows; ++r)
                for (size_t i = 0; i < mChipRects[r].size() && i < mChips[r].size(); ++i)
                    if (mChipRects[r][i].contains(local))
                    {
                        if (onCommand) onCommand(std::string("settings set ") + q(std::string(kKeys[r]) + "=" + mChips[r][i].value));
                        return true;
                    }
            return true;
        }
        default:
            return true; // a modal swallows the rest
        }
    }

    bool SettingsSheet::handleKey(const KeyEvent &e)
    {
        if (!isOpen()) return false;
        if (e.type == KeyEvent::Type::Down && (e.keyCode == 27 || e.keyCode == 13)) close();
        return true; // a modal owns the keyboard
    }

    void SettingsSheet::onOverlay(IRenderTarget &t) const
    {
        const double a = mAppear.value();
        if (!mOpen || a <= 0.001) return;
        const double W = width.value(), H = height.value();
        drawRoundedRect(t, Rect{0, 0, W, H}, 0.0, Paint::filled(surface::scrim(a)));

        // ── the content, measured: sections, each a title then its rows; chip rows wrap at the inner width ──
        const double cw = std::min(kCardW, W - 32.0), inner = cw - 2 * kPad;
        double y = kPad + 16.0 + 22.0;                       // title baseline + gap
        std::vector<Rect> rowRects[kRows];
        double labelY[kRows];
        std::vector<double> sectionY;
        double foldersY = 0;
        for (const auto &sec : kSections)
        {
            sectionY.push_back(y);
            y += 22.0;                                       // the section title, then its first label
            if (sec.folders)
            {
                foldersY = y - 8.0;                          // the list reads from just under the title (its note sits beside it)
                y += std::max<size_t>(1, mFolders.size()) * kFolderRowH + 8.0 + kChipH + kSectionGap - 8.0;
                continue;
            }
            for (int r : sec.rows)
            {
                labelY[r] = y;
                y += kRowLabelGap;
                double x = 0;
                for (const auto &c : mChips[r])
                {
                    const std::string lbl = textfit::ellipsize(t, c.label, inner, kFontPx, font::sans());
                    const double w = std::min(inner, t.measureText(lbl, kFontPx, font::sans()) + 22.0);
                    if (x > 0 && x + w > inner) { x = 0; y += kChipH + 6.0; }
                    rowRects[r].push_back(Rect{x, y, w, kChipH});
                    x += w + 6.0;
                }
                y += kChipH + kRowGap;
            }
            y += kSectionGap - kRowGap;
        }
        const double doneY = y;
        mContentH = y + kChipH + kPad;

        const Rect c = cardRect();
        mCard = c;
        mScroll.setExtent(c.y, c.h, mContentH);
        const double oy = c.y - mScroll.value();                 // content → screen
        drawRoundedRect(t, c, radius::control(), Paint::filledStroked(fade(palette::popover(), a), fade(palette::border(), a), 1.0));
        t.save();
        t.clipRect(c.x, c.y, c.w, c.h);
        t.setFill(fade(palette::foreground(), a));
        t.drawText("Settings", c.x + kPad, oy + kPad + 16.0, 14.0, font::sansSemiBold());
        t.setFill(fade(palette::mutedForeground(), a));
        t.drawText("this machine \xC2\xB7 never a song's", c.x + kPad + t.measureText("Settings", 14.0, font::sansSemiBold()) + 10.0, oy + kPad + 16.0,
                   10.0, font::sans());

        // a section label: the UPPERCASE word tracked +0.13 (the micro-label formula), then a plain note
        auto label = [&](const std::string &word, const std::string &note, double baseY) {
            t.setFill(fade(palette::mutedForeground(), a));
            t.drawText(word, c.x + kPad, baseY, 9.0, font::sansSemiBold(), 0.13 * 9.0);
            if (note.empty()) return;
            const double nx = c.x + kPad + t.measureText(word, 9.0, font::sansSemiBold(), 0.13 * 9.0) + 10.0;
            t.setFill(fade(palette::mutedForeground(), 0.8 * a));
            t.drawText(textfit::ellipsize(t, note, std::max(0.0, c.x + kPad + inner - nx), 10.0, font::sans()), nx, baseY, 10.0, font::sans());
        };
        // a section title: 12 px SemiBold, a hairline above all but the first
        for (size_t k = 0; k < sectionY.size(); ++k)
        {
            if (k > 0)
            {
                t.setStroke(fade(palette::border(), a), 1.0);
                t.beginPath(); t.moveTo(c.x + kPad, oy + sectionY[k] - 14.0); t.lineTo(c.x + kPad + inner, oy + sectionY[k] - 14.0); t.strokePath();
            }
            t.setFill(fade(palette::foreground(), a));
            t.drawText(kSections[k].title, c.x + kPad, oy + sectionY[k] + 4.0, 12.0, font::sansSemiBold());
            if (kSections[k].folders)
            {
                t.setFill(fade(palette::mutedForeground(), 0.8 * a));
                t.drawText("the browser lists these first", c.x + kPad + t.measureText(kSections[k].title, 12.0, font::sansSemiBold()) + 10.0,
                           oy + sectionY[k] + 4.0, 10.0, font::sans());
            }
        }
        int hid = 0;
        for (int r = 0; r < kRows; ++r)
        {
            std::string note;
            if (r == kOutput) note = "the clock \xE2\x80\x94 every other device follows it";
            if (r == kRate) note = "new songs start here";
            if (r == kMetronome) note = "clicks while playing \xE2\x80\x94 never in a render";
            if (r == kNewBpm) note = "a new song starts at this tempo";
            if (r == kMotion) note = "every tween collapses \xE2\x80\x94 the system's own setting counts too";
            if (r == kBuffer)
            {
                char b[64];
                std::snprintf(b, sizeof b, "%.1f ms of latency", mLatencyMs);
                note = b;
            }
            label(kLabels[r], note, oy + labelY[r]);
            mChipRects[r].clear();
            for (size_t i = 0; i < rowRects[r].size(); ++i, ++hid)
            {
                const Rect b{c.x + kPad + rowRects[r][i].x, oy + rowRects[r][i].y, rowRects[r][i].w, rowRects[r][i].h};
                mChipRects[r].push_back(b);
                const double ch = i < mChosen[r].size() ? mChosen[r][i].value() : 0.0;
                drawRoundedRect(t, b, radius::control(), Paint::filledStroked(fade(palette::secondary(), a), fade(palette::border(), a), 1.0));
                if (ch > 0.001) drawRoundedRect(t, b, radius::control(), Paint::filled(fade(palette::primary(), a * ch)));
                const double hv = mHover.amount(hid) * a;
                if (hv > 0.001) drawRoundedRect(t, b, radius::control(), Paint::filled(palette::hoverWash(hv)));
                const std::string lab = textfit::ellipsize(t, mChips[r][i].label, b.w - 22.0, kFontPx, font::sans());
                t.setFill(fade(lerpColor(palette::foreground(), palette::primaryForeground(), ch), a));
                t.drawText(lab, b.x + (b.w - t.measureText(lab, kFontPx, font::sans())) * 0.5, textfit::baseline(b.y + b.h * 0.5, kFontPx), kFontPx,
                           ch > 0.5 ? font::sansMedium() : font::sans());
            }
        }

        // ── sample folders ──
        mRemove.clear();
        double fy = oy + foldersY;
        if (mFolders.empty())
        {
            t.setFill(fade(palette::mutedForeground(), 0.8 * a));
            t.drawText("None yet \xE2\x80\x94 add the folders you keep samples in.", c.x + kPad, textfit::baseline(fy + kFolderRowH * 0.5, kFontPx), kFontPx, font::sans());
            fy += kFolderRowH;
        }
        for (size_t i = 0; i < mFolders.size(); ++i)
        {
            const Rect row{c.x + kPad, fy, inner, kFolderRowH};
            const Rect rm{row.right() - 22.0, row.y + (row.h - 18.0) * 0.5, 18.0, 18.0};
            mRemove.push_back(rm);
            const double hv = mHover.amount(kRemoveBase + (int)i) * a;
            if (i + 1 < mFolders.size())
            {
                t.setStroke(fade(palette::border(), a), 1.0);
                t.beginPath(); t.moveTo(row.x, row.bottom()); t.lineTo(row.right(), row.bottom()); t.strokePath();
            }
            t.setFill(fade(palette::foreground(), a));
            t.drawText(tailFit(t, mFolders[i], inner - 34.0, 10.0, font::mono()), row.x, textfit::baseline(row.y + row.h * 0.5, 10.0), 10.0, font::mono());
            if (hv > 0.001) drawRoundedRect(t, rm, radius::control(), Paint::filled(palette::hoverWash(hv)));
            const Color xc = fade(lerpColor(palette::mutedForeground(), palette::destructive(), hv), a);
            glyph::line(t, rm.x + 5.5, rm.y + 5.5, rm.right() - 5.5, rm.bottom() - 5.5, xc, 1.3);
            glyph::line(t, rm.right() - 5.5, rm.y + 5.5, rm.x + 5.5, rm.bottom() - 5.5, xc, 1.3);
            fy += kFolderRowH;
        }
        {
            const std::string lbl = "Add folder\xE2\x80\xA6";
            const double w = t.measureText(lbl, kFontPx, font::sans()) + 40.0;
            mAddFolder = Rect{c.x + kPad, fy + 8.0, w, kChipH};
            const double hv = mHover.amount(kAddId) * a;
            drawRoundedRect(t, mAddFolder, radius::control(),
                            Paint::filledStroked(fade(palette::primaryAlpha(0.10 * hv), a), fade(lerpColor(palette::border(), palette::primary(), 0.5 + 0.5 * hv), a), 1.0));
            glyph::plus(t, Rect{mAddFolder.x + 10.0, mAddFolder.y + (kChipH - 11.0) * 0.5, 11.0, 11.0}, fade(palette::foreground(), a), 1.4);
            t.setFill(fade(palette::foreground(), a));
            t.drawText(lbl, mAddFolder.x + 28.0, textfit::baseline(mAddFolder.y + kChipH * 0.5, kFontPx), kFontPx, font::sans());
        }

        // ── Done (primary) ──
        mDone = Rect{c.right() - kPad - 88.0, oy + doneY, 88.0, kChipH + 4.0};
        drawRoundedRect(t, mDone, radius::control(), Paint::filled(fade(palette::primary(), a)));
        const double dhv = mHover.amount(kDoneId) * a;
        if (dhv > 0.001) drawRoundedRect(t, mDone, radius::control(), Paint::filled(palette::hoverWash(dhv)));
        t.setFill(fade(palette::primaryForeground(), a));
        t.drawText("Done", mDone.x + (mDone.w - t.measureText("Done", 12.0, font::sansMedium())) * 0.5, textfit::baseline(mDone.y + mDone.h * 0.5, 12.0), 12.0,
                   font::sansMedium());
        t.restore();
        mScroll.drawBar(t, c.right() - 4.0, a);
    }
}
}
