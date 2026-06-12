/*
 *  Web entry for the Studio example: wires StudioApp to the Canvas2D web adapter
 *  and exposes init/frame/audio/notes to JS. Compiled only by the web (emcc) build.
 */
#include "StudioApp.h"
#include "../../Artboard/src/adapter/web/Canvas2DTarget.h"
#include <emscripten/bind.h>
#include <memory>
#include <vector>

using namespace emscripten;
using namespace arstro::examples;

static std::unique_ptr<StudioApp> gApp;
static artboard::Canvas2DTarget gTarget;
static std::vector<float> gAudio;

static void init(double w, double h, double sr) { gApp.reset(new StudioApp(w, h)); gApp->setSampleRate(sr); }
static void frame(double nowMs) { if (gApp) gApp->render(gTarget, nowMs); }
static void noteOn(int midi, double vel) { if (gApp) gApp->noteOn(midi, vel); }
static void noteOff(int midi) { if (gApp) gApp->noteOff(midi); }
static void pointer(int kind, double x, double y, int button, double t) { if (gApp) gApp->pointer(kind, x, y, button, t); }
static uintptr_t renderAudio(int frames)
{
    gAudio.resize((size_t)frames * 2);
    if (gApp) gApp->renderAudio(gAudio.data(), frames);
    return reinterpret_cast<uintptr_t>(gAudio.data());
}

EMSCRIPTEN_BINDINGS(arstro_studio)
{
    emscripten::function("init", &init);
    emscripten::function("frame", &frame);
    emscripten::function("noteOn", &noteOn);
    emscripten::function("noteOff", &noteOff);
    emscripten::function("pointer", &pointer);
    emscripten::function("renderAudio", &renderAudio, allow_raw_pointers());
}
