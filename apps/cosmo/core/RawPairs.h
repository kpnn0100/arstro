/*
 *  Arstro cosmo_core — RawPairs: a RAW and the JPEG beside it are one photo (R-HOME-5a).
 *
 *  A camera set to RAW+JPEG writes two files per shot. Selecting a card's worth of them put
 *  every photo in the project twice; this picks the RAW and leaves its rendering out.
 *
 *  A pure function over path STRINGS — no filesystem, no codec, no decoder — so the service
 *  can apply it to any list a front end hands it and a test can check it with paths that do
 *  not exist. Whether the decoder can actually read the RAW it prefers is the caller's
 *  question (`IImageDecoder::decodesRaw`): this file only knows what the names say.
 */
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace cosmo
{
    struct RawPairing
    {
        /** The paths to open, in the order given, the left-out ones removed. */
        std::vector<std::string> keep;
        /** Each path left out, with the RAW it was left out for: (rendered, raw). */
        std::vector<std::pair<std::string, std::string>> dropped;
    };

    /** Drop every non-RAW path that has a RAW in the SAME folder with the same stem
     *  (compared case-insensitively). Different folders are different photos; two RAWs, or two
     *  non-RAWs, with one stem are both kept. Order is preserved. */
    RawPairing preferRaw(const std::vector<std::string> &paths);
}
}
