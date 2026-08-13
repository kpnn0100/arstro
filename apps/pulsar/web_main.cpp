/*
 *  Web entry for Pulsar by arstro: wires PulsarApp to the Canvas2D web adapter.
 *  UI-only (no audio yet). Compiled only by the web (emcc) build.
 */
#include "PulsarApp.h"
#include "../../core/Artboard/src/adapter/web/Canvas2DTarget.h"
#include <emscripten/bind.h>
#include <memory>

using namespace emscripten;
using namespace arstro::pulsar;

static std::unique_ptr<PulsarApp> gApp;
static artboard::Canvas2DTarget gTarget;

static void init(double w, double h) { gApp.reset(new PulsarApp(w, h)); }
static void frame(double nowMs) { if (gApp) gApp->render(gTarget, nowMs); }
static void pointer(int kind, double x, double y, int button, double t) { if (gApp) gApp->pointer(kind, x, y, button, t); }

EMSCRIPTEN_BINDINGS(arstro_pulsar)
{
    emscripten::function("init", &init);
    emscripten::function("frame", &frame);
    emscripten::function("pointer", &pointer);
}
