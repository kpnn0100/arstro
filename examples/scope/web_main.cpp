/*
 *  Web entry for the Scope example: wires the platform-free ScopeApp to the web
 *  adapter (Canvas2DTarget) and exposes init/frame/audio/notes to JS via embind.
 *  Compiled only by the web (emcc) build.
 */
#include "ScopeApp.h"
#include "../../Artboard/src/adapter/web/Canvas2DTarget.h"
#include <emscripten/bind.h>
#include <memory>
#include <vector>

using namespace emscripten;
using namespace arstro::examples;

static std::unique_ptr<ScopeApp> gApp;
static artboard::Canvas2DTarget gTarget;
static std::vector<float> gAudio;

static void init(double w, double h, double sr)
{
    gApp.reset(new ScopeApp(w, h));
    gApp->setSampleRate(sr);
}
static void frame(double nowMs) { if (gApp) gApp->render(gTarget, nowMs); }
static void noteOn(int midi, double vel) { if (gApp) gApp->noteOn(midi, vel); }
static void noteOff(int midi) { if (gApp) gApp->noteOff(midi); }

// Render `frames` interleaved stereo floats; return heap address for HEAPF32.
static uintptr_t renderAudio(int frames)
{
    gAudio.resize((size_t)frames * 2);
    if (gApp) gApp->renderAudio(gAudio.data(), frames);
    return reinterpret_cast<uintptr_t>(gAudio.data());
}

EMSCRIPTEN_BINDINGS(arstro_scope)
{
    emscripten::function("init", &init);
    emscripten::function("frame", &frame);
    emscripten::function("noteOn", &noteOn);
    emscripten::function("noteOff", &noteOff);
    emscripten::function("renderAudio", &renderAudio, allow_raw_pointers());
}
