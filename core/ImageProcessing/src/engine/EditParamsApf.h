/*
 *  Arstro ImageProcessing Library
 *
 *  Maps the image engine's EditParams to/from the generic .apf preset envelope
 *  (see Apf.h). The image engine's domain string is "image"; presets built here
 *  carry that so another Arstro engine rejects them. Parameters are grouped into
 *  user-facing CATEGORIES so a preset can be saved/applied selectively (tick a
 *  subset), and so the loader applies only the categories present + selected.
 */
#pragma once
#include "EditParams.h"
#include "Apf.h"
#include <string>
#include <vector>

namespace arstro
{
    /** The engine-domain string image presets are tagged with. */
    const char *apfImageEngine();

    /** Canonical, ordered list of image preset categories (for the save UI). */
    std::vector<std::string> apfImageCategories();

    /** Build an .apf document from `p` containing ONLY the requested categories. */
    apf::Document editParamsToApf(const EditParams &p, const std::vector<std::string> &categories,
                                  const std::string &name, const std::string &app = "cosmo");

    /** Which canonical categories are present in a loaded document (for the import UI). */
    std::vector<std::string> apfPresentImageCategories(const apf::Document &doc);

    /** Apply the requested categories (that are present in `doc`) onto `io`, leaving
     *  other fields untouched. Returns false if the document targets a different
     *  engine (doc.engine set and != "image") — the caller should reject it. */
    bool applyApfToEditParams(const apf::Document &doc, const std::vector<std::string> &categories, EditParams &io);
}
