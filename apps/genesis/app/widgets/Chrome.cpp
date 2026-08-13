#include "Chrome.h"
#include "../App.h"
#include "Modal.h"

namespace genesis
{
namespace ui
{
    namespace
    {
        constexpr double kBtnH = 26.0;
    }

    Chrome::Chrome(App &app) : mApp(app)
    {
        const struct { const char *label; void (*run)(App &); } defs[] = {
            {"New", [](App &a) { a.modal()->openNew(); }},
            {"Open", [](App &a) { a.modal()->openBrowse(); }},
            {"Save", [](App &a) { if (a.path().empty()) a.modal()->openSaveAs(); else a.saveDocument(); }},
            {"Export", [](App &a) { a.exportCode(); }},
            {"Verify", [](App &a) { a.startVerify(); }},
        };
        for (const auto &d : defs)
        {
            Action action;
            action.label = d.label;
            action.button = std::make_shared<artboard::Button>(d.label, theme().button);
            App *app_ = &mApp;
            auto run = d.run;
            action.button->onClick = [app_, run] { run(*app_); };
            action.button->height.set(kBtnH);
            action.button->focusable = true;
            addChild(action.button);
            mActions.push_back(action);
        }
    }

    void Chrome::layout(double w)
    {
        width.set(w);
        height.set(metrics::chromeH());

        // Right-aligned action row; each button is sized to its own label so nothing clips.
        double x = w - metrics::pad();
        for (auto it = mActions.rbegin(); it != mActions.rend(); ++it)
        {
            const double bw = std::max(58.0, textWidth(it->label, type::body(), font::sansMedium()) + 22.0);
            x -= bw;
            it->button->x.set(x);
            it->button->y.set((metrics::chromeH() - kBtnH) * 0.5);
            it->button->width.set(bw);
            x -= 6.0;
        }
    }

    void Chrome::advance(double nowMs)
    {
        mNowMs = nowMs;
        // The Verify button reads as busy while a check is in flight (R2: never dead).
        for (auto &a : mActions)
            if (a.label == "Verify")
            {
                a.button->text = mApp.verifyRunning() ? "Verifying…" : "Verify";
                a.button->enabled = !mApp.verifyRunning();
            }
        Segment::advance(nowMs);
    }

    void Chrome::onPaint(artboard::IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        artboard::drawRoundedRect(t, {0, 0, w, h}, 0.0, artboard::Paint::filled(palette::chromeBg()));
        artboard::drawRoundedRect(t, {0, h - 1, w, 1}, 0.0, artboard::Paint::filled(palette::border()));

        double x = metrics::pad();
        // App mark: a small accent square, the one place the accent is decorative.
        artboard::drawRoundedRect(t, {x, h * 0.5 - 7.0, 14, 14}, radius::hairline(),
                                  artboard::Paint::filled(palette::primary()));
        x += 14 + 8;
        t.setFill(palette::foreground());
        t.drawText("Genesis", x, centreBaseline(0, h, type::body()), type::body(), font::sansSemiBold(), 0.2);
        x += textWidth("Genesis", type::body(), font::sansSemiBold(), 0.2) + 14;

        artboard::drawRoundedRect(t, {x, h * 0.5 - 9.0, 1, 18}, 0.0,
                                  artboard::Paint::filled(palette::border()));
        x += 14;

        const std::string name = mApp.doc().name + (mApp.dirty() ? " •" : "");
        const double nameMax = std::max(40.0, width.value() * 0.22);
        drawFitted(t, name, x, centreBaseline(0, h, type::title()), nameMax, type::title(),
                   palette::foreground(), font::sansMedium());
        x += std::min(nameMax, textWidth(name, type::title(), font::sansMedium())) + 10;
        x += drawChip(t, mApp.doc().base, x, h * 0.5 - 8.0, 16.0, palette::secondary(),
                      palette::secondaryForeground()) + 10;

        // Status: fades in on change and holds; errors keep their colour so they are not missed.
        const double statusRight = w - metrics::pad() - 5 * 68.0;
        const double statusW = std::max(0.0, statusRight - x - 12.0);
        if (statusW > 40.0)
        {
            artboard::Color c = palette::mutedForeground();
            switch (mApp.statusLevel())
            {
            case StatusLevel::Good: c = palette::success(); break;
            case StatusLevel::Warn: c = palette::warning(); break;
            case StatusLevel::Bad: c = palette::destructive(); break;
            case StatusLevel::Info: break;
            }
            drawFitted(t, mApp.statusText(), x + 12.0, centreBaseline(0, h, type::small()), statusW,
                       type::small(), c, font::sans());
        }
    }
}
}
