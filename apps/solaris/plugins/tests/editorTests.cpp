// R-VST-8: the plugins' own editor over the REAL VST3 controller, headless. A fake host records the
// edits the editor makes; the frames are written as PNGs for a look. Run by ctest as solaris_plugin_editor.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "PluginAccess.h"
#include "adapter/native/CairoTarget.h"
#include "widgets/DevicePanel.h"
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
        arstro::solaris_ui::ParamAccess &inner;
        std::vector<std::pair<int, double>> performed;
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
            resize(InstrumentEditor::kWidth, InstrumentEditor::kHeight);
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
        Rig r("synth");
        r.settle();
        auto &p = r.editor.panel();
        const auto &t = r.controller.type();
        assert(p.rowCount() == (int)t.params.size());                          // a row per registry parameter (law 2)
        assert(p.slider("filter.cutoff") != nullptr && p.slider("osc1.wave") == nullptr); // a choice is not a slider
        assert(!p.songControls && r.editor.model().strips[0].devices[0].label == "Basic Synth");
        r.write("plugin-editor-synth");
        r.resize(InstrumentEditor::kMinWidth, InstrumentEditor::kMinHeight); // R4: the least a host may make it
        r.settle();
        assert(r.editor.width() == InstrumentEditor::kMinWidth && p.width.value() == InstrumentEditor::kMinWidth);
        r.write("plugin-editor-synth-small");
        pass("the synth's editor: a row per registry parameter, a slider per number, a choice per choice, titled Basic Synth; laid out at its size and its minimum");
    }

    void test_a_drag_is_the_hosts_edit_with_the_shared_mapping()
    {
        Rig r("synth");
        r.settle();
        auto &p = r.editor.panel();
        const int cut = r.index("filter.cutoff");
        const auto &spec = r.controller.type().params[(size_t)cut];
        p.reveal("filter.cutoff");
        r.settle();
        const artboard::Rect row = p.rowRect(r.row("filter.cutoff"));
        const double sx = row.x + arstro::cosmo_v2::SliderRow::kLabelWidth + 20.0, y = row.y + row.h * 0.5;
        r.drag(sx, y, sx + 60.0, 6, false);
        // held: begun ONCE, performed per step, not ended
        auto e = r.host.of((Vst::ParamID)cut);
        assert(e.size() >= 3 && e.front().kind == 'b' && e.back().kind == 'p');
        int begins = 0;
        for (const auto &x : e) begins += x.kind == 'b' ? 1 : 0;
        assert(begins == 1);
        r.editor.pointer(2, sx + 60.0, y, 0, r.now);
        r.frame();
        e = r.host.of((Vst::ParamID)cut);
        assert(e.back().kind == 'e');
        // each step the host is told IS the shared mapping of the engineering value the panel chose — to the last digit
        std::vector<double> chose;
        for (const auto &pv : r.rec.performed)
            if (pv.first == cut) chose.push_back(pv.second);
        double last = -1.0;
        size_t k = 0;
        for (const auto &x : e)
            if (x.kind == 'p')
            {
                assert(k < chose.size() && x.value == arstro::normalizedFromValue(spec, chose[k]));
                ++k;
                assert(x.value > last); // dragged right: up, and never the same value twice
                last = x.value;
            }
        assert(k == chose.size() && k >= 2);
        assert(r.controller.getParamNormalized((Vst::ParamID)cut) == last); // the controller holds what the host was told
        assert(r.editor.model().strips[0].devices[0].lastChanged == "filter.cutoff");
        // a choice: a click on its right half is the NEXT name — begun, performed, ended at once
        const int wave = r.index("osc1.wave");
        p.reveal("osc1.wave");
        r.settle();
        const artboard::Rect wr = p.rowRect(r.row("osc1.wave"));
        const double before = r.controller.plainValue(wave);
        r.click(wr.right() - 12.0, wr.y + wr.h * 0.5);
        r.frame();
        const auto w = r.host.of((Vst::ParamID)wave);
        assert(w.size() == 3 && w[0].kind == 'b' && w[1].kind == 'p' && w[2].kind == 'e');
        assert(r.controller.plainValue(wave) == before + 1.0);
        pass("a slider dragged is the host's edit — beginEdit once, performEdit per step with the shared mapping's value (to the last digit), endEdit on release; a choice clicked is the next name, begun/performed/ended (R-VST-7)");
    }

    void test_a_host_change_eases_in_and_is_lit()
    {
        Rig r("synth");
        r.settle();
        auto &p = r.editor.panel();
        const int cut = r.index("filter.cutoff");
        const auto &spec = r.controller.type().params[(size_t)cut];
        auto *s = p.slider("filter.cutoff");
        const double before = s->displayValue();
        // the host moves it (automation, a preset): the thumb springs there, it does not jump
        r.controller.setParamNormalized((Vst::ParamID)cut, arstro::normalizedFromValue(spec, spec.max));
        r.frame();
        r.frame();
        const double target = s->value();
        assert(target != before && s->displayValue() != before && s->displayValue() != target);
        assert(std::fabs(s->displayValue() - before) < std::fabs(target - before)); // between: caught mid-ease
        r.settle();
        assert(std::fabs(s->displayValue() - target) < 1e-3);
        assert(r.editor.model().strips[0].devices[0].lastChanged == "filter.cutoff" && p.litAmount("filter.cutoff") > 0.5);
        assert(r.host.edits.empty()); // the host's own change is not echoed back as an edit
        pass("a value the host changes eases in (the thumb caught between) and its row is lit; nothing echoed back to the host (R-VST-7, design rule §1)");
    }

    void test_the_drum_machine_editor_builds()
    {
        Rig r("drums");
        r.settle();
        assert(r.editor.panel().rowCount() == (int)r.controller.type().params.size() && r.editor.model().strips[0].devices[0].label == "Drum Machine");
        r.write("plugin-editor-drums");
        pass("the drum machine's editor: its registry's rows, titled Drum Machine");
    }
}

int main()
{
    test_the_synth_editor_is_the_registry_in_the_arstro_look();
    test_a_drag_is_the_hosts_edit_with_the_shared_mapping();
    test_a_host_change_eases_in_and_is_lit();
    test_the_drum_machine_editor_builds();
    std::printf("\n%d passed, 0 failed\n", gPassed);
    return 0;
}
