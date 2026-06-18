#include "PulsarApp.h"

namespace arstro
{
namespace pulsar
{
    using namespace artboard;

    namespace
    {
        const Color kBg = Color::hex(0x0a0c11);

        Theme makePulsarTheme(const Color &accent)
        {
            Theme th = Theme::basicTheme();
            const Color bg = Color::hex(0x0c0e13), panel = Color::hex(0x161a23);
            const Color border = Color::hex(0x2a3040), ink = Color::hex(0xe6e9ef);
            const Color muted = Color::hex(0x8b94a7), surface = Color::hex(0x222838);

            th.knob.dial = {Paint::filled(bg), 999.0};
            th.knob.trackColor = surface;
            th.knob.valueColor = accent;
            th.knob.indicatorColor = accent;
            th.knob.label = {muted, 9.0};

            th.toggle.trackOff = {Paint::filledStroked(surface, border, 1.0), 999.0};
            th.toggle.trackOn = {Paint::filledStroked(accent, accent, 1.0), 999.0};
            th.toggle.thumb = {Paint::filledStroked(ink, border, 1.0), 999.0};

            th.combo.field = {Paint::filledStroked(panel, border, 1.0), 6.0};
            th.combo.popup = {Paint::filledStroked(panel, accent, 1.0), 6.0};
            th.combo.rowSelected = {Paint::filled(Color::hex(0x2a3a4a)), 0.0};
            th.combo.text = {ink, 13.0};
            th.combo.caretColor = accent;

            // POSITION timeline slider
            th.slider.track = {Paint::filledStroked(surface, border, 1.0), 7.0};
            th.slider.rangeFill = {Paint::filled(accent), 7.0};
            th.slider.thumb = {Paint::filledStroked(accent, accent, 2.0), 999.0}; // thumb = osc colour
            th.slider.thumbRadius = 7.0;
            return th;
        }
    }

    PulsarApp::PulsarApp(double width, double height)
        : mW(width), mH(height), mTitleColor(Color::hex(0x4de2ff))
    {
        mRoot = std::make_shared<Segment>();
        mRoot->width.set(width);
        mRoot->height.set(height);

        // Per-oscillator accent.
        const Color accents[3] = {Color::hex(0xe32272), Color::hex(0x22e3dd), Color::hex(0xedc61a)};
        for (int i = 0; i < 3; ++i)
        {
            Theme th = makePulsarTheme(accents[i]);
            mOsc[i] = std::make_shared<OscillatorPanel>("OSC" + std::to_string(i + 1), th, accents[i]);
        }

        // SUB + FILTER stack in a narrow column to the RIGHT of OSC3.
        const Color filterAccent = Color::hex(0x35d0a0); // green
        const Color subAccent = Color::hex(0x8a93ff);    // indigo
        mSub = std::make_shared<SubOscPanel>(makePulsarTheme(subAccent), subAccent);
        mFilter = std::make_shared<FilterPanel>(makePulsarTheme(filterAccent), filterAccent);
        auto sideCol = std::make_shared<Column>();
        sideCol->spacing = OscillatorPanel::kSectionGap;
        sideCol->addChild(mSub);
        sideCol->addChild(mFilter);

        // Top row: OSC1 OSC2 OSC3 then the SUB/FILTER column, all left-to-right.
        auto topRow = std::make_shared<Row>();
        topRow->spacing = OscillatorPanel::kSectionGap;
        topRow->x.set(16.0);
        topRow->y.set(48.0);
        for (auto &o : mOsc)
            topRow->addChild(o);
        topRow->addChild(sideCol);
        mRoot->addChild(topRow);

        // modulation row: ENV + LFO + MACRO.
        const Color envAccent = Color::hex(0xff8a3d);   // orange
        const Color lfoAccent = Color::hex(0xb46bff);   // purple
        const Color macroAccent = Color::hex(0xffcf3d); // yellow
        mEnv = std::make_shared<EnvPanel>(makePulsarTheme(envAccent), envAccent, "ENV");
        mLfo = std::make_shared<LfoPanel>(makePulsarTheme(lfoAccent), lfoAccent);
        mMacro = std::make_shared<MacroPanel>(makePulsarTheme(macroAccent), macroAccent);
        auto modRow = std::make_shared<Row>();
        modRow->spacing = OscillatorPanel::kSectionGap;
        modRow->x.set(16.0);
        modRow->y.set(417.0);
        modRow->addChild(mEnv);
        modRow->addChild(mLfo);
        modRow->addChild(mMacro);
        mRoot->addChild(modRow);

        mRecognizer.setSink([this](const Gesture &g) { mRoot->onGesture(g); });
    }

    void PulsarApp::pointer(int kind, double x, double y, int button, double timeMs)
    {
        RawPointer::Kind k = kind == 0 ? RawPointer::Kind::Down
                             : kind == 2 ? RawPointer::Kind::Up
                                         : RawPointer::Kind::Move;
        PointerButton b = button == 2 ? PointerButton::Right : PointerButton::Left;
        mRecognizer.feed(RawPointer{k, Point{x, y}, b, timeMs});
    }

    void PulsarApp::render(IRenderTarget &target, double nowMs)
    {
        mRoot->advance(nowMs); // resolves the snap chain (OSC3 -> OSC2 -> OSC1)

        // background + title
        target.save();
        target.setTransform(Transform::identity());
        drawRoundedRect(target, Rect{0, 0, mW, mH}, 0.0, Paint::filled(kBg));
        target.setFill(mTitleColor);
        target.drawText("PULSAR", 18.0, 30.0, 22.0);
        target.setFill(Color{1, 1, 1, 0.35});
        target.drawText("by arstro", 110.0, 30.0, 12.0);
        target.restore();

        mRoot->render(target);
    }
}
}
