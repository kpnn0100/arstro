/*
 *  solaris_web_tokens — the web UI's design tokens, printed FROM Theme.h (R-SVC-7; cosmo's
 *  cosmo_web_tokens, apps/cosmo/cli/webTokens.cpp, for Solaris).
 *
 *  The web face must look like the window, and two hand-kept palettes are the first thing that makes
 *  two faces look different (arstro.design.rule §2: a forked token file is a divergence with a delay
 *  fuse). So the stylesheet's tokens are GENERATED: Solaris's accent is installed exactly as the
 *  window installs it (`installSolarisAccent`, teal), then EVERY token function `apps/solaris/app/
 *  Theme.h` reaches — every namespace: cosmo's `palette` (through Solaris's alias), `radius`, `font`,
 *  `metrics`, Solaris's `space`, `motion` and `surface`, the shared Artboard theme (`sharedTheme()`)
 *  and Artboard's `interaction` constants Theme.h's motion is built on — is printed as a CSS custom
 *  property, so a widget port never needs a hex, a duration or a size that Theme.h already has, and
 *  nobody ever edits this file to get one. Names: a function's name in kebab case under its namespace
 *  (`surface::deckBg` → `--deck-bg`; cosmo's palette keeps cosmo's names, so the borrowed cosmo kit —
 *  `tok()` in ui/controls.js, dialogs.css — reads the same properties it reads in cosmo).
 *
 *  A parametric token prints its parameter's base: `whiteAlpha(a)` / `primaryAlpha(a)` /
 *  `successAlpha(a)` → `--white-rgb` etc. (`rgba(var(--white-rgb), a)`), `hoverWash(t)` at t = 1 plus
 *  its alpha per unit, `scrim(a)` at a = 1 plus its alpha, `space::u(n)` as the unit, `track(i)` for
 *  i = 0 … 9 (it wraps at 10).
 *
 *  HIGH CONTRAST (R-ACCESS-1): the neutrals are a runtime blend (`palette::setContrast`, eased by the
 *  App). The colours are printed twice — at contrast 0 under `:root`, at contrast 1 under `:root.hc` —
 *  and every colour is registered (`@property … <color>`) so the page EASES between them by a CSS
 *  transition on `:root`; `--contrast-props` lists them for that transition (base.css).
 *
 *  apps/solaris/web/css/tokens.css is the committed output; ctest `solaris_web_tokens_current` fails
 *  when Theme.h moves and it was not regenerated:
 *
 *      build/apps/solaris/cli/solaris_web_tokens > apps/solaris/web/css/tokens.css
 */
#include "Theme.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace arstro::solaris_ui;
using artboard::Color;

namespace
{
    int byte(double v) { return (int)std::lround(std::fmax(0.0, std::fmin(1.0, v)) * 255.0); }

    std::string colourText(const Color &c)
    {
        char b[64];
        if (c.a >= 0.9995) std::snprintf(b, sizeof b, "#%02X%02X%02X", byte(c.r), byte(c.g), byte(c.b));
        else std::snprintf(b, sizeof b, "rgba(%d, %d, %d, %g)", byte(c.r), byte(c.g), byte(c.b), c.a);
        return b;
    }

    /** The pass being printed: colours land in `:root` (contrast 0) or `:root.hc` (contrast 1); the rest only once. */
    bool gHigh = false;
    std::vector<std::pair<std::string, std::string>> gColours; // name → its normal value (the @property initial value)

    void colour(const std::string &name, const Color &c)
    {
        std::printf("  --%s: %s;\n", name.c_str(), colourText(c).c_str());
        if (!gHigh) gColours.emplace_back(name, colourText(c));
    }
    void rgb(const char *name, const Color &c)
    {
        if (!gHigh) std::printf("  --%s-rgb: %d, %d, %d;\n", name, byte(c.r), byte(c.g), byte(c.b));
    }
    void px(const std::string &name, double v)
    {
        if (!gHigh) std::printf("  --%s: %gpx;\n", name.c_str(), v);
    }
    void num(const std::string &name, double v)
    {
        if (!gHigh) std::printf("  --%s: %g;\n", name.c_str(), v);
    }
    void ms(const char *name, double v)
    {
        if (!gHigh) std::printf("  --%s: %gms;\n", name, v);
    }
    void family(const std::string &name, const std::string &f)
    {
        if (!gHigh) std::printf("  --%s: \"%s\";\n", name.c_str(), f.c_str());
    }

    void box(const std::string &name, const artboard::BoxStyle &b)
    {
        colour(name + "-fill", b.paint.hasFill ? b.paint.fill : Color{0, 0, 0, 0});
        colour(name + "-stroke", b.paint.hasStroke ? b.paint.stroke : Color{0, 0, 0, 0});
        px(name + "-stroke-width", b.paint.hasStroke ? b.paint.strokeWidth : 0.0);
        px(name + "-radius", b.cornerRadius);
    }
    void text(const std::string &name, const artboard::TextStyle &s)
    {
        colour(name + "-color", s.color);
        px(name + "-size", s.sizePx);
        family(name + "-family", s.fontFamily);
        px(name + "-letter-spacing", s.letterSpacingPx);
    }

    void tokens()
    {
        // ── palette:: (cosmo's, through Solaris's alias) — cosmo's own names ──────────────────────────
        colour("background", palette::background());
        colour("foreground", palette::foreground());
        colour("card", palette::card());
        colour("popover", palette::popover());
        colour("primary", palette::primary());
        colour("primary-foreground", palette::primaryForeground());
        colour("secondary", palette::secondary());
        colour("secondary-foreground", palette::secondaryForeground());
        colour("muted", palette::muted());
        colour("muted-foreground", palette::mutedForeground());
        colour("destructive", palette::destructive());
        colour("success", palette::success());
        colour("border", palette::border());
        colour("input", palette::input());
        colour("input-light", palette::inputLight());
        colour("input-light-text", palette::inputLightText());
        colour("switch-background", palette::switchBackground());
        colour("ring", palette::ring());
        colour("canvas-bg", palette::canvasBg());
        colour("histogram-bg", palette::histogramBg());
        colour("left-rail-bg", palette::leftRailBg());
        colour("filmstrip-bg", palette::filmstripBg());
        colour("folder-chip-bg", palette::folderChipBg());
        colour("segmented-bg", palette::segmentedBg());
        colour("curve-plot-bg", palette::curvePlotBg());
        colour("white", palette::white());
        // parametric: whiteAlpha(a), primaryAlpha(a), successAlpha(a) = rgba(var(--x-rgb), a); hoverWash(t) = whiteAlpha(0.07 t)
        rgb("white", palette::whiteAlpha(1.0));
        rgb("primary", palette::primaryAlpha(1.0));
        rgb("success", palette::successAlpha(1.0));
        colour("hover-wash", palette::hoverWash(1.0));
        num("hover-wash-alpha", palette::hoverWash(1.0).a); // per unit of hover: rgba(var(--white-rgb), calc(var(--hover-wash-alpha) * t))

        // ── radius::, metrics:: ───────────────────────────────────────────────────────────────────────
        px("radius-hairline", radius::hairline());
        px("radius-control", radius::control());
        px("radius-pill", radius::pill());
        px("anchor-hit-radius", metrics::anchorHitRadius());

        // ── font:: — the family names (base.css maps them to the files `ntwb install` ships) ──────────
        family("font-sans", font::sans());
        family("font-sans-medium", font::sansMedium());
        family("font-sans-semibold", font::sansSemiBold());
        family("font-mono", font::mono());
        family("font-mono-medium", font::monoMedium());

        // ── space:: — cosmo's 3.25 px unit and its named steps ────────────────────────────────────────
        px("space-u", space::u(1));
        px("space-pad-x", space::padX());
        px("space-gap", space::gap());
        px("space-row-h", space::rowH());
        px("space-bar-h", space::barH());

        // ── motion:: — the durations the window eases with (design rule §2.5) ─────────────────────────
        ms("motion-hover", motion::kHoverMs);
        ms("motion-modal-open", motion::kModalOpenMs);
        ms("motion-modal-close", motion::kModalCloseMs);
        ms("motion-scroll", motion::kScrollMs);
        ms("motion-cross-fade", motion::kCrossFadeMs);
        ms("motion-dissolve", motion::kDissolveMs);
        ms("motion-select", motion::kSelectMs);
        ms("motion-slide", motion::kSlideMs);
        ms("motion-catch-up", motion::kCatchUpMs);
        ms("motion-reflow", motion::kReflowMs);
        ms("motion-screen-fade", motion::kScreenFadeMs);
        ms("motion-toast-in", motion::kToastInMs);
        ms("motion-toast-out", motion::kToastOutMs);
        ms("motion-toast-hold", motion::kToastHoldMs);
        ms("motion-meter-fall", motion::kMeterFallMs);
        // Artboard's interaction constants (motion::kHoverMs is one of them): what a hover lifts, what a disable removes
        num("interaction-hover-fill-lift", artboard::interaction::kHoverFillLift);
        num("interaction-hover-stroke-lift", artboard::interaction::kHoverStrokeLift);
        num("interaction-disabled-fade", artboard::interaction::kDisabledFade);

        // ── surface:: — Solaris's own: the lanes, the ruler, the dock, the meters, the analog face ────
        colour("stage-bg", surface::stageBg());
        colour("ruler-bg", surface::rulerBg());
        colour("header-bg", surface::headerBg());
        colour("lane-bg", surface::laneBg());
        colour("lane-alt-bg", surface::laneAltBg());
        colour("deck-bg", surface::deckBg());
        colour("strip-bg", surface::stripBg());
        colour("scrim", surface::scrim(1.0)); // scrim(a): rgba(0, 0, 0, calc(var(--scrim-alpha) * a))
        num("scrim-alpha", surface::scrim(1.0).a);
        colour("playhead", surface::playhead());
        colour("meter-low", surface::meterLow());
        colour("meter-mid", surface::meterMid());
        colour("meter-high", surface::meterHigh());
        colour("solo", surface::solo());
        colour("skeleton", surface::skeleton());
        colour("module-bg", surface::moduleBg());
        colour("screen-bg", surface::screenBg());
        colour("knob-cap", surface::knobCap());
        colour("knob-track", surface::knobTrack());
        colour("screen-grid", surface::screenGrid());
        for (int i = 0; i < 10; ++i) colour("track-" + std::to_string(i), surface::track(i)); // track(i) wraps at 10

        // ── sharedTheme() — the Artboard theme cosmo builds (Theme.cpp makeCosmoV2Theme), every style ──
        const artboard::Theme &th = sharedTheme();
        box("theme-slider-track", th.slider.track);
        box("theme-slider-range-fill", th.slider.rangeFill);
        box("theme-slider-thumb", th.slider.thumb);
        px("theme-slider-thumb-radius", th.slider.thumbRadius);
        box("theme-button-idle", th.button.idle);
        box("theme-button-pressed", th.button.pressed);
        text("theme-button-label", th.button.label);
        box("theme-checkbox-box", th.checkbox.box);
        box("theme-checkbox-indicator", th.checkbox.indicator);
        text("theme-checkbox-label", th.checkbox.label);
        box("theme-text-box-idle", th.textBox.idle);
        box("theme-text-box-focused", th.textBox.focused);
        text("theme-text-box-text", th.textBox.text);
        text("theme-text-box-placeholder", th.textBox.placeholder);
        colour("theme-text-box-caret", th.textBox.caretColor);
        colour("theme-text-box-selection", th.textBox.selectionColor);
        box("theme-knob-dial", th.knob.dial);
        colour("theme-knob-track", th.knob.trackColor);
        colour("theme-knob-value", th.knob.valueColor);
        colour("theme-knob-indicator", th.knob.indicatorColor);
        px("theme-knob-arc-width", th.knob.arcWidth);
        text("theme-knob-label", th.knob.label);
        box("theme-toggle-track-off", th.toggle.trackOff);
        box("theme-toggle-track-on", th.toggle.trackOn);
        box("theme-toggle-thumb", th.toggle.thumb);
        box("theme-progress-track", th.progress.track);
        box("theme-progress-fill", th.progress.fill);
        box("theme-combo-field", th.combo.field);
        box("theme-combo-popup", th.combo.popup);
        box("theme-combo-row-selected", th.combo.rowSelected);
        text("theme-combo-text", th.combo.text);
        colour("theme-combo-caret", th.combo.caretColor);
        box("theme-tab-idle", th.tab.tabIdle);
        box("theme-tab-active", th.tab.tabActive);
        text("theme-tab-label", th.tab.label);
        text("theme-tab-label-active", th.tab.labelActive);
        colour("theme-tab-indicator", th.tab.activeIndicatorColor);
        px("theme-tab-indicator-height", th.tab.activeIndicatorHeight);
        box("theme-scroll-viewport", th.scroll.viewport);
        box("theme-scroll-track", th.scroll.track);
        box("theme-scroll-thumb", th.scroll.thumb);
        box("theme-graph-background", th.graph.background);
        colour("theme-graph-grid", th.graph.gridColor);
        colour("theme-graph-line", th.graph.lineColor);
        colour("theme-graph-fill", th.graph.fillColor);
        px("theme-graph-line-width", th.graph.lineWidth);
        text("theme-label", th.label);
        text("theme-text-default", artboard::TextStyle{}); // Artboard's default TextStyle (the colour tok() calls abText)
    }
}

int main()
{
    installSolarisAccent(); // the window's first call (R-UI-2): every accent-derived token follows it
    std::printf("/* GENERATED by solaris_web_tokens from apps/solaris/app/Theme.h (cosmo's palette, Solaris's teal) - do not edit.\n"
                "   Regenerate: solaris_web_tokens > apps/solaris/web/css/tokens.css (ctest solaris_web_tokens_current). */\n"
                ":root {\n");
    palette::setContrast(0.0);
    gHigh = false;
    tokens();
    std::printf("}\n\n/* R-ACCESS-1: high contrast - the same colours at palette::setContrast(1); the shell sets the class, the page eases */\n"
                ":root.hc {\n");
    palette::setContrast(1.0);
    gHigh = true;
    tokens();
    palette::setContrast(0.0);
    std::printf("}\n\n/* every colour, registered, so a contrast switch EASES (a transition on :root over --contrast-props) */\n");
    for (const auto &c : gColours)
        std::printf("@property --%s { syntax: \"<color>\"; inherits: true; initial-value: %s; }\n", c.first.c_str(), c.second.c_str());
    std::printf(":root {\n  --contrast-props: ");
    for (size_t i = 0; i < gColours.size(); ++i) std::printf("%s--%s", i ? ", " : "", gColours[i].first.c_str());
    std::printf(";\n}\n");
    return 0;
}
