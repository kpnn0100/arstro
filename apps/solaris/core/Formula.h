/*
 *  solaris_core — Formula: a binding's text → the engine's program (R-AUTO-2).
 *
 *      =au_1                        an automation's curve
 *      =ch_2.gain - 6               a LINK: another address's evaluated value
 *      =0.5 + 0.5 * sin(beat * pi)  an LFO, in beats
 *      =60000 / bpm * 0.75          a dotted eighth in ms (R-EDM-5)
 *
 *  Numbers; + − * / ^ (right-associative), parentheses, unary minus; the functions sin cos tan
 *  abs sign min max clamp lerp pow exp log sqrt floor ceil round frac; `pi`; the clock — `beat`,
 *  `bar`, `bpm`, `t` (seconds). Any other name is a SYMBOL the caller resolves (an automation id or
 *  an address) — the parser only says which names appear, in first-use order. A leading `=` is
 *  optional. Errors name the offending text and its column.
 */
#pragma once
#include "Expr.h"
#include <string>
#include <vector>

namespace arstro
{
namespace solaris
{
    struct ParsedFormula
    {
        engine::Expr expr;              // symbol slots are placeholders until `bindSymbols`
        std::vector<std::string> names; // the symbols, in first-use order
    };

    bool parseFormula(const std::string &text, ParsedFormula &out, std::string &err);
    /** Point each symbol at its slot (`slots[i]` for `names[i]`). */
    void bindSymbols(ParsedFormula &f, const std::vector<int> &slots);
    /** True for a name the language itself owns (a function, `pi`, the clock). */
    bool isFormulaWord(const std::string &name);
}
}
