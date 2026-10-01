/*
 *  interstellar_core — ApiDoc: the API document, GENERATED from the tables the code runs on (R-API-1).
 *
 *  Four tables, four sections, no prose maintained by hand:
 *    commands   ← commandSpecs()     (the parser's own input)
 *    events     ← eventSpecs()       (what formatEvent prints)
 *    model      ← appModelFields()   (drift-tested against what modelToJson writes)
 *    addresses  ← paramDefs()        (what `set` routes on)
 *
 *  `docs/api.json` and `docs/API.md` are this output, COMMITTED, and a ctest regenerates and diffs
 *  them — rung 4 of `arstro.rule` §5. A command added without regenerating the doc fails the build's
 *  tests, which is the only way a document stays true.
 */
#pragma once
#include <string>

namespace arstro
{
namespace interstellar
{
    std::string apiJson();
    std::string apiMarkdown();
}
}
