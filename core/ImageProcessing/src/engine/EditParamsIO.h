/*
 *  Arstro ImageProcessing Library
 *
 *  EditParamsIO: plain-text (de)serialization of EditParams. Keeps persistence in
 *  the engine (UI-independent) so a sidecar/session file written by the photo editor
 *  can be read back by anything — another tool, a batch run, a video editor. The
 *  format is line-based `key=value`; deserialize tolerates missing/unknown keys
 *  (they keep their defaults), so it is forward/backward compatible.
 */
#pragma once
#include "EditParams.h"
#include <string>

namespace arstro
{
    /** Serialize an EditParams to a text blob (no surrounding context). */
    std::string serializeParams(const EditParams &p);

    /** Parse a text blob into `out` (tolerant; unknown/missing keys keep defaults). */
    bool deserializeParams(const std::string &text, EditParams &out);
}
