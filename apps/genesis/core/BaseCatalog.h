/*
 *  Genesis — BaseCatalog: the authorable base classes, as DATA.
 *
 *  A base class is a contract: here is what the host does to you, here is when you are
 *  told about it, here is what you may read. Genesis holds that contract in one table so
 *  the New dialog, the Signals panel, the code emitter, the preview runtime, and the
 *  validator all read the SAME description. Adding a base is a table entry plus the
 *  Artboard class — never a new code path in the emitter.
 */
#pragma once
#include <artboard/artboard.h>
#include <string>
#include <vector>

namespace genesis
{
    /** One thing a base class tells its subclass. */
    struct SignalDef
    {
        std::string name;      // authoring name, e.g. "loopStart"
        std::string hook;      // the Artboard protected virtual, e.g. "onLoopStart"
        std::string argType;   // "" | "double" | "int" | "bool"
        std::string argName;   // "" | "v" | "index" | "on"
        std::string doc;
        bool expected = false; // the export checklist warns when nothing reacts to it
    };

    /** One read-only value a base publishes to Gene as `base.<name>`. */
    struct BaseRead
    {
        std::string name;   // "phase"
        std::string cpp;    // "cyclePhase()"
        std::string doc;
    };

    struct BaseDef
    {
        std::string name;      // "VisualLoop"
        std::string cppClass;  // "artboard::VisualLoop"
        std::string summary;
        std::string mustDraw;  // what the author is responsible for drawing
        std::vector<SignalDef> signals;
        std::vector<BaseRead> reads;
    };

    /** Every authorable base, in menu order. */
    const std::vector<BaseDef> &bases();
    /** Null when `name` is not an authorable base. */
    const BaseDef *findBase(const std::string &name);
    /** Signals every base has (attach/resize/hover/focus), already merged into each BaseDef. */
    const std::vector<SignalDef> &commonSignals();
    /** Reads every base has (hover/enabled/focused), already merged into each BaseDef. */
    const std::vector<BaseRead> &commonReads();
    /** The Easing enumerators an authored track may name. Does NOT include `"Custom"`, which is
     *  not an `artboard::Easing` at all but a request to build one from the track's authored
     *  speeds (G-25) — the dropdown adds it, the lookup below cannot answer it. */
    const std::vector<std::string> &easingNames();
    /** True when `name` is one of them. */
    bool isEasingName(const std::string &name);
    /** The `artboard::Easing` a name selects; `Linear` for anything unrecognised — including
     *  `"Custom"`, whose curve is `Easing::Hermite` with slopes only the caller can compute.
     *
     *  Lives here beside the name table rather than in the runtime, because the interpreter, the
     *  emitter's neighbour-speed arithmetic and the editor's readouts all need the same answer. */
    artboard::Easing easingFromName(const std::string &name);
}
