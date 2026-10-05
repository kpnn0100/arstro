#include "OutputSpec.h"
#include "SegmentedStyle.h"
#include "CommandLine.h"
#include "Glyphs.h"
#include "TextFit.h"
#include "../../../cosmo/widgets/SectionHeader.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        constexpr double kPadX = 9.75;
        constexpr double kNoteH = 20.0;
        constexpr double kSegH = 24.0;
        constexpr double kGap = 6.5;          // u(2): between rows
        constexpr double kLabelW = 58.5;      // u(18): the row-label column
        constexpr double kFieldH = 26.0;
        constexpr double kButtonH = 30.0;
        constexpr double kLineH = 19.5;       // u(6): a sentence row
        const char *kCodecs[5] = {"h264", "h265", "prores", "dnxhr", "png-seq"};
        const char *kProres[5] = {"proxy", "lt", "standard", "hq", "4444"};
        const char *kDnx[5] = {"lb", "sq", "hq", "hqx", "444"};
        const int kCrf[4] = {28, 23, 18, 12};
        const char *kSpeeds[3] = {"fast", "medium", "slow"};
        // the rate list: 0 = the project's own; the NTSC rates are exact fractions
        struct Rate { const char *label, *arg; double fps; };
        const Rate kRates[] = {{"Project", "", 0.0},          {"23.976", "24000/1001", 24000.0 / 1001.0},
                               {"24", "24", 24.0},            {"25", "25", 25.0},
                               {"29.97", "30000/1001", 30000.0 / 1001.0}, {"30", "30", 30.0},
                               {"50", "50", 50.0},            {"59.94", "60000/1001", 60000.0 / 1001.0},
                               {"60", "60", 60.0}};
        constexpr int kRateCount = (int)(sizeof kRates / sizeof kRates[0]);
        Color fade(Color c, double a) { c.a *= a; return c; }
        std::string rateText(double fps)
        {
            char b[32];
            std::snprintf(b, sizeof b, "%.3f", fps);
            std::string r = b;
            while (!r.empty() && r.back() == '0') r.pop_back();
            if (!r.empty() && r.back() == '.') r.pop_back();
            return r;
        }
    }

    std::shared_ptr<cosmo_v2::SegmentedControl> OutputSpec::segmented(std::vector<std::string> labels, int selected)
    {
        auto s = std::make_shared<cosmo_v2::SegmentedControl>(std::move(labels));
        styleSegmented(*s);
        s->setSelectedImmediate(selected);
        addChild(s);
        return s;
    }

    OutputSpec::OutputSpec()
    {
        clipToBounds = true;
        mCodec = segmented({"H.264", "H.265", "ProRes", "DNxHR", "PNG"}, 0);
        mCodec->onChange = [this](int) { refreshDefaultPath(); };
        mProres = segmented({"Proxy", "LT", "422", "HQ", "4444"}, 2);
        mDnx = segmented({"LB", "SQ", "HQ", "HQX", "444"}, 2);
        mQuality = segmented({"Draft", "Good", "High", "Master"}, 2);
        mSpeed = segmented({"Fast", "Medium", "Slow"}, 1);
        mDepth = segmented({"8-bit", "10-bit"}, 0);
        mSize = segmented({"Full", "\xC2\xBD", "\xC2\xBC"}, 0);
        mRange = segmented({"Whole", "In \xE2\x80\x93 Out"}, 0);

        auto pill = [this](const char *label) {
            auto b = std::make_shared<cosmo_v2::PillButton>(label);
            b->idleBox = {Paint::filledStroked(palette::secondary(), palette::border(), 1.0), radius::control()};
            b->activeBox = b->idleBox;
            b->idleText = {palette::foreground(), 10.0, font::sans()};
            b->activeText = b->idleText;
            b->hoverEmphasis = palette::white();
            addChild(b);
            return b;
        };
        mSetIn = pill("Set In");
        mSetIn->onClick = [this] {
            mIn = mPlayhead;
            if (mOut >= 0 && mOut <= mIn) mOut = -1.0;   // an out before the in: back to the end
        };
        mSetOut = pill("Set Out");
        mSetOut->onClick = [this] {
            mOut = mPlayhead;
            if (mOut <= mIn) mIn = 0.0;
        };

        TextBoxStyle st;
        st.idle = {Paint::filledStroked(palette::input(), palette::border(), 1.0), radius::control()};
        st.focused = {Paint::filledStroked(palette::input(), palette::ring(), 1.0), radius::control()};
        st.text = {palette::foreground(), 10.0, font::mono()};
        st.placeholder = {palette::mutedForeground(), 10.0, font::mono()};
        st.caretColor = palette::primary();
        st.selectionColor = palette::primaryAlpha(0.38);
        mPath = std::make_shared<TextBox>(st);
        mPath->focusable = true;
        mPath->placeholder = "output path";
        addChild(mPath);

        mRender = std::make_shared<cosmo_v2::PillButton>("Render");
        mRender->idleBox = {Paint::filled(palette::primary()), radius::control()};
        mRender->activeBox = mRender->idleBox;
        mRender->idleText = {palette::primaryForeground(), 12.0, font::sansSemiBold()};
        mRender->activeText = mRender->idleText;
        mRender->hoverEmphasis = palette::white();
        mRender->onClick = [this] {
            if (!onCommand || mTimeline.empty() || mPath->text.empty()) return;
            onCommand(renderLine());
        };
        addChild(mRender);
    }

    std::string OutputSpec::format() const { return kCodecs[std::clamp(mCodec->selected(), 0, 4)]; }

    void OutputSpec::outputSizeFor(int &w, int &h) const
    {
        // the service's own arithmetic (a long edge, aspect kept), so the line it gets is one it takes
        w = mProjW;
        h = mProjH;
        const int div = mSize->selected() == 1 ? 2 : mSize->selected() == 2 ? 4 : 1;
        if (div == 1 || w <= 0 || h <= 0) return;
        const int longEdge = std::max(w, h), edge = longEdge / div;
        const double s = (double)edge / longEdge;
        w = std::max(1, (int)std::lround(mProjW * s));
        h = std::max(1, (int)std::lround(mProjH * s));
    }

    double OutputSpec::outputFps() const { return mRateIndex == 0 ? mProjFps : kRates[mRateIndex].fps; }
    std::string OutputSpec::fpsArg() const { return mRateIndex == 0 ? std::string() : kRates[mRateIndex].arg; }
    std::string OutputSpec::rateLabel() const
    {
        return mRateIndex == 0 ? "Project \xC2\xB7 " + rateText(mProjFps) + " fps" : std::string(kRates[mRateIndex].label) + " fps";
    }

    std::string OutputSpec::renderLine() const
    {
        const std::string f = format();
        std::string line = "render --timeline " + cmd::quote(mTimeline) + " --out " + cmd::quote(mPath->text) + " --format " + f;
        // a default is left out — the service's defaults are the same, and a plain render stays plain
        if (f == "prores" && mProres->selected() != 2) line += std::string(" --profile ") + kProres[mProres->selected()];
        if (f == "dnxhr" && mDnx->selected() != 2) line += std::string(" --profile ") + kDnx[mDnx->selected()];
        if (f == "h264" || f == "h265")
        {
            if (mQuality->selected() != 2) line += " --quality " + std::to_string(kCrf[std::clamp(mQuality->selected(), 0, 3)]);
            if (mSpeed->selected() != 1) line += std::string(" --speed ") + kSpeeds[std::clamp(mSpeed->selected(), 0, 2)];
        }
        if (f == "h265" && mDepth->selected() == 1) line += " --bits 10";
        if (mSize->selected() != 0)
        {
            int w = 0, h = 0;
            outputSizeFor(w, h);
            line += " --res " + std::to_string(w) + "x" + std::to_string(h);
        }
        if (mRateIndex != 0) line += " --fps " + fpsArg();
        if (mRange->selected() == 1)
        {
            const double fps = mProjFps > 0 ? mProjFps : 24.0;
            const double out = mOut >= 0 ? mOut : mDuration;
            line += " --range " + cmd::seconds(mIn, fps) + ":" + cmd::seconds(out, fps);
        }
        return line;
    }

    std::string OutputSpec::summary() const
    {
        int w = 0, h = 0;
        outputSizeFor(w, h);
        const double fps = outputFps();
        const double a = mRange->selected() == 1 ? mIn : 0.0, b = mRange->selected() == 1 && mOut >= 0 ? mOut : mDuration;
        const long long frames = std::max<long long>(0, std::llround(b * fps) - std::llround(a * fps));
        std::string s = std::to_string(w) + "\xC3\x97" + std::to_string(h) + " \xC2\xB7 " + rateText(fps) + " fps \xC2\xB7 ";
        s += mRange->selected() == 1 ? cmd::timecode(a, mProjFps) + "\xE2\x80\x93" + cmd::timecode(b, mProjFps) : std::string("whole timeline");
        return s + " \xC2\xB7 " + std::to_string(frames) + " frames";
    }

    std::string OutputSpec::defaultPath() const
    {
        if (mTimeline.empty()) return std::string();
        const std::string dir = mProjectDir.empty() ? std::string("renders") : mProjectDir + "/renders";
        const std::string f = format();
        if (f == "png-seq") return dir + "/" + mTimeline + "-png/";
        if (f == "prores" || f == "dnxhr") return dir + "/" + mTimeline + (f == "dnxhr" ? "-dnxhr" : "") + ".mov";
        return dir + "/" + mTimeline + (f == "h265" ? "-hevc" : "") + ".mp4";
    }

    void OutputSpec::refreshDefaultPath()
    {
        // follow the default until the user has typed their own path
        const std::string d = defaultPath();
        if (mPath->text.empty() || mPath->text == mLastDefault)
        {
            mPath->text = d;
            mPath->caretToEnd();
        }
        mLastDefault = d;
    }

    void OutputSpec::bind(const interstellar::AppModel &m)
    {
        mTimelines = m.timelines;
        mCurrent = m.currentTimeline;
        mProjW = m.width;
        mProjH = m.height;
        mProjFps = m.fps > 0 ? m.fps : 24.0;
        mDuration = m.duration;
        mPlayhead = m.playhead;
        const auto slash = m.projectPath.find_last_of('/');
        mProjectDir = slash == std::string::npos ? std::string() : m.projectPath.substr(0, slash);
        bool still = false;
        for (const auto &tl : mTimelines) if (tl.id == mTimeline) still = true;
        if (!still) mTimeline = mCurrent;   // default to the version being edited — never tied to it
        int idx = -1;
        for (int i = 0; i < (int)mTimelines.size(); ++i) if (mTimelines[i].id == mTimeline) idx = i;
        mSel.setHovered(idx);
        refreshDefaultPath();
    }

    double OutputSpec::listTop() const { return mListY; }
    double OutputSpec::listH() const { return std::min((int)mTimelines.size(), kMaxVisibleRows) * kRowH; }

    Rect OutputSpec::timelineRowRect(int i) const
    {
        return Rect{0, listTop() + i * kRowH - mScroll.value(), width.value(), kRowH};
    }

    Rect OutputSpec::rateStepRect(int dir) const
    {
        const double x0 = kPadX + kLabelW, x1 = width.value() - kPadX;
        return dir < 0 ? Rect{x0, mRateY, 26.0, kSegH} : Rect{x1 - 26.0, mRateY, 26.0, kSegH};
    }

    bool OutputSpec::rowWanted(int r) const
    {
        const std::string f = format();
        switch (r)
        {
        case ProresProfile: return f == "prores";
        case DnxProfile: return f == "dnxhr";
        case Quality: case Speed: return f == "h264" || f == "h265";
        case Depth: return f == "h265";
        case PngNote: return f == "png-seq";
        case InOut: return mRange->selected() == 1;
        default: return false;
        }
    }

    void OutputSpec::layout()
    {
        // One top-to-bottom pass from the LIVE row amounts and the live column scroll, so a codec
        // change re-lays every row below it through the same eased values (layout contract).
        const double w = width.value();
        const double cw = std::max(0.0, w - kPadX - kPadX - kLabelW);
        double y = -mColScroll.value();
        auto place = [&](Segment &s, double x, double yy, double ww, double hh, double alpha) {
            s.x.set(x); s.y.set(yy); s.width.set(std::max(0.0, ww)); s.height.set(hh);
            s.opacity.set(alpha);
            s.visible = alpha > 0.001 && yy + hh > 0.0 && yy < height.value();   // culled: no input when hidden or scrolled out
        };
        auto seg = [&](cosmo_v2::SegmentedControl &s, double x, double yy, double ww, double alpha) {
            place(s, x, yy, ww, kSegH, alpha);
            s.layout();
        };
        y += cosmo_v2::kSectionHeaderHeight;              // TIMELINE
        mListY = y;
        y += listH();
        mNoteY = y;
        y += kNoteH;

        mHdrFormatY = y;
        y += cosmo_v2::kSectionHeaderHeight;
        seg(*mCodec, kPadX, y, w - 2 * kPadX, 1.0);
        y += kSegH + kGap;
        auto optional = [&](int r, cosmo_v2::SegmentedControl *s) {
            const double a = mRows[r].amt.value();
            mRowY[r] = y;
            // the opacity leads the collapse (a²), so a leaving row is nearly gone before the row
            // arriving under it has travelled up to where it was
            if (s) seg(*s, kPadX + kLabelW, y, cw, a * a);
            y += (kSegH + kGap) * a;
        };
        optional(ProresProfile, mProres.get());
        optional(DnxProfile, mDnx.get());
        optional(Quality, mQuality.get());
        optional(Speed, mSpeed.get());
        optional(Depth, mDepth.get());
        {
            const double a = mRows[PngNote].amt.value();
            mRowY[PngNote] = y;
            y += (kLineH + kGap) * a;
        }

        mHdrSizeY = y;
        y += cosmo_v2::kSectionHeaderHeight;
        mSizeY = y;
        seg(*mSize, kPadX + kLabelW, y, cw, 1.0);
        y += kSegH + kGap;
        mRateY = y;
        y += kSegH + kGap;

        mHdrRangeY = y;
        y += cosmo_v2::kSectionHeaderHeight;
        mRangeY = y;
        seg(*mRange, kPadX + kLabelW, y, cw, 1.0);
        y += kSegH + kGap;
        {
            const double a = mRows[InOut].amt.value();
            mRowY[InOut] = y;
            // each mark carries its own time, so the row needs no readout beside it
            const double bw = std::max(0.0, (cw - kGap) * 0.5);
            mSetIn->setLabel("In  " + cmd::timecode(mIn, mProjFps));
            mSetOut->setLabel("Out  " + cmd::timecode(mOut >= 0 ? mOut : mDuration, mProjFps));
            place(*mSetIn, kPadX + kLabelW, y, bw, kSegH, a);
            place(*mSetOut, kPadX + kLabelW + bw + kGap, y, bw, kSegH, a);
            y += (kSegH + kGap) * a;
        }
        mAudioY = y;
        y += kLineH + kGap;

        mHdrOutputY = y;
        y += cosmo_v2::kSectionHeaderHeight;
        place(*mPath, kPadX, y, w - 2 * kPadX, kFieldH, 1.0);
        y += kFieldH + kGap * 2;
        place(*mRender, kPadX, y, w - 2 * kPadX, kButtonH, 1.0);
        y += kButtonH + kGap;
        mSummaryY = y;
        y += 2 * kLineH * 0.75 + kGap;   // two lines: what, then which span
        mContentH = y + mColScroll.value();
    }

    bool OutputSpec::handleGesture(const Gesture &g, const Point &local)
    {
        const Rect list{0, listTop(), width.value(), listH()};
        auto rowAt = [&](const Point &p) {
            if (!list.contains(p)) return -1;
            for (int i = 0; i < (int)mTimelines.size(); ++i) if (timelineRowRect(i).contains(p)) return i;
            return -1;
        };
        auto stepAt = [&](const Point &p) { return rateStepRect(-1).contains(p) ? -1 : rateStepRect(1).contains(p) ? 1 : 0; };
        switch (g.type)
        {
        case Gesture::Type::Move:
        {
            mHover.setHovered(rowAt(local));
            const int sd = stepAt(local);
            mStepHover.setHovered(sd < 0 ? 0 : sd > 0 ? 1 : -1);
            return true;
        }
        case Gesture::Type::Down:
            return true;
        case Gesture::Type::Click:
        {
            if (const int sd = stepAt(local))
            {
                mRateIndex = std::clamp(mRateIndex + sd, 0, kRateCount - 1);   // presentation: the line carries it
                return true;
            }
            const int i = rowAt(local);
            if (i >= 0 && mTimelines[i].id != mTimeline)
            {
                mTimeline = mTimelines[i].id;   // presentation state: which version to render
                mSel.setHovered(i);
                refreshDefaultPath();
            }
            return true;
        }
        case Gesture::Type::Scroll:
            if (list.contains(local) && mScroll.scrollBy(g.delta.y)) return true;   // the list first,
            return mColScroll.scrollBy(g.delta.y);                                    // then the column
        default:
            break;
        }
        return Segment::handleGesture(g, local);
    }

    void OutputSpec::advance(double nowMs)
    {
        mScroll.setExtent(listTop(), listH(), (double)mTimelines.size() * kRowH);
        mScroll.advance(nowMs);
        mColScroll.setExtent(0.0, height.value(), mContentH);
        mColScroll.advance(nowMs);
        for (int r = 0; r < kRows; ++r)
        {
            Row &row = mRows[r];
            const bool want = rowWanted(r);
            if (!row.init) { row.amt.set(want ? 1.0 : 0.0); row.applied = want; row.init = true; }
            else if (want != row.applied)
            {
                row.amt.animateTo(want ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
                row.applied = want;
            }
            row.amt.update(nowMs);
        }
        int dangling = 0;
        for (const auto &tl : mTimelines) if (tl.id == mTimeline) dangling = tl.danglingDeltas;
        mNoteWanted = dangling > 0;
        if (dangling > 0) mNoteCount = dangling;   // keep the last count while the note fades out
        if (!mNoteInit) { mNoteAmt.set(mNoteWanted ? 1.0 : 0.0); mNoteApplied = mNoteWanted; mNoteInit = true; }
        if (mNoteWanted != mNoteApplied)
        {
            mNoteAmt.animateTo(mNoteWanted ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            mNoteApplied = mNoteWanted;
        }
        mNoteAmt.update(nowMs);
        if (!isHovered()) { mHover.clear(); mStepHover.clear(); }
        mHover.advance(nowMs);
        mStepHover.advance(nowMs);
        mSel.advance(nowMs, motion::kSelectMs);
        mRender->enabled = !mTimeline.empty() && !mPath->text.empty();
        layout();
        Segment::advance(nowMs);
    }

    void OutputSpec::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(palette::card()));
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0.5, 0); t.lineTo(0.5, h); t.strokePath();

        cosmo_v2::drawSectionHeader(t, kPadX, listTop() - cosmo_v2::kSectionHeaderHeight, w - 2 * kPadX, "TIMELINE");
        const Rect list{0, listTop(), w, listH()};
        t.save();
        t.clipRect(list.x, std::max(0.0, list.y), list.w, std::max(0.0, list.bottom() - std::max(0.0, list.y)));
        for (int i = 0; i < (int)mTimelines.size(); ++i)
        {
            const auto &tl = mTimelines[i];
            if (!mScroll.bandVisible(i * kRowH, kRowH)) continue;
            const Rect r = timelineRowRect(i);
            const double sel = mSel.amount(i), hv = mHover.amount(i);
            if (sel > 0.001) drawRoundedRect(t, Rect{r.x + 4, r.y + 1, r.w - 8, r.h - 2}, radius::control(), Paint::filled(palette::primaryAlpha(0.14 * sel)));
            if (hv > 0.001) drawRoundedRect(t, Rect{r.x + 4, r.y + 1, r.w - 8, r.h - 2}, radius::control(), Paint::filled(palette::hoverWash(hv)));
            const double cy = r.y + r.h * 0.5;
            // radio: ring, with an accent dot eased by the selection amount
            const double rx = kPadX + 6.0 + tl.depth * space::indent();
            drawCircle(t, rx, cy, 5.0, Paint::stroked(lerpColor(palette::mutedForeground(), palette::primary(), sel), 1.0));
            if (sel > 0.001) drawCircle(t, rx, cy, 2.5 * sel, Paint::filled(palette::primary()));
            double x = rx + 12.0;
            if (tl.colourPinned || tl.cutFrozen) { glyph::lock(t, Rect{x, cy - 5.0, 8.0, 10.0}, palette::mutedForeground()); x += 12.0; }
            std::string tag = tl.id == mCurrent ? "editing" : std::string();
            const double tagW = tag.empty() ? 0.0 : t.measureText(tag, 9.0, font::sans()) + 8.0;
            t.setFill(palette::foreground());
            t.drawText(textfit::ellipsize(t, tl.name.empty() ? tl.id : tl.name, w - kPadX - tagW - x, 11.0, font::sans()), x, textfit::baseline(cy, 11.0), 11.0, font::sans());
            if (!tag.empty())
            {
                t.setFill(palette::mutedForeground());
                t.drawText(tag, w - kPadX - tagW + 8.0, textfit::baseline(cy, 9.0), 9.0, font::sans());
            }
        }
        t.restore();
        mScroll.drawBar(t, w - 2.0);
        if (mTimelines.empty())
        {
            t.setFill(palette::mutedForeground());
            t.drawText("No versions to render.", kPadX, listTop() + 14.0, 11.0, font::sans());
        }

        // a chosen version with dangling deltas: renderable, but not silently
        const double na = mNoteAmt.value();
        if (na > 0.001)
        {
            const double cy = mNoteY + kNoteH * 0.5;
            glyph::warn(t, Rect{kPadX, cy - 5.0, 10.0, 10.0}, fade(palette::destructive(), na));
            const std::string s = std::to_string(mNoteCount) + " dangling \xE2\x80\x94 rebase before delivering?";
            t.setFill(fade(palette::destructive(), na));
            t.drawText(textfit::ellipsize(t, s, w - 2 * kPadX - 16.0, 9.5, font::sans()), kPadX + 16.0, textfit::baseline(cy, 9.5), 9.5, font::sans());
        }

        // the row labels, faded with their rows
        auto label = [&](const char *s, double y, double a) {
            if (a <= 0.001) return;
            t.setFill(fade(palette::mutedForeground(), a));
            t.drawText(s, kPadX, textfit::baseline(y + kSegH * 0.5, 10.0), 10.0, font::sans());
        };
        cosmo_v2::drawSectionHeader(t, kPadX, mHdrFormatY, w - 2 * kPadX, "FORMAT");
        auto sq = [](double a) { return a * a; };
        label("Profile", mRowY[ProresProfile], sq(mRows[ProresProfile].amt.value()));
        label("Profile", mRowY[DnxProfile], sq(mRows[DnxProfile].amt.value()));
        label("Quality", mRowY[Quality], sq(mRows[Quality].amt.value()));
        label("Speed", mRowY[Speed], sq(mRows[Speed].amt.value()));
        label("Depth", mRowY[Depth], sq(mRows[Depth].amt.value()));
        if (const double a = mRows[PngNote].amt.value(); a > 0.001)
        {
            t.setFill(fade(palette::mutedForeground(), a));
            t.drawText(textfit::ellipsize(t, "8-bit RGBA, one lossless PNG per frame", w - 2 * kPadX, 10.0, font::sans()),
                       kPadX, textfit::baseline(mRowY[PngNote] + kLineH * 0.5, 10.0), 10.0, font::sans());
        }

        cosmo_v2::drawSectionHeader(t, kPadX, mHdrSizeY, w - 2 * kPadX, "SIZE & RATE");
        label("Size", mSizeY, 1.0);
        label("Rate", mRateY, 1.0);
        {
            // the rate stepper: ‹ value › on the segmented surface
            const Rect r{kPadX + kLabelW, mRateY, std::max(0.0, w - 2 * kPadX - kLabelW), kSegH};
            drawRoundedRect(t, r, radius::control(), Paint::filledStroked(palette::segmentedBg(), palette::border(), 1.0));
            for (int k = 0; k < 2; ++k)
            {
                const Rect b = rateStepRect(k == 0 ? -1 : 1);
                const bool live = k == 0 ? mRateIndex > 0 : mRateIndex < kRateCount - 1;
                const double hv = mStepHover.amount(k);
                if (hv > 0.001 && live) drawRoundedRect(t, Rect{b.x + 2, b.y + 2, b.w - 4, b.h - 4}, radius::hairline(), Paint::filled(palette::hoverWash(hv)));
                const Color gc = live ? lerpColor(palette::mutedForeground(), palette::foreground(), hv) : fade(palette::mutedForeground(), 0.35);
                const Rect gr{b.x + 7.0, b.y + 6.0, 12.0, 12.0};
                if (k == 0) glyph::chevronLeft(t, gr, gc); else glyph::chevronRightSmall(t, gr, gc);
            }
            const std::string v = rateLabel();
            const double tw = t.measureText(v, 10.0, font::mono());
            t.setFill(palette::foreground());
            t.drawText(v, r.x + (r.w - tw) * 0.5, textfit::baseline(r.y + r.h * 0.5, 10.0), 10.0, font::mono());
        }

        cosmo_v2::drawSectionHeader(t, kPadX, mHdrRangeY, w - 2 * kPadX, "RANGE");
        label("Range", mRangeY, 1.0);
        if (mRows[InOut].amt.value() > 0.001) label("Marks", mRowY[InOut], mRows[InOut].amt.value());
        {
            // audio: said, not discovered (R-AUD-5)
            const double cy = mAudioY + kLineH * 0.5;
            glyph::speaker(t, Rect{kPadX, cy - 5.0, 10.0, 10.0}, fade(palette::mutedForeground(), 0.7), 1.0);
            t.setFill(palette::mutedForeground());
            t.drawText(textfit::ellipsize(t, "No audio: v1 places audio, it does not mix it", w - 2 * kPadX - 16.0, 9.5, font::sans()),
                       kPadX + 16.0, textfit::baseline(cy, 9.5), 9.5, font::sans());
        }

        cosmo_v2::drawSectionHeader(t, kPadX, mHdrOutputY, w - 2 * kPadX, "OUTPUT");
        {
            // what will be written, in two lines: the frame and count, then the span
            const std::string all = summary();
            const auto cut = all.rfind(" \xC2\xB7 ");   // "… · N frames" moves to line one
            std::string l1 = all, l2;
            const auto rate = all.find(" fps \xC2\xB7 ");
            if (rate != std::string::npos && cut != std::string::npos && cut > rate)
            {
                l1 = all.substr(0, rate + 4) + all.substr(cut);
                l2 = all.substr(rate + 4 + std::string(" \xC2\xB7 ").size(), cut - (rate + 4 + std::string(" \xC2\xB7 ").size()));
            }
            t.setFill(palette::mutedForeground());
            t.drawText(textfit::ellipsize(t, l1, w - 2 * kPadX, 9.5, font::mono()), kPadX, textfit::baseline(mSummaryY + kLineH * 0.375, 9.5), 9.5, font::mono());
            if (!l2.empty())
                t.drawText(textfit::ellipsize(t, l2, w - 2 * kPadX, 9.5, font::mono()), kPadX, textfit::baseline(mSummaryY + kLineH * 1.125, 9.5), 9.5, font::mono());
        }
        mColScroll.drawBar(t, w - 2.0);
    }
}
}
