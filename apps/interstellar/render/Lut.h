/*
 *  interstellar_render — Lut: the `.cube` LUT, read, applied and written (R-COLOR-5, R-COLOR-6).
 *
 *  The format is Adobe's / Resolve's text `.cube`: `TITLE`, `LUT_1D_SIZE n` or `LUT_3D_SIZE n`,
 *  optional `DOMAIN_MIN` / `DOMAIN_MAX` (three numbers each) or Resolve's `LUT_1D_INPUT_RANGE` /
 *  `LUT_3D_INPUT_RANGE` (two), `#` comments, then n (1D) or n³ (3D) lines of "r g b" with red
 *  changing fastest. A file that says one size and carries another number of rows is refused,
 *  naming the line — a half-read LUT is a wrong picture, not a degraded one.
 *
 *  Applied on ENCODED values (what the frame holds), 8-bit or deep alike: a 1D LUT per channel with
 *  linear interpolation, a 3D one with TETRAHEDRAL interpolation (exact on the lattice and on the
 *  grey axis, the standard choice — trilinear tints greys between lattice points). Input outside
 *  the domain is clamped to it.
 *
 *  Knows no project: the service resolves a path, loads it once (keyed by path, size and mtime) and
 *  hands the LUT to the render path.
 */
#pragma once
#include "Raster.h"
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
namespace render
{
    struct Lut
    {
        std::string title;
        int size1d = 0, size3d = 0;              // exactly one is non-zero
        std::vector<float> table;                // rows of r,g,b: n (1D) or n³ (3D, red fastest)
        float domainMin[3] = {0, 0, 0}, domainMax[3] = {1, 1, 1};

        bool empty() const { return table.empty(); }
        /** One pixel, encoded 0..1 in and out. */
        void map(const float in[3], float out[3]) const;
        /** In place, 8-bit or deep; alpha untouched. `mix` 0 = the input, 1 = the LUT. */
        void apply(Raster &img, double mix = 1.0) const;
    };

    /** Read a .cube; false with `err` saying why (and on which line). */
    bool readCube(const std::string &path, Lut &out, std::string &err);
    /** Write a 3D or 1D .cube; `comments` become `#` lines before the header. */
    bool writeCube(const std::string &path, const Lut &lut, const std::vector<std::string> &comments, std::string &err);
}
}
}
