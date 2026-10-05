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
        const char *kCodecs[7] = {"h264", "h265", "prores", "dnxhr", "png-seq", "dcp", "imf"};
        // R-DLV-4: a DCP's containers, each a frame the picture is fitted inside
        const char *kContainers[4] = {"2k-flat", "2k-scope", "4k-flat", "4k-scope"};
        const char *kContainerLabels[4] = {"2K Flat", "2K Scope", "4K Flat", "4K Scope"};
        const int kContainerW[4] = {1998, 2048, 3996, 4096}, kContainerH[4] = {1080, 858, 2160, 1716};
        const char *kProres[5] = {"proxy", "lt", "standard", "hq", "4444"};
        const char *kDnx[5] = {"lb", "sq", "hq", "hqx", "444"};
        const int kCrf[4] = {28, 23, 18, 12};
        const char *kSpeeds[3] = {"fast", "medium", "slow"};
        // R-COLOR-4: the output transform; 4 and 5 are HDR (10 bits at least)
        const char *kOutputs[6] = {"rec709", "rec709-2.4", "srgb", "p3d65", "pq", "hlg"};
        const char *kOutputLabels[6] = {"Rec.709", "Rec.709 2.4", "sRGB", "P3-D65", "HDR PQ", "HDR HLG"};
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
        mCodec = segmented({"H.264", "H.265", "ProRes", "DNxHR", "PNG", "DCP", "IMF"}, 0);
        mCodec->onChange = [this](int i) {
            // an 8-bit codec cannot carry HDR: the colour goes back to Rec.709 rather than a refused line
            if (mColour && mColour->selected() >= 4 && (i == 0 || i == 4)) mColour->setSelected(0);
            refreshDefaultPath();
        };
        mProres = segmented({"Proxy", "LT", "422", "HQ", "4444"}, 2);
        mDnx = segmented({"LB", "SQ", "HQ", "HQX", "444"}, 2);
        mQuality = segmented({"Draft", "Good", "High", "Master"}, 2);
        mSpeed = segmented({"Fast", "Medium", "Slow"}, 1);
        mDepth = segmented({"8-bit", "10-bit"}, 0);
        mDepth->onChange = [this](int i) { if (i == 0 && mColour && mColour->selected() >= 4) mColour->setSelected(0); };
        mDnx->onChange = [this](int i) { if (i < 3 && mColour && mColour->selected() >= 4) mColour->setSelected(0); };
        mColour = segmented({"709", "2.4", "sRGB", "P3", "PQ", "HLG"}, 0);
        mColour->onChange = [this](int i) {
            if (i < 4) return;
            // HDR needs 10 bits: H.265 10-bit unless the codec already is a 10-bit one
            const std::string f = format();
            if (f == "h264" || f == "png-seq") mCodec->setSelected(1);
            if (format() == "h265") mDepth->setSelected(1);
            if (format() == "dnxhr" && mDnx->selected() < 3) mDnx->setSelected(3);
        };
        mSize = segmented({"Full", "\xC2\xBD", "\xC2\xBC"}, 0);
        mContainer = segmented({kContainerLabels[0], kContainerLabels[1], kContainerLabels[2], kContainerLabels[3]}, 0);
        mContainer->onChange = [this](int) { if (!mContainerSync) mContainerChosen = true; };
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
        // R-DLV-3: a preset in one step, or the controls kept as one
        mPresetBtn = pill("Preset: Custom");
        mPresetBtn->onClick = [this] {
            if (!onPresetMenu) return;
            const Point o = mPresetBtn->worldTransform().apply(Point{0, 0});
            onPresetMenu(Rect{o.x, o.y, mPresetBtn->width.value(), mPresetBtn->height.value()});
        };
        // R-DLV-2: this render's burn-ins, from a menu — the column keeps its height
        mBurnBtn = pill("No burn-ins");
        mBurnBtn->onClick = [this] {
            if (!onBurnMenu) return;
            const Point o = mBurnBtn->worldTransform().apply(Point{0, 0});
            onBurnMenu(Rect{o.x, o.y, mBurnBtn->width.value(), mBurnBtn->height.value()});
        };
        // R-DLV-1: the timeline's captions on this render, from a menu
        mCapBtn = pill("No captions");
        mCapBtn->onClick = [this] {
            if (!onCaptionsMenu) return;
            const Point o = mCapBtn->worldTransform().apply(Point{0, 0});
            onCaptionsMenu(Rect{o.x, o.y, mCapBtn->width.value(), mCapBtn->height.value()});
        };
        mSavePreset = pill("Save Preset\xE2\x80\xA6");
        mSavePreset->onClick = [this] { if (onSavePreset) onSavePreset(specFlags()); };
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

    std::string OutputSpec::format() const { return kCodecs[std::clamp(mCodec->selected(), 0, 6)]; }

    void OutputSpec::outputSizeFor(int &w, int &h) const
    {
        // the service's own arithmetic (a long edge, aspect kept), so the line it gets is one it takes
        w = mProjW;
        h = mProjH;
        if (format() == "dcp" && w > 0 && h > 0)
        {
            // the service's fit: inside the container, never enlarged
            const int k = std::clamp(mContainer->selected(), 0, 3);
            const double s = std::min(1.0, std::min((double)kContainerW[k] / w, (double)kContainerH[k] / h));
            w = std::min(kContainerW[k], (int)std::lround(mProjW * s));
            h = std::min(kContainerH[k], (int)std::lround(mProjH * s));
            return;
        }
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

    std::vector<int> OutputSpec::controlState() const
    {
        return {mCodec->selected(), mProres->selected(), mDnx->selected(), mQuality->selected(), mSpeed->selected(),
                mDepth->selected(), mColour->selected(), mSize->selected(), mRateIndex};
    }

    void OutputSpec::setPreset(const std::string &name)
    {
        mPreset = name;
        mPresetSnap = controlState();
        mPresetBtn->setLabel("Preset: " + (name.empty() ? std::string("Custom") : name));
    }

    std::string OutputSpec::burnFlag() const
    {
        // R-DLV-2: this render's burn-ins (not a preset's: they belong to one render, like its range)
        static const char *items[kBurns] = {"tc@bl", "srctc@br", "clip@tl", "source@tr", ""};
        std::string spec;
        for (int b = 0; b < kBurns; ++b)
        {
            if (!mBurnOn[b]) continue;
            std::string item = b == BurnText ? "text=" + mBurnText + "@tc" : items[b];
            if (b == BurnText && (mBurnText.empty() || mBurnText.find_first_of(",@\"") != std::string::npos)) continue;   // the grammar's own separators
            spec += (spec.empty() ? "" : ",") + item;
        }
        return spec.empty() ? std::string() : " --burnin " + cmd::quote(spec);
    }

    int OutputSpec::timelineCaptions() const
    {
        for (const auto &tl : mTimelines) if (tl.id == mTimeline) return tl.captions;
        return 0;
    }

    std::string OutputSpec::captionsFlag() const
    {
        // R-DLV-1: only when the timeline has captions; a PNG sequence has no track to carry them
        if (timelineCaptions() == 0) return std::string();
        static const char *ways[kCapWays] = {"burn", "track", "sidecar"};
        std::string spec;
        for (int k = 0; k < kCapWays; ++k)
            if (mCapOn[k] && !(k == CapTrack && format() == "png-seq")) spec += (spec.empty() ? "" : ",") + std::string(ways[k]);
        return spec.empty() ? std::string() : " --captions " + spec;
    }

    std::string OutputSpec::specFlags() const
    {
        // the line the controls would write, less what belongs to one render
        const std::string line = renderLine();
        const auto at = line.find(" --format ");
        std::string flags = at == std::string::npos ? std::string() : line.substr(at + 1);
        // a render's own parts are not the spec: its range, its burn-ins and its captions
        for (const char *own : {" --range ", " --burnin ", " --captions "})
        {
            const auto at = flags.find(own);
            if (at != std::string::npos) flags = flags.substr(0, at);
        }
        return flags;
    }

    std::string OutputSpec::renderLine() const
    {
        const std::string f = format();
        if (!mPreset.empty())
        {
            // R-DLV-3: the preset is the spec; the range is this render's own
            std::string line = "render --timeline " + cmd::quote(mTimeline) + " --out " + cmd::quote(mPath->text) + " --preset " + cmd::quote(mPreset) + burnFlag() + captionsFlag();
            if (mRange->selected() == 1)
            {
                const double fps = mProjFps > 0 ? mProjFps : 24.0;
                line += " --range " + cmd::seconds(mIn, fps) + ":" + cmd::seconds(mOut >= 0 ? mOut : mDuration, fps);
            }
            return line;
        }
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
        // R-DLV-4: a DCP's colour is DCI X'Y'Z' and its size its container
        if (f != "dcp" && mColour->selected() != 0) line += std::string(" --output ") + kOutputs[std::clamp(mColour->selected(), 0, 5)];
        if (f == "dcp") line += std::string(" --container ") + kContainers[std::clamp(mContainer->selected(), 0, 3)];
        else if (mSize->selected() != 0)
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
        return line + burnFlag() + captionsFlag();   // R-DLV-2: this render's burn-ins; R-DLV-1: its captions
    }

    std::string OutputSpec::audioSentence() const
    {
        bool sound = false;
        for (const auto &tl : mTimelines) if (tl.id == mTimeline) sound = tl.hasSound;
        const std::string f = format();
        if (!sound) return "No sound on this timeline: the render is picture only";
        if (f == "png-seq") return "A PNG sequence carries no sound: render a video for the mix";
        if (f == "dcp") return "Sound: the master mix on L/R of 5.1, 24-bit PCM 48 kHz";
        if (f == "imf") return "Sound: the master mix, 24-bit PCM 48 kHz stereo";
        return std::string("Sound: the master mix, ") + (f == "prores" || f == "dnxhr" ? "24-bit PCM" : "AAC") + " 48 kHz stereo";
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
        s += " \xC2\xB7 " + std::to_string(frames) + " frames";
        if (format() == "dcp") s += std::string(" \xC2\xB7 in ") + kContainerLabels[std::clamp(mContainer->selected(), 0, 3)] + " \xC2\xB7 X'Y'Z'";
        else if (mColour->selected() != 0) s += std::string(" \xC2\xB7 ") + kOutputLabels[std::clamp(mColour->selected(), 0, 5)];
        return s;
    }

    std::string OutputSpec::defaultPath() const
    {
        if (mTimeline.empty()) return std::string();
        const std::string dir = mProjectDir.empty() ? std::string("renders") : mProjectDir + "/renders";
        const std::string f = format();
        if (f == "png-seq") return dir + "/" + mTimeline + "-png/";
        if (f == "dcp" || f == "imf") return dir + "/" + mTimeline + (f == "dcp" ? "_DCP" : "_IMF");   // a package is a folder
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
        if (!mContainerChosen && mProjW > 0 && mProjH > 0)
        {
            // R-DLV-4: the container the service would choose — scope for 2:1 and wider, 4K when the picture fills it
            const int k = (mProjW >= 3996 || mProjH >= 2160 ? 2 : 0) + ((double)mProjW / mProjH >= 2.0 ? 1 : 0);
            if (mContainer->selected() != k) { mContainerSync = true; mContainer->setSelected(k); mContainerSync = false; }
        }
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
        case Container: case DcpNote: return f == "dcp";
        case ImfNote: return f == "imf";
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
        {
            // R-DLV-3: the preset and Save Preset… ride the FORMAT header's line, right-aligned — the
            // column keeps its height, so nothing below it moves
            mPresetY = y;
            const double ph = std::min(kSegH, cosmo_v2::kSectionHeaderHeight - 4.0);
            const double py = y + (cosmo_v2::kSectionHeaderHeight - ph) * 0.5;
            const double saveW = 92.0, presetW = std::clamp(w - 2 * kPadX - saveW - kGap - 64.0, 90.0, 190.0);
            place(*mSavePreset, w - kPadX - saveW, py, saveW, ph, mPreset.empty() ? 1.0 : 0.45);
            place(*mPresetBtn, w - kPadX - saveW - kGap - presetW, py, presetW, ph, 1.0);
        }
        y += cosmo_v2::kSectionHeaderHeight;
        const double dim = 1.0 - 0.55 * mPresetAmt.value();   // R-DLV-3: a preset decides the spec
        seg(*mCodec, kPadX, y, w - 2 * kPadX, dim);
        y += kSegH + kGap;
        auto optional = [&](int r, cosmo_v2::SegmentedControl *s) {
            const double a = mRows[r].amt.value();
            mRowY[r] = y;
            // the opacity leads the collapse (a²), so a leaving row is nearly gone before the row
            // arriving under it has travelled up to where it was
            if (s) seg(*s, kPadX + kLabelW, y, cw, a * a * dim);
            y += (kSegH + kGap) * a;
        };
        optional(ProresProfile, mProres.get());
        optional(DnxProfile, mDnx.get());
        optional(Quality, mQuality.get());
        optional(Speed, mSpeed.get());
        optional(Depth, mDepth.get());
        for (const int r : {(int)PngNote, (int)DcpNote, (int)ImfNote})
        {
            const double a = mRows[r].amt.value();
            mRowY[r] = y;
            y += (kLineH + kGap) * a;
        }
        const double pa = mRows[Container].amt.value(), pa2 = pa * pa, keep = (1.0 - pa) * (1.0 - pa);
        mColourY = y;                                     // R-COLOR-4: every codec has one (a DCP's is fixed: it cross-fades to words)
        seg(*mColour, kPadX + kLabelW, y, cw, dim * keep);
        y += kSegH + kGap;

        mHdrSizeY = y;
        y += cosmo_v2::kSectionHeaderHeight;
        mSizeY = y;                                       // R-DLV-4: a DCP's container takes the size's place, cross-faded
        seg(*mSize, kPadX + kLabelW, y, cw, dim * keep);
        seg(*mContainer, kPadX + kLabelW, y, cw, dim * pa2);
        y += kSegH + kGap;
        mRateY = y;
        y += kSegH + kGap;

        mHdrRangeY = y;
        {
            // R-DLV-1: the captions ride the RANGE header's line, right-aligned; dimmed while the timeline has none
            int n = 0;
            for (int k = 0; k < kCapWays; ++k) n += mCapOn[k] ? 1 : 0;
            static const char *one[kCapWays] = {"Captions: burn", "Captions: track", "Captions: .srt"};
            int only = 0;
            for (int k = 0; k < kCapWays; ++k) if (mCapOn[k]) only = k;
            mCapBtn->setLabel(timelineCaptions() == 0 ? std::string("No captions") : n == 0 ? std::string("Captions: off")
                              : n == 1 ? std::string(one[only]) : "Captions: " + std::to_string(n) + " ways");
            const double ph = std::min(kSegH, cosmo_v2::kSectionHeaderHeight - 4.0);
            place(*mCapBtn, w - kPadX - 100.0, y + (cosmo_v2::kSectionHeaderHeight - ph) * 0.5, 100.0, ph, 0.45 + 0.55 * mCapAvail.value());
        }
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
        {
            // R-DLV-2: the burn-ins ride the OUTPUT header's line, right-aligned, as the preset rides FORMAT's
            int n = 0;
            for (int b = 0; b < kBurns; ++b) n += burn(b) && (b != BurnText || !mBurnText.empty()) ? 1 : 0;
            mBurnBtn->setLabel(n == 0 ? std::string("No burn-ins") : std::to_string(n) + (n == 1 ? " burn-in" : " burn-ins"));
            const double ph = std::min(kSegH, cosmo_v2::kSectionHeaderHeight - 4.0);
            place(*mBurnBtn, w - kPadX - 92.0, y + (cosmo_v2::kSectionHeaderHeight - ph) * 0.5, 92.0, ph, 1.0);
        }
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
        // R-DLV-3: a control touched while a preset is chosen: the spec is the controls' again; the
        // controls dim while a preset decides (eased — they stay usable: touching one means Custom)
        if (!mPreset.empty() && controlState() != mPresetSnap) setPreset(std::string());
        if (mPreset.empty() == mPresetApplied)
        {
            mPresetAmt.animateTo(mPreset.empty() ? 0.0 : 1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            mPresetApplied = !mPreset.empty();
        }
        mPresetAmt.update(nowMs);
        // R-DLV-1: the captions pill brightens when the chosen timeline has captions
        const bool capAvail = timelineCaptions() > 0;
        if (!mCapAvailInit) { mCapAvail.set(capAvail ? 1.0 : 0.0); mCapAvailApplied = capAvail; mCapAvailInit = true; }
        else if (capAvail != mCapAvailApplied)
        {
            mCapAvail.animateTo(capAvail ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            mCapAvailApplied = capAvail;
        }
        mCapAvail.update(nowMs);
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
        label("Colour", mColourY, 1.0);
        auto note = [&](int r, const char *s) {
            const double a = mRows[r].amt.value();
            if (a <= 0.001) return;
            t.setFill(fade(palette::mutedForeground(), a));
            t.drawText(textfit::ellipsize(t, s, w - 2 * kPadX, 10.0, font::sans()), kPadX, textfit::baseline(mRowY[r] + kLineH * 0.5, 10.0), 10.0, font::sans());
        };
        note(PngNote, "8-bit RGBA, one lossless PNG per frame");
        // R-DLV-4: said before the render, as the render's row says it after
        note(DcpNote, "Not validated here \xE2\x80\x94 check it before a cinema");
        note(ImfNote, "Not validated here \xE2\x80\x94 check it with Photon");
        if (const double pa = mRows[Container].amt.value(); pa > 0.001)
        {
            const double a = pa * pa;
            t.setFill(fade(palette::foreground(), a));
            t.drawText("DCI X'Y'Z' 12-bit", kPadX + kLabelW + 8.0, textfit::baseline(mColourY + kSegH * 0.5, 10.0), 10.0, font::sans());
        }

        cosmo_v2::drawSectionHeader(t, kPadX, mHdrSizeY, w - 2 * kPadX, "SIZE & RATE");
        {
            const double pa = mRows[Container].amt.value();
            label("Size", mSizeY, (1.0 - pa) * (1.0 - pa));
            label("Container", mSizeY, pa * pa);
        }
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
            // the sound this render will carry: said, not discovered (R-AUD-9)
            const double cy = mAudioY + kLineH * 0.5;
            glyph::speaker(t, Rect{kPadX, cy - 5.0, 10.0, 10.0}, fade(palette::mutedForeground(), 0.7), 1.0);
            t.setFill(palette::mutedForeground());
            t.drawText(textfit::ellipsize(t, audioSentence(), w - 2 * kPadX - 16.0, 9.5, font::sans()),
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
