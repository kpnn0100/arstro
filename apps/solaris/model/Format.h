/*
 *  solaris_model — Format: the spellings the `.slp` is canonical in (R-FMT-2;
 *  docs/project-format.md §10).
 *
 *  One spelling per value, so "no change" is literally no diff and every surface that prints a
 *  value — the file, the event stream, `state print`, the API document — prints the same text:
 *
 *    number   the shortest decimal that reads back to the same double, always with a point (0.0)
 *    beats    rounded to the tick (1/960 beat), then the fewest decimals that read back to it
 *    seconds  rounded to the microsecond, then the shortest spelling
 *    string   bare unless it holds a space, a quote, `;`, `=` or `#` — then in double quotes
 */
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace solaris
{
    constexpr int kPpq = 960;

    std::string canonicalNumber(double v);
    std::string canonicalBeats(double beats);
    std::string canonicalSeconds(double seconds);
    /** Beats rounded to the nearest tick — what a stored time IS. */
    double toTick(double beats);
    std::string quoteIfNeeded(const std::string &s);
    std::string boolText(bool b);

    /** Parse a number; false (and `out` untouched) if `s` is not one. Non-finite text parses. */
    bool parseNumber(const std::string &s, double &out);
    /** true/false/1/0 → bool; false if neither. */
    bool parseBool(const std::string &s, bool &out);

    /** `key=value` tokens of a node line, quotes respected; a `;` outside quotes starts the comment. */
    struct LineTokens
    {
        std::string type;                                         // "#atrack" without the '#'
        std::vector<std::pair<std::string, std::string>> fields;  // in order
        std::vector<std::string> bare;                            // tokens with no '='
        std::string comment;                                      // text after the ';', trimmed
    };
    LineTokens tokenizeLine(const std::string &line);
}
}
