/*
 *  Genesis — CppEmitter: Document -> a .h/.cpp pair.
 *
 *  The emitter is the product. Genesis ships no runtime, so what comes out here is the
 *  ONLY thing a consuming app ever sees: plain C++17 that includes <artboard/artboard.h>
 *  and nothing else. The test of the emitter is not "does it compile" but "would a person
 *  have written this" — the generated file is reviewed as a code artifact.
 *
 *  Determinism is a contract: the same document always emits byte-identical output, so a
 *  build can regenerate and a stale generated file is a CI failure rather than a mystery.
 */
#pragma once
#include "../Document.h"
#include <string>

namespace genesis
{
    struct EmittedCode
    {
        std::string headerName;   // "CoolVisualLoop.h"
        std::string sourceName;   // "CoolVisualLoop.cpp"
        std::string header;
        std::string source;
        std::string error;        // non-empty when emission failed
        bool ok() const { return error.empty(); }
    };

    /** Emit `doc`. Fails (with `error` set) if the document has validation errors. */
    EmittedCode emitCpp(const Document &doc);

    /** Write `code` into `dir`; returns false and fills `error` on an I/O failure. */
    bool writeEmitted(const EmittedCode &code, const std::string &dir, std::string *error);
}
