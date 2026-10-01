/*
 *  interstellar_core — AppModelCodec: the AppModel as text (`state print`).
 *
 *  What an agent asserts against (R-API-2) and what an equality test diffs: a project driven by the
 *  CLI and the same project driven by the GUI must dump the SAME stable text.
 *
 *  - **Deterministic.** Fixed key order, canonical numbers.
 *  - **`stable` drops what legitimately differs between two front ends showing the same state**:
 *    `revision`, the frame metadata, the recents index, and absolute paths (reduced to file names).
 *
 *  `appModelFields()` documents every key the JSON form can contain. The API document is generated
 *  from it, and a test dumps a fully populated model and fails if a key is undocumented or a
 *  documented key is never written — so the two cannot drift (R-API-1).
 */
#pragma once
#include "AppModel.h"
#include "Json.h"
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
    struct ModelDumpOptions
    {
        bool stable = false;
        bool json = false;
        bool params = true;   // include the grade target's EditParams (long, but it is the Grade tab)
    };

    const char *screenName(Screen s);
    const char *provenanceName(Provenance p);

    Json modelToJson(const AppModel &m, const ModelDumpOptions &o = ModelDumpOptions{});

    /** `--json` → JSON; otherwise a flat `path=value` listing, one per line, same order. */
    std::string formatModel(const AppModel &m, const ModelDumpOptions &o = ModelDumpOptions{});

    struct ModelFieldDoc
    {
        std::string path;    // "rack[].bindName"
        std::string type;    // string | number | integer | bool | enum(a|b) | object | array
        std::string doc;
        bool machineDependent = false;   // omitted (or reduced) by --stable
    };
    const std::vector<ModelFieldDoc> &appModelFields();
}
}
