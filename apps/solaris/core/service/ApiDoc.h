/*
 *  solaris_core — ApiDoc: the API document, GENERATED from the tables the code runs on (R-API-1).
 *
 *    commands  ← commandSpecs()           (the parser's own input)
 *    events    ← eventSpecs()             (what formatEvent prints)
 *    model     ← appModelFields()         (checked against what modelToJson writes)
 *    devices   ← DeviceRegistry::types()  (the DSP library's: every instrument and effect, every
 *                                          parameter with unit, range, default, choices)
 *
 *  `docs/api.json` and `docs/API.md` are this output, COMMITTED, and a ctest regenerates and diffs
 *  them — rung 4 of `arstro.rule` §5. A command or a DSP parameter added without regenerating the
 *  document fails the build's tests.
 */
#pragma once
#include <string>

namespace arstro
{
namespace solaris
{
    std::string apiJson();
    std::string apiMarkdown();
}
}
