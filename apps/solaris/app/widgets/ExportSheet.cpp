#include "ExportSheet.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;

    namespace
    {
        constexpr double kHeaderH = 60.0;  // the card's title band (the Settings sheet's)
        constexpr double kFooterH = 56.0;  // Cancel · Export…, under a hairline
        constexpr int kExportId = 900, kCancelId = 901;
        constexpr double kDimmed = 0.35;   // a row that does not apply: still there, so the sheet does not jump
        const char *kLabels[ExportSheet::kRows] = {"Format", "Sample rate", "Bit depth", "Quality", "Loudness", "Ceiling", "Stems", "With the video"};
        const char *kNotes[ExportSheet::kRows] = {"the file's kind \xE2\x80\x94 its extension follows",
                                                  "the song rendered at it: no resampler",
                                                  "16 and 24 are dithered (TPDF)",
                                                  "how much a lossy file keeps",
                                                  "the whole mix gained to an integrated loudness",
                                                  "\xE2\x80\xA6 held so its true peak stays under this",
                                                  "a file per strip beside the mix, gained alike",
                                                  "the song's picture and the mix in one file"};
        Color fade(Color c, double a) { c.a *= a; return c; }
        bool lossy(const std::string &f) { return f == "mp3" || f == "ogg" || f == "opus"; }
    }

    ExportSheet::ExportSheet()
    {
        for (double &l : mRowLast) l = -1.0;
        mOpts[kFormat] = {{"WAV", "wav"}, {"FLAC", "flac"}, {"MP3", "mp3"}, {"Ogg Vorbis", "ogg"}, {"Opus", "opus"}};
        mAudioFormats = mOpts[kFormat];
        mOpts[kRate] = {{"The song's", ""}, {"44.1 kHz", "44100"}, {"48 kHz", "48000"}, {"88.2 kHz", "88200"}, {"96 kHz", "96000"}};
        mOpts[kBits] = {{"16-bit", "16"}, {"24-bit", "24"}, {"32-bit float", "32f"}};
        mOpts[kLoudness] = {{"Off", ""}, {"\xE2\x88\x92" "23 LUFS \xE2\x80\x94 broadcast", "-23"}, {"\xE2\x88\x92" "16 LUFS \xE2\x80\x94 podcasts", "-16"},
                            {"\xE2\x88\x92" "14 LUFS \xE2\x80\x94 streaming", "-14"}, {"\xE2\x88\x92" "9 LUFS \xE2\x80\x94 loud", "-9"}};
        mOpts[kCeiling] = {{"\xE2\x88\x92" "0.3 dBTP", "-0.3"}, {"\xE2\x88\x92" "1 dBTP", "-1"}, {"\xE2\x88\x92" "2 dBTP", "-2"}};
        for (int r = 0; r < kRows; ++r)
        {
            if (r == kStems || r == kVideo)
            {
                auto tg = std::make_shared<SettingToggle>();
                addChild(tg);
                (r == kStems ? mStems : mVideo) = tg;
                if (r == kVideo) tg->onChange = [this](bool) { videoChanged(); };
                continue;
            }
            auto cb = std::make_shared<SettingCombo>();
            cb->width.set(kComboW);
            cb->height.set(kControlH);
            if (r == kFormat) cb->onChange = [this](int) { formatChanged(); };
            addChild(cb);
            mCombos[r] = cb;
        }
        for (int r : {kFormat, kRate, kBits, kLoudness, kCeiling}) setOptions(r, mOpts[r], r == kBits ? "24" : r == kCeiling ? "-1" : "");
        formatChanged();
        opacity.set(0.0);
    }

    void ExportSheet::setOptions(int row, std::vector<Opt> opts, const std::string &select)
    {
        mOpts[row] = std::move(opts);
        std::vector<std::string> labels;
        int at = 0;
        for (size_t i = 0; i < mOpts[row].size(); ++i)
        {
            labels.push_back(mOpts[row][i].label);
            if (mOpts[row][i].value == select) at = (int)i;
        }
        mCombos[row]->setOptions(labels);
        mCombos[row]->setSelectedIndex(at); // silent: the label cross-fades to it (SettingCombo)
    }

    std::string ExportSheet::value(int row) const
    {
        if (row == kStems) return mStems->on() ? "on" : "";
        if (row == kVideo) return withVideo() ? "on" : "";
        const int i = mCombos[row]->selectedIndex();
        return i >= 0 && i < (int)mOpts[row].size() ? mOpts[row][(size_t)i].value : std::string();
    }

    void ExportSheet::formatChanged()
    {
        // the qualities a format has, its own default first chosen; a choice the format cannot take moves to one it can
        const std::string f = value(kFormat);
        if (f == "opus") setOptions(kQuality, {{"96 kbps", "96"}, {"128 kbps", "128"}, {"192 kbps", "192"}, {"256 kbps", "256"}}, "192");
        else if (lossy(f))
            setOptions(kQuality, {{"128 kbps", "128"}, {"192 kbps", "192"}, {"256 kbps", "256"}, {"320 kbps", "320"}}, f == "mp3" ? "320" : "256");
        else setOptions(kQuality, {{"\xE2\x80\x94", ""}}, "");
        if (f == "flac" && value(kBits) == "32f") mCombos[kBits]->setSelectedIndex(1); // FLAC: 16 or 24
        if (f == "mp3" && std::atoi(value(kRate).c_str()) > 48000) mCombos[kRate]->setSelectedIndex(2); // MP3: to 48 kHz
    }

    void ExportSheet::videoChanged()
    {
        // R-VIDEO-1: with the video the file is a picture's — MP4 or MKV; without, the audio format it was left at
        if (withVideo())
        {
            const std::string f = value(kFormat);
            if (f != "mp4" && f != "mkv") mAudioFormat = f;
            setOptions(kFormat, {{"MP4", "mp4"}, {"MKV", "mkv"}}, f == "mkv" ? "mkv" : "mp4");
        }
        else if (value(kFormat) == "mp4" || value(kFormat) == "mkv")
            setOptions(kFormat, mAudioFormats, mAudioFormat);
        formatChanged();
    }

    bool ExportSheet::applies(int row) const
    {
        const std::string f = value(kFormat);
        const bool video = withVideo();
        switch (row)
        {
        case kRate: return f != "opus";            // an Opus file is 48 kHz
        case kBits: return !lossy(f) && !video;    // the picture's mix is encoded as its container's (AAC / FLAC)
        case kQuality: return lossy(f) && !video;
        case kCeiling: return !value(kLoudness).empty();
        case kStems: return !video;                // ONE file: the picture and the mix
        case kVideo: return mHasVideo;
        default: return true;
        }
    }

    double ExportSheet::rowAmount(int row) const { return row >= 0 && row < kRows ? mRowOn[row].value() : 0.0; }

    std::string ExportSheet::extension() const { return "." + value(kFormat); }

    std::string ExportSheet::flags() const
    {
        // the choices that apply, as `render` spells them (R-EXPORT-1)
        const std::string f = value(kFormat);
        std::string s = withVideo() ? "--video" : "--format " + f; // R-VIDEO-1: the extension says MP4 or MKV
        if (applies(kRate) && !value(kRate).empty()) s += " --rate " + value(kRate);
        if (applies(kBits)) s += " --bits " + value(kBits);
        if (applies(kQuality)) s += " --kbps " + value(kQuality);
        if (!value(kLoudness).empty()) s += " --normalize " + value(kLoudness) + " --ceiling " + value(kCeiling);
        if (applies(kStems) && mStems->on()) s += " --stems all";
        return s;
    }

    // ── geometry ──────────────────────────────────────────────────────────────────────────────────

    Rect ExportSheet::cardRect() const
    {
        const double W = width.value(), H = height.value();
        const double w = std::min(kCardW, W - 32.0), h = std::min(kHeaderH + kRows * kRowH + 12.0 + kFooterH, H - 48.0);
        return Rect{std::floor((W - w) * 0.5), std::floor((H - h) * 0.5), w, h};
    }

    void ExportSheet::layoutControls()
    {
        const Rect c = cardRect();
        for (int r = 0; r < kRows; ++r)
        {
            Segment *s = control(r);
            const double top = c.y + kHeaderH + r * kRowH;
            s->x.set(c.right() - kPad - s->width.value());
            s->y.set(top + (kRowH - s->height.value()) * 0.5);
            const bool fits = top + kRowH <= c.bottom() - kFooterH; // a short window: rows under the footer are hidden
            s->visible = fits;
            s->opacity.set(kDimmed + (1.0 - kDimmed) * mRowOn[r].value());
            s->inputTransparent = !applies(r);
        }
    }

    Segment *ExportSheet::control(int row) const
    {
        return row == kStems ? (Segment *)mStems.get() : row == kVideo ? (Segment *)mVideo.get() : (Segment *)mCombos[row].get();
    }

    Rect ExportSheet::controlRect(int row) const
    {
        if (row < 0 || row >= kRows) return Rect{};
        const Segment *s = control(row);
        const Point p = s->worldTransform().apply(Point{0.0, 0.0});
        return Rect{p.x, p.y, s->width.value(), s->height.value()};
    }

    int ExportSheet::optionCount(int row) const { return row >= 0 && row < kRows && mCombos[row] ? (int)mCombos[row]->options().size() : 0; }
    std::string ExportSheet::optionLabel(int row, int i) const { return i >= 0 && i < optionCount(row) ? mCombos[row]->options()[(size_t)i] : std::string(); }
    bool ExportSheet::comboOpen(int row) const { return row >= 0 && row < kRows && mCombos[row] && mCombos[row]->isOpen(); }
    std::string ExportSheet::shownValue(int row) const { return row >= 0 && row < kRows && mCombos[row] ? mCombos[row]->shown() : std::string(); }

    Rect ExportSheet::optionRect(int row, int i) const
    {
        if (i < 0 || i >= optionCount(row) || !mCombos[row]->isOpen()) return Rect{};
        const auto &cb = *mCombos[row];
        const Rect f = controlRect(row);
        return Rect{f.x, f.y + cb.popup().top + i * cb.rowHeight - cb.popupScroll(), f.w, cb.rowHeight};
    }

    void ExportSheet::closeCombos()
    {
        for (auto &c : mCombos)
            if (c) c->close();
    }

    // ── time ──────────────────────────────────────────────────────────────────────────────────────

    void ExportSheet::show(const solaris::AppModel &m)
    {
        mName = m.projectName.empty() ? std::string("Untitled") : m.projectName;
        mSongRate = m.sampleRate;
        mHasVideo = !m.video.id.empty(); // R-VIDEO-1: no picture, no video row (greyed, so the sheet never jumps)
        if (!mHasVideo && mVideo->on())
        {
            mVideo->setOn(false);
            videoChanged();
        }
        char b[48];
        std::snprintf(b, sizeof b, "The song's \xE2\x80\x94 %g kHz", mSongRate / 1000.0);
        auto rates = mOpts[kRate];
        rates[0].label = b;
        setOptions(kRate, rates, value(kRate));
        mShowWanted = true;
        mCloseWanted = false;
    }

    void ExportSheet::close()
    {
        mCloseWanted = true;
        mShowWanted = false;
        closeCombos();
    }

    void ExportSheet::advance(double nowMs)
    {
        if (mShowWanted)
        {
            mOpen = true;
            mClosing = false;
            mAppear.animateTo(1.0, motion::kModalOpenMs, Easing::EaseOutCubic, nowMs);
            mShowWanted = false;
        }
        if (mCloseWanted && mOpen && !mClosing)
        {
            mClosing = true;
            mAppear.animateTo(0.0, motion::kModalCloseMs, Easing::EaseOutCubic, nowMs);
            mCloseWanted = false;
        }
        mAppear.update(nowMs);
        if (mClosing && !mAppear.isAnimating()) { mOpen = false; mClosing = false; }
        opacity.set(mOpen ? mAppear.value() : 0.0); // the sheet fades as one group
        for (int r = 0; r < kRows; ++r)
        {
            const double want = applies(r) ? 1.0 : 0.0;
            if (!mRowsPlaced) { mRowOn[r].set(want); mRowLast[r] = want; }
            else if (want != mRowLast[r]) { mRowOn[r].animateTo(want, motion::kSelectMs, Easing::EaseOutCubic, nowMs); mRowLast[r] = want; }
            mRowOn[r].update(nowMs);
        }
        mRowsPlaced = true;
        mHover.advance(nowMs);
        layoutControls();
        Segment::advance(nowMs);
    }

    // ── input ─────────────────────────────────────────────────────────────────────────────────────

    bool ExportSheet::handleGesture(const Gesture &g, const Point &local)
    {
        if (!mOpen || mClosing) return false;
        switch (g.type)
        {
        case Gesture::Type::Move:
            mHover.setHovered(mExport.contains(local) ? kExportId : mCancel.contains(local) ? kCancelId : -1);
            return true;
        case Gesture::Type::Click:
            closeCombos();
            if (!cardRect().contains(local) || mCancel.contains(local)) { close(); return true; } // the scrim closes, as cosmo's
            if (mExport.contains(local))
            {
                const std::string f = flags(), name = mName + extension();
                close();
                if (onExport) onExport(f, name);
                return true;
            }
            return true;
        default:
            return true; // a modal swallows the rest
        }
    }

    bool ExportSheet::handleKey(const KeyEvent &e)
    {
        if (!isOpen()) return false;
        if (e.type == KeyEvent::Type::Down && e.keyCode == 27)
        {
            bool open = false;
            for (const auto &c : mCombos) open = open || (c && c->isOpen());
            if (open) closeCombos(); // Escape: the list first
            else close();
        }
        return true; // a modal owns the keyboard
    }

    // ── paint ─────────────────────────────────────────────────────────────────────────────────────

    void ExportSheet::onPaint(IRenderTarget &t) const
    {
        const double W = width.value(), H = height.value();
        drawRoundedRect(t, Rect{0, 0, W, H}, 0.0, Paint::filled(surface::scrim(1.0))); // the group's opacity fades it
        const Rect c = cardRect();
        drawRoundedRect(t, c, radius::control(), Paint::filledStroked(palette::popover(), palette::border(), 1.0));
        t.setFill(palette::foreground());
        t.drawText("Export", c.x + kPad, c.y + 34.0, 14.0, font::sansSemiBold());
        t.setFill(palette::mutedForeground());
        const double tx = c.x + kPad + t.measureText("Export", 14.0, font::sansSemiBold()) + 10.0;
        t.drawText(textfit::ellipsize(t, mName + extension(), std::max(0.0, c.right() - kPad - tx), 10.0, font::mono()), tx, c.y + 34.0, 10.0, font::mono());
        t.save();
        t.clipRect(c.x, c.y + kHeaderH, c.w, std::max(0.0, c.h - kHeaderH - kFooterH));
        for (int r = 0; r < kRows; ++r)
        {
            const double top = c.y + kHeaderH + r * kRowH, a = kDimmed + (1.0 - kDimmed) * mRowOn[r].value();
            const double room = c.w - 2.0 * kPad - (r == kStems || r == kVideo ? 40.0 : kComboW) - 16.0;
            t.setFill(fade(palette::foreground(), a));
            t.drawText(textfit::ellipsize(t, kLabels[r], room, 12.0, font::sans()), c.x + kPad, top + 19.0, 12.0, font::sans());
            t.setFill(fade(palette::mutedForeground(), a));
            t.drawText(textfit::ellipsize(t, kNotes[r], room, 10.0, font::sans()), c.x + kPad, top + 34.0, 10.0, font::sans());
        }
        t.restore();
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(c.x, c.bottom() - kFooterH); t.lineTo(c.right(), c.bottom() - kFooterH); t.strokePath();
        // Cancel (secondary) · Export… (primary)
        mExport = Rect{c.right() - kPad - 104.0, c.bottom() - kFooterH + (kFooterH - 30.0) * 0.5, 104.0, 30.0};
        mCancel = Rect{mExport.x - 8.0 - 88.0, mExport.y, 88.0, 30.0};
        drawRoundedRect(t, mCancel, radius::control(), Paint::filled(lerpColor(palette::secondary(), palette::card(), mHover.amount(kCancelId))));
        t.setFill(palette::secondaryForeground());
        t.drawText("Cancel", mCancel.x + (mCancel.w - t.measureText("Cancel", 12.0, font::sans())) * 0.5, textfit::baseline(mCancel.y + mCancel.h * 0.5, 12.0), 12.0,
                   font::sans());
        drawRoundedRect(t, mExport, radius::control(), Paint::filled(palette::primary()));
        const double hv = mHover.amount(kExportId);
        if (hv > 0.001) drawRoundedRect(t, mExport, radius::control(), Paint::filled(palette::hoverWash(hv)));
        t.setFill(palette::primaryForeground());
        const char *lbl = "Export\xE2\x80\xA6";
        t.drawText(lbl, mExport.x + (mExport.w - t.measureText(lbl, 12.0, font::sansMedium())) * 0.5, textfit::baseline(mExport.y + mExport.h * 0.5, 12.0), 12.0,
                   font::sansMedium());
    }
}
}
