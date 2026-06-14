#include "UiDemoApp.h"
#include "../../Artboard/src/adapter/web/Canvas2DTarget.h"
#include <emscripten/bind.h>
#include <memory>

using namespace emscripten;
using namespace arstro::examples;

static std::unique_ptr<UiDemoApp> gApp;
static artboard::Canvas2DTarget gTarget;

static void init(double w, double h)
{
    gApp.reset(new UiDemoApp(w, h));
}

static void frame(double nowMs)
{
    if (gApp)
        gApp->render(gTarget, nowMs);
}

static void pointer(int kind, double x, double y, int button, double timeMs)
{
    if (gApp)
        gApp->pointer(kind, x, y, button, timeMs);
}

static void keyDown(int keyCode, bool shift, bool ctrl, bool alt)
{
    if (gApp)
        gApp->keyDown(keyCode, shift, ctrl, alt);
}

static void keyUp(int keyCode, bool shift, bool ctrl, bool alt)
{
    if (gApp)
        gApp->keyUp(keyCode, shift, ctrl, alt);
}

static void textInput(const std::string &text)
{
    if (gApp)
        gApp->textInput(text);
}

EMSCRIPTEN_BINDINGS(arstro_ui_demo)
{
    emscripten::function("init", &init);
    emscripten::function("frame", &frame);
    emscripten::function("pointer", &pointer);
    emscripten::function("keyDown", &keyDown);
    emscripten::function("keyUp", &keyUp);
    emscripten::function("textInput", &textInput);
}