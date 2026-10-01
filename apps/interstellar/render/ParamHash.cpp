/*
 *  interstellar_render — ParamHash implementation. See ParamHash.h for why it hashes the codec.
 */
#include "ParamHash.h"
#include "engine/EditParamsIO.h"

namespace arstro
{
namespace interstellar
{
namespace render
{
    uint64_t fnv1a64(const std::string &bytes)
    {
        uint64_t h = 14695981039346656037ULL;  // FNV offset basis, 0xcbf29ce484222325
        for (unsigned char c : bytes)
        {
            h ^= c;
            h *= 1099511628211ULL;             // FNV prime
        }
        return h;
    }

    uint64_t hashParams(const arstro::EditParams &p)
    {
        const uint64_t h = fnv1a64(arstro::serializeParams(p));
        return h == 0 ? 1 : h;   // 0 is FrameCache::kUngraded
    }
}
}
}
