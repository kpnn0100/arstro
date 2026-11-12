#include "RawPairs.h"

#include "decode/ImageDecoder.h"

#include <unordered_map>

namespace arstro
{
namespace cosmo
{
    namespace
    {
        /** The folder (exact) plus the stem (lower-cased), with the extension cut off — the key
         *  two files of one shot share. The folder is compared exactly: one dialog or one listing
         *  spells a folder one way, and a mismatch errs towards keeping both files, which never
         *  loses a photo. The stem is compared without case because cameras and copy tools
         *  disagree on it (`P1090446.JPG` beside `p1090446.rw2` after a lower-casing copy). */
        std::string shotKey(const std::string &path)
        {
            const auto slash = path.find_last_of("/\\");
            const std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
            std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
            const auto dot = base.find_last_of('.');
            if (dot != std::string::npos) base.resize(dot);
            for (char &c : base)
                if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            return dir + base;
        }
    }

    RawPairing preferRaw(const std::vector<std::string> &paths)
    {
        // First RAW per shot. A second RAW of the same shot does not matter here: it is kept in
        // its own right, and the rendering only needs to name ONE RAW it was left out for.
        std::unordered_map<std::string, const std::string *> rawOf;
        for (const std::string &p : paths)
            if (isRawExtension(p)) rawOf.emplace(shotKey(p), &p);

        RawPairing out;
        out.keep.reserve(paths.size());
        for (const std::string &p : paths)
        {
            if (!isRawExtension(p))
            {
                const auto it = rawOf.find(shotKey(p));
                if (it != rawOf.end())
                {
                    out.dropped.emplace_back(p, *it->second);
                    continue;
                }
            }
            out.keep.push_back(p);
        }
        return out;
    }
}
}
