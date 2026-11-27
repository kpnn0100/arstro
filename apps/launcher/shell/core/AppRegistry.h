/*
 *  arstro-shell — reading installed bundles (R-PKG-1, R-PKG-2).
 *
 *  A bundle is a directory holding `manifest.arstro` — `key = value` lines — next to the app's
 *  files. A deliberately tiny format: it is read on every boot of a small board by a shell that
 *  must not need a JSON library to find out what is installed.
 *
 *  This is host-side code (it reads a disk); the service only ever sees the AppInfo list.
 */
#pragma once
#include "Model.h"
#include <string>
#include <vector>

namespace arstro
{
namespace shell
{
    /** Parse a manifest's text. `dir` resolves relative `exec`/`icon`. False + `error` when a
     *  required key (id, name, exec) is missing or the id is not a legal identifier. */
    bool parseManifest(const std::string &text, const std::string &dir, AppInfo &out, std::string &error);
    /** Every valid bundle directly under each root; a broken one is skipped and reported in `problems`. */
    std::vector<AppInfo> scanApps(const std::vector<std::string> &roots, std::vector<std::string> *problems = nullptr);
    /** Where bundles live: $ARSTRO_APPS (colon separated) if set, else the per-user and system dirs. */
    std::vector<std::string> defaultAppRoots();
    bool validAppId(const std::string &id);
}
}
