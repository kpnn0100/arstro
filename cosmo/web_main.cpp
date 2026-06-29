/*
 *  Web entry for Cosmo by arstro: wires CosmoApp to the Canvas2D web adapter.
 *  Images are decoded in JS (the browser); JS asks for an input buffer
 *  (allocInput), writes the RGBA8 bytes into the wasm heap, then calls loadImage.
 *  Owning the buffer C++-side avoids exporting _malloc/_free to JS. Compiled only
 *  by the web (emcc) build.
 */
#include "CosmoApp.h"
#include "../Artboard/src/adapter/web/Canvas2DTarget.h"
#include <emscripten.h>
#include <emscripten/bind.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace emscripten;
using arstro::cosmo::CosmoApp;

static std::unique_ptr<CosmoApp> gApp;
static artboard::Canvas2DTarget gTarget;
static std::vector<uint8_t> gInput;  // staging buffer JS writes decoded pixels into

static void init(double w, double h)
{
    gApp.reset(new CosmoApp(w, h));
    // File > Open on the web opens the browser file picker (the page's <input>).
    gApp->onOpenRequested = [] { EM_ASM({ var el = document.getElementById('file'); if (el) el.click(); }); };
}
static void frame(double nowMs) { if (gApp) gApp->render(gTarget, nowMs); }
static void pointer(int kind, double x, double y, int button, double t, bool alt) { if (gApp) gApp->pointer(kind, x, y, button, t, alt); }
static void resize(double w, double h) { if (gApp) gApp->setSize(w, h); }

// JS calls allocInput(bytes) -> heap pointer, fills HEAPU8 at it, then loadImage().
static uintptr_t allocInput(int bytes)
{
    if (bytes < 0) bytes = 0;
    gInput.resize((size_t)bytes);
    return reinterpret_cast<uintptr_t>(gInput.data());
}
static void loadImage(int w, int h, std::string name)
{
    if (gApp && w > 0 && h > 0 && gInput.size() >= (size_t)w * h * 4)
        gApp->openImage(gInput.data(), w, h, name);
}

EMSCRIPTEN_BINDINGS(arstro_cosmo)
{
    emscripten::function("init", &init);
    emscripten::function("frame", &frame);
    emscripten::function("pointer", &pointer);
    emscripten::function("resize", &resize);
    emscripten::function("allocInput", &allocInput);
    emscripten::function("loadImage", &loadImage);
}
