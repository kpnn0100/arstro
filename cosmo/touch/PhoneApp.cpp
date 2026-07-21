#include "PhoneApp.h"
#include "Theme.h"
#include "base/Parallel.h"  // arstro::par::setThreads

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace artboard;

namespace arstro
{
namespace cosmo_touch
{
namespace
{
    namespace pal = cosmo_v2::palette;
    namespace fnt = cosmo_v2::font;

    const Color kGreen = Color::rgba(0x4C, 0xB5, 0x73);   // stacked/"final" reach

    // touch metrics (dp)
    constexpr double kTopBar = 52.0;
    constexpr double kToolBar = 64.0;
    constexpr double kRowH = 46.0;
    constexpr double kLabelW = 104.0;
    constexpr double kValueW = 40.0;
    constexpr double kChipH = 30.0;
    constexpr double kBodyTop = 62.0;   // below grab handle + section chips

    // UI<->engine unit conversions (see cosmo_v2::UnitConversions).
    double idc(double v) { return v; }
    double toKelvin(double v) { return 6500.0 + v / 100.0 * 3500.0; }
    double fromKelvin(double k) { return (k - 6500.0) / 3500.0 * 100.0; }
    double toTint(double v) { return v * 1.5; }
    double fromTint(double t) { return t / 1.5; }

    struct SliderDef
    {
        const char *label;
        double mn, mx, def;
        float EditParams::*field;
        double (*toEng)(double);
        double (*fromEng)(double);
        bool grad = false;
        Color gl{}, gr{};
    };

    struct Section { const char *name; std::vector<SliderDef> rows; };

    // The Basic/Detail tab, section-for-section as in the design brief. Temp/Tint use the
    // friendly UI scale; everything else is the engine's own units 1:1.
    const std::vector<Section> &basicSections()
    {
        static const std::vector<Section> s = {
            {"TONE", {
                {"Exposure", -5, 5, 0, &EditParams::exposure, idc, idc},
                {"Contrast", -100, 100, 0, &EditParams::contrast, idc, idc},
                {"Highlights", -100, 100, 0, &EditParams::highlights, idc, idc},
                {"Shadows", -100, 100, 0, &EditParams::shadows, idc, idc},
                {"Whites", -100, 100, 0, &EditParams::whites, idc, idc},
                {"Blacks", -100, 100, 0, &EditParams::blacks, idc, idc},
            }},
            {"COLOUR", {
                {"Temperature", -100, 100, 0, &EditParams::temp, toKelvin, fromKelvin, true,
                 Color::rgba(0x4F, 0xA3, 0xE8), Color::rgba(0xE8, 0xA8, 0x4F)},
                {"Tint", -100, 100, 0, &EditParams::tint, toTint, fromTint, true,
                 Color::rgba(0x4F, 0xB5, 0x73), Color::rgba(0xB5, 0x4F, 0xA8)},
                {"Vibrance", -100, 100, 0, &EditParams::vibrance, idc, idc},
                {"Saturation", -100, 100, 0, &EditParams::saturation, idc, idc},
            }},
            {"PRESENCE", {
                {"Texture", -100, 100, 0, &EditParams::texture, idc, idc},
                {"Clarity", -100, 100, 0, &EditParams::clarity, idc, idc},
            }},
            {"EFFECTS", {
                {"Dehaze", -100, 100, 0, &EditParams::dehaze, idc, idc},
                {"Grain Amount", 0, 100, 0, &EditParams::grainAmount, idc, idc},
                {"Grain Size", 0, 100, 25, &EditParams::grainSize, idc, idc},
            }},
            {"SHARPEN", {
                {"Amount", 0, 150, 0, &EditParams::sharpenAmount, idc, idc},
                {"Radius", 0, 3, 1, &EditParams::sharpenRadius, idc, idc},
                {"Masking", 0, 100, 0, &EditParams::sharpenMasking, idc, idc},
            }},
            {"NOISE", {
                {"Luminance", 0, 100, 0, &EditParams::nrLuminance, idc, idc},
                {"Colour", 0, 100, 0, &EditParams::nrColor, idc, idc},
            }},
            {"LENS", {
                {"Distortion", -100, 100, 0, &EditParams::lensDistortion, idc, idc},
                {"Chromatic Ab.", 0, 100, 0, &EditParams::lensCA, idc, idc},
                {"Vignette", -100, 100, 0, &EditParams::lensVignette, idc, idc},
            }},
        };
        return s;
    }

    const char *kTabLabels[5] = {"Basic", "Mask", "Curve", "Grade", "Xform"};

    // ── ParamSlider: label + touch Slider + signed value readout ─────────────────
    class ParamSlider : public Segment
    {
    public:
        std::function<void(double)> onChangeValue;

        ParamSlider(std::string label, double mn, double mx, bool grad, Color gl, Color gr)
            : mLabel(std::move(label))
        {
            SliderStyle st = cosmo_v2::sharedTheme().slider;
            st.thumbRadius = 11.0;  // touch grab
            mSlider = std::make_shared<Slider>(st);
            mSlider->setRange(mn, mx);
            mSlider->setClickJumps(false);
            mSlider->setSubValueColor(kGreen);
            mSlider->height.set(18.0);
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
            mSlider->x.set(kLabelW);
            mSlider->y.set((kRowH - mSlider->height.value()) * 0.5);
            mSlider->width.set(std::max(10.0, w - kLabelW - kValueW - 16.0));
        }

    protected:
        void onPaint(IRenderTarget &t) const override
        {
            t.setFill(pal::mutedForeground());
            t.drawText(mLabel, 16.0, kRowH * 0.5 + 4.5, 13.0, fnt::sans());
            char buf[16];
            int v = (int)std::lround(mSlider->value());
            std::snprintf(buf, sizeof buf, "%s%d", v > 0 ? "+" : "", v);
            t.setFill(pal::foreground());
            t.drawText(buf, width.value() - kValueW - 4.0, kRowH * 0.5 + 4.5, 13.0, fnt::mono());
        }

    private:
        std::string mLabel;
        std::shared_ptr<Slider> mSlider;
    };
}  // namespace

// ── Tray: the bottom card — grab handle, section chips, slider rows, tool bar ────
class Tray : public Segment
{
public:
    enum class Detent { Rail, Half, Full };

    explicit Tray(cosmo::EditSession &s) : mSession(s)
    {
        clipToBounds = true;
        rebuildRows(0.0);
    }

    void configure(double screenW, double screenH, double nowMs)
    {
        mScreenW = screenW; mScreenH = screenH;
        x.set(0.0); width.set(screenW);
        applyDetent(nowMs);
        layoutRows();
    }

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
            // green stacked reach = effective - own (0 until groups exist)
            double effUi = defs[i].fromEng(eff.*(defs[i].field));
            mRows[i]->setSubOffset(effUi - own);
        }
    }

    void advance(double nowMs) override
    {
        Segment::advance(nowMs);
        y.set(mScreenH - height.value());  // stay bottom-anchored while animating
    }

protected:
    void onPaint(IRenderTarget &t) const override
    {
        const double w = width.value(), h = height.value();
        // card
        t.setFill(pal::card());
        roundedTop(t, 0, 0, w, h, 10);
        t.fillPath();
        // grab handle
        t.setFill(pal::mutedForeground());
        roundedTop(t, w / 2 - 18, 8, 36, 4, 2);
        t.fillPath();

        const bool expanded = mDetent != Detent::Rail;
        if (expanded)
        {
            // section chips
            double cx = 12;
            for (size_t i = 0; i < basicSections().size(); ++i)
            {
                const char *nm = basicSections()[i].name;
                double cw = 12 + 7.4 * (double)std::string(nm).size();
                bool on = (int)i == mSection;
                t.setFill(on ? pal::primaryAlpha(0.15) : pal::card());
                rrect(t, cx, 22, cw, kChipH, 2); t.fillPath();
                t.setStroke(on ? pal::primary() : pal::border(), 1.0);
                rrect(t, cx, 22, cw, kChipH, 2); t.strokePath();
                t.setFill(on ? pal::primary() : pal::mutedForeground());
                t.drawText(nm, cx + 8, 22 + kChipH * 0.5 + 4, 11, fnt::sansMedium());
                cx += cw + 6;
            }
        }

        // tool bar (always visible) at the bottom
        const double tbY = h - kToolBar;
        t.setStroke(pal::border(), 1.0);
        t.beginPath(); t.moveTo(0, tbY); t.lineTo(w, tbY); t.strokePath();
        const double tabW = w / 5.0;
        for (int i = 0; i < 5; ++i)
        {
            bool active = (i == mTab) && expanded;
            double tx = i * tabW;
            if (active)
            {
                t.setFill(pal::primary());
                rrect(t, tx + tabW / 2 - 14, tbY + 6, 28, 2, 1); t.fillPath();
            }
            t.setFill(active ? pal::primary() : pal::mutedForeground());
            t.drawText(kTabLabels[i], tx + tabW / 2 - 3.3 * std::string(kTabLabels[i]).size(),
                       tbY + kToolBar * 0.5 + 12, 11, active ? fnt::sansSemiBold() : fnt::sans());
        }

        if (expanded && mTab != 0)
        {
            t.setFill(pal::mutedForeground());
            t.drawText("Coming soon", w / 2 - 42, kBodyTop + 40, 14, fnt::sans());
        }
    }

    bool handleGesture(const Gesture &g, const Point &lp) override
    {
        if (g.type != Gesture::Type::Click && g.type != Gesture::Type::Down) return true;
        const double w = width.value(), h = height.value();
        const double tbY = h - kToolBar;
        if (g.type == Gesture::Type::Click)
        {
            if (lp.y >= tbY)  // tool bar tab
            {
                int tab = std::min(4, std::max(0, (int)(lp.x / (w / 5.0))));
                onTab(tab);
                return true;
            }
            if (lp.y <= 20)  // grab handle → cycle detent
            {
                cycleDetent();
                return true;
            }
            if (mDetent != Detent::Rail && lp.y >= 22 && lp.y <= 22 + kChipH)  // chips
            {
                double cx = 12;
                for (size_t i = 0; i < basicSections().size(); ++i)
                {
                    double cw = 12 + 7.4 * (double)std::string(basicSections()[i].name).size();
                    if (lp.x >= cx && lp.x <= cx + cw) { onSection((int)i); break; }
                    cx += cw + 6;
                }
                return true;
            }
        }
        return true;  // swallow taps on the tray (don't fall through to the photo)
    }

    bool hitTestSelf(const Point &p) const override { return localBounds().contains(p); }

private:
    void onTab(int tab)
    {
        if (tab == mTab && mDetent != Detent::Rail) { setDetent(Detent::Rail); return; }
        mTab = tab;
        setDetent(Detent::Half);
    }
    void onSection(int s)
    {
        if (s == mSection) return;
        mSection = s;
        rebuildRows(mNowMs);
        layoutRows();
        syncFromSession();
    }
    void cycleDetent()
    {
        setDetent(mDetent == Detent::Full ? Detent::Half
                  : mDetent == Detent::Half ? Detent::Rail : Detent::Half);
    }
    void setDetent(Detent d)
    {
        mDetent = d;
        applyDetent(mNowMs);
        for (auto &r : mRows) r->visible = (mDetent != Detent::Rail && mTab == 0);
    }
    void applyDetent(double nowMs)
    {
        double target = mDetent == Detent::Rail ? kToolBar
                        : mDetent == Detent::Half ? mScreenH * 0.46 : mScreenH * 0.9;
        height.animateTo(target, 280.0, Easing::EaseOutCubic, nowMs);
    }

    void rebuildRows(double nowMs)
    {
        (void)nowMs;
        clearChildren();
        mRows.clear();
        const auto &defs = basicSections()[mSection].rows;
        for (const auto &d : defs)
        {
            auto row = std::make_shared<ParamSlider>(d.label, d.mn, d.mx, d.grad, d.gl, d.gr);
            SliderDef def = d;
            row->onChangeValue = [this, def](double ui) {
                EditParams *p = mSession.curParams();
                if (!p) return;
                EditParams np = *p;
                np.*(def.field) = (float)def.toEng(ui);
                mSession.applyParams(np);
            };
            row->visible = (mDetent != Detent::Rail && mTab == 0);
            addChild(row);
            mRows.push_back(row);
        }
    }

    void layoutRows()
    {
        double w = width.value();
        double y0 = kBodyTop;
        for (auto &r : mRows)
        {
            r->layout(w);
            r->x.set(0);
            r->y.set(y0);
            y0 += kRowH;
        }
    }

    // path helpers
    static void rrect(IRenderTarget &t, double x, double y, double w, double h, double r)
    {
        r = std::min(r, std::min(w, h) * 0.5);
        t.beginPath();
        t.moveTo(x + r, y); t.lineTo(x + w - r, y); t.quadTo(x + w, y, x + w, y + r);
        t.lineTo(x + w, y + h - r); t.quadTo(x + w, y + h, x + w - r, y + h);
        t.lineTo(x + r, y + h); t.quadTo(x, y + h, x, y + h - r);
        t.lineTo(x, y + r); t.quadTo(x, y, x + r, y); t.closePath();
    }
    static void roundedTop(IRenderTarget &t, double x, double y, double w, double h, double r)
    {
        t.beginPath();
        t.moveTo(x + r, y); t.lineTo(x + w - r, y); t.quadTo(x + w, y, x + w, y + r);
        t.lineTo(x + w, y + h); t.lineTo(x, y + h); t.lineTo(x, y + r);
        t.quadTo(x, y, x + r, y); t.closePath();
    }

    cosmo::EditSession &mSession;
    Detent mDetent = Detent::Half;
    int mTab = 0, mSection = 0;
    double mScreenW = 393, mScreenH = 852;
    double mNowMs = 0.0;
    std::vector<std::shared_ptr<ParamSlider>> mRows;

    friend class EditorScreen;
public:
    void setNow(double n) { mNowMs = n; }
};

// ── EditorScreen: stage bg + top bar + photo canvas + tray ───────────────────────
class EditorScreen : public Segment
{
public:
    explicit EditorScreen(cosmo::EditSession &s)
    {
        mPhoto = std::make_shared<ImageView>();
        mPhoto->setFit(ImageView::Fit::Contain);
        addChild(mPhoto);
        mTray = std::make_shared<Tray>(s);
        addChild(mTray);
    }

    void resize(double w, double h, double nowMs)
    {
        width.set(w); height.set(h);
        mPhoto->x.set(0); mPhoto->y.set(kTopBar);
        mPhoto->width.set(w); mPhoto->height.set(h - kTopBar);
        mTray->setNow(nowMs);
        mTray->configure(w, h, nowMs);
    }

    void setPhoto(const uint8_t *rgba, int w, int h) { mPhoto->setImage(rgba, w, h); }
    void syncControls() { mTray->syncFromSession(); }
    void setNow(double n) { mTray->setNow(n); }

protected:
    void onPaint(IRenderTarget &t) const override
    {
        // stage backdrop behind the photo
        t.setFill(pal::canvasBg());
        t.beginPath(); t.moveTo(0, kTopBar); t.lineTo(width.value(), kTopBar);
        t.lineTo(width.value(), height.value()); t.lineTo(0, height.value()); t.closePath(); t.fillPath();
        // top bar
        t.setFill(pal::background());
        t.beginPath(); t.moveTo(0, 0); t.lineTo(width.value(), 0);
        t.lineTo(width.value(), kTopBar); t.lineTo(0, kTopBar); t.closePath(); t.fillPath();
        t.setStroke(pal::border(), 1.0);
        t.beginPath(); t.moveTo(0, kTopBar); t.lineTo(width.value(), kTopBar); t.strokePath();
        // wordmark
        t.setFill(pal::foreground());
        t.drawText("cosmo", 16, kTopBar * 0.5 + 6, 18, fnt::sansSemiBold());
        t.setFill(pal::primary());
        t.drawText(".", 16 + 60, kTopBar * 0.5 + 6, 18, fnt::sansSemiBold());
    }

private:
    std::shared_ptr<ImageView> mPhoto;
    std::shared_ptr<Tray> mTray;
};

// ── PhoneApp ─────────────────────────────────────────────────────────────────────
PhoneApp::PhoneApp(double width, double height) : mW(width), mH(height)
{
    par::setThreads(4);
    mEditor = std::make_shared<EditorScreen>(mSession);
    mRoot = mEditor;
    mRoot->width.set(width);
    mRoot->height.set(height);
    mRecognizer.setSink([this](const Gesture &g) { mRoot->onGesture(g); });
    mEditor->resize(width, height, 0.0);
}

PhoneApp::~PhoneApp() = default;

void PhoneApp::setSize(double width, double height)
{
    mW = width; mH = height;
    mRoot->width.set(width);
    mRoot->height.set(height);
    mEditor->resize(width, height, mNowMs);
}

int PhoneApp::openImage(const uint8_t *rgba, int w, int h, const std::string &name)
{
    int slot = mSession.openImage(rgba, w, h, name);
    mSession.setUseGpu(true);   // prefer the GLES compute backend for the accelerated subset
    mEditor->syncControls();
    return slot;
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
    mRoot->advance(nowMs);
    mRoot->render(t);
    mRoot->renderOverlay(t);
}

void PhoneApp::pointer(int kind, double x, double y, int button, double timeMs,
                       bool alt, bool shift, bool ctrl)
{
    (void)shift; (void)ctrl;
    RawPointer rp;
    rp.kind = kind == 0 ? RawPointer::Kind::Down : kind == 2 ? RawPointer::Kind::Up : RawPointer::Kind::Move;
    rp.pos = Point{x, y};
    rp.button = button == 2 ? PointerButton::Right : PointerButton::Left;
    rp.timeMs = timeMs;
    rp.alt = alt;
    mRecognizer.feed(rp);
}

void PhoneApp::wheel(double x, double y, double delta, bool ctrl)
{
    (void)x; (void)y; (void)delta; (void)ctrl;  // pinch-zoom wired in a later milestone
}
}  // namespace cosmo_touch
}  // namespace arstro
