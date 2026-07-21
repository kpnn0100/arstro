#include "PhoneApp.h"
#include "Theme.h"
#include "TouchIcons.h"
#include "widgets/Icons.h"      // cosmo_v2::icon:: panelLeft/save/upload/download/trash2
#include "base/Parallel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace artboard;

namespace arstro
{
namespace cosmo_touch
{
namespace
{
    namespace fnt = cosmo_v2::font;
    namespace ci = cosmo_v2::icon;

    // Design tokens — verbatim from ref/phone-ui/src/App.tsx (`T`), so colours match exactly.
    const Color BG = Color::rgba(0x14, 0x14, 0x14);
    const Color CARD = Color::rgba(0x1c, 0x1c, 0x1c);
    const Color POP = Color::rgba(0x22, 0x22, 0x22);
    const Color INPUT = Color::rgba(0x25, 0x25, 0x25);
    const Color STAGE = Color::rgba(0x0a, 0x0a, 0x0a);
    const Color FG = Color::rgba(0xdb, 0xdb, 0xdb);
    const Color MUTED = Color::rgba(0x8a, 0x8a, 0x8a);
    const Color ACCENT = Color::rgba(0x4f, 0x7e, 0xf7);
    const Color GREEN = Color::rgba(0x4c, 0xb5, 0x73);
    const Color WHITE = Color::rgba(0xff, 0xff, 0xff);
    const Color BORDER = Color(1, 1, 1, 0.072);
    const Color ACCENT15 = Color(0x4f / 255.0, 0x7e / 255.0, 0xf7 / 255.0, 0.15);
    const Color PRESS = Color(1, 1, 1, 0.08);

    // metrics (design dp)
    constexpr double kStatus = 0.0;      // real device draws its own status bar
    constexpr double kTopBar = 48.0;
    constexpr double kCrumb = 24.0;
    constexpr double kFilm = 72.0;
    constexpr double kToolBar = 56.0;
    constexpr double kAction = 52.0;
    constexpr double kHandle = 20.0;
    constexpr double kHeader = 36.0;
    constexpr double kChipsRow = 44.0;
    constexpr double kRowH = 48.0;
    constexpr double kLabelW = 120.0;
    constexpr double kValueW = 36.0;

    const char *kTabLabels[5] = {"Basic", "Mask", "Curve", "Grade", "Xform"};

    double idc(double v) { return v; }
    double toKelvin(double v) { return 6500.0 + v / 100.0 * 3500.0; }
    double fromKelvin(double k) { return (k - 6500.0) / 3500.0 * 100.0; }
    double toTint(double v) { return v * 1.5; }
    double fromTint(double t) { return t / 1.5; }

    struct SliderDef
    {
        const char *label; double mn, mx, def;
        float EditParams::*field;
        double (*toEng)(double); double (*fromEng)(double);
        bool grad = false; Color gl{}, gr{};
    };
    struct Section { const char *name; std::vector<SliderDef> rows; };

    const std::vector<Section> &basicSections()
    {
        static const std::vector<Section> s = {
            {"TONE", {
                {"Exposure", -5, 5, 0, &EditParams::exposure, idc, idc},
                {"Contrast", -100, 100, 0, &EditParams::contrast, idc, idc},
                {"Highlights", -100, 100, 0, &EditParams::highlights, idc, idc},
                {"Shadows", -100, 100, 0, &EditParams::shadows, idc, idc},
                {"Whites", -100, 100, 0, &EditParams::whites, idc, idc},
                {"Blacks", -100, 100, 0, &EditParams::blacks, idc, idc}}},
            {"COLOUR", {
                {"Temperature", -100, 100, 0, &EditParams::temp, toKelvin, fromKelvin, true,
                 Color::rgba(0x4f, 0xa3, 0xe8), Color::rgba(0xe8, 0xa8, 0x4f)},
                {"Tint", -100, 100, 0, &EditParams::tint, toTint, fromTint, true,
                 Color::rgba(0x4f, 0xb5, 0x73), Color::rgba(0xb5, 0x4f, 0xa8)},
                {"Vibrance", -100, 100, 0, &EditParams::vibrance, idc, idc},
                {"Saturation", -100, 100, 0, &EditParams::saturation, idc, idc}}},
            {"PRESENCE", {
                {"Texture", -100, 100, 0, &EditParams::texture, idc, idc},
                {"Clarity", -100, 100, 0, &EditParams::clarity, idc, idc}}},
            {"EFFECTS", {
                {"Dehaze", -100, 100, 0, &EditParams::dehaze, idc, idc},
                {"Grain Amount", 0, 100, 0, &EditParams::grainAmount, idc, idc},
                {"Grain Size", 0, 100, 25, &EditParams::grainSize, idc, idc}}},
            {"SHARPENING", {
                {"Amount", 0, 150, 0, &EditParams::sharpenAmount, idc, idc},
                {"Radius", 0, 3, 1, &EditParams::sharpenRadius, idc, idc},
                {"Masking", 0, 100, 0, &EditParams::sharpenMasking, idc, idc}}},
            {"NOISE", {
                {"Luminance", 0, 100, 0, &EditParams::nrLuminance, idc, idc},
                {"Colour", 0, 100, 0, &EditParams::nrColor, idc, idc}}},
            {"LENS", {
                {"Distortion", -100, 100, 0, &EditParams::lensDistortion, idc, idc},
                {"Chromatic Ab.", 0, 100, 0, &EditParams::lensCA, idc, idc},
                {"Vignette", -100, 100, 0, &EditParams::lensVignette, idc, idc}}},
        };
        return s;
    }

    // path helpers
    void rrectPath(IRenderTarget &t, double x, double y, double w, double h, double r)
    {
        r = std::min(r, std::min(w, h) * 0.5);
        t.beginPath();
        t.moveTo(x + r, y); t.lineTo(x + w - r, y); t.quadTo(x + w, y, x + w, y + r);
        t.lineTo(x + w, y + h - r); t.quadTo(x + w, y + h, x + w - r, y + h);
        t.lineTo(x + r, y + h); t.quadTo(x, y + h, x, y + h - r);
        t.lineTo(x, y + r); t.quadTo(x, y, x + r, y); t.closePath();
    }
    void rectPath(IRenderTarget &t, double x, double y, double w, double h)
    {
        t.beginPath(); t.moveTo(x, y); t.lineTo(x + w, y); t.lineTo(x + w, y + h); t.lineTo(x, y + h); t.closePath();
    }
    void hline(IRenderTarget &t, double x0, double x1, double y, const Color &c, double sw = 1.0)
    {
        t.setStroke(c, sw); t.beginPath(); t.moveTo(x0, y); t.lineTo(x1, y); t.strokePath();
    }
    // centered text (approx width: sizePx*0.52 per char for DM Sans)
    double textW(const std::string &s, double px) { return s.size() * px * 0.52; }

    // ── ParamSlider: label + touch Slider + signed value readout ─────────────────
    class ParamSlider : public Segment
    {
    public:
        std::function<void(double)> onChangeValue;
        ParamSlider(const std::string &label, double mn, double mx, bool grad, Color gl, Color gr)
            : mLabel(label)
        {
            SliderStyle st = cosmo_v2::sharedTheme().slider;
            st.thumbRadius = 10.0;         // 20px thumb (design)
            mSlider = std::make_shared<Slider>(st);
            mSlider->setRange(mn, mx);
            mSlider->setClickJumps(false);
            mSlider->setSubValueColor(GREEN);
            mSlider->height.set(11.5);     // track = 0.35*h ~ 4px (design)
            if (grad) mSlider->setTrackGradient(gl, gr);
            mSlider->onChange = [this](double v) { if (onChangeValue) onChangeValue(v); };
            addChild(mSlider);
            height.set(kRowH);
        }
        void setValue(double v) { mSlider->setValue(v); }
        void setDefault(double v) { mSlider->setDefault(v); }
        void setSubOffset(double o) { mSlider->setSubValueOffset(o); }
        void layout(double w)
        {
            width.set(w);
            mSlider->x.set(kLabelW + 8);
            mSlider->y.set((kRowH - mSlider->height.value()) * 0.5);
            mSlider->width.set(std::max(10.0, w - kLabelW - 8 - kValueW - 32));
        }
    protected:
        void onPaint(IRenderTarget &t) const override
        {
            t.setFill(MUTED);
            t.drawText(mLabel, 16.0, kRowH * 0.5 + 4.5, 13.0, fnt::sans());
            char buf[16];
            int v = (int)std::lround(mSlider->value());
            std::snprintf(buf, sizeof buf, "%s%d", v > 0 ? "+" : "", v);
            t.setFill(FG);
            std::string s = buf;
            t.drawText(s, width.value() - 16 - textW(s, 13), kRowH * 0.5 + 4.5, 13.0, fnt::mono());
        }
    private:
        std::string mLabel;
        std::shared_ptr<Slider> mSlider;
    };
}  // namespace

// ── Before/Split/After pill (floats over the photo, under the tray) ──────────────
class BeforeAfterPill : public Segment
{
public:
    BeforeAfterPill() { width.set(190); height.set(34); }
protected:
    void onPaint(IRenderTarget &t) const override
    {
        const double w = width.value(), h = height.value();
        t.setFill(Color(0, 0, 0, 0.7));
        rrectPath(t, 0, 0, w, h, h * 0.5); t.fillPath();
        t.setStroke(BORDER, 1.0); rrectPath(t, 0, 0, w, h, h * 0.5); t.strokePath();
        const char *opt[3] = {"Before", "Split", "After"};
        double seg = w / 3.0;
        for (int i = 0; i < 3; ++i)
        {
            bool on = i == mSel;
            if (on) { t.setFill(ACCENT); rrectPath(t, i * seg + 2, 3, seg - 4, h - 6, (h - 6) * 0.5); t.fillPath(); }
            t.setFill(on ? WHITE : FG);
            std::string s = opt[i];
            t.drawText(s, i * seg + (seg - textW(s, 12)) * 0.5, h * 0.5 + 4, 12, fnt::sansMedium());
        }
    }
    bool handleGesture(const Gesture &g, const Point &lp) override
    {
        if (g.type == Gesture::Type::Click) mSel = std::min(2, std::max(0, (int)(lp.x / (width.value() / 3.0))));
        return true;
    }
    int mSel = 2;  // After
};

// ── Tray: grab handle, header, section chips, slider rows, action bar, tool bar ──
class Tray : public Segment
{
public:
    enum class Detent { Rail, Half, Full };
    explicit Tray(cosmo::EditSession &s) : mSession(s) { clipToBounds = true; rebuildRows(); }

    void configure(double w, double h, double now)
    {
        mScreenW = w; mScreenH = h; mNowMs = now;
        x.set(0); width.set(w);
        applyDetent(now); layoutRows();
    }
    void setNow(double n) { mNowMs = n; }
    int tab() const { return mTab; }

    void syncFromSession()
    {
        const EditParams *p = mSession.curParams();
        if (!p) return;
        const EditParams eff = mSession.effectiveEditParams();
        const auto &defs = basicSections()[mSection].rows;
        for (size_t i = 0; i < mRows.size() && i < defs.size(); ++i)
        {
            double own = defs[i].fromEng((*p).*(defs[i].field));
            mRows[i]->setValue(own);
            mRows[i]->setDefault(defs[i].def);
            mRows[i]->setSubOffset(defs[i].fromEng(eff.*(defs[i].field)) - own);
        }
    }

    void advance(double nowMs) override
    {
        Segment::advance(nowMs);
        y.set(mScreenH - height.value());
    }

protected:
    void onPaint(IRenderTarget &t) const override
    {
        const double w = width.value(), h = height.value();
        t.setFill(CARD); rrectPath(t, 0, 0, w, h, 10); t.fillPath();
        // grab handle
        t.setFill(Color(0x8a / 255.0, 0x8a / 255.0, 0x8a / 255.0, 0.4));
        rrectPath(t, w / 2 - 18, 8, 36, 4, 2); t.fillPath();

        const bool expanded = mDetent != Detent::Rail;
        const double tbY = h - kToolBar;
        if (expanded)
        {
            // header: tab title (uppercase) + expand chevron
            std::string title = kTabLabels[mTab]; for (auto &ch : title) ch = (char)toupper(ch);
            t.setFill(FG);
            t.drawText(title, 16, kHandle + kHeader * 0.5 + 4, 13, fnt::sansSemiBold());
            Rect chevBox{w - 40, kHandle + 6, 24, 24};
            icon::chevronUp(t, chevBox, MUTED, 1.6);
            hline(t, 0, w, kHandle + kHeader, BORDER);

            if (mTab == 0)
            {
                // section chips (SegCtrl-style)
                double cx = 12, cy = kHandle + kHeader + 8;
                for (size_t i = 0; i < basicSections().size(); ++i)
                {
                    std::string nm = basicSections()[i].name;
                    double cw = 20 + textW(nm, 11);
                    bool on = (int)i == mSection;
                    t.setFill(on ? ACCENT15 : CARD);
                    rrectPath(t, cx, cy, cw, 28, 2); t.fillPath();
                    t.setStroke(on ? ACCENT : BORDER, 1.0);
                    rrectPath(t, cx, cy, cw, 28, 2); t.strokePath();
                    t.setFill(on ? ACCENT : MUTED);
                    t.drawText(nm, cx + 10, cy + 18, 11, fnt::sansMedium());
                    cx += cw + 6;
                }
                hline(t, 0, w, kHandle + kHeader + kChipsRow, BORDER);
            }
            else
            {
                t.setFill(MUTED);
                std::string s = "Coming soon";
                t.drawText(s, (w - textW(s, 14)) * 0.5, kHandle + kHeader + 60, 14, fnt::sans());
            }

            // action bar (Save / Import / Export) above the tool bar
            const double abY = tbY - kAction;
            hline(t, 0, w, abY, BORDER);
            double pad = 16, gap = 8, bh = 38, by = abY + (kAction - bh) * 0.5;
            double saveW = (w - 2 * pad - 2 * gap) * 0.5;
            double sideW = (w - 2 * pad - 2 * gap) * 0.25;
            t.setFill(ACCENT); rrectPath(t, pad, by, saveW, bh, 2); t.fillPath();
            t.setFill(WHITE); t.drawText("Save", pad + (saveW - textW("Save", 13)) * 0.5, by + bh * 0.5 + 4.5, 13, fnt::sansSemiBold());
            double ix = pad + saveW + gap;
            for (const char *lbl : {"Import", "Export"})
            {
                t.setStroke(BORDER, 1.0); rrectPath(t, ix, by, sideW, bh, 2); t.strokePath();
                t.setFill(FG); std::string s = lbl;
                t.drawText(s, ix + (sideW - textW(s, 13)) * 0.5, by + bh * 0.5 + 4.5, 13, fnt::sans());
                ix += sideW + gap;
            }
        }

        // tool bar (always) — 5 tabs: icon + label, active accent + underline
        hline(t, 0, w, tbY, BORDER);
        double tabW = w / 5.0;
        for (int i = 0; i < 5; ++i)
        {
            bool active = (i == mTab) && expanded;
            double tx = i * tabW;
            Color ic = active ? ACCENT : MUTED;
            if (active) { t.setFill(ACCENT); rrectPath(t, tx + tabW / 2 - 14, tbY, 28, 2, 1); t.fillPath(); }
            Rect ib{tx + tabW / 2 - 10, tbY + 8, 20, 20};
            switch (i)
            {
            case 0: icon::tabBasic(t, ib, ic, 1.6); break;
            case 1: icon::tabMask(t, ib, ic, 1.6); break;
            case 2: icon::tabCurve(t, ib, ic, 1.6); break;
            case 3: icon::tabGrade(t, ib, ic, 1.6); break;
            case 4: icon::tabXform(t, ib, ic, 1.6); break;
            }
            t.setFill(ic);
            std::string s = kTabLabels[i];
            t.drawText(s, tx + (tabW - textW(s, 10)) * 0.5, tbY + 44, 10, active ? fnt::sansSemiBold() : fnt::sans());
        }
    }

    bool handleGesture(const Gesture &g, const Point &lp) override
    {
        if (g.type != Gesture::Type::Click) return true;
        const double w = width.value(), h = height.value();
        const double tbY = h - kToolBar;
        if (lp.y >= tbY) { onTab(std::min(4, std::max(0, (int)(lp.x / (w / 5.0))))); return true; }
        if (lp.y <= kHandle) { cycleDetent(); return true; }
        if (mDetent != Detent::Rail && mTab == 0)
        {
            double cy = kHandle + kHeader + 8;
            if (lp.y >= cy && lp.y <= cy + 28)
            {
                double cx = 12;
                for (size_t i = 0; i < basicSections().size(); ++i)
                {
                    double cw = 20 + textW(basicSections()[i].name, 11);
                    if (lp.x >= cx && lp.x <= cx + cw) { onSection((int)i); break; }
                    cx += cw + 6;
                }
            }
        }
        return true;
    }
    bool hitTestSelf(const Point &p) const override { return localBounds().contains(p); }

private:
    void onTab(int tab)
    {
        if (tab == mTab && mDetent != Detent::Rail) { setDetent(Detent::Rail); return; }
        mTab = tab; setDetent(Detent::Half);
    }
    void onSection(int s) { if (s != mSection) { mSection = s; rebuildRows(); layoutRows(); syncFromSession(); } }
    void cycleDetent()
    {
        setDetent(mDetent == Detent::Full ? Detent::Half : mDetent == Detent::Half ? Detent::Rail : Detent::Half);
    }
    void setDetent(Detent d)
    {
        mDetent = d; applyDetent(mNowMs);
        for (auto &r : mRows) r->visible = (mDetent != Detent::Rail && mTab == 0);
    }
    void applyDetent(double now)
    {
        double target = mDetent == Detent::Rail ? kToolBar + 8 : mDetent == Detent::Half ? mScreenH * 0.46 : mScreenH * 0.9;
        height.animateTo(target, 280.0, Easing::EaseOutCubic, now);
    }
    void rebuildRows()
    {
        clearChildren(); mRows.clear();
        for (const auto &d : basicSections()[mSection].rows)
        {
            auto row = std::make_shared<ParamSlider>(d.label, d.mn, d.mx, d.grad, d.gl, d.gr);
            SliderDef def = d;
            row->onChangeValue = [this, def](double ui) {
                EditParams *p = mSession.curParams(); if (!p) return;
                EditParams np = *p; np.*(def.field) = (float)def.toEng(ui); mSession.applyParams(np);
            };
            row->visible = (mDetent != Detent::Rail && mTab == 0);
            addChild(row); mRows.push_back(row);
        }
    }
    void layoutRows()
    {
        double w = width.value(), y0 = kHandle + kHeader + kChipsRow + 6;
        for (auto &r : mRows) { r->layout(w); r->x.set(0); r->y.set(y0); y0 += kRowH; }
    }

    cosmo::EditSession &mSession;
    Detent mDetent = Detent::Half;
    int mTab = 0, mSection = 0;
    double mScreenW = 393, mScreenH = 852, mNowMs = 0;
    std::vector<std::shared_ptr<ParamSlider>> mRows;
};

// ── EditorScreen: top bar + photo + pill + breadcrumb + filmstrip + tray ─────────
class EditorScreen : public Segment
{
public:
    explicit EditorScreen(cosmo::EditSession &s) : mSession(s)
    {
        mPhoto = std::make_shared<ImageView>();
        mPhoto->setFit(ImageView::Fit::Contain);
        mPhoto->inputTransparent = true;
        addChild(mPhoto);
        mPill = std::make_shared<BeforeAfterPill>();
        addChild(mPill);
        mTray = std::make_shared<Tray>(s);
        addChild(mTray);
    }
    void resize(double w, double h, double now)
    {
        width.set(w); height.set(h);
        double photoY = kTopBar, photoH = h - kTopBar - kCrumb - kFilm;
        mPhoto->x.set(0); mPhoto->y.set(photoY); mPhoto->width.set(w); mPhoto->height.set(photoH);
        mPill->x.set((w - mPill->width.value()) * 0.5);
        mPill->y.set(photoY + photoH - mPill->height.value() - 12);
        mTray->setNow(now); mTray->configure(w, h, now);
    }
    void setPhoto(const uint8_t *rgba, int w, int h) { mPhoto->setImage(rgba, w, h); }
    void syncControls() { mTray->syncFromSession(); }
    void setNow(double n) { mTray->setNow(n); }

protected:
    void onPaint(IRenderTarget &t) const override
    {
        const double w = width.value(), h = height.value();
        // stage backdrop (photo region + breadcrumb + filmstrip base)
        t.setFill(STAGE); rectPath(t, 0, kTopBar, w, h - kTopBar); t.fillPath();

        // breadcrumb strip
        double crumbY = h - kFilm - kCrumb;
        t.setFill(BG); rectPath(t, 0, crumbY, w, kCrumb); t.fillPath();
        hline(t, 0, w, crumbY, BORDER); hline(t, 0, w, crumbY + kCrumb, BORDER);
        drawBreadcrumb(t, crumbY);

        // filmstrip
        double filmY = h - kFilm;
        t.setFill(BG); rectPath(t, 0, filmY, w, kFilm); t.fillPath();
        drawFilmstrip(t, filmY, w);

        // top bar (drawn last of the chrome so it sits above the stage)
        t.setFill(BG); rectPath(t, 0, 0, w, kTopBar); t.fillPath();
        hline(t, 0, w, kTopBar, BORDER);
        double c = kTopBar * 0.5;
        ci::panelLeft(t, Rect{10, c - 10, 20, 20}, FG, 1.5);
        icon::back(t, Rect{54, c - 10, 20, 20}, FG, 1.75);
        // project name (centered)
        std::string name = mName.empty() ? "Untitled" : mName;
        t.setFill(FG);
        t.drawText(name, (w - textW(name, 14)) * 0.5, c + 5, 14, fnt::sansMedium());
        // right cluster: undo (disabled), redo, more
        icon::undo(t, Rect{w - 132, c - 10, 20, 20}, MUTED, 1.75);       // disabled look
        icon::redo(t, Rect{w - 88, c - 10, 20, 20}, FG, 1.75);
        icon::more(t, Rect{w - 44, c - 10, 20, 20}, FG, 1.75);
    }

    bool handleGesture(const Gesture &g, const Point &lp) override
    {
        if (g.type != Gesture::Type::Click) return true;
        const double w = width.value(), h = height.value();
        if (lp.y <= kTopBar)
        {
            if (lp.x < 88) { if (onHome) onHome(); }                       // panel/back -> home
            else if (lp.x >= w - 100 && lp.x < w - 56) { mSession.redo(); syncControls(); }  // redo
            return true;
        }
        // filmstrip cell selection
        double filmY = h - kFilm, cy = filmY + (kFilm - 54) * 0.5;
        if (lp.y >= filmY && lp.y <= filmY + kFilm)
        {
            auto cells = mSession.currentGroupCells();
            double x = 8;
            for (size_t i = 0; i < cells.size(); ++i)
            {
                if (lp.x >= x && lp.x <= x + 72 && lp.y >= cy && lp.y <= cy + 54)
                {
                    if (!cells[i].group && cells[i].slot >= 0) { mSession.selectImage(cells[i].slot); syncControls(); }
                    break;
                }
                x += 72 + 6;
            }
            return true;
        }
        return true;
    }

private:
    void drawBreadcrumb(IRenderTarget &t, double y) const
    {
        std::vector<std::string> path = mSession.breadcrumbPath();
        if (path.empty()) path.push_back(mName.empty() ? "Photo" : mName);
        double x = 12, baseline = y + kCrumb * 0.5 + 4;
        for (size_t i = 0; i < path.size(); ++i)
        {
            bool isLast = (i + 1 == path.size());
            t.setFill(isLast ? FG : MUTED);
            t.drawText(path[i], x, baseline, 11, fnt::sans());
            x += textW(path[i], 11) + 6;
            if (!isLast) { t.setFill(MUTED); t.drawText(">", x, baseline, 10, fnt::sans()); x += 12; }
        }
    }
    // Real filmstrip: one cell per child of the current group; the selected image gets an
    // accent ring; groups render as a dashed folder chip. Thumbnails come from the session
    // (registered once per slot).
    void drawFilmstrip(IRenderTarget &t, double y, double w) const
    {
        (void)w;
        auto cells = mSession.currentGroupCells();
        int curSlot = mSession.currentSlot();
        double x = 8, cw = 72, ch = 54, cy = y + (kFilm - ch) * 0.5;
        for (const auto &c : cells)
        {
            if (c.group)
            {
                t.setStroke(BORDER, 1.5); rrectPath(t, x, cy, cw, ch, 2); t.strokePath();
                icon::folderOpen(t, Rect{x + cw / 2 - 9, cy + 8, 18, 18}, MUTED, 1.4);
                t.setFill(MUTED);
                char b[24]; std::snprintf(b, sizeof b, "%s . %d", c.name.c_str(), c.count);
                std::string s = b;
                t.drawText(s, x + (cw - textW(s, 9)) * 0.5, cy + ch - 7, 9, fnt::sans());
            }
            else
            {
                t.setFill(INPUT); rrectPath(t, x, cy, cw, ch, 2); t.fillPath();
                int id = thumbId(t, c.slot);
                if (id >= 0) { t.save(); rrectPath(t, x, cy, cw, ch, 2); t.clipRect(x, cy, cw, ch); t.drawImage(id, Rect{x, cy, cw, ch}); t.restore(); }
                if (c.slot == curSlot) { t.setStroke(ACCENT, 2.0); rrectPath(t, x, cy, cw, ch, 2); t.strokePath(); }
            }
            x += cw + 6;
        }
    }
    int thumbId(IRenderTarget &t, int slot) const
    {
        auto it = mThumbIds.find(slot);
        if (it != mThumbIds.end()) return it->second;
        const cosmo::EditSession::Thumb *th = mSession.thumbForSlot(slot);
        if (!th || th->w <= 0) return -1;
        int id = t.registerImage(th->rgba.data(), th->w, th->h);
        mThumbIds[slot] = id;
        return id;
    }

    cosmo::EditSession &mSession;
    std::shared_ptr<ImageView> mPhoto;
    std::shared_ptr<BeforeAfterPill> mPill;
    std::shared_ptr<Tray> mTray;
    std::string mName;
    mutable std::map<int, int> mThumbIds;

public:
    std::function<void()> onHome;
    void setName(const std::string &n) { mName = n; }
};

// ── HomeScreen: launcher (wordmark, actions, recents, footer) ────────────────────
class HomeScreen : public Segment
{
public:
    std::function<void()> onOpenProject;
    HomeScreen() {}
    void setProject(const std::string &name, int count, const std::string &sizeStr,
                    const std::vector<uint8_t> &thumb, int tw, int th)
    {
        mName = name; mCount = count; mSize = sizeStr; mThumb = thumb; mTw = tw; mTh = th; mThumbId = -1;
    }
    const std::string &name() const { return mName; }
    int count() const { return mCount; }

protected:
    void onPaint(IRenderTarget &t) const override
    {
        const double w = width.value();
        t.setFill(BG); rectPath(t, 0, 0, w, height.value()); t.fillPath();
        double y = 44;
        // wordmark + tagline
        t.setFill(FG); t.drawText("cosmo", 24, y + 26, 36, fnt::sansSemiBold());
        t.setFill(ACCENT); t.drawText(".", 24 + textW("cosmo", 36) + 2, y + 26, 36, fnt::sansSemiBold());
        t.setFill(MUTED); t.drawText("Dark-room tools for serious work.", 24, y + 52, 14, fnt::sans());
        y += 76;
        // primary actions
        struct Act { const char *label; bool accent; int icon; };
        const Act acts[3] = {{"New Project", true, 0}, {"Open Project", false, 1}, {"Import Catalog", false, 2}};
        for (auto &a : acts)
        {
            t.setFill(a.accent ? ACCENT : BG);
            rrectPath(t, 16, y, w - 32, 52, 2); t.fillPath();
            if (!a.accent) { t.setStroke(BORDER, 1.0); rrectPath(t, 16, y, w - 32, 52, 2); t.strokePath(); }
            Color ic = a.accent ? WHITE : FG;
            Rect ib{32, y + 16, 20, 20};
            if (a.icon == 0) icon::plusCircle(t, ib, ic, 1.75);
            else if (a.icon == 1) icon::folderOpen(t, ib, ic, 1.6);
            else icon::importDown(t, ib, ic, 1.6);
            t.setFill(ic); t.drawText(a.label, 64, y + 32, 15, fnt::sansMedium());
            y += 60;
        }
        y += 12;
        // recents header + count
        t.setFill(FG); t.drawText("Recent Projects", 16, y + 14, 15, fnt::sansSemiBold());
        double cntX = 16 + textW("Recent Projects", 15) + 10;
        char cb[8]; std::snprintf(cb, sizeof cb, "%d", mCount > 0 ? 1 : 0);
        t.setFill(CARD); rrectPath(t, cntX, y, 22, 18, 2); t.fillPath();
        t.setStroke(BORDER, 1.0); rrectPath(t, cntX, y, 22, 18, 2); t.strokePath();
        t.setFill(MUTED); t.drawText(cb, cntX + 8, y + 13, 11, fnt::mono());
        y += 30;
        // search field
        t.setFill(INPUT); rrectPath(t, 16, y, w - 32, 40, 2); t.fillPath();
        t.setStroke(BORDER, 1.0); rrectPath(t, 16, y, w - 32, 40, 2); t.strokePath();
        icon::search(t, Rect{26, y + 10, 18, 18}, MUTED, 1.5);
        t.setFill(MUTED); t.drawText("Search projects", 52, y + 25, 14, fnt::sans());
        y += 52;
        // project card
        mCardY = y;
        if (mCount > 0)
        {
            t.setFill(CARD); rrectPath(t, 16, y, w - 32, 80, 2); t.fillPath();
            t.setStroke(BORDER, 1.0); rrectPath(t, 16, y, w - 32, 80, 2); t.strokePath();
            // thumb
            int id = thumbId(t);
            t.setFill(INPUT); rrectPath(t, 28, y + 12, 80, 56, 1); t.fillPath();
            if (id >= 0) { t.save(); t.clipRect(28, y + 12, 80, 56); t.drawImage(id, Rect{28, y + 12, 80, 56}); t.restore(); }
            t.setFill(FG); t.drawText(mName, 120, y + 30, 14, fnt::sansMedium());
            t.setFill(ACCENT); // edited dot
            double nx = 120 + textW(mName, 14) + 8;
            rrectPath(t, nx, y + 22, 6, 6, 3); t.fillPath();
            char meta[64]; std::snprintf(meta, sizeof meta, "%d photos . %s . Opened recently", mCount, mSize.c_str());
            t.setFill(MUTED); t.drawText(meta, 120, y + 50, 12, fnt::sans());
            y += 88;
        }
        // new-project dashed card
        t.setStroke(BORDER, 1.5); rrectPath(t, 16, y, w - 32, 72, 2); t.strokePath();
        t.setFill(MUTED);
        icon::plusCircle(t, Rect{w / 2 - 60, y + 27, 18, 18}, MUTED, 1.6);
        t.drawText("New Project", w / 2 - 34, y + 41, 14, fnt::sans());
        y += 96;
        // footer links
        for (const char *lbl : {"Settings", "What's New", "Help & Documentation"})
        {
            t.setFill(MUTED); t.drawText(lbl, 24, y + 27, 13, fnt::sans());
            hline(t, 24, w - 24, y + 44, BORDER);
            y += 44;
        }
        t.setFill(Color(0x8a / 255.0, 0x8a / 255.0, 0x8a / 255.0, 0.5));
        t.drawText("cosmo v1.0.0-beta", 24, y + 24, 11, fnt::mono());
    }

    bool handleGesture(const Gesture &g, const Point &lp) override
    {
        if (g.type == Gesture::Type::Click)
        {
            // any primary action or the project card opens the (demo) project
            if (lp.y >= 120 || (mCount > 0 && lp.y >= mCardY && lp.y <= mCardY + 80))
                if (onOpenProject) onOpenProject();
        }
        return true;
    }
    bool hitTestSelf(const Point &p) const override { return localBounds().contains(p); }

private:
    int thumbId(IRenderTarget &t) const
    {
        if (mThumbId >= 0 || mTw <= 0) return mThumbId;
        mThumbId = t.registerImage(mThumb.data(), mTw, mTh);
        return mThumbId;
    }
    std::string mName, mSize;
    int mCount = 0, mTw = 0, mTh = 0;
    std::vector<uint8_t> mThumb;
    mutable int mThumbId = -1;
    mutable double mCardY = 0;
};

// ── LoadingScreen: star-sky + flying wordmark + progress (3-phase transition) ────
class LoadingScreen : public Segment
{
public:
    std::function<void()> onDone;
    void begin(const std::string &name, int count, double now)
    {
        mName = name; mCount = count; mStart = now; mProgress = 0; mFired = false;
    }
    void advance(double now) override
    {
        Segment::advance(now);
        double elapsed = now - mStart;
        mProgress = artboard::reducedMotion() ? 1.0 : std::min(1.0, elapsed / 1100.0);
        mNow = now;
        if (!mFired && mProgress >= 1.0 && elapsed >= 600.0) { mFired = true; if (onDone) onDone(); }
    }

protected:
    void onPaint(IRenderTarget &t) const override
    {
        const double w = width.value(), h = height.value();
        t.setFill(BG); rectPath(t, 0, 0, w, h); t.fillPath();
        // star-sky (deterministic positions; twinkle unless reduced motion)
        bool twinkle = !artboard::reducedMotion();
        for (int i = 0; i < 60; ++i)
        {
            double sx = std::fmod(i * 137.5, w), sy = std::fmod(i * 269.13 + 40, h);
            double r = 0.4 + (i % 3) * 0.45;
            double base = 0.2 + (i % 5) * 0.12;
            double o = twinkle ? base * (0.4 + 0.6 * std::fabs(std::sin(mNow / 700.0 + i))) : base;
            t.setFill(Color(1, 1, 1, o));
            rrectPath(t, sx, sy, r * 2, r * 2, r); t.fillPath();
        }
        double cx = w / 2, cy = h / 2 - 40;
        t.setFill(FG);
        double ww = textW("cosmo", 32);
        t.drawText("cosmo", cx - (ww + 8) / 2, cy, 32, fnt::sansSemiBold());
        t.setFill(ACCENT); t.drawText(".", cx - (ww + 8) / 2 + ww + 2, cy, 32, fnt::sansSemiBold());
        t.setFill(FG); t.drawText(mName, cx - textW(mName, 18) / 2, cy + 40, 18, fnt::sansMedium());
        // progress bar
        double bw = 200, bx = cx - bw / 2, by = cy + 72;
        t.setFill(CARD); rrectPath(t, bx, by, bw, 3, 1.5); t.fillPath();
        t.setFill(ACCENT); rrectPath(t, bx, by, bw * mProgress, 3, 1.5); t.fillPath();
        char b[48]; std::snprintf(b, sizeof b, "Loading %d / %d photos", (int)std::lround(mProgress * mCount), mCount);
        t.setFill(MUTED); std::string s = b; t.drawText(s, cx - textW(s, 12) / 2, cy + 100, 12, fnt::sans());
    }

private:
    std::string mName;
    int mCount = 0;
    double mStart = 0, mProgress = 0, mNow = 0;
    bool mFired = false;
};

// ── PhoneApp ─────────────────────────────────────────────────────────────────────
PhoneApp::PhoneApp(double width, double height) : mW(width), mH(height)
{
    par::setThreads(4);
    mHome = std::make_shared<HomeScreen>();
    mLoading = std::make_shared<LoadingScreen>();
    mEditor = std::make_shared<EditorScreen>(mSession);
    for (Segment *s : {(Segment *)mHome.get(), (Segment *)mLoading.get(), (Segment *)mEditor.get()})
    { s->width.set(width); s->height.set(height); }
    mEditor->resize(width, height, 0.0);

    mHome->onOpenProject = [this] {
        setScreen(Screen::Loading, mNowMs);
        mLoading->begin(mHome->name(), mHome->count(), mNowMs);
    };
    mLoading->onDone = [this] { setScreen(Screen::Editor, mNowMs); };
    mEditor->onHome = [this] { setScreen(Screen::Home, mNowMs); };

    // gestures route to whichever screen is active
    mRecognizer.setSink([this](const Gesture &g) { if (auto *r = activeRoot()) r->onGesture(g); });
}
PhoneApp::~PhoneApp() = default;

artboard::Segment *PhoneApp::activeRoot() const
{
    switch (mScreen)
    {
    case Screen::Home: return mHome.get();
    case Screen::Loading: return mLoading.get();
    default: return mEditor.get();
    }
}

void PhoneApp::setScreen(Screen s, double nowMs)
{
    mScreen = s;
    if (s == Screen::Editor) mEditor->syncControls();
    mFade.set(1.0);
    mFade.animateTo(0.0, 320.0, Easing::EaseOutCubic, nowMs);  // fade each screen in
}

void PhoneApp::setSize(double width, double height)
{
    mW = width; mH = height;
    for (Segment *s : {(Segment *)mHome.get(), (Segment *)mLoading.get(), (Segment *)mEditor.get()})
    { s->width.set(width); s->height.set(height); }
    mEditor->resize(width, height, mNowMs);
}

void PhoneApp::addProjectImage(const uint8_t *rgba, int w, int h, const std::string &name)
{
    if (mImageCount == 0) mSession.openImage(rgba, w, h, name);
    else mSession.openImageInto(mSession.currentGroup(), rgba, w, h, name, "");
    ++mImageCount;
}

void PhoneApp::finishProject(const std::string &projectName)
{
    if (mImageCount == 0) return;
    mSession.selectImage(0);
    mSession.setUseGpu(true);
    mEditor->setName(projectName);
    // home card thumbnail = first image's session thumb
    const cosmo::EditSession::Thumb *th = mSession.thumbForSlot(0);
    std::vector<uint8_t> tb; int tw = 0, thh = 0;
    if (th && th->w > 0) { tb = th->rgba; tw = th->w; thh = th->h; }
    mHome->setProject(projectName, mImageCount, "demo", tb, tw, thh);
    mEditor->syncControls();
}

bool PhoneApp::gpuAvailable() const { return mSession.gpuAvailable(); }

void PhoneApp::poll()
{
    RenderService::Frame f;
    if (mSession.renderService().tryAcquire(f) && f.width > 0)
        mEditor->setPhoto(f.rgba.data(), f.width, f.height);
}

void PhoneApp::render(IRenderTarget &t, double nowMs)
{
    mNowMs = nowMs;
    mEditor->setNow(nowMs);
    mSession.tick(nowMs);
    poll();
    Segment *r = activeRoot();
    r->advance(nowMs);
    r->render(t);
    r->renderOverlay(t);
    // cross-fade scrim (screen-change fade-in)
    double a = mFade.update(nowMs);
    if (a > 0.002)
    {
        t.save();
        t.setTransform(Transform::identity());
        t.setFill(Color(0x14 / 255.0, 0x14 / 255.0, 0x14 / 255.0, a));
        t.beginPath(); t.moveTo(0, 0); t.lineTo(mW, 0); t.lineTo(mW, mH); t.lineTo(0, mH); t.closePath(); t.fillPath();
        t.restore();
    }
}

void PhoneApp::pointer(int kind, double x, double y, int button, double timeMs, bool alt, bool shift, bool ctrl)
{
    (void)shift; (void)ctrl;
    RawPointer rp;
    rp.kind = kind == 0 ? RawPointer::Kind::Down : kind == 2 ? RawPointer::Kind::Up : RawPointer::Kind::Move;
    rp.pos = Point{x, y};
    rp.button = button == 2 ? PointerButton::Right : PointerButton::Left;
    rp.timeMs = timeMs; rp.alt = alt;
    mRecognizer.feed(rp);
}

void PhoneApp::wheel(double x, double y, double delta, bool ctrl) { (void)x; (void)y; (void)delta; (void)ctrl; }
}  // namespace cosmo_touch
}  // namespace arstro
