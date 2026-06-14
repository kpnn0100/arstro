#include "UiDemoApp.h"
#include "../../Artboard/include/artboard/artboard.h"
#include <cmath>
#include <cstdio>

using namespace arstro::examples;
using artboard::DrawOp;
using artboard::Point;
using artboard::RecordingTarget;

namespace
{
    int gFail = 0;

    void check(bool ok, const char *what)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok)
            ++gFail;
    }

    void click(UiDemoApp &app, const Point &point, double timeMs)
    {
        app.pointer(0, point.x, point.y, 0, timeMs);
        app.pointer(2, point.x, point.y, 0, timeMs + 20.0);
    }
}

int main()
{
    UiDemoApp app(960, 560);
    RecordingTarget target;

    app.render(target, 0.0);
    check(target.count(DrawOp::Kind::FillPath) >= 6, "renders layered panels and controls");
    check(target.count(DrawOp::Kind::DrawText) >= 6, "renders labels and captions");

    click(app, app.sliderPoint(0.75), 10.0);
    app.render(target, 20.0);
    check(app.sliderValue() > 74.0 && app.sliderValue() < 76.0, "slider responds to pointer input");

    click(app, app.checkboxPoint(), 40.0);
    app.render(target, 50.0);
    check(!app.badgeVisible(), "checkbox toggles nested child visibility");

    click(app, app.textBoxPoint(), 60.0);
    app.textInput("N");
    app.textInput("e");
    app.textInput("s");
    app.textInput("t");
    app.textInput("e");
    app.textInput("d");
    app.textInput(" ");
    app.textInput("c");
    app.textInput("a");
    app.textInput("r");
    app.textInput("d");
    app.render(target, 80.0);
    check(app.previewTitle() == "Segment cardNested card", "textbox updates preview title");

    const double startX = app.previewPanelX();
    click(app, app.buttonPoint(), 100.0);
    app.render(target, 420.0);
    check(app.previewPanelX() > startX + 40.0, "button animates the parent card");

    std::printf("\n%s\n", gFail == 0 ? "ui_demo_native OK" : "ui_demo_native FAILED");
    return gFail == 0 ? 0 : 1;
}