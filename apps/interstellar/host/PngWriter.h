/*
 *  interstellar_host — PNG out, for `export-still` and `render --format png-seq` (R-RENDER-3).
 *  GdkPixbuf, which cosmo_core already links: a PNG sequence is the one output a golden test can
 *  compare byte for byte, so it must not need FFmpeg.
 */
#pragma once
#include "Raster.h"
#include <string>

namespace arstro
{
namespace interstellar_host
{
    bool writePng(const std::string &path, const interstellar::Raster &frame, std::string &err);
}
}
