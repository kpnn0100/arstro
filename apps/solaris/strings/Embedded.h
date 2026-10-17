/*
 *  solaris_strings — the tables compiled in (generated from lang/*.strings by embed_strings.cmake; Round 21).
 */
#pragma once
#include <cstddef>

namespace arstro
{
namespace solaris_strings
{
    struct EmbeddedTable
    {
        const char *file;          // "vi.strings"
        const unsigned char *data; // its bytes, as written
        std::size_t size;
    };
    extern const EmbeddedTable kEmbedded[];
    extern const int kEmbeddedCount;
}
}
