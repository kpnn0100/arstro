#include "PulsarApp.h"

namespace arstro
{
namespace pulsar
{
    using namespace artboard;

    namespace
    {
        const Color kBg = Color::hex(0x0b0d12);
        // Neutral chassis + meaning-only colour. Every audio-path section shares one
        // signal colour (OSC B a subtle cooler sibling of OSC A); the three modulation
        // sources carry the only other hues, so a mod ring genuinely pops.
        const Color kSignal = Color::hex(0x54cfe6);  // OSC A / sub / filter / gain (cyan)
        const Color kSignalB = Color::hex(0x62b8ee); // OSC B (cooler neighbouring blue)
        const Color kEnv = Color::hex(0xf2a13c);     // modulation: envelope (amber)
        const Color kLfo = Color::hex(0xac8bff);     // modulation: LFO (violet)
        const Color kMacro = Color::hex(0xf27ba6);   // modulation: macro (rose)

        // Give every Knob in the tree access to the shared modulation bus.
        void wireModBus(Segment *s, const ModBus *bus)
        {
            if (auto *k = dynamic_cast<Knob *>(s)) k->setModBus(bus);
            for (const auto &c : s->children()) wireModBus(c.get(), bus);
        }
        int countMods(Segment *s)
        {
            int n = 0;
            if (auto *k = dynamic_cast<Knob *>(s)) n += (int)k->modulations().size();
            for (const auto &c : s->children()) n += countMods(c.get());
            return n;
        }
        // Topmost visible Knob under a world point (deepest child first).
        Knob *knobAt(Segment *s, const Point &world)
        {
            const auto &ch = s->children();
            for (auto it = ch.rbegin(); it != ch.rend(); ++it)
            {
                Segment *c = it->get();
                if (!c->visible || !c->hitTest(world)) continue;
                if (Knob *deeper = knobAt(c, world)) return deeper;
                if (Knob *k = dynamic_cast<Knob *>(c)) return k;
            }
            return nullptr;
        }

        Theme makePulsarTheme(const Color &accent)
        {
            Theme th = Theme::basicTheme();
            const Color bg = Color::hex(0x0e111a), panel = Color::hex(0x181c28);
            const Color border = Color::hex(0x2b3242), ink = Color::hex(0xe6e9ef);
            const Color muted = Color::hex(0x848da0);
            const Color track = Color{1, 1, 1, 0.10}; // neutral knob/slider track

            th.knob.dial = {Paint::filled(bg), 999.0};
            th.knob.trackColor = track;
            th.knob.valueColor = accent;
            th.knob.indicatorColor = accent;
            th.knob.label = {muted, 9.0};

            th.toggle.trackOff = {Paint::filledStroked(panel, border, 1.0), 999.0};
            th.toggle.trackOn = {Paint::filledStroked(accent, accent, 1.0), 999.0};
            th.toggle.thumb = {Paint::filledStroked(ink, border, 1.0), 999.0};

            th.combo.field = {Paint::filledStroked(panel, border, 1.0), 6.0};
            th.combo.popup = {Paint::filledStroked(panel, accent, 1.0), 6.0};
            th.combo.rowSelected = {Paint::filled(Color::hex(0x22303f)), 0.0};
            th.combo.text = {ink, 13.0};
            th.combo.caretColor = accent;

            // POSITION timeline slider
            th.slider.track = {Paint::filledStroked(panel, border, 1.0), 7.0};
            th.slider.rangeFill = {Paint::filled(accent), 7.0};
            th.slider.thumb = {Paint::filledStroked(accent, accent, 2.0), 999.0}; // thumb = osc colour
            th.slider.thumbRadius = 7.0;
            return th;
        }
    }

    PulsarApp::PulsarApp(double width, double height)
        : mW(width), mH(height), mTitleColor(Color::hex(0x54cfe6))
    {
        mRoot = std::make_shared<Segment>();
        mRoot->width.set(width);
        mRoot->height.set(height);

        // Two oscillators: OSC A is the signal colour, OSC B a subtle cooler sibling.
        const Color accents[2] = {kSignal, kSignalB};
        for (int i = 0; i < 2; ++i)
        {
            Theme th = makePulsarTheme(accents[i]);
            mOsc[i] = std::make_shared<OscillatorPanel>("OSC" + std::to_string(i + 1), th, accents[i]);
        }

        // SUB + FILTER stack in a narrow column right of the oscillators, then a
        // GAIN output column (per-note gain + pan) — all one signal colour.
        const Color filterAccent = kSignal;
        const Color subAccent = kSignal;
        const Color gainAccent = kSignal;
        mSub = std::make_shared<SubOscPanel>(makePulsarTheme(subAccent), subAccent);
        mFilter = std::make_shared<FilterPanel>(makePulsarTheme(filterAccent), filterAccent);
        mGain = std::make_shared<GainPanel>(makePulsarTheme(gainAccent), gainAccent);
        auto sideCol = std::make_shared<Column>();
        sideCol->spacing = OscillatorPanel::kSectionGap;
        sideCol->addChild(mSub);
        sideCol->addChild(mFilter);

        // Top row: OSC1 OSC2, the SUB/FILTER column, then GAIN — left-to-right.
        auto topRow = std::make_shared<Row>();
        topRow->spacing = OscillatorPanel::kSectionGap;
        topRow->x.set(16.0);
        topRow->y.set(48.0);
        for (auto &o : mOsc)
            topRow->addChild(o);
        topRow->addChild(sideCol);
        topRow->addChild(mGain);
        mRoot->addChild(topRow);

        // modulation row: ENV + LFO + MACRO.
        const Color envAccent = kEnv;
        const Color lfoAccent = kLfo;
        const Color macroAccent = kMacro;
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

        // on-screen keyboard along the bottom — gates the LFOs (note-only motion).
        mKeyboard = std::make_shared<Keyboard>(mTitleColor);
        mKeyboard->x.set(16.0); mKeyboard->y.set(693.0);
        mKeyboard->width.set(width - 32.0); mKeyboard->height.set(84.0);
        LfoPanel *lfo = mLfo.get();
        EnvPanel *env = mEnv.get();
        mKeyboard->onGate = [lfo, env](bool on) { lfo->setGate(on); env->setGate(on); };
        mRoot->addChild(mKeyboard);

        // modulation: every knob reads the shared bus; dropping a source badge on a
        // knob routes that source to it (Serum-style).
        wireModBus(mRoot.get(), &mBus);
        Segment *root = mRoot.get();
        const int envId = mEnv->sourceId();
        auto assign = [root, envId](int sourceId, const Color &color, const Point &world) {
            if (Knob *k = knobAt(root, world)) // LFO bipolar; macro/env unipolar
                k->addModulation(sourceId, color, sourceId == envId ? 1.0 : 0.25, /*bipolar=*/sourceId < 100);
        };
        mLfo->setAssignSink(assign);
        mMacro->setAssignSink(assign);
        mEnv->setAssignSink(assign);

        // default routing: the amp envelope drives the GAIN (relinkable via its badge).
        mGain->gainKnob()->addModulation(mEnv->sourceId(), mEnv->accent(), 1.0, /*bipolar=*/false);

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

    int PulsarApp::modCount() const { return countMods(mRoot.get()); }

    void PulsarApp::render(IRenderTarget &target, double nowMs)
    {
        mRoot->advance(nowMs); // lays out the rows + ticks every animation

        // publish live modulation-source values so the knob rings track them this frame
        for (int i = 0; i < mLfo->count(); ++i)
            mBus.set(mLfo->sourceId(i), mLfo->output(i));
        for (int i = 0; i < mMacro->count(); ++i)
            mBus.set(mMacro->sourceId(i), mMacro->value(i));
        mBus.set(mEnv->sourceId(), mEnv->output());

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
        mRoot->renderOverlay(target);  // open dropdowns/popups, on top of everything
    }
}
}
