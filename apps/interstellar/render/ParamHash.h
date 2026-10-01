/*
 *  interstellar_render — ParamHash: one 64-bit number for a whole grade, for the FrameCache key.
 *
 *  It hashes the parameter CODEC's text (`serializeParams`), not the struct's fields. A hand-written
 *  field walk is a second list of what EditParams contains, and it goes stale the first time a field
 *  is added — at which point the cache starts serving the old picture for the new grade, silently.
 *  The codec is the list every project file is already written with, so it cannot fall behind
 *  without breaking persistence too, which somebody would notice.
 *
 *  FNV-1a 64: stable across runs and machines (std::hash is not), so a key means the same thing in
 *  every process — which a persisted or shared cache would need, and which a golden test can assert.
 *
 *  Never returns 0: `FrameCache::kUngraded` is reserved for ungraded source frames (R-VOL-5).
 *
 *  Known limit: the codec writes floats with 7 significant digits, so two grades that differ only
 *  beyond the 7th digit hash equal. That is below what any slider can produce and below one 8-bit
 *  code value for every stage measured, but it is a property of the codec, stated here.
 */
#pragma once
#include "engine/EditParams.h"
#include <cstdint>
#include <string>

namespace arstro
{
namespace interstellar
{
namespace render
{
    /** FNV-1a 64 over raw bytes; exposed so a test can pin the function to known vectors. */
    uint64_t fnv1a64(const std::string &bytes);

    uint64_t hashParams(const arstro::EditParams &p);
}
}
}
