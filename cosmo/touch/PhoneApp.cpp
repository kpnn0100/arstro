#include "PhoneApp.h"
#include "Theme.h"
#include "TouchIcons.h"
#include "widgets/Icons.h"
#include "base/Parallel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
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

    // Design tokens — verbatim from ref/phone-ui/src/App.tsx (`T`).
    const Color BG = Color::rgba(0x14, 0x14, 0x14);
    const Color CARD = Color::rgba(0x1c, 0x1c, 0x1c);
    const Color POP = Color::rgba(0x22, 0x22, 0x22);
    const Color INPUT = Color::rgba(0x25, 0x25, 0x25);
    const Color STAGE = Color::rgba(0x0a, 0x0a, 0x0a);
    const Color FG = Color::rgba(0xdb, 0xdb, 0xdb);
    const Color MUTED = Color::rgba(0x8a, 0x8a, 0x8a);
    const Color ACCENT = Color::rgba(0x4f, 0x7e, 0xf7);
    const Color GREEN = Color::rgba(0x4c, 0xb5, 0x73);
    const Color DESTRUCT = Color::rgba(0xe5, 0x48, 0x4d);
    const Color WHITE = Color::rgba(0xff, 0xff, 0xff);
    const Color BORDER = Color(1, 1, 1, 0.072);
    const Color ACCENT15 = Color(0x4f / 255.0, 0x7e / 255.0, 0xf7 / 255.0, 0.15);
    const Color CHR = Color::rgba(0xe6, 0x52, 0x52), CHG = Color::rgba(0x61, 0xcc, 0x6b), CHB = Color::rgba(0x6b, 0x94, 0xf5);

    constexpr double kTopBar = 48.0, kCrumb = 24.0, kFilm = 72.0, kToolBar = 56.0, kAction = 52.0;
    constexpr double kHandle = 20.0, kHeader = 36.0, kRowH = 48.0, kLabelW = 120.0, kValueW = 40.0, kHist = 60.0;

    const char *kTabLabels[5] = {"Basic", "Mask", "Curve", "Grade", "Xform"};

    // path + text helpers (measureText-based alignment)
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
    { t.beginPath(); t.moveTo(x, y); t.lineTo(x + w, y); t.lineTo(x + w, y + h); t.lineTo(x, y + h); t.closePath(); }
    void hline(IRenderTarget &t, double x0, double x1, double y, const Color &c, double sw = 1.0)
    { t.setStroke(c, sw); t.beginPath(); t.moveTo(x0, y); t.lineTo(x1, y); t.strokePath(); }
    void txt(IRenderTarget &t, const std::string &s, double x, double y, double px, const char *f, const Color &c)
    { t.setFill(c); t.drawText(s, x, y, px, f); }
    void txtC(IRenderTarget &t, const std::string &s, double cx, double y, double px, const char *f, const Color &c)
    { txt(t, s, cx - t.measureText(s, px, f) * 0.5, y, px, f, c); }
    void txtR(IRenderTarget &t, const std::string &s, double rx, double y, double px, const char *f, const Color &c)
    { txt(t, s, rx - t.measureText(s, px, f), y, px, f, c); }

    double idc(double v) { return v; }
    double toKelvin(double v) { return 6500.0 + v / 100.0 * 3500.0; }
    double fromKelvin(double k) { return (k - 6500.0) / 3500.0 * 100.0; }
    double toTint(double v) { return v * 1.5; }
    double fromTint(double t) { return t / 1.5; }

    struct SliderDef { const char *label; double mn, mx, def; float EditParams::*field;
                       double (*toEng)(double); double (*fromEng)(double); bool grad = false; Color gl{}, gr{}; };
    struct Section { const char *name; std::vector<SliderDef> rows; };

    const std::vector<Section> &basicSections()
    {
        static const std::vector<Section> s = {
            {"TONE", {{"Exposure", -5, 5, 0, &EditParams::exposure, idc, idc}, {"Contrast", -100, 100, 0, &EditParams::contrast, idc, idc},
                      {"Highlights", -100, 100, 0, &EditParams::highlights, idc, idc}, {"Shadows", -100, 100, 0, &EditParams::shadows, idc, idc},
                      {"Whites", -100, 100, 0, &EditParams::whites, idc, idc}, {"Blacks", -100, 100, 0, &EditParams::blacks, idc, idc}}},
            {"COLOUR", {{"Temperature", -100, 100, 0, &EditParams::temp, toKelvin, fromKelvin, true, Color::rgba(0x4f, 0xa3, 0xe8), Color::rgba(0xe8, 0xa8, 0x4f)},
                        {"Tint", -100, 100, 0, &EditParams::tint, toTint, fromTint, true, Color::rgba(0x4f, 0xb5, 0x73), Color::rgba(0xb5, 0x4f, 0xa8)},
                        {"Vibrance", -100, 100, 0, &EditParams::vibrance, idc, idc}, {"Saturation", -100, 100, 0, &EditParams::saturation, idc, idc}}},
            {"PRESENCE", {{"Texture", -100, 100, 0, &EditParams::texture, idc, idc}, {"Clarity", -100, 100, 0, &EditParams::clarity, idc, idc}}},
            {"EFFECTS", {{"Dehaze", -100, 100, 0, &EditParams::dehaze, idc, idc}, {"Grain Amount", 0, 100, 0, &EditParams::grainAmount, idc, idc},
                         {"Grain Size", 0, 100, 25, &EditParams::grainSize, idc, idc}}},
            {"SHARPENING", {{"Amount", 0, 150, 0, &EditParams::sharpenAmount, idc, idc}, {"Radius", 0, 3, 1, &EditParams::sharpenRadius, idc, idc},
                            {"Masking", 0, 100, 0, &EditParams::sharpenMasking, idc, idc}}},
            {"NOISE", {{"Luminance", 0, 100, 0, &EditParams::nrLuminance, idc, idc}, {"Colour", 0, 100, 0, &EditParams::nrColor, idc, idc}}},
            {"LENS", {{"Distortion", -100, 100, 0, &EditParams::lensDistortion, idc, idc}, {"Chromatic Ab.", 0, 100, 0, &EditParams::lensCA, idc, idc},
                      {"Vignette", -100, 100, 0, &EditParams::lensVignette, idc, idc}}},
        };
        return s;
    }

    // ── ParamSlider: label + touch Slider + signed value (measureText-aligned) ────
    class ParamSlider : public Segment
    {
    public:
        std::function<void(double)> onChangeValue;
        ParamSlider(const std::string &label, double mn, double mx, bool grad, Color gl, Color gr) : mLabel(label)
        {
            SliderStyle st = cosmo_v2::sharedTheme().slider; st.thumbRadius = 10.0;
            mSlider = std::make_shared<Slider>(st);
            mSlider->setRange(mn, mx); mSlider->setClickJumps(false); mSlider->setSubValueColor(GREEN);
            mSlider->height.set(11.5);
            if (grad) mSlider->setTrackGradient(gl, gr);
            mSlider->onChange = [this](double v) { if (onChangeValue) onChangeValue(v); };
            addChild(mSlider); height.set(kRowH);
        }
        void setValue(double v) { mSlider->setValue(v); }
        void setDefault(double v) { mSlider->setDefault(v); }
        void setSubOffset(double o) { mSlider->setSubValueOffset(o); }
        void layout(double w)
        {
            width.set(w);
            mSlider->x.set(kLabelW + 8); mSlider->y.set((kRowH - mSlider->height.value()) * 0.5);
            mSlider->width.set(std::max(10.0, w - kLabelW - 8 - kValueW - 32));
        }
    protected:
        void onPaint(IRenderTarget &t) const override
        {
            txt(t, mLabel, 16.0, kRowH * 0.5 + 4.5, 13.0, fnt::sans(), MUTED);
            char b[16]; int v = (int)std::lround(mSlider->value());
            std::snprintf(b, sizeof b, "%s%d", v > 0 ? "+" : "", v);
            txtR(t, b, width.value() - 16, kRowH * 0.5 + 4.5, 13.0, fnt::mono(), FG);
        }
    private:
        std::string mLabel;
        std::shared_ptr<Slider> mSlider;
    };

    // ── CurveEditor: interactive bezier/corner plot wired to a CurvePoint vector ──
    class CurveEditor : public Segment
    {
    public:
        std::function<void(const std::vector<CurvePoint> &)> onChange;
        Color line = ACCENT;
        bool cyclic = false;   // mixer hue curve wraps
        bool hueStrip = false; // draw the 48-swatch hue strip (mixer)
        void setPoints(const std::vector<CurvePoint> &p) { mPts = p; }
        void setRef(const std::vector<CurvePoint> &p, bool has) { mRef = p; mHasRef = has; }
    protected:
        void onPaint(IRenderTarget &t) const override
        {
            const double w = width.value(), h = height.value();
            double plotH = hueStrip ? h - 18 : h, plotY = hueStrip ? 18 : 0;
            if (hueStrip)
            {
                for (int i = 0; i < 48; ++i)  // hue strip
                { double hh = i / 48.0 * 360.0; t.setFill(hsv(hh, 0.8, 0.9)); rectPath(t, i * w / 48.0, 0, w / 48.0 + 1, 16); t.fillPath(); }
            }
            t.setFill(Color::rgba(0x0d, 0x0d, 0x0d)); rrectPath(t, 0, plotY, w, plotH, 2); t.fillPath();
            t.setStroke(BORDER, 1.0); rrectPath(t, 0, plotY, w, plotH, 2); t.strokePath();
            for (double q : {0.25, 0.5, 0.75})
            { hline(t, 0, w, plotY + q * plotH, BORDER); t.setStroke(BORDER, 1.0); t.beginPath(); t.moveTo(q * w, plotY); t.lineTo(q * w, plotY + plotH); t.strokePath(); }
            if (!cyclic) { t.setStroke(BORDER, 1.0); t.beginPath(); t.moveTo(0, plotY + plotH); t.lineTo(w, plotY); t.strokePath(); }
            else hline(t, 0, w, plotY + plotH * 0.5, BORDER);
            if (mHasRef) drawCurve(t, mRef, Color(GREEN.r, GREEN.g, GREEN.b, 0.5), plotY, plotH, 1.5);
            drawCurve(t, mPts, line, plotY, plotH, 2.0);
            for (size_t i = 0; i < mPts.size(); ++i)
            {
                double sx = mPts[i].x * w, sy = plotY + (1 - mPts[i].y) * plotH;
                double r = ((int)i == mDrag) ? 10 : 8;   // touch-sized nodes
                t.setFill(STAGE); rrectPath(t, sx - r, sy - r, 2 * r, 2 * r, r); t.fillPath();
                t.setStroke(line, 2.5); rrectPath(t, sx - r, sy - r, 2 * r, 2 * r, r); t.strokePath();
            }
        }
        // Touch-friendly editing: generous grab radius, drag clamps a node BETWEEN its
        // neighbours (no mid-drag re-sort, so the grabbed index stays valid), tap on empty
        // space adds a node (only when clearly away from existing ones), double-tap a node
        // removes it (endpoints excepted).
        bool handleGesture(const Gesture &g, const Point &lp) override
        {
            const double w = width.value(), h = height.value();
            double plotH = hueStrip ? h - 18 : h, plotY = hueStrip ? 18 : 0;
            auto toNorm = [&](const Point &p) { return Point{std::clamp(p.x / w, 0.0, 1.0), std::clamp(1 - (p.y - plotY) / plotH, 0.0, 1.0)}; };
            auto nearest = [&](const Point &p) { int best = -1; double bd = kGrab; for (size_t i = 0; i < mPts.size(); ++i) { double sx = mPts[i].x * w, sy = plotY + (1 - mPts[i].y) * plotH; double d = std::hypot(sx - p.x, sy - p.y); if (d < bd) { bd = d; best = (int)i; } } return best; };
            if (g.type == Gesture::Type::Down) { mDrag = nearest(lp); return true; }
            if ((g.type == Gesture::Type::Drag || g.type == Gesture::Type::DragStart) && mDrag >= 0)
            {
                Point n = toNorm(lp);
                bool end = (mDrag == 0 || mDrag == (int)mPts.size() - 1);
                if (!end)
                {
                    double lo = mPts[mDrag - 1].x + 0.01, hi = mPts[mDrag + 1].x - 0.01;
                    mPts[mDrag].x = (float)std::clamp(n.x, lo, hi);
                }
                mPts[mDrag].y = (float)n.y;
                if (onChange) onChange(mPts);
                return true;
            }
            if (g.type == Gesture::Type::DoubleClick)
            {
                int hit = nearest(lp);
                if (hit > 0 && hit < (int)mPts.size() - 1) { mPts.erase(mPts.begin() + hit); mDrag = -1; if (onChange) onChange(mPts); }
                return true;
            }
            if (g.type == Gesture::Type::Click && mDrag < 0)   // empty tap -> add a node
            {
                Point n = toNorm(lp); CurvePoint p; p.x = (float)std::clamp(n.x, 0.02, 0.98); p.y = (float)n.y;
                mPts.push_back(p);
                std::sort(mPts.begin(), mPts.end(), [](const CurvePoint &a, const CurvePoint &b) { return a.x < b.x; });
                if (onChange) onChange(mPts);
                return true;
            }
            return true;
        }
        bool hitTestSelf(const Point &p) const override { return localBounds().contains(p); }
        static constexpr double kGrab = 32.0;   // finger-friendly node grab radius
    private:
        static Color hsv(double hh, double s, double v)
        {
            double c = v * s, x = c * (1 - std::fabs(std::fmod(hh / 60.0, 2) - 1)), m = v - c, r = 0, g = 0, b = 0;
            if (hh < 60) { r = c; g = x; } else if (hh < 120) { r = x; g = c; } else if (hh < 180) { g = c; b = x; }
            else if (hh < 240) { g = x; b = c; } else if (hh < 300) { r = x; b = c; } else { r = c; b = x; }
            return Color(r + m, g + m, b + m, 1);
        }
        void drawCurve(IRenderTarget &t, const std::vector<CurvePoint> &pts, const Color &c, double plotY, double plotH, double sw) const
        {
            if (pts.size() < 2) return;
            const double w = width.value();
            t.setStroke(c, sw); t.beginPath();
            t.moveTo(pts[0].x * w, plotY + (1 - pts[0].y) * plotH);
            for (size_t i = 1; i < pts.size(); ++i) t.lineTo(pts[i].x * w, plotY + (1 - pts[i].y) * plotH);
            t.strokePath();
        }
        std::vector<CurvePoint> mPts{CurvePoint{0, 0}, CurvePoint{1, 1}}, mRef;
        bool mHasRef = false;
        int mDrag = -1;
    };
}  // namespace

// ── BeforeAfterPill ──────────────────────────────────────────────────────────────
class BeforeAfterPill : public Segment
{
public:
    BeforeAfterPill() { width.set(190); height.set(34); }
protected:
    void onPaint(IRenderTarget &t) const override
    {
        const double w = width.value(), h = height.value();
        t.setFill(Color(0, 0, 0, 0.7)); rrectPath(t, 0, 0, w, h, h * 0.5); t.fillPath();
        t.setStroke(BORDER, 1.0); rrectPath(t, 0, 0, w, h, h * 0.5); t.strokePath();
        const char *opt[3] = {"Before", "Split", "After"};
        double seg = w / 3.0;
        for (int i = 0; i < 3; ++i)
        {
            bool on = i == mSel;
            if (on) { t.setFill(ACCENT); rrectPath(t, i * seg + 2, 3, seg - 4, h - 6, (h - 6) * 0.5); t.fillPath(); }
            txtC(t, opt[i], i * seg + seg * 0.5, h * 0.5 + 4, 12, fnt::sansMedium(), on ? WHITE : FG);
        }
    }
    bool handleGesture(const Gesture &g, const Point &lp) override
    { if (g.type == Gesture::Type::Click) mSel = std::min(2, std::max(0, (int)(lp.x / (width.value() / 3.0)))); return true; }
    int mSel = 2;
};

// ── Tray: header, per-tab body (Basic/Mask/Curve/Grade/Xform), action + tool bar ─
class Tray : public Segment
{
public:
    enum class Detent { Rail, Half, Full };
    explicit Tray(cosmo::EditSession &s) : mSession(s) { clipToBounds = true; rebuildBody(); }
    void configure(double w, double h, double now) { mScreenW = w; mScreenH = h; mNowMs = now; x.set(0); width.set(w); applyDetent(now); layoutBody(); }
    void setNow(double n) { mNowMs = n; }
    void setHist(const HistogramData &h) { mHist = h; mHasHist = true; }

    void syncFromSession()
    {
        const EditParams *p = mSession.curParams(); if (!p) return;
        const EditParams eff = mSession.effectiveEditParams();
        for (auto &r : mRows) { double own = r.get(*p); r.w->setValue(own); r.w->setDefault(r.def); r.w->setSubOffset(r.get(eff) - own); }
        if (mCurve) syncCurve();
    }
    void advance(double nowMs) override { Segment::advance(nowMs); y.set(mScreenH - height.value()); }

protected:
    void onPaint(IRenderTarget &t) const override
    {
        mZones.clear();
        const double w = width.value(), h = height.value();
        t.setFill(CARD); rrectPath(t, 0, 0, w, h, 10); t.fillPath();
        t.setFill(Color(0x8a / 255.0, 0x8a / 255.0, 0x8a / 255.0, 0.4)); rrectPath(t, w / 2 - 18, 8, 36, 4, 2); t.fillPath();

        const bool expanded = mDetent != Detent::Rail;
        const double tbY = h - kToolBar, abY = tbY - kAction;
        if (expanded)
        {
            std::string title = kTabLabels[mTab]; for (auto &c : title) c = (char)toupper(c);
            txt(t, title, 16, kHandle + kHeader * 0.5 + 4, 13, fnt::sansSemiBold(), FG);
            icon::chevronUp(t, Rect{w - 40, kHandle + 6, 24, 24}, MUTED, 1.6);
            hline(t, 0, w, kHandle + kHeader, BORDER);
            drawHistogram(t, 12, kHandle + kHeader + 6, w - 24, kHist - 12);   // DR-EDIT-3
            hline(t, 0, w, bodyTop(), BORDER);
            switch (mTab)
            {
            case 0: paintBasic(t, w); break;
            case 1: paintMask(t, w); break;
            case 2: paintCurve(t, w); break;
            case 3: paintGrade(t, w); break;
            case 4: paintXform(t, w); break;
            }
            // action bar
            hline(t, 0, w, abY, BORDER);
            double pad = 16, gap = 8, bh = 38, by = abY + (kAction - bh) * 0.5;
            double saveW = (w - 2 * pad - 2 * gap) * 0.5, sideW = (w - 2 * pad - 2 * gap) * 0.25;
            t.setFill(ACCENT); rrectPath(t, pad, by, saveW, bh, 2); t.fillPath();
            txtC(t, "Save", pad + saveW * 0.5, by + bh * 0.5 + 4.5, 13, fnt::sansSemiBold(), WHITE);
            zoneRect(pad, by, saveW, bh, 960);
            double ix = pad + saveW + gap; int zid = 961;
            for (const char *lbl : {"Import", "Export"})
            { t.setStroke(BORDER, 1.0); rrectPath(t, ix, by, sideW, bh, 2); t.strokePath(); txtC(t, lbl, ix + sideW * 0.5, by + bh * 0.5 + 4.5, 13, fnt::sans(), FG); zoneRect(ix, by, sideW, bh, zid++); ix += sideW + gap; }
        }
        // tool bar
        hline(t, 0, w, tbY, BORDER);
        double tabW = w / 5.0;
        for (int i = 0; i < 5; ++i)
        {
            bool active = (i == mTab) && expanded; double tx = i * tabW; Color icn = active ? ACCENT : MUTED;
            if (active) { t.setFill(ACCENT); rrectPath(t, tx + tabW / 2 - 14, tbY, 28, 2, 1); t.fillPath(); }
            Rect ib{tx + tabW / 2 - 10, tbY + 8, 20, 20};
            switch (i) { case 0: icon::tabBasic(t, ib, icn, 1.6); break; case 1: icon::tabMask(t, ib, icn, 1.6); break;
                         case 2: icon::tabCurve(t, ib, icn, 1.6); break; case 3: icon::tabGrade(t, ib, icn, 1.6); break; default: icon::tabXform(t, ib, icn, 1.6); }
            txtC(t, kTabLabels[i], tx + tabW * 0.5, tbY + 44, 10, active ? fnt::sansSemiBold() : fnt::sans(), icn);
        }
    }

    bool handleGesture(const Gesture &g, const Point &lp) override
    {
        if (g.type != Gesture::Type::Click) return true;
        const double w = width.value(), h = height.value(), tbY = h - kToolBar;
        if (lp.y >= tbY) { onTab(std::min(4, std::max(0, (int)(lp.x / (w / 5.0))))); return true; }
        if (lp.y <= kHandle) { cycleDetent(); return true; }
        for (auto it = mZones.rbegin(); it != mZones.rend(); ++it)
            if (it->first.contains(lp)) { onZone(it->second); return true; }
        return true;
    }
    bool hitTestSelf(const Point &p) const override { return localBounds().contains(p); }

private:
    struct Row { std::shared_ptr<ParamSlider> w; std::function<double(const EditParams &)> get; std::function<void(EditParams &, double)> set; double def; };

    void zoneRect(double x, double y, double w, double h, int id) const { mZones.push_back({Rect{x, y, w, h}, id}); }

    // segmented picker: equal segments, returns nothing; registers zones base+i
    void seg(IRenderTarget &t, double x, double y, double w, double h, const std::vector<const char *> &labels, int sel, int base, const std::vector<Color> *cols = nullptr) const
    {
        t.setFill(INPUT); rrectPath(t, x, y, w, h, 2); t.fillPath();
        t.setStroke(BORDER, 1.0); rrectPath(t, x, y, w, h, 2); t.strokePath();
        double sw = w / labels.size();
        for (size_t i = 0; i < labels.size(); ++i)
        {
            bool on = (int)i == sel; Color ac = cols ? (*cols)[i] : ACCENT;
            if (on) { t.setFill(ac); rrectPath(t, x + i * sw + 1, y + 1, sw - 2, h - 2, 2); t.fillPath(); }
            txtC(t, labels[i], x + i * sw + sw * 0.5, y + h * 0.5 + 4, 11, fnt::sansMedium(), on ? WHITE : MUTED);
            zoneRect(x + i * sw, y, sw, h, base + (int)i);
        }
    }
    // variable-width chips
    void chips(IRenderTarget &t, double x, double y, const std::vector<std::string> &labels, int sel, int base) const
    {
        double cx = x;
        for (size_t i = 0; i < labels.size(); ++i)
        {
            double cw = 20 + t.measureText(labels[i], 11, fnt::sansMedium()); bool on = (int)i == sel;
            t.setFill(on ? ACCENT15 : CARD); rrectPath(t, cx, y, cw, 28, 2); t.fillPath();
            t.setStroke(on ? ACCENT : BORDER, 1.0); rrectPath(t, cx, y, cw, 28, 2); t.strokePath();
            txt(t, labels[i], cx + 10, y + 18, 11, fnt::sansMedium(), on ? ACCENT : MUTED);
            zoneRect(cx, y, cw, 28, base + (int)i); cx += cw + 6;
        }
    }
    void toggle(IRenderTarget &t, double x, double y, bool on, int id) const
    {
        t.setFill(on ? ACCENT : INPUT); rrectPath(t, x, y, 44, 26, 13); t.fillPath();
        t.setFill(WHITE); rrectPath(t, x + (on ? 21 : 3), y + 3, 20, 20, 10); t.fillPath();
        zoneRect(x, y, 44, 26, id);
    }

    double bodyTop() const { return kHandle + kHeader + kHist; }

    void drawHistogram(IRenderTarget &t, double x, double y, double w, double h) const
    {
        t.setFill(Color::rgba(0x0f, 0x0f, 0x0f)); rrectPath(t, x, y, w, h, 2); t.fillPath();
        if (!mHasHist) return;
        double maxc = std::log(1.0 + std::max<uint32_t>(1, mHist.maxCount));
        auto plot = [&](const std::array<uint32_t, 256> &ch, Color c, bool fill) {
            t.beginPath();
            if (fill) t.moveTo(x, y + h);
            for (int i = 0; i < 256; ++i)
            { double v = std::log(1.0 + ch[i]) / maxc, px = x + i / 255.0 * w, py = y + h - v * h;
              if (i == 0 && !fill) t.moveTo(px, py); else t.lineTo(px, py); }
            if (fill) { t.lineTo(x + w, y + h); t.closePath(); t.setFill(Color(c.r, c.g, c.b, 0.45)); t.fillPath(); }
            else { t.setStroke(c, 1.2); t.strokePath(); }
        };
        plot(mHist.r, CHR, true); plot(mHist.g, CHG, true); plot(mHist.b, CHB, true); plot(mHist.lum, WHITE, false);
    }

    void paintBasic(IRenderTarget &t, double w) const
    {
        chips(t, 12, bodyTop() + 8, sectionNames(), mSection, 100);
        hline(t, 0, w, bodyTop() + 44, BORDER);
    }
    void paintMask(IRenderTarget &t, double w) const
    {
        txt(t, "ADD MASK", 16, bodyTop() + 20, 11, fnt::sansMedium(), MUTED);
        double cw = (w - 32 - 16) / 3.0;
        const char *tp[3] = {"Radial", "Linear", "Brush"};
        for (int i = 0; i < 3; ++i)
        { double cx = 16 + i * (cw + 8); t.setStroke(BORDER, 1.0); rrectPath(t, cx, bodyTop() + 28, cw, 34, 2); t.strokePath(); txtC(t, tp[i], cx + cw * 0.5, bodyTop() + 50, 13, fnt::sans(), FG); zoneRect(cx, bodyTop() + 28, cw, 34, 700 + i); }
        auto *p = mSession.curParams();
        if (!p || p->masks.empty()) { txtC(t, "Tap a button above to add a mask", w * 0.5, bodyTop() + 96, 13, fnt::sans(), MUTED); return; }
        // mask list
        double ly = bodyTop() + 74;
        for (size_t i = 0; i < p->masks.size(); ++i)
        {
            bool on = (int)i == mMaskSel; const char *tn[3] = {"Radial", "Linear", "Brush"};
            char b[24]; std::snprintf(b, sizeof b, "%s %d", tn[p->masks[i].type], (int)i + 1);
            if (on) { t.setFill(ACCENT15); rrectPath(t, 12, ly, w - 24, 36, 2); t.fillPath(); }
            txt(t, b, 20, ly + 23, 13, fnt::sans(), on ? ACCENT : FG);
            zoneRect(12, ly, w - 24, 36, 800 + (int)i); ly += 40;
        }
        if (mMaskSel >= 0 && mMaskSel < (int)p->masks.size())
        {
            toggle(t, 16, ly + 4, p->masks[mMaskSel].inverted, 901);
            txt(t, "Invert", 68, ly + 22, 13, fnt::sans(), FG);
        }
    }
    void paintCurve(IRenderTarget &t, double w) const
    {
        seg(t, 16, bodyTop() + 8, w - 32, 30, {"Curve", "Mixer"}, mCurveMode, 200);
        if (mCurveMode == 0)
        {
            std::vector<Color> cc = {ACCENT, CHR, CHG, CHB};
            seg(t, 16, bodyTop() + 46, w - 32, 30, {"RGB", "R", "G", "B"}, mCurveCh, 300, &cc);
        }
        else seg(t, 16, bodyTop() + 46, w - 32, 30, {"Hue", "Sat", "Lum"}, mMixerCh, 320);
        txtR(t, "Reset", w - 16, bodyTop() + 92, 11, fnt::sans(), MUTED); zoneRect(w - 60, bodyTop() + 80, 44, 20, 950);
    }
    void paintGrade(IRenderTarget &t, double w) const
    {
        seg(t, 16, bodyTop() + 8, w - 32, 30, {"Shadows", "Midtones", "Highlights"}, mGradeRegion, 400);
        auto *p = mSession.curParams();
        toggle(t, w - 60, mRemapToggleY, p && p->remapEnable, 900);
        txt(t, "Hue Range Remap", 16, mRemapToggleY + 17, 13, fnt::sans(), FG);
    }
    void paintXform(IRenderTarget &t, double w) const
    {
        auto *p = mSession.curParams(); double rot = p ? p->rotation : 0;
        char b[16]; std::snprintf(b, sizeof b, "%.1f", rot); txtR(t, std::string(b) + " deg", w - 16, bodyTop() + 22, 13, fnt::mono(), FG);
        // buttons row (below rotation slider)
        double by = mXBtnY; const char *bl[3] = {"-90", "+90", "Reset"};
        for (int i = 0; i < 3; ++i)
        { double bx = 16 + i * 84; t.setStroke(BORDER, 1.0); rrectPath(t, bx, by, 76, 32, 2); t.strokePath(); txtC(t, bl[i], bx + 38, by + 21, 13, fnt::sans(), i == 2 ? MUTED : FG); zoneRect(bx, by, 76, 32, 600 + i); }
        txt(t, "ASPECT", 16, by + 56, 11, fnt::sansMedium(), MUTED);
        const char *asp[6] = {"Free", "1:1", "4:3", "16:9", "3:2", "5:4"};
        double ax = 16, ay = by + 66;
        for (int i = 0; i < 6; ++i)
        { double aw = 20 + t.measureText(asp[i], 13, fnt::sans()); if (ax + aw > w - 16) { ax = 16; ay += 40; } bool on = i == mAspect;
          t.setFill(on ? ACCENT15 : CARD); rrectPath(t, ax, ay, aw, 32, 2); t.fillPath(); t.setStroke(on ? ACCENT : BORDER, 1.0); rrectPath(t, ax, ay, aw, 32, 2); t.strokePath();
          txtC(t, asp[i], ax + aw * 0.5, ay + 21, 13, fnt::sans(), on ? ACCENT : FG); zoneRect(ax, ay, aw, 32, 500 + i); ax += aw + 6; }
    }

    std::vector<std::string> sectionNames() const { std::vector<std::string> v; for (auto &s : basicSections()) v.push_back(s.name); return v; }

    void onTab(int tab) { if (tab == mTab && mDetent != Detent::Rail) { setDetent(Detent::Rail); return; } mTab = tab; setDetent(Detent::Half); rebuildBody(); layoutBody(); syncFromSession(); }
    void onZone(int id)
    {
        auto *p = mSession.curParams();
        if (id >= 100 && id < 120) { if (id - 100 != mSection) { mSection = id - 100; rebuildBody(); layoutBody(); syncFromSession(); } }
        else if (id == 200 || id == 201) { mCurveMode = id - 200; rebuildBody(); layoutBody(); syncFromSession(); }
        else if (id >= 300 && id < 304) { mCurveCh = id - 300; syncCurve(); }
        else if (id >= 320 && id < 323) { mMixerCh = id - 320; syncCurve(); }
        else if (id >= 400 && id < 403) { mGradeRegion = id - 400; rebuildBody(); layoutBody(); syncFromSession(); }
        else if (id == 900 && p) { EditParams np = *p; np.remapEnable = !np.remapEnable; mSession.applyParams(np); rebuildBody(); layoutBody(); syncFromSession(); }
        else if (id == 901 && p && mMaskSel >= 0 && mMaskSel < (int)p->masks.size()) { EditParams np = *p; np.masks[mMaskSel].inverted = !np.masks[mMaskSel].inverted; mSession.applyParams(np); }
        else if (id >= 700 && id < 703 && p) { EditParams np = *p; MaskParams m; m.type = id - 700; np.masks.push_back(m); mSession.applyParams(np); mMaskSel = (int)np.masks.size() - 1; rebuildBody(); layoutBody(); syncFromSession(); }
        else if (id >= 800 && id < 900) { mMaskSel = id - 800; rebuildBody(); layoutBody(); syncFromSession(); }
        else if (id >= 600 && id < 603 && p) { EditParams np = *p; if (id == 600) np.quarterTurns = (np.quarterTurns + 3) % 4; else if (id == 601) np.quarterTurns = (np.quarterTurns + 1) % 4; else { np.rotation = 0; np.quarterTurns = 0; } mSession.applyParams(np); }
        else if (id >= 500 && id < 506 && p) { mAspect = id - 500; EditParams np = *p; setAspect(np, mAspect); mSession.applyParams(np); }
        else if (id == 950 && p) { EditParams np = *p; resetCurve(np); mSession.applyParams(np); syncCurve(); }
        // action bar 960..962: Save/Import/Export — no-op stand-ins (SAF wiring later)
    }
    static void setAspect(EditParams &p, int a)
    {
        const double ar[6] = {0, 1.0, 4.0 / 3, 16.0 / 9, 3.0 / 2, 5.0 / 4};
        if (a == 0) { p.cropX = 0; p.cropY = 0; p.cropW = 1; p.cropH = 1; return; }
        // centered crop of ~0.9 with the requested aspect (approx; framed-normalised)
        double cw = 0.9, ch = 0.9; if (ar[a] >= 1) ch = cw / ar[a]; else cw = ch * ar[a];
        p.cropW = (float)cw; p.cropH = (float)ch; p.cropX = (float)((1 - cw) * 0.5); p.cropY = (float)((1 - ch) * 0.5);
    }
    void resetCurve(EditParams &p)
    {
        std::vector<CurvePoint> id{CurvePoint{0, 0}, CurvePoint{1, 1}};
        if (mCurveMode == 1) { if (mMixerCh >= 0) p.mixer[mMixerCh].clear(); }
        else if (mCurveCh == 0) p.curve = id; else p.curveChannel[mCurveCh - 1] = id;
    }
    void syncCurve()
    {
        if (!mCurve) return;
        auto *p = mSession.curParams(); if (!p) return;
        std::vector<CurvePoint> pts;
        if (mCurveMode == 1) { pts = p->mixer[mMixerCh]; if (pts.empty()) pts = {CurvePoint{0, 0.5f}, CurvePoint{1, 0.5f}}; mCurve->cyclic = true; mCurve->hueStrip = (mMixerCh == 0); mCurve->line = ACCENT; }
        else { pts = (mCurveCh == 0) ? p->curve : p->curveChannel[mCurveCh - 1]; mCurve->cyclic = false; mCurve->hueStrip = false; mCurve->line = (mCurveCh == 0 ? ACCENT : mCurveCh == 1 ? CHR : mCurveCh == 2 ? CHG : CHB); }
        mCurve->setPoints(pts);
    }

    void rebuildBody()
    {
        clearChildren(); mRows.clear(); mCurve = nullptr;
        auto addRow = [&](const std::string &label, double mn, double mx, double def, bool grad, Color gl, Color gr,
                          std::function<double(const EditParams &)> get, std::function<void(EditParams &, double)> set) {
            auto w = std::make_shared<ParamSlider>(label, mn, mx, grad, gl, gr);
            w->onChangeValue = [this, set](double ui) { auto *p = mSession.curParams(); if (!p) return; EditParams np = *p; set(np, ui); mSession.applyParams(np); };
            addChild(w); mRows.push_back({w, get, set, def});
        };
        if (mTab == 0)
            for (const auto &d : basicSections()[mSection].rows)
            { auto f = d.field; auto te = d.toEng; auto fe = d.fromEng;
              addRow(d.label, d.mn, d.mx, d.def, d.grad, d.gl, d.gr, [f, fe](const EditParams &p) { return fe(p.*f); }, [f, te](EditParams &p, double v) { p.*f = (float)te(v); }); }
        else if (mTab == 3)
        {
            int rg = mGradeRegion;
            addRow("Hue", 0, 360, 0, false, {}, {}, [rg](const EditParams &p) { return (double)p.grade[rg].hue; }, [rg](EditParams &p, double v) { p.grade[rg].hue = (float)v; });
            addRow("Saturation", 0, 100, 0, false, {}, {}, [rg](const EditParams &p) { return (double)p.grade[rg].sat; }, [rg](EditParams &p, double v) { p.grade[rg].sat = (float)v; });
            addRow("Luminance", -100, 100, 0, false, {}, {}, [rg](const EditParams &p) { return (double)p.grade[rg].lum; }, [rg](EditParams &p, double v) { p.grade[rg].lum = (float)v; });
            addRow("Balance", -100, 100, 0, false, {}, {}, [](const EditParams &p) { return (double)p.balance; }, [](EditParams &p, double v) { p.balance = (float)v; });
            auto *p = mSession.curParams();
            if (p && p->remapEnable)
            {
                addRow("Source Hue", 0, 360, 0, false, {}, {}, [](const EditParams &p) { return (double)p.remapSrc; }, [](EditParams &p, double v) { p.remapSrc = (float)v; });
                addRow("Range", 0, 180, 30, false, {}, {}, [](const EditParams &p) { return (double)p.remapRange; }, [](EditParams &p, double v) { p.remapRange = (float)v; });
                addRow("Target Hue", 0, 360, 0, false, {}, {}, [](const EditParams &p) { return (double)p.remapDst; }, [](EditParams &p, double v) { p.remapDst = (float)v; });
                addRow("Strength", 0, 100, 0, false, {}, {}, [](const EditParams &p) { return (double)p.remapStrength; }, [](EditParams &p, double v) { p.remapStrength = (float)v; });
            }
        }
        else if (mTab == 4)
            addRow("", -45, 45, 0, false, {}, {}, [](const EditParams &p) { return (double)p.rotation; }, [](EditParams &p, double v) { p.rotation = (float)v; });
        else if (mTab == 1)
        {
            auto *p = mSession.curParams();
            if (p && mMaskSel >= 0 && mMaskSel < (int)p->masks.size())
            {
                int mi = mMaskSel;
                addRow("Feather", 0, 100, 50, false, {}, {}, [mi](const EditParams &p) { return (double)(p.masks[mi].feather * 100); }, [mi](EditParams &p, double v) { p.masks[mi].feather = (float)(v / 100); });
                addRow("Exposure", -5, 5, 0, false, {}, {}, [mi](const EditParams &p) { return (double)p.masks[mi].adjust.exposure; }, [mi](EditParams &p, double v) { p.masks[mi].adjust.exposure = (float)v; });
                addRow("Contrast", -100, 100, 0, false, {}, {}, [mi](const EditParams &p) { return (double)p.masks[mi].adjust.contrast; }, [mi](EditParams &p, double v) { p.masks[mi].adjust.contrast = (float)v; });
                addRow("Clarity", -100, 100, 0, false, {}, {}, [mi](const EditParams &p) { return (double)p.masks[mi].adjust.clarity; }, [mi](EditParams &p, double v) { p.masks[mi].adjust.clarity = (float)v; });
            }
        }
        else if (mTab == 2)
        { mCurve = std::make_shared<CurveEditor>(); mCurve->onChange = [this](const std::vector<CurvePoint> &pts) { writeCurve(pts); }; addChild(mCurve); syncCurve(); }
    }
    void writeCurve(const std::vector<CurvePoint> &pts)
    {
        auto *p = mSession.curParams(); if (!p) return; EditParams np = *p;
        if (mCurveMode == 1) np.mixer[mMixerCh] = pts;
        else if (mCurveCh == 0) np.curve = pts; else np.curveChannel[mCurveCh - 1] = pts;
        mSession.applyParams(np);
    }
    void layoutBody()
    {
        double w = width.value();
        double y0 = bodyTop();
        if (mTab == 0) y0 += 52;                 // chips
        else if (mTab == 3) { y0 += 46; }         // region picker
        else if (mTab == 4) y0 += 8;              // rotation slider first
        else if (mTab == 1) y0 += (mSession.curParams() && !mSession.curParams()->masks.empty() ? 74 + (int)mSession.curParams()->masks.size() * 40 + 34 : 0);
        for (auto &r : mRows) { r.w->layout(w); r.w->x.set(0); r.w->y.set(y0); y0 += kRowH; }
        if (mTab == 3) mRemapToggleY = bodyTop() + 46 + 4 * kRowH + 6;   // after H/S/L/Balance
        if (mTab == 4) mXBtnY = bodyTop() + 8 + kRowH + 8;
        if (mCurve) { mCurve->x.set(16); mCurve->y.set(bodyTop() + 84); mCurve->width.set(w - 32); mCurve->height.set(std::max(120.0, height.value() - kToolBar - kAction - bodyTop() - 96)); }
    }
    void cycleDetent() { setDetent(mDetent == Detent::Full ? Detent::Half : mDetent == Detent::Half ? Detent::Rail : Detent::Half); }
    void setDetent(Detent d) { mDetent = d; applyDetent(mNowMs); bool vis = d != Detent::Rail; for (auto &r : mRows) r.w->visible = vis; if (mCurve) mCurve->visible = vis; }
    void applyDetent(double now) { double tg = mDetent == Detent::Rail ? kToolBar + 8 : mDetent == Detent::Half ? mScreenH * 0.5 : mScreenH * 0.92; height.animateTo(tg, 280.0, Easing::EaseOutCubic, now); }

    cosmo::EditSession &mSession;
    Detent mDetent = Detent::Half;
    int mTab = 0, mSection = 0, mCurveMode = 0, mCurveCh = 0, mMixerCh = 0, mGradeRegion = 1, mAspect = 0, mMaskSel = -1;
    double mScreenW = 393, mScreenH = 852, mNowMs = 0, mRemapToggleY = 0, mXBtnY = 0;
    std::vector<Row> mRows;
    std::shared_ptr<CurveEditor> mCurve;
    mutable std::vector<std::pair<Rect, int>> mZones;
    HistogramData mHist{};
    bool mHasHist = false;
};

// ── PresetDrawer: left slide-over showing the preset tree ────────────────────────
class PresetDrawer : public Segment
{
public:
    PresetDrawer() { visible = false; mX.set(-kW); }
    void open(double now) { visible = true; mX.animateTo(0, 240, Easing::EaseOutCubic, now); mScrim.animateTo(0.5, 240, Easing::EaseOutCubic, now); }
    void close(double now) { mX.animateTo(-kW, 240, Easing::EaseInCubic, now); mScrim.animateTo(0, 240, Easing::EaseInCubic, now); mClosing = true; }
    void advance(double now) override { Segment::advance(now); mX.update(now); mScrim.update(now); if (mClosing && mX.value() <= -kW + 0.5) { visible = false; mClosing = false; } }
    static constexpr double kW = 300.0;
protected:
    void onPaint(IRenderTarget &t) const override
    {
        double w = width.value(), h = height.value(), dx = mX.value();
        t.setFill(Color(0, 0, 0, mScrim.value())); rectPath(t, 0, 0, w, h); t.fillPath();
        t.setFill(CARD); rectPath(t, dx, 0, kW, h); t.fillPath();
        t.setStroke(BORDER, 1.0); t.beginPath(); t.moveTo(dx + kW, 0); t.lineTo(dx + kW, h); t.strokePath();
        txt(t, "PRESETS", dx + 16, 30, 11, fnt::sansSemiBold(), MUTED);
        icon::back(t, Rect{dx + kW - 40, 14, 20, 20}, FG, 1.75);
        double y = 48;
        for (size_t f = 0; f < tree().size(); ++f)
        {
            bool open = (int)f == mExpanded;
            hline(t, dx, dx + kW, y + 44, BORDER);
            (void)open;
            ci::chevronRight(t, Rect{dx + 12, y + 12, 20, 20}, MUTED, 1.3);
            txt(t, tree()[f].first, dx + 40, y + 28, 13, fnt::sansMedium(), FG);
            y += 44;
            if (open)
                for (auto &leaf : tree()[f].second)
                { bool sel = (mSelFolder == (int)f && leaf == mSelLeaf); if (sel) { t.setFill(ACCENT15); rectPath(t, dx, y, kW, 44); t.fillPath(); t.setFill(ACCENT); rectPath(t, dx, y, 2, 44); t.fillPath(); }
                  txt(t, leaf, dx + 40, y + 27, 13, fnt::sans(), sel ? ACCENT : FG); y += 44; }
        }
    }
    bool handleGesture(const Gesture &g, const Point &lp) override
    {
        if (g.type != Gesture::Type::Click) return true;
        double dx = mX.value();
        if (lp.x > dx + kW) { close(mNow); return true; }              // scrim
        if (lp.x > dx + kW - 44 && lp.y < 44) { close(mNow); return true; }  // back
        double y = 48;
        for (size_t f = 0; f < tree().size(); ++f)
        {
            if (lp.y >= y && lp.y < y + 44) { mExpanded = (mExpanded == (int)f ? -1 : (int)f); return true; }
            y += 44;
            if (mExpanded == (int)f)
                for (auto &leaf : tree()[f].second) { if (lp.y >= y && lp.y < y + 44) { mSelFolder = (int)f; mSelLeaf = leaf; return true; } y += 44; }
        }
        return true;
    }
    bool hitTestSelf(const Point &p) const override { return visible && localBounds().contains(p); }
public:
    void setNow(double n) { mNow = n; }
private:
    using Tree = std::vector<std::pair<std::string, std::vector<std::string>>>;
    static const Tree &tree()
    { static const Tree t = {{"Portrait", {"Clean Skin", "Warm Bloom", "Matte Portrait"}}, {"Landscape", {"Golden Hour", "Cool Shadows"}}, {"Black & White", {"Filmic B&W", "High Contrast"}}}; return t; }
    Property mX{-kW}, mScrim{0.0};
    bool mClosing = false;
    int mExpanded = 0, mSelFolder = -1;
    std::string mSelLeaf;
    double mNow = 0;
};

// ── SheetLayer: overflow menu / settings / history tree / confirm (DR-SHELL-3, HIST) ─
class SheetLayer : public Segment
{
public:
    enum class Mode { None, Overflow, Settings, History, Confirm };
    std::function<void()> onChanged;   // sync editor after a session-mutating action
    std::function<void()> onReset;     // reset workspace -> host (go home)
    explicit SheetLayer(cosmo::EditSession &s) : mSession(s) { visible = false; }
    void open(Mode m, double now) { mMode = m; visible = true; mClosing = false; mScrim.set(0); mScrim.animateTo(0.55, 180, Easing::EaseOutCubic, now); mRise.set(1); mRise.animateTo(0, 240, Easing::EaseOutCubic, now); }
    void close(double now) { mScrim.animateTo(0, 140, Easing::EaseInCubic, now); mClosing = true; }
    bool isOpen() const { return visible && !mClosing; }
    void setNow(double n) { mNow = n; }
    void advance(double now) override { Segment::advance(now); mScrim.update(now); mRise.update(now); if (mClosing && mScrim.value() < 0.02) { visible = false; mClosing = false; mMode = Mode::None; } }

protected:
    void onPaint(IRenderTarget &t) const override
    {
        mZones.clear();
        const double w = width.value(), h = height.value();
        t.setFill(Color(0, 0, 0, mScrim.value())); rectPath(t, 0, 0, w, h); t.fillPath();
        if (mMode == Mode::Overflow) paintOverflow(t, w, h);
        else if (mMode == Mode::Settings) paintSettings(t, w, h);
        else if (mMode == Mode::History) paintHistory(t, w, h);
        else if (mMode == Mode::Confirm) paintConfirm(t, w, h);
    }
    bool handleGesture(const Gesture &g, const Point &lp) override
    {
        if (g.type != Gesture::Type::Click) return true;
        for (auto it = mZones.rbegin(); it != mZones.rend(); ++it) if (it->first.contains(lp)) { onZone(it->second); return true; }
        close(mNow);   // tap outside any zone (scrim) dismisses
        return true;
    }
    bool hitTestSelf(const Point &p) const override { return visible && localBounds().contains(p); }

private:
    void zoneRect(double x, double y, double w, double h, int id) const { mZones.push_back({Rect{x, y, w, h}, id}); }
    std::vector<int> allImageSlots() const { std::vector<int> v; for (auto &n : mSession.nodes()) if (!n.group && n.slot >= 0) v.push_back(n.slot); return v; }

    void seg(IRenderTarget &t, double x, double y, double w, double h, const std::vector<const char *> &labels, int sel, int base) const
    {
        t.setFill(INPUT); rrectPath(t, x, y, w, h, 2); t.fillPath(); t.setStroke(BORDER, 1.0); rrectPath(t, x, y, w, h, 2); t.strokePath();
        double sw = w / labels.size();
        for (size_t i = 0; i < labels.size(); ++i)
        { bool on = (int)i == sel; if (on) { t.setFill(ACCENT); rrectPath(t, x + i * sw + 1, y + 1, sw - 2, h - 2, 2); t.fillPath(); }
          txtC(t, labels[i], x + i * sw + sw * 0.5, y + h * 0.5 + 4, 11, fnt::sansMedium(), on ? WHITE : MUTED); zoneRect(x + i * sw, y, sw, h, base + (int)i); }
    }

    void paintOverflow(IRenderTarget &t, double w, double h) const
    {
        struct It { const char *l; int id; bool danger; };
        static const It items[] = {{"Undo", 1, false}, {"Redo", 2, false}, {"Copy Settings", 3, false}, {"Paste to All Images", 4, false},
                                   {"Group Selection", 5, false}, {"Ungroup Selection", 6, false}, {"Show History Tree", 7, false},
                                   {"Engine Settings", 8, false}, {"Reset Workspace", 9, true}};
        int n = 9; double rowH = 48, sheetH = 24 + n * rowH + 16, sy = h - sheetH + mRise.value() * sheetH;
        t.setFill(POP); rrectPath(t, 0, sy, w, sheetH + 40, 12); t.fillPath();
        t.setFill(Color(0x8a / 255.0, 0x8a / 255.0, 0x8a / 255.0, 0.4)); rrectPath(t, w / 2 - 18, sy + 8, 36, 4, 2); t.fillPath();
        double y = sy + 24;
        for (auto &it : items)
        { bool dis = (it.id == 1 && !mSession.canUndo()) || (it.id == 2 && !mSession.canRedo());
          txt(t, it.l, 24, y + rowH * 0.5 + 5, 15, fnt::sans(), dis ? Color(FG.r, FG.g, FG.b, 0.35) : it.danger ? DESTRUCT : FG);
          hline(t, 20, w - 20, y + rowH, BORDER); if (!dis) zoneRect(0, y, w, rowH, it.id); y += rowH; }
    }
    void paintSettings(IRenderTarget &t, double w, double h) const
    {
        double sheetH = 260, sy = h - sheetH + mRise.value() * sheetH;
        t.setFill(POP); rrectPath(t, 0, sy, w, sheetH + 40, 12); t.fillPath();
        t.setFill(Color(0x8a / 255.0, 0x8a / 255.0, 0x8a / 255.0, 0.4)); rrectPath(t, w / 2 - 18, sy + 8, 36, 4, 2); t.fillPath();
        txt(t, "Engine Settings", 20, sy + 40, 15, fnt::sansSemiBold(), FG);
        int q = mSession.previewEdge() <= 1200 ? 0 : mSession.previewEdge() <= 2000 ? 1 : 2;
        txt(t, "PREVIEW QUALITY", 20, sy + 74, 11, fnt::sansMedium(), MUTED);
        seg(t, 20, sy + 84, w - 40, 30, {"Draft", "Standard", "High"}, q, 100);
        int th = par::threadsRef() == 0 ? 0 : par::threadsRef() == 2 ? 1 : par::threadsRef() == 4 ? 2 : 3;
        txt(t, "CPU THREADS", 20, sy + 138, 11, fnt::sansMedium(), MUTED);
        seg(t, 20, sy + 148, w - 40, 30, {"Auto", "2", "4", "8"}, th, 200);
        bool avail = mSession.gpuAvailable(), on = mSession.useGpu();
        txt(t, avail ? "GPU Acceleration" : "GPU Acceleration . unavailable", 20, sy + 214, 14, fnt::sans(), avail ? FG : MUTED);
        t.setFill(on && avail ? ACCENT : INPUT); rrectPath(t, w - 60, sy + 198, 44, 26, 13); t.fillPath();
        t.setFill(WHITE); rrectPath(t, w - 60 + (on && avail ? 21 : 3), sy + 201, 20, 20, 10); t.fillPath();
        if (avail) zoneRect(w - 60, sy + 198, 44, 26, 300);
    }
    void paintHistory(IRenderTarget &t, double w, double h) const
    {
        double cw = w - 48, ch = h * 0.7, cx = 24, cy = (h - ch) * 0.5 + mRise.value() * 30;
        t.setFill(CARD); rrectPath(t, cx, cy, cw, ch, 4); t.fillPath(); t.setStroke(BORDER, 1.0); rrectPath(t, cx, cy, cw, ch, 4); t.strokePath();
        txt(t, "History", cx + 16, cy + 30, 15, fnt::sansSemiBold(), FG);
        icon::back(t, Rect{cx + cw - 36, cy + 12, 20, 20}, MUTED, 1.6); zoneRect(cx + cw - 44, cy + 8, 40, 32, 9000);   // close
        cosmo::History *hy = mSession.currentHistory(); if (!hy) return;
        double y = cy + 56;
        for (size_t i = 0; i < hy->nodes.size() && y < cy + ch - 20; ++i)
        {
            int depth = 0; for (int p = hy->nodes[i].parent; p >= 0; p = hy->nodes[p].parent) ++depth;
            double nx = cx + 24 + depth * 24, cur = ((int)i == hy->current);
            if (hy->nodes[i].parent >= 0) { t.setStroke(BORDER, 1.0); t.beginPath(); t.moveTo(nx - 12, y - 20); t.lineTo(nx, y); t.strokePath(); }
            t.setFill(cur ? ACCENT : Color(0x25 / 255.0, 0x25 / 255.0, 0x25 / 255.0, 1)); rrectPath(t, nx - 5, y - 5, 10, 10, 5); t.fillPath();
            std::string lbl = hy->nodes[i].label.empty() ? "Edit" : hy->nodes[i].label;
            txt(t, lbl, nx + 14, y + 5, 13, fnt::sans(), cur ? ACCENT : FG);
            zoneRect(cx, y - 16, cw, 32, 1000 + (int)i); y += 34;
        }
    }
    void paintConfirm(IRenderTarget &t, double w, double h) const
    {
        double cw = w - 80, cardH = 150, cx = 40, cy = (h - cardH) * 0.5 + mRise.value() * 20;
        t.setFill(CARD); rrectPath(t, cx, cy, cw, cardH, 4); t.fillPath(); t.setStroke(BORDER, 1.0); rrectPath(t, cx, cy, cw, cardH, 4); t.strokePath();
        txt(t, "Reset workspace?", cx + 20, cy + 34, 15, fnt::sansSemiBold(), FG);
        txt(t, "This clears all photos and edits.", cx + 20, cy + 62, 13, fnt::sans(), MUTED);
        double bw = 90, bh = 36, by = cy + cardH - bh - 16;
        t.setStroke(BORDER, 1.0); rrectPath(t, cx + cw - 2 * bw - 28, by, bw, bh, 2); t.strokePath();
        txtC(t, "Cancel", cx + cw - 2 * bw - 28 + bw / 2, by + bh * 0.5 + 4.5, 13, fnt::sans(), FG); zoneRect(cx + cw - 2 * bw - 28, by, bw, bh, 2000);
        t.setFill(DESTRUCT); rrectPath(t, cx + cw - bw - 20, by, bw, bh, 2); t.fillPath();
        txtC(t, "Reset", cx + cw - bw - 20 + bw / 2, by + bh * 0.5 + 4.5, 13, fnt::sansSemiBold(), WHITE); zoneRect(cx + cw - bw - 20, by, bw, bh, 2001);
    }

    void onZone(int id)
    {
        switch (id)
        {
        case 1: mSession.undo(); done(); break;
        case 2: mSession.redo(); done(); break;
        case 3: mSession.copyCurrent(); close(mNow); break;
        case 4: mSession.pasteTo(allImageSlots()); done(); break;
        case 5: mSession.createGroupFromSelection(); done(); break;
        case 6: mSession.ungroupSelected(); done(); break;
        case 7: open(Mode::History, mNow); break;
        case 8: open(Mode::Settings, mNow); break;
        case 9: open(Mode::Confirm, mNow); break;
        case 100: case 101: case 102: mSession.setPreviewEdge(id == 100 ? 1000 : id == 101 ? 1600 : 2400); break;
        case 200: case 201: case 202: case 203: par::setThreads(id == 200 ? 0 : id == 201 ? 2 : id == 202 ? 4 : 8); mSession.submit(); break;
        case 300: mSession.setUseGpu(!mSession.useGpu()); break;
        case 2000: close(mNow); break;
        case 2001: mSession.resetWorkspace(); close(mNow); if (onReset) onReset(); break;
        case 9000: close(mNow); break;
        default:
            if (id >= 1000 && id < 2000) { mSession.jumpToHistory(id - 1000); done(); }
            break;
        }
    }
    void done() { if (onChanged) onChanged(); close(mNow); }

    cosmo::EditSession &mSession;
    Mode mMode = Mode::None;
    Property mScrim{0.0}, mRise{1.0};
    bool mClosing = false;
    double mNow = 0;
    mutable std::vector<std::pair<Rect, int>> mZones;
};

// ── EditorScreen ─────────────────────────────────────────────────────────────────
class EditorScreen : public Segment
{
public:
    std::function<void()> onHome;
    explicit EditorScreen(cosmo::EditSession &s) : mSession(s)
    {
        mPhoto = std::make_shared<ImageView>(); mPhoto->setFit(ImageView::Fit::Contain); mPhoto->inputTransparent = true; addChild(mPhoto);
        mPill = std::make_shared<BeforeAfterPill>(); addChild(mPill);
        mTray = std::make_shared<Tray>(s); addChild(mTray);
        mDrawer = std::make_shared<PresetDrawer>(); addChild(mDrawer);
        mSheets = std::make_shared<SheetLayer>(s);
        mSheets->onChanged = [this] { syncControls(); };
        mSheets->onReset = [this] { if (onHome) onHome(); };
        addChild(mSheets);
    }
    void resize(double w, double h, double now)
    {
        width.set(w); height.set(h);
        double py = kTopBar, ph = h - kTopBar - kCrumb - kFilm;
        mPhoto->x.set(0); mPhoto->y.set(py); mPhoto->width.set(w); mPhoto->height.set(ph);
        mPill->x.set((w - mPill->width.value()) * 0.5); mPill->y.set(py + ph - mPill->height.value() - 12);
        mDrawer->x.set(0); mDrawer->y.set(0); mDrawer->width.set(w); mDrawer->height.set(h);
        mSheets->x.set(0); mSheets->y.set(0); mSheets->width.set(w); mSheets->height.set(h);
        mTray->setNow(now); mTray->configure(w, h, now);
    }
    void setPhoto(const uint8_t *rgba, int w, int h) { mPhoto->setImage(rgba, w, h); }
    void setEmpty(bool e) { mPhoto->visible = !e; mThumbIds.clear(); }   // empty project: blank canvas
    void setHist(const HistogramData &h) { mTray->setHist(h); }          // DR-EDIT-3
    void syncControls() { mTray->syncFromSession(); }
    void setNow(double n) { mTray->setNow(n); mDrawer->setNow(n); mSheets->setNow(n); }
    void setName(const std::string &n) { mName = n; }
protected:
    void onPaint(IRenderTarget &t) const override
    {
        const double w = width.value(), h = height.value();
        t.setFill(STAGE); rectPath(t, 0, kTopBar, w, h - kTopBar); t.fillPath();
        double crumbY = h - kFilm - kCrumb;
        t.setFill(BG); rectPath(t, 0, crumbY, w, kCrumb); t.fillPath(); hline(t, 0, w, crumbY, BORDER); hline(t, 0, w, crumbY + kCrumb, BORDER);
        drawBreadcrumb(t, crumbY);
        double filmY = h - kFilm; t.setFill(BG); rectPath(t, 0, filmY, w, kFilm); t.fillPath(); drawFilmstrip(t, filmY, w);
        t.setFill(BG); rectPath(t, 0, 0, w, kTopBar); t.fillPath(); hline(t, 0, w, kTopBar, BORDER);
        double c = kTopBar * 0.5;
        ci::panelLeft(t, Rect{10, c - 10, 20, 20}, FG, 1.5);
        icon::back(t, Rect{54, c - 10, 20, 20}, FG, 1.75);
        txtC(t, mName.empty() ? "Untitled" : mName, w * 0.5, c + 5, 14, fnt::sansMedium(), FG);
        icon::undo(t, Rect{w - 132, c - 10, 20, 20}, mSession.canUndo() ? FG : MUTED, 1.75);
        icon::redo(t, Rect{w - 88, c - 10, 20, 20}, mSession.canRedo() ? FG : MUTED, 1.75);
        icon::more(t, Rect{w - 44, c - 10, 20, 20}, FG, 1.75);
    }
    bool handleGesture(const Gesture &g, const Point &lp) override
    {
        if (g.type != Gesture::Type::Click) return true;
        const double w = width.value(), h = height.value();
        if (lp.y <= kTopBar)
        {
            if (lp.x < 44) mDrawer->open(mNow);                                  // panel -> preset drawer
            else if (lp.x < 88) { if (onHome) onHome(); }                         // back -> home
            else if (lp.x >= w - 140 && lp.x < w - 96) { if (mSession.canUndo()) { mSession.undo(); syncControls(); } }  // undo
            else if (lp.x >= w - 96 && lp.x < w - 52) { if (mSession.canRedo()) { mSession.redo(); syncControls(); } }   // redo
            else if (lp.x >= w - 52) mSheets->open(SheetLayer::Mode::Overflow, mNow);   // more -> menu
            return true;
        }
        double filmY = h - kFilm, cy = filmY + (kFilm - 54) * 0.5;
        if (lp.y >= filmY && lp.y <= filmY + kFilm)
        {
            auto cells = mSession.currentGroupCells(); double x = 8;
            for (auto &cc : cells) { if (lp.x >= x && lp.x <= x + 72 && lp.y >= cy && lp.y <= cy + 54) { if (!cc.group && cc.slot >= 0) { mSession.selectImage(cc.slot); syncControls(); } break; } x += 78; }
            return true;
        }
        return true;
    }
    bool hitTestSelf(const Point &p) const override { return localBounds().contains(p); }
private:
    void drawBreadcrumb(IRenderTarget &t, double y) const
    {
        auto path = mSession.breadcrumbPath(); if (path.empty()) path.push_back(mName.empty() ? "Photo" : mName);
        double x = 12, bl = y + kCrumb * 0.5 + 4;
        for (size_t i = 0; i < path.size(); ++i)
        { bool last = i + 1 == path.size(); txt(t, path[i], x, bl, 11, fnt::sans(), last ? FG : MUTED); x += t.measureText(path[i], 11, fnt::sans()) + 6;
          if (!last) { txt(t, ">", x, bl, 10, fnt::sans(), MUTED); x += 12; } }
    }
    void drawFilmstrip(IRenderTarget &t, double y, double w) const
    {
        (void)w; auto cells = mSession.currentGroupCells(); int cur = mSession.currentSlot();
        double x = 8, cw = 72, ch = 54, cy = y + (kFilm - ch) * 0.5;
        for (auto &c : cells)
        {
            if (c.group)
            { t.setStroke(BORDER, 1.5); rrectPath(t, x, cy, cw, ch, 2); t.strokePath(); icon::folderOpen(t, Rect{x + cw / 2 - 9, cy + 8, 18, 18}, MUTED, 1.4);
              char b[24]; std::snprintf(b, sizeof b, "%s . %d", c.name.c_str(), c.count); txtC(t, b, x + cw * 0.5, cy + ch - 7, 9, fnt::sans(), MUTED); }
            else
            { t.setFill(INPUT); rrectPath(t, x, cy, cw, ch, 2); t.fillPath(); int id = thumbId(t, c.slot);
              if (id >= 0) { t.save(); t.clipRect(x, cy, cw, ch); t.drawImage(id, Rect{x, cy, cw, ch}); t.restore(); }
              if (c.slot == cur) { t.setStroke(ACCENT, 2.0); rrectPath(t, x, cy, cw, ch, 2); t.strokePath(); } }
            x += 78;
        }
    }
    int thumbId(IRenderTarget &t, int slot) const
    {
        auto it = mThumbIds.find(slot); if (it != mThumbIds.end()) return it->second;
        const cosmo::EditSession::Thumb *th = mSession.thumbForSlot(slot); if (!th || th->w <= 0) return -1;
        int id = t.registerImage(th->rgba.data(), th->w, th->h); mThumbIds[slot] = id; return id;
    }
    cosmo::EditSession &mSession;
    std::shared_ptr<ImageView> mPhoto;
    std::shared_ptr<BeforeAfterPill> mPill;
    std::shared_ptr<Tray> mTray;
    std::shared_ptr<PresetDrawer> mDrawer;
    std::shared_ptr<SheetLayer> mSheets;
    std::string mName;
    double mNow = 0;
    mutable std::map<int, int> mThumbIds;
public:
    void setNowAll(double n) { mNow = n; setNow(n); }
};

// ── HomeScreen ───────────────────────────────────────────────────────────────────
class HomeScreen : public Segment
{
public:
    std::function<void()> onNew, onOpen, onImport;
    void setProject(const std::string &name, int count, const std::string &sizeStr, const std::vector<uint8_t> &thumb, int tw, int th)
    { mName = name; mCount = count; mSize = sizeStr; mThumb = thumb; mTw = tw; mTh = th; mThumbId = -1; }
    const std::string &name() const { return mName; }
    int count() const { return mCount; }
protected:
    void onPaint(IRenderTarget &t) const override
    {
        const double w = width.value(); t.setFill(BG); rectPath(t, 0, 0, w, height.value()); t.fillPath();
        double y = 44;
        txt(t, "cosmo", 24, y + 26, 36, fnt::sansSemiBold(), FG);
        txt(t, ".", 24 + t.measureText("cosmo", 36, fnt::sansSemiBold()), y + 26, 36, fnt::sansSemiBold(), ACCENT);
        txt(t, "Dark-room tools for serious work.", 24, y + 52, 14, fnt::sans(), MUTED);
        y += 76;
        struct A { const char *l; bool ac; int ic; }; const A acts[3] = {{"New Project", true, 0}, {"Open Project", false, 1}, {"Import Catalog", false, 2}};
        mActBtns.clear();
        for (auto &a : acts)
        { t.setFill(a.ac ? ACCENT : BG); rrectPath(t, 16, y, w - 32, 52, 2); t.fillPath(); if (!a.ac) { t.setStroke(BORDER, 1.0); rrectPath(t, 16, y, w - 32, 52, 2); t.strokePath(); }
          Color ic = a.ac ? WHITE : FG; Rect ib{32, y + 16, 20, 20};
          if (a.ic == 0) icon::plusCircle(t, ib, ic, 1.75); else if (a.ic == 1) icon::folderOpen(t, ib, ic, 1.6); else icon::importDown(t, ib, ic, 1.6);
          txt(t, a.l, 64, y + 32, 15, fnt::sansMedium(), ic); mActBtns.push_back(Rect{16, y, w - 32, 52}); y += 60; }
        y += 12;
        txt(t, "Recent Projects", 16, y + 14, 15, fnt::sansSemiBold(), FG);
        double cx = 16 + t.measureText("Recent Projects", 15, fnt::sansSemiBold()) + 10;
        t.setFill(CARD); rrectPath(t, cx, y, 22, 18, 2); t.fillPath(); t.setStroke(BORDER, 1.0); rrectPath(t, cx, y, 22, 18, 2); t.strokePath();
        txtC(t, mCount > 0 ? "1" : "0", cx + 11, y + 13, 11, fnt::mono(), MUTED);
        y += 30;
        t.setFill(INPUT); rrectPath(t, 16, y, w - 32, 40, 2); t.fillPath(); t.setStroke(BORDER, 1.0); rrectPath(t, 16, y, w - 32, 40, 2); t.strokePath();
        icon::search(t, Rect{26, y + 10, 18, 18}, MUTED, 1.5); txt(t, "Search projects", 52, y + 25, 14, fnt::sans(), MUTED);
        y += 52;
        mCardRect = Rect{0, 0, 0, 0};
        if (mCount > 0)
        {
            t.setFill(CARD); rrectPath(t, 16, y, w - 32, 80, 2); t.fillPath(); t.setStroke(BORDER, 1.0); rrectPath(t, 16, y, w - 32, 80, 2); t.strokePath();
            int id = thumbId(t); t.setFill(INPUT); rrectPath(t, 28, y + 12, 80, 56, 1); t.fillPath();
            if (id >= 0) { t.save(); t.clipRect(28, y + 12, 80, 56); t.drawImage(id, Rect{28, y + 12, 80, 56}); t.restore(); }
            txt(t, mName, 120, y + 30, 14, fnt::sansMedium(), FG);
            t.setFill(ACCENT); rrectPath(t, 120 + t.measureText(mName, 14, fnt::sansMedium()) + 8, y + 22, 6, 6, 3); t.fillPath();
            char meta[64]; std::snprintf(meta, sizeof meta, "%d photos . %s . Opened recently", mCount, mSize.c_str());
            txt(t, meta, 120, y + 50, 12, fnt::sans(), MUTED);
            mCardRect = Rect{16, y, w - 32, 80}; y += 88;
        }
        t.setStroke(BORDER, 1.5); rrectPath(t, 16, y, w - 32, 72, 2); t.strokePath();
        icon::plusCircle(t, Rect{w / 2 - 62, y + 27, 18, 18}, MUTED, 1.6); txt(t, "New Project", w / 2 - 36, y + 41, 14, fnt::sans(), MUTED);
        mNewCard = Rect{16, y, w - 32, 72}; y += 96;
        for (const char *l : {"Settings", "What's New", "Help & Documentation"}) { txt(t, l, 24, y + 27, 13, fnt::sans(), MUTED); hline(t, 24, w - 24, y + 44, BORDER); y += 44; }
        txt(t, "cosmo v1.0.0-beta", 24, y + 24, 11, fnt::mono(), Color(MUTED.r, MUTED.g, MUTED.b, 0.5));
    }
    bool handleGesture(const Gesture &g, const Point &lp) override
    {
        if (g.type != Gesture::Type::Click) return true;
        for (size_t i = 0; i < mActBtns.size(); ++i)
            if (mActBtns[i].contains(lp))
            { if (i == 0 && onNew) onNew(); else if (i == 1 && onOpen) onOpen(); else if (i == 2 && onImport) onImport(); return true; }
        if (mCardRect.w > 0 && mCardRect.contains(lp)) { if (onOpen) onOpen(); return true; }   // recent card -> open
        if (mNewCard.w > 0 && mNewCard.contains(lp)) { if (onNew) onNew(); return true; }       // dashed card -> new
        return true;   // search / footer / empty -> nothing
    }
    bool hitTestSelf(const Point &p) const override { return localBounds().contains(p); }
private:
    int thumbId(IRenderTarget &t) const { if (mThumbId >= 0 || mTw <= 0) return mThumbId; mThumbId = t.registerImage(mThumb.data(), mTw, mTh); return mThumbId; }
    std::string mName, mSize; int mCount = 0, mTw = 0, mTh = 0; std::vector<uint8_t> mThumb;
    mutable int mThumbId = -1;
    mutable std::vector<Rect> mActBtns; mutable Rect mCardRect{}, mNewCard{};
};

// ── LoadingScreen ────────────────────────────────────────────────────────────────
class LoadingScreen : public Segment
{
public:
    std::function<void()> onDone;
    void begin(const std::string &name, int count, double now) { mName = name; mCount = count; mStart = now; mProgress = 0; mFired = false; }
    void advance(double now) override
    {
        Segment::advance(now); double el = now - mStart;
        mProgress = artboard::reducedMotion() ? 1.0 : std::min(1.0, el / 1100.0); mNow = now;
        if (!mFired && mProgress >= 1.0 && el >= 600.0) { mFired = true; if (onDone) onDone(); }
    }
protected:
    void onPaint(IRenderTarget &t) const override
    {
        const double w = width.value(), h = height.value(); t.setFill(BG); rectPath(t, 0, 0, w, h); t.fillPath();
        bool tw = !artboard::reducedMotion();
        for (int i = 0; i < 60; ++i)
        { double sx = std::fmod(i * 137.5, w), sy = std::fmod(i * 269.13 + 40, h), r = 0.4 + (i % 3) * 0.45, base = 0.2 + (i % 5) * 0.12;
          double o = tw ? base * (0.4 + 0.6 * std::fabs(std::sin(mNow / 700.0 + i))) : base; t.setFill(Color(1, 1, 1, o)); rrectPath(t, sx, sy, r * 2, r * 2, r); t.fillPath(); }
        double cx = w / 2, cy = h / 2 - 40;
        double ww = t.measureText("cosmo", 32, fnt::sansSemiBold());
        txt(t, "cosmo", cx - ww * 0.5 - 4, cy, 32, fnt::sansSemiBold(), FG); txt(t, ".", cx + ww * 0.5 - 4, cy, 32, fnt::sansSemiBold(), ACCENT);
        txtC(t, mName, cx, cy + 40, 18, fnt::sansMedium(), FG);
        double bw = 200, bx = cx - bw / 2, by = cy + 72;
        t.setFill(CARD); rrectPath(t, bx, by, bw, 3, 1.5); t.fillPath(); t.setFill(ACCENT); rrectPath(t, bx, by, bw * mProgress, 3, 1.5); t.fillPath();
        char b[48]; std::snprintf(b, sizeof b, "Loading %d / %d photos", (int)std::lround(mProgress * mCount), mCount);
        txtC(t, b, cx, cy + 100, 12, fnt::sans(), MUTED);
    }
private:
    std::string mName; int mCount = 0; double mStart = 0, mProgress = 0, mNow = 0; bool mFired = false;
};

// ── PhoneApp ─────────────────────────────────────────────────────────────────────
PhoneApp::PhoneApp(double width, double height) : mW(width), mH(height)
{
    par::setThreads(4);
    mHome = std::make_shared<HomeScreen>(); mLoading = std::make_shared<LoadingScreen>(); mEditor = std::make_shared<EditorScreen>(mSession);
    for (Segment *s : {(Segment *)mHome.get(), (Segment *)mLoading.get(), (Segment *)mEditor.get()}) { s->width.set(width); s->height.set(height); }
    mEditor->resize(width, height, 0.0);
    mHome->onNew = [this] { newProject(); };
    mHome->onOpen = [this] { openProject(); };
    mHome->onImport = [this] { importCatalog(); };
    mLoading->onDone = [this] { setScreen(Screen::Editor, mNowMs); };
    mEditor->onHome = [this] { setScreen(Screen::Home, mNowMs); };
    mRecognizer.setSink([this](const Gesture &g) { if (auto *r = activeRoot()) r->onGesture(g); });
}
PhoneApp::~PhoneApp() = default;

artboard::Segment *PhoneApp::activeRoot() const
{ switch (mScreen) { case Screen::Home: return mHome.get(); case Screen::Loading: return mLoading.get(); default: return mEditor.get(); } }

void PhoneApp::setScreen(Screen s, double now) { mScreen = s; if (s == Screen::Editor) mEditor->syncControls(); mFade.set(1.0); mFade.animateTo(0.0, 320.0, Easing::EaseOutCubic, now); }

void PhoneApp::setSize(double width, double height)
{ mW = width; mH = height; for (Segment *s : {(Segment *)mHome.get(), (Segment *)mLoading.get(), (Segment *)mEditor.get()}) { s->width.set(width); s->height.set(height); } mEditor->resize(width, height, mNowMs); }

void PhoneApp::addProjectImage(const uint8_t *rgba, int w, int h, const std::string &name)
{ mImgs.push_back({std::vector<uint8_t>(rgba, rgba + (size_t)w * h * 4), w, h, name}); }  // keep source

void PhoneApp::buildSession(const std::string &name, bool empty)
{
    mSession.resetWorkspace();
    mImageCount = 0;
    if (!empty)
        for (auto &im : mImgs)
        { if (mImageCount == 0) mSession.openImage(im.rgba.data(), im.w, im.h, im.name);
          else mSession.openImageInto(mSession.currentGroup(), im.rgba.data(), im.w, im.h, im.name, ""); ++mImageCount; }
    if (mImageCount > 0) mSession.selectImage(0);
    mSession.setUseGpu(true);
    mEditor->setName(name);
    mEditor->setEmpty(empty);
    if (!empty)
    {
        const cosmo::EditSession::Thumb *th = mSession.thumbForSlot(0);
        std::vector<uint8_t> tb; int tw = 0, thh = 0; if (th && th->w > 0) { tb = th->rgba; tw = th->w; thh = th->h; }
        mHome->setProject(name, mImageCount, "demo", tb, tw, thh);
    }
    mEditor->syncControls();
}

void PhoneApp::finishProject(const std::string &projectName)
{ if (!mImgs.empty()) buildSession(projectName, false); }   // startup: build, stay on Home

void PhoneApp::newProject()      { buildSession("Untitled", true);         mLoading->begin("Untitled", 0, mNowMs);          setScreen(Screen::Loading, mNowMs); }
void PhoneApp::openProject()     { buildSession("Sample Project", false);  mLoading->begin("Sample Project", mImageCount, mNowMs);   setScreen(Screen::Loading, mNowMs); }
void PhoneApp::importCatalog()   { buildSession("Imported Catalog", false); mLoading->begin("Imported Catalog", mImageCount, mNowMs); setScreen(Screen::Loading, mNowMs); }

bool PhoneApp::gpuAvailable() const { return mSession.gpuAvailable(); }

void PhoneApp::poll()
{
    RenderService::Frame f;
    if (mSession.renderService().tryAcquire(f) && f.width > 0)
    { mEditor->setPhoto(f.rgba.data(), f.width, f.height); mEditor->setHist(f.hist); }
}

void PhoneApp::render(IRenderTarget &t, double nowMs)
{
    mNowMs = nowMs; mEditor->setNowAll(nowMs); mSession.tick(nowMs); poll();
    Segment *r = activeRoot(); r->advance(nowMs); r->render(t); r->renderOverlay(t);
    double a = mFade.update(nowMs);
    if (a > 0.002) { t.save(); t.setTransform(Transform::identity()); t.setFill(Color(0x14 / 255.0, 0x14 / 255.0, 0x14 / 255.0, a));
        t.beginPath(); t.moveTo(0, 0); t.lineTo(mW, 0); t.lineTo(mW, mH); t.lineTo(0, mH); t.closePath(); t.fillPath(); t.restore(); }
}

void PhoneApp::pointer(int kind, double x, double y, int button, double timeMs, bool alt, bool shift, bool ctrl)
{
    (void)shift; (void)ctrl; RawPointer rp;
    rp.kind = kind == 0 ? RawPointer::Kind::Down : kind == 2 ? RawPointer::Kind::Up : RawPointer::Kind::Move;
    rp.pos = Point{x, y}; rp.button = button == 2 ? PointerButton::Right : PointerButton::Left; rp.timeMs = timeMs; rp.alt = alt;
    mRecognizer.feed(rp);
}
void PhoneApp::wheel(double x, double y, double delta, bool ctrl) { (void)x; (void)y; (void)delta; (void)ctrl; }
}  // namespace cosmo_touch
}  // namespace arstro
