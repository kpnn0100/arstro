/*
 *  solaris_core — the AppModel codec: JSON for `state print`, and the field table the API
 *  document's model section is generated from (R-API-1).
 *
 *  `stable` omits what is a property of WHEN the dump was taken rather than of the state
 *  (`revision`) — the list is `appModelFields()`'s `stable = false` rows, and nothing else may be
 *  excluded: hiding a field to make an equality test pass is falsifying the test (arstro.rule §1).
 */
#pragma once
#include "AppModel.h"
#include "Json.h"
#include <string>
#include <vector>

namespace arstro
{
namespace solaris
{
    struct ModelField
    {
        std::string path;   // "strips[].gain"
        std::string type;   // "number", "string", "bool", "int", "string[]", "object[]"
        std::string summary;
        bool stable = true; // false = left out of `state print --stable`
    };

    const std::vector<ModelField> &appModelFields();
    /** `compact` (R-SVC-8): the song only, non-default values, notes in the notation — `state print --json --compact`. */
    Json modelToJson(const AppModel &m, bool stable, bool compact = false);
}
}
