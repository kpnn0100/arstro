/*
 *  cosmo_v2 by arstro — ExportWriter: the host-side image encoder behind the
 *  Export modal (R-EXPORT). Given one rendered full-resolution RGBA frame plus
 *  the modal's Request, it resolves the output path, optionally downscales to a
 *  long-edge cap, encodes as JPEG / PNG / TIFF through GdkPixbuf, and applies
 *  the metadata options (R-EXPORT-5).
 *
 *  This is deliberately in the APP/host layer, not in cosmo_core or Artboard:
 *  it does OS file I/O and talks to GdkPixbuf, so it belongs on the platform
 *  side of the seam (the same reasoning as Log.{h,cpp}).
 *
 *  Metadata, honestly scoped — a toggle is applied where the container can
 *  carry it and otherwise silently does nothing (R-EXPORT-5):
 *    * EXIF     — JPEG output from a JPEG source: the source's APP1/Exif segment
 *                 is copied into the written file (GdkPixbuf writes none itself).
 *    * GPS      — when EXIF is embedded, the GPS IFD pointer entry (tag 0x8825)
 *                 is removed from IFD0, so no location survives.
 *    * sRGB     — JPEG/TIFF: GdkPixbuf's "icc-profile" save option carries a
 *                 compact sRGB v2 profile. PNG: GdkPixbuf's PNG saver rejects
 *                 icc-profile (it hands libpng the profile before the colour type
 *                 is set), so the PNG spec's own sRGB + gAMA chunks are written
 *                 instead — the standards-blessed way to declare sRGB, and it
 *                 needs no compression.
 */
#pragma once
#include "widgets/ExportDialog.h"
#include <cstdint>
#include <string>

namespace arstro
{
namespace cosmo_v2
{
namespace exporter
{
    /** Where `slot`'s output lands for this request: the resolved directory (created
     *  if needed) joined with the prefixed filename + the format's extension.
     *  `sourcePath` is the image's original file (drives "same as source" + the stem);
     *  `fallbackName` is used when the source path is empty (a never-saved image).
     *
     *  R-EXPORT-3a (D-72): the result is NEVER `sourcePath` and never a file that already
     *  exists - the stem gets `_edit` when it would be the source, then `-1`, `-2`, … while
     *  the name is taken. Call it just before writing, one photo at a time. */
    std::string resolvePath(const ExportDialog::Request &req, const std::string &sourcePath,
                            const std::string &fallbackName);

    /** For a writer handed a ready-made path (the scripted `export <outdir>`): `path` unless
     *  it IS the source file, in which case `<stem>_edit<ext>` beside it (R-EXPORT-3a). An
     *  existing file that is not the source is left to be replaced - a script that renders
     *  twice into one folder expects the same names. */
    std::string awayFromSource(const std::string &path, const std::string &sourcePath);

    /** Encode `rgba` (w*h, 4 bytes/px, straight RGBA) to `path` per `req`.
     *  Returns true on success; `error` carries a human-readable reason otherwise. */
    bool write(const ExportDialog::Request &req, const uint8_t *rgba, int w, int h,
               const std::string &path, const std::string &sourcePath, std::string &error);
}
}
}
