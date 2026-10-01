#include "OutputSpec.h"
#include "CommandLine.h"
#include "Glyphs.h"
#include "TextFit.h"
#include "../../../cosmo/widgets/SectionHeader.h"
#include <algorithm>

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
        constexpr double kFieldH = 26.0;
        constexpr double kButtonH = 30.0;
        const char *kFormats[3] = {"h264", "prores", "png-seq"};
        Color fade(Color c, double a) { c.a *= a; return c; }
    }

    OutputSpec::OutputSpec()
    {
        clipToBounds = true;
        mFormat = std::make_shared<cosmo_v2::SegmentedControl>(std::vector<std::string>{"H.264", "ProRes", "PNG seq"});
        mFormat->containerBox = {Paint::filledStroked(palette::segmentedBg(), palette::border(), 1.0), radius::control()};
        mFormat->idleSegBox = {Paint{}, radius::hairline()};
        mFormat->activeSegBox = {Paint::filled(palette::primary()), radius::hairline()};
        mFormat->edgeRadius = radius::control();
        mFormat->idleText = {palette::mutedForeground(), 10.0, font::sans()};
        mFormat->activeText = {palette::white(), 10.0, font::sans()};
        mFormat->padding = 2.0;
        mFormat->onChange = [this](int i) { mFormatIndex = i; refreshDefaultPath(); };
        addChild(mFormat);

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

    std::string OutputSpec::format() const { return kFormats[std::clamp(mFormatIndex, 0, 2)]; }

    std::string OutputSpec::renderLine() const
    {
        return "render --timeline " + cmd::quote(mTimeline) + " --out " + cmd::quote(mPath->text) + " --format " + format();
    }

    std::string OutputSpec::defaultPath() const
    {
        if (mTimeline.empty()) return std::string();
        const std::string dir = mProjectDir.empty() ? std::string("renders") : mProjectDir + "/renders";
        switch (std::clamp(mFormatIndex, 0, 2))
        {
        case 1: return dir + "/" + mTimeline + ".mov";
        case 2: return dir + "/" + mTimeline + "-png/";
        default: return dir + "/" + mTimeline + ".mp4";
        }
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

    double OutputSpec::listTop() const { return cosmo_v2::kSectionHeaderHeight; }
    double OutputSpec::listH() const { return std::min((int)mTimelines.size(), kMaxVisibleRows) * kRowH; }
    double OutputSpec::formatTop() const { return listTop() + listH() + kNoteH; }

    Rect OutputSpec::timelineRowRect(int i) const
    {
        return Rect{0, listTop() + i * kRowH - mScroll.value(), width.value(), kRowH};
    }

    void OutputSpec::layout()
    {
        const double w = width.value();
        double y = formatTop() + cosmo_v2::kSectionHeaderHeight;
        mFormat->x.set(kPadX); mFormat->y.set(y); mFormat->width.set(std::max(0.0, w - 2 * kPadX)); mFormat->height.set(kSegH);
        mFormat->layout();
        y += kSegH + 6.5 + cosmo_v2::kSectionHeaderHeight;
        mPath->x.set(kPadX); mPath->y.set(y); mPath->width.set(std::max(0.0, w - 2 * kPadX)); mPath->height.set(kFieldH);
        y += kFieldH + 19.5;
        mRender->x.set(kPadX); mRender->y.set(y); mRender->width.set(std::max(0.0, w - 2 * kPadX)); mRender->height.set(kButtonH);
    }

    bool OutputSpec::handleGesture(const Gesture &g, const Point &local)
    {
        const Rect list{0, listTop(), width.value(), listH()};
        auto rowAt = [&](const Point &p) {
            if (!list.contains(p)) return -1;
            for (int i = 0; i < (int)mTimelines.size(); ++i) if (timelineRowRect(i).contains(p)) return i;
            return -1;
        };
        switch (g.type)
        {
        case Gesture::Type::Move:
            mHover.setHovered(rowAt(local));
            return true;
        case Gesture::Type::Down:
            return true;
        case Gesture::Type::Click:
        {
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
            if (list.contains(local)) return mScroll.scrollBy(g.delta.y);
            return false;
        default:
            break;
        }
        return Segment::handleGesture(g, local);
    }

    void OutputSpec::advance(double nowMs)
    {
        mScroll.setExtent(listTop(), listH(), (double)mTimelines.size() * kRowH);
        mScroll.advance(nowMs);
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
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
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

        cosmo_v2::drawSectionHeader(t, kPadX, 0.0, w - 2 * kPadX, "TIMELINE");
        const Rect list{0, listTop(), w, listH()};
        t.save();
        t.clipRect(list.x, list.y, list.w, list.h);
        int chosen = -1;
        for (int i = 0; i < (int)mTimelines.size(); ++i)
        {
            const auto &tl = mTimelines[i];
            if (tl.id == mTimeline) chosen = i;
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

        // a chosen version with dangling deltas: renderable, but not silently
        (void)chosen;
        const double na = mNoteAmt.value();
        if (na > 0.001)
        {
            const double cy = listTop() + listH() + kNoteH * 0.5;
            glyph::warn(t, Rect{kPadX, cy - 5.0, 10.0, 10.0}, fade(palette::destructive(), na));
            const std::string s = std::to_string(mNoteCount) + " dangling \xE2\x80\x94 rebase before delivering?";
            t.setFill(fade(palette::destructive(), na));
            t.drawText(textfit::ellipsize(t, s, w - 2 * kPadX - 16.0, 9.5, font::sans()), kPadX + 16.0, textfit::baseline(cy, 9.5), 9.5, font::sans());
        }
        cosmo_v2::drawSectionHeader(t, kPadX, formatTop(), w - 2 * kPadX, "FORMAT");
        cosmo_v2::drawSectionHeader(t, kPadX, formatTop() + cosmo_v2::kSectionHeaderHeight + kSegH + 6.5, w - 2 * kPadX, "OUTPUT");
        if (mTimelines.empty())
        {
            const std::string s = "No versions to render.";
            t.setFill(palette::mutedForeground());
            t.drawText(s, kPadX, listTop() + 14.0, 11.0, font::sans());
        }
    }
}
}
