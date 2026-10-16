// R-VST-8: the plugins' own editor over the REAL VST3 controller, headless. A fake host records the
// edits the editor makes; the frames are written as PNGs for a look. Run by ctest as solaris_plugin_editor.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "PluginAccess.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "adapter/native/CairoTarget.h"
#include "widgets/DevicePanel.h"
#include "widgets/AnalogFace.h"
#include "base/AudioConfig.h"
#include "equalizer/StateVariableFilter.h"
#include "../../../cosmo/widgets/SliderRow.h"
#include <cairo/cairo.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Steinberg;
using arstro::solaris_ui::InstrumentEditor;

namespace
{
    int gPassed = 0;
    void pass(const char *what) { std::printf("[PASS] %s\n", what); ++gPassed; }

    /** A host's component handler: every edit, in order. */
    struct FakeHost : public Vst::IComponentHandler
    {
        struct Edit { char kind; Vst::ParamID id; Vst::ParamValue value; };
        std::vector<Edit> edits;
        tresult PLUGIN_API beginEdit(Vst::ParamID id) override { edits.push_back({'b', id, 0.0}); return kResultOk; }
        tresult PLUGIN_API performEdit(Vst::ParamID id, Vst::ParamValue v) override { edits.push_back({'p', id, v}); return kResultOk; }
        tresult PLUGIN_API endEdit(Vst::ParamID id) override { edits.push_back({'e', id, 0.0}); return kResultOk; }
        tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }
        tresult PLUGIN_API queryInterface(const TUID, void **obj) override { *obj = nullptr; return kNoInterface; }
        uint32 PLUGIN_API addRef() override { return 1; }
        uint32 PLUGIN_API release() override { return 1; }
        std::vector<Edit> of(Vst::ParamID id) const
        {
            std::vector<Edit> out;
            for (const auto &e : edits)
                if (e.id == id) out.push_back(e);
            return out;
        }
    };

    /** The editor's side of the plugin's access, recorded: the ENGINEERING value it chose for each step. */
    struct RecordingAccess : public arstro::solaris_ui::ParamAccess
    {
        explicit RecordingAccess(arstro::solaris_ui::ParamAccess &a) : inner(a) {}
        double value(int i) const override { return inner.value(i); }
        void begin(int i) override { inner.begin(i); }
        void perform(int i, double v) override { performed.emplace_back(i, v); inner.perform(i, v); }
        void end(int i) override { inner.end(i); }
        void play(int pitch, int velocity) override { played.emplace_back(pitch, velocity); inner.play(pitch, velocity); }
        arstro::solaris_ui::ParamAccess &inner;
        std::vector<std::pair<int, double>> performed;
        std::vector<std::pair<int, int>> played;
    };

    /** A plugin, its controller wired to the fake host, its editor drawn into an image — no window. */
    struct Rig
    {
        static constexpr double kFrameMs = 16.0;
        arstro::vst3::Controller controller;
        FakeHost host;
        arstro::solaris_ui::ControllerAccess access;
        RecordingAccess rec;
        InstrumentEditor editor;
        cairo_surface_t *surf = nullptr;
        cairo_t *cr = nullptr;
        artboard::CairoTarget target;
        double now = 0.0;

        explicit Rig(const char *type)
            : controller(arstro::vst3::PluginId{type, FUID(), FUID()}), access(controller), rec(access), editor(init(controller, host).type(), rec)
        {
            double w = 0, h = 0;
            editor.preferredSize(w, h); // what a host opens it at
            resize(w, h);
        }
        static arstro::vst3::Controller &init(arstro::vst3::Controller &c, FakeHost &h)
        {
            assert(c.initialize(nullptr) == kResultOk);
            c.setComponentHandler(&h);
            return c;
        }
        ~Rig()
        {
            if (cr) cairo_destroy(cr);
            if (surf) cairo_surface_destroy(surf);
            controller.setComponentHandler(nullptr);
            controller.terminate();
        }
        void resize(double w, double h)
        {
            if (cr) cairo_destroy(cr);
            if (surf) cairo_surface_destroy(surf);
            editor.setSize(w, h);
            surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, (int)editor.width(), (int)editor.height());
            cr = cairo_create(surf);
        }
        void frame()
        {
            target.setContext(cr);
            editor.render(target, now);
            cairo_surface_flush(surf);
            now += kFrameMs;
        }
        void settle() { for (int i = 0; i < 50; ++i) frame(); }
        void drag(double x0, double y, double x1, int steps, bool release = true)
        {
            editor.pointer(1, x0, y, 0, now);
            editor.pointer(0, x0, y, 0, now);
            frame();
            for (int k = 1; k <= steps; ++k)
            {
                editor.pointer(1, x0 + (x1 - x0) * k / steps, y, 0, now);
                frame();
            }
            if (release) { editor.pointer(2, x1, y, 0, now); frame(); }
        }
        /** A vertical drag — a knob's (R-UI-12): up turns it up. */
        void dragV(double x, double y0, double y1, int steps, bool release = true, bool shift = false)
        {
            editor.pointer(1, x, y0, 0, now, shift);
            editor.pointer(0, x, y0, 0, now, shift);
            frame();
            for (int k = 1; k <= steps; ++k)
            {
                editor.pointer(1, x, y0 + (y1 - y0) * k / steps, 0, now, shift);
                frame();
            }
            if (release) { editor.pointer(2, x, y1, 0, now, shift); frame(); }
        }
        void click(double x, double y)
        {
            editor.pointer(1, x, y, 0, now);
            editor.pointer(0, x, y, 0, now);
            editor.pointer(2, x, y, 0, now + 40.0);
            frame();
        }
        int index(const std::string &name) const
        {
            const auto &ps = controller.type().params;
            for (size_t i = 0; i < ps.size(); ++i)
                if (ps[i].name == name) return (int)i;
            return -1;
        }
        int row(const std::string &name)
        {
            auto &p = editor.panel();
            for (int i = 0; i < p.rowCount(); ++i)
                if (p.rowParam(i) == name) return i;
            return -1;
        }
        void write(const std::string &name) { cairo_surface_write_to_png(surf, (name + ".png").c_str()); }
    };

    void test_the_synth_editor_is_the_registry_in_the_arstro_look()
    {
        // R-UI-12: the synth's editor is its analog face — a module per registry group, titled by the registry,
        // a knob per number, a switch per choice, a screen where the parameters say — at its own size
        Rig r("synth");
        r.settle();
        auto &p = r.editor.panel();
        auto *f = p.face();
        const auto &t = r.controller.type();
        assert(f && p.rowCount() == (int)t.params.size());                      // every registry parameter (law 2)
        assert(f->isKnob(f->controlIndex("filter.cutoff")) && !f->isKnob(f->controlIndex("osc1.wave"))); // a choice is a switch
        assert(r.editor.width() == InstrumentEditor::kFaceWidth && r.editor.height() > 400.0 && r.editor.height() < 700.0);
        // the DSP's REQ-synth2-7/-8 brought Voice (mode, glide, bend) and Vibrato with no Solaris code (law 2)
        const char *titles[] = {"Oscillator 1", "Oscillator 2", "Filter", "Filter Envelope", "Amplifier", "Voice", "Vibrato", "Mix & Output"};
        using S = arstro::solaris_ui::AnalogFace::Screen;
        const S screens[] = {S::Wave, S::Wave, S::Filter, S::Envelope, S::Envelope, S::None, S::None, S::None};
        assert(f->sectionCount() == 8);
        for (int k = 0; k < 8; ++k) assert(f->sectionTitle(k) == titles[k] && f->sectionScreen(k) == screens[k]);
        // at its width the modules share rows: the sources and the filter on one; the envelopes, the voice and the
        // vibrato on the next; the mix on the last
        assert(f->sectionRect(0).y == f->sectionRect(2).y && f->sectionRect(3).y > f->sectionRect(2).y && f->sectionRect(3).y == f->sectionRect(6).y);
        assert(f->sectionRect(7).y > f->sectionRect(6).y);
        assert(!p.songControls && r.editor.model().strips[0].devices[0].label == "Basic Synth");
        assert(f->contentHeight() <= p.height.value() - p.bodyTop() + 1.0); // no scroll at its own size
        r.write("plugin-editor-synth");
        r.resize(InstrumentEditor::kMinWidth, InstrumentEditor::kMinHeight); // R4: the least a host may make it — the modules wrap
        r.settle();
        assert(r.editor.width() == InstrumentEditor::kMinWidth && p.width.value() == InstrumentEditor::kMinWidth);
        // the modules wrap: none spills past the width; the wide ones stand alone, two small ones may share a row
        for (int k = 0; k < f->sectionCount(); ++k)
            assert(f->sectionRect(k).x >= arstro::solaris_ui::AnalogFace::kPad && f->sectionRect(k).right() <= p.width.value() - arstro::solaris_ui::AnalogFace::kPad + 0.5);
        assert(f->sectionRect(0).x == arstro::solaris_ui::AnalogFace::kPad && f->sectionRect(1).x == arstro::solaris_ui::AnalogFace::kPad);
        r.write("plugin-editor-synth-small");
        pass("the synth's editor is its analog face: eight modules titled by the registry, knobs and switches, wave/filter/envelope screens; three rows at its own size (980 wide), wrapped at its minimum with nothing spilling (R-UI-12)");
    }

    void test_a_drag_is_the_hosts_edit_with_the_shared_mapping()
    {
        Rig r("synth");
        r.settle();
        auto &p = r.editor.panel();
        auto *f = p.face();
        const int cut = r.index("filter.cutoff");
        const auto &spec = r.controller.type().params[(size_t)cut];
        const artboard::Rect dial = f->dialRect(f->controlIndex("filter.cutoff"));
        const double x = f->x.value() + dial.x + dial.w * 0.5, y = f->y.value() + dial.y + dial.h * 0.5;
        r.dragV(x, y, y - 60.0, 6, false);                                      // up: turned up
        // held: begun ONCE, performed per step, not ended; the knob is the pointer's
        auto e = r.host.of((Vst::ParamID)cut);
        assert(e.size() >= 3 && e.front().kind == 'b' && e.back().kind == 'p');
        int begins = 0;
        for (const auto &x2 : e) begins += x2.kind == 'b' ? 1 : 0;
        assert(begins == 1 && f->dragging() && f->turnLive("filter.cutoff") == f->turnTarget("filter.cutoff"));
        r.editor.pointer(2, x, y - 60.0, 0, r.now);
        r.frame();
        e = r.host.of((Vst::ParamID)cut);
        assert(e.back().kind == 'e');
        // each step the host is told IS the shared mapping of the engineering value the face chose — to the last digit
        std::vector<double> chose;
        for (const auto &pv : r.rec.performed)
            if (pv.first == cut) chose.push_back(pv.second);
        double last = -1.0;
        size_t k = 0;
        for (const auto &x2 : e)
            if (x2.kind == 'p')
            {
                assert(k < chose.size() && x2.value == arstro::normalizedFromValue(spec, chose[k]));
                ++k;
                assert(x2.value > last); // turned up, and never the same value twice
                last = x2.value;
            }
        assert(k == chose.size() && k >= 2);
        assert(r.controller.getParamNormalized((Vst::ParamID)cut) == last); // the controller holds what the host was told
        assert(r.editor.model().strips[0].devices[0].lastChanged == "filter.cutoff");
        // Shift is five times finer
        const double t0 = f->turnLive("filter.res");
        const artboard::Rect rd = f->dialRect(f->controlIndex("filter.res"));
        const double rx = f->x.value() + rd.x + rd.w * 0.5, ry = f->y.value() + rd.y + rd.h * 0.5;
        r.dragV(rx, ry, ry - 39.0, 4, true, true);
        const double fine = f->turnLive("filter.res") - t0;
        assert(std::fabs(fine - 0.2 * 39.0 / arstro::solaris_ui::AnalogFace::kTurnPx) < 0.01);
        // a switch's key: that choice — begun, performed, ended at once
        const int wave = r.index("osc1.wave");
        const int wi = f->controlIndex("osc1.wave");
        const artboard::Rect key = f->keyRect(wi, 2);                             // square
        r.click(f->x.value() + key.x + key.w * 0.5, f->y.value() + key.y + key.h * 0.5);
        r.frame();
        const auto w = r.host.of((Vst::ParamID)wave);
        assert(w.size() == 3 && w[0].kind == 'b' && w[1].kind == 'p' && w[2].kind == 'e');
        assert(r.controller.plainValue(wave) == 2.0);
        pass("a knob dragged up is the host's edit — beginEdit once, performEdit per step with the shared mapping's value, endEdit on release; Shift five times finer; a switch's key is that choice, begun/performed/ended (R-VST-7, R-UI-12)");
    }

    void test_a_host_change_eases_in_and_is_lit()
    {
        Rig r("synth");
        r.settle();
        auto &p = r.editor.panel();
        auto *f = p.face();
        const int cut = r.index("filter.cutoff");
        const auto &spec = r.controller.type().params[(size_t)cut];
        const double before = f->turnLive("filter.cutoff");
        // the host moves it (automation, a preset): the knob turns there, it does not jump
        r.controller.setParamNormalized((Vst::ParamID)cut, arstro::normalizedFromValue(spec, spec.max));
        r.frame();
        r.frame();
        const double target = f->turnTarget("filter.cutoff");
        assert(target == 1.0 && f->turnLive("filter.cutoff") > before && f->turnLive("filter.cutoff") < target); // caught mid-turn
        // a choice changed by the host: the switch's light slides
        const int mode = r.index("filter.mode");
        r.controller.setParamNormalized((Vst::ParamID)mode, arstro::normalizedFromValue(r.controller.type().params[(size_t)mode], 2.0));
        r.frame();
        r.frame();
        assert(f->lightLive("filter.mode") > 0.0 && f->lightLive("filter.mode") < 2.0);
        r.settle();
        assert(std::fabs(f->turnLive("filter.cutoff") - target) < 1e-9 && f->lightLive("filter.mode") == 2.0);
        assert(r.editor.model().strips[0].devices[0].lastChanged == "filter.mode" && p.litAmount("filter.mode") > 0.5 && p.litAmount("filter.cutoff") < 0.5);
        assert(r.host.edits.empty()); // the host's own change is not echoed back as an edit
        pass("a value the host changes turns its knob there (caught mid-turn), a choice's light slides, the last change is lit; nothing echoed back (R-VST-7, R-UI-12, design rule §1)");
    }

    void test_the_filter_screen_is_the_filter()
    {
        // R-UI-12: the filter screen draws AnalogFace::responseDb — it must be what the DSP library's filter does.
        // Measured: a sine through the real StateVariableFilter, its gain against the screen's formula at the
        // prewarped frequency (the filter's (S6): the digital response IS the analog one at ω_a)
        arstro::AudioConfig::instance().setSampleRate(48000.0);
        const double fs = 48000.0, fc = 1000.0;
        const char *modes[] = {"lowpass", "bandpass", "highpass", "notch"};
        double worst = 0.0;
        for (int m = 0; m < 4; ++m)
            for (double res : {0.0, 0.3, 0.7})
                for (double f : {250.0, 700.0, 1000.0, 1500.0, 4000.0})
                {
                    if (m == 3 && std::fabs(f - fc) < 1.0) continue; // the notch's floor: −∞ dB, nothing to compare
                    arstro::StateVariableFilter svf;
                    svf.setMode((arstro::StateVariableFilter::Mode)m);
                    svf.setResonance(res);
                    // the steady-state amplitude by RMS over the second half (a sampled peak misses a shifted crest)
                    double sum = 0.0;
                    const int n = 48000;
                    for (int i = 0; i < n; ++i)
                    {
                        const double y = svf.tick(std::sin(2.0 * 3.14159265358979 * f * i / fs), 0, fc);
                        if (i >= n / 2) sum += y * y;
                    }
                    const double peak = std::sqrt(2.0 * sum / (n / 2));
                    const double wa = std::tan(3.14159265358979 * f / fs) / std::tan(3.14159265358979 * fc / fs);
                    const double drawn = arstro::solaris_ui::AnalogFace::responseDb(modes[m], wa, res);
                    const double err = std::fabs(20.0 * std::log10(peak) - drawn);
                    if (err > 0.1) std::fprintf(stderr, "    %s res %.1f at %.0f Hz: measured %.3f dB, drawn %.3f dB\n", modes[m], res, f, 20.0 * std::log10(peak), drawn);
                    worst = std::max(worst, err);
                }
        std::printf("    the filter screen against the filter: worst %.3f dB\n", worst);
        assert(worst < 0.1);
        pass("the filter screen is the filter: AnalogFace::responseDb against the DSP's StateVariableFilter, 4 modes × 3 resonances × 5 frequencies, within 0.1 dB (R-UI-12)");
    }

    void test_the_drum_machine_editor_builds()
    {
        Rig r("drums");
        r.settle();
        assert(r.editor.panel().rowCount() == (int)r.controller.type().params.size() && r.editor.model().strips[0].devices[0].label == "Drum Machine");
        r.write("plugin-editor-drums");
        Rig s("synth");
        assert(s.editor.pads() == nullptr); // a melodic instrument has no pads
        pass("the drum machine's editor: its registry's rows, titled Drum Machine; a synth's has no pads");
    }

    void test_a_pad_plays_and_shows_its_parameters()
    {
        // R-VST-7: the kit's pads — named by the registry, in its order; a press plays the pad through the
        // plugin and picks it (the panel scrolls to what shapes it); the release is its note-off
        Rig r("drums");
        r.resize(InstrumentEditor::kFaceWidth, InstrumentEditor::kMinHeight); // shorter than its face: the pads' modules scroll
        r.settle();
        auto *pads = r.editor.pads();
        const auto &t = r.controller.type();
        assert(pads && pads->padCount() == (int)t.noteNames.size());
        int hat = -1;
        for (int i = 0; i < pads->padCount(); ++i)
        {
            assert(pads->padName(i) == t.noteNames[(size_t)i].second && pads->padNote(i) == t.noteNames[(size_t)i].first);
            if (pads->padName(i) == "Closed Hat") hat = i;
        }
        assert(hat >= 0 && pads->padPrefix(hat) == "chat"); // the registry joins the key to its parameters
        auto &p = r.editor.panel();
        auto rowOf = [&](const std::string &n) { return p.rowRect(r.row(n)); };
        assert(rowOf("chat.tune").y > p.height.value());    // below the fold at first
        artboard::Rect pr = pads->padRect(hat);
        pr.y += pads->y.value();                             // the grid sits in the panel's band, under its title
        pr.x += pads->x.value();
        r.editor.pointer(1, pr.x + pr.w * 0.5, pr.y + pr.h * 0.5, 0, r.now);
        r.editor.pointer(0, pr.x + pr.w * 0.5, pr.y + pr.h * 0.5, 0, r.now);
        assert(r.rec.played.size() == 1 && r.rec.played[0] == std::make_pair(42, 100)); // heard on the press
        r.frame();
        r.frame();
        assert(pads->flashAmount(hat) > 0.0 && pads->flashAmount(hat) < 1.0);   // the hit, dying away
        assert(pads->pickAmount(hat) > 0.0 && pads->pickAmount(hat) < 1.0);     // the pick, easing in
        assert(pads->picked() == hat);
        r.editor.pointer(2, pr.x + pr.w * 0.5, pr.y + pr.h * 0.5, 0, r.now + 60.0);
        r.frame();
        assert(r.rec.played.size() == 2 && r.rec.played[1] == std::make_pair(42, 0)); // its note-off on release
        r.settle();
        assert(pads->pickAmount(hat) == 1.0 && pads->flashAmount(hat) == 0.0);
        const artboard::Rect row = rowOf("chat.tune");
        // scrolled so the hat's group begins at the top of the rows (its header, then `chat.tune`)
        assert(row.y >= p.bodyTop() - 1.0 && row.y < p.bodyTop() + 3.0 * arstro::cosmo_v2::SliderRow::kRowHeight);
        assert(pads->y.value() == arstro::solaris_ui::DevicePanel::kHeaderH && p.band == pads->contentHeight());
        assert(r.host.edits.empty()); // playing a pad edits nothing
        r.write("plugin-editor-drums-pads");
        pass("a pad: named by the registry, heard through the plugin on the press (42 at 100) and released (42 at 0), its flash and its pick caught easing, the panel scrolled to `chat.*`; no edit (R-VST-7)");
    }

    void test_a_pad_is_heard_through_the_processor()
    {
        // R-VST-7 end to end, in process: the editor's pad → the controller's message → the host's connection →
        // the processor's queue → a note at the next block. The SDK's own host classes carry the message.
        IPtr<Vst::HostApplication> app = owned(new Vst::HostApplication());
        const arstro::vst3::PluginId id{"drums", FUID(), FUID()};
        IPtr<arstro::vst3::Processor> proc = owned(new arstro::vst3::Processor(id));
        IPtr<arstro::vst3::Controller> ctl = owned(new arstro::vst3::Controller(id));
        assert(proc->initialize(app) == kResultOk && ctl->initialize(app) == kResultOk);
        assert(proc->connect(ctl) == kResultOk && ctl->connect(proc) == kResultOk); // what a host does for the two halves
        Vst::SpeakerArrangement out = Vst::SpeakerArr::kStereo;
        Vst::ProcessSetup setup{Vst::kRealtime, Vst::kSample32, 256, 48000.0};
        assert(proc->setBusArrangements(nullptr, 0, &out, 1) == kResultOk && proc->setupProcessing(setup) == kResultOk);
        proc->activateBus(Vst::kAudio, Vst::kOutput, 0, true);
        proc->activateBus(Vst::kEvent, Vst::kInput, 0, true);
        assert(proc->setActive(true) == kResultOk);
        proc->setProcessing(true);
        Vst::HostProcessData data;
        data.prepare(*proc, 256, Vst::kSample32);
        auto blocks = [&](int n) {
            double peak = 0.0;
            for (int b = 0; b < n; ++b)
            {
                data.numSamples = 256;
                data.inputEvents = nullptr;
                data.inputParameterChanges = nullptr;
                proc->process(data);
                for (int i = 0; i < 256; ++i) peak = std::max(peak, (double)std::fabs(data.outputs[0].channelBuffers32[0][i]));
            }
            return peak;
        };
        arstro::solaris_ui::ControllerAccess access(*ctl);
        InstrumentEditor editor(ctl->type(), access);
        cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, (int)editor.width(), (int)editor.height());
        cairo_t *cr = cairo_create(surf);
        artboard::CairoTarget target(cr);
        double now = 0.0;
        for (int i = 0; i < 30; ++i) { editor.render(target, now); now += 16.0; }
        assert(blocks(4) == 0.0); // silence until a pad is played
        auto *pads = editor.pads();
        artboard::Rect kick = pads->padRect(0);
        kick.x += pads->x.value();
        kick.y += pads->y.value();
        editor.pointer(1, kick.x + 10.0, kick.y + 10.0, 0, now);
        editor.pointer(0, kick.x + 10.0, kick.y + 10.0, 0, now);
        const double heard = blocks(8);
        editor.pointer(2, kick.x + 10.0, kick.y + 10.0, 0, now + 50.0);
        std::printf("    the kick pad, through the processor: peak %.3f\n", heard);
        assert(heard > 0.05);
        cairo_destroy(cr);
        cairo_surface_destroy(surf);
        proc->setProcessing(false);
        proc->setActive(false);
        ctl->disconnect(proc);
        proc->disconnect(ctl);
        ctl->terminate();
        proc->terminate();
        pass("a pad is heard: editor → controller → host message → processor → the kick at the next block, where there was silence (R-VST-7, REQ-vst-7)");
    }
}

int main()
{
    test_the_synth_editor_is_the_registry_in_the_arstro_look();
    test_a_drag_is_the_hosts_edit_with_the_shared_mapping();
    test_a_host_change_eases_in_and_is_lit();
    test_the_filter_screen_is_the_filter();
    test_the_drum_machine_editor_builds();
    test_a_pad_plays_and_shows_its_parameters();
    test_a_pad_is_heard_through_the_processor();
    std::printf("\n%d passed, 0 failed\n", gPassed);
    return 0;
}
