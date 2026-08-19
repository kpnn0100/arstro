/*
 *  Cosmo by arstro — UiInspectable: what a self-drawn widget reports about itself (P0.6).
 *
 *  A Segment tree dump answers where a node is and whether it is showing. It cannot answer
 *  what a node DREW, and cosmo's most interesting widgets — the splash, the filmstrip, the
 *  histogram — are single self-painting leaves with no children to walk. Dumping the splash
 *  says "SplashScreen 420x260, visible" and stays silent on the one thing being asked: is
 *  the progress bar there, and how full is it.
 *
 *  So a widget that paints its own state declares that state here, in one line, and the dump
 *  prints it. Opt-in: a widget whose children already say everything implements nothing.
 *
 *  Cosmo-side rather than a virtual on artboard::Segment, deliberately — Artboard is a
 *  general UI engine and debugging cosmo is not its job, and `core/Artboard` is a submodule
 *  whose every change costs a second commit. dumpSegmentTree finds implementors by
 *  dynamic_cast, which it can afford: it already uses RTTI to name nodes.
 */
#pragma once
#include <string>

namespace arstro
{
namespace cosmo_v2
{
    class UiInspectable
    {
    public:
        virtual ~UiInspectable() = default;
        /** One line of `key=value` pairs describing what this widget is currently showing.
         *  Report ANIMATED values as they stand this frame, not the targets they are heading
         *  for: a dump is a photograph, and "where it will end up" is the one thing a caller
         *  can already work out for itself. Quote anything containing a space. */
        virtual std::string uiDetail() const = 0;
    };
}
}
