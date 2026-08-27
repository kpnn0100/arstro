/*
 *  cosmo_v2 by arstro — Exif: read a handful of named EXIF tags out of a JPEG.
 *
 *  For the image-information panel (R-INFO). A JPEG carries its shooting data in an APP1/Exif
 *  segment holding a TIFF structure; this walks IFD0 and the Exif sub-IFD and returns the tags
 *  worth showing to a photographer, formatted for display.
 *
 *  Beside `NativeImageDecoder`, its only consumer, and in the same layer: `core/decode/` is
 *  already where cosmo_core keeps the platform-specific decoders that touch LibRaw and GdkPixbuf,
 *  so a file reader belongs here rather than up in the host directory. Deliberately NOT merged
 *  with ExportWriter's
 *  APP1 code even though both touch the same bytes — that one MOVES a segment verbatim without
 *  understanding it (which is why it can preserve tags it has never heard of), and this one
 *  understands a few tags without moving anything. Sharing a "parser" between the two would make
 *  each depend on the other's reason for existing.
 *
 *  Scope is deliberately small and honest: eight or so tags that a photographer actually reads,
 *  each returned as a formatted string or absent. It is not an EXIF library and does not pretend
 *  to be — a file whose tags it cannot read simply reports fewer rows.
 */
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
namespace exif
{
    /** Label/value pairs, in display order; empty when the file has no readable Exif.
     *  Values are already formatted for a human ("1/250 s", "f/2.8", "ISO 400"). */
    std::vector<std::pair<std::string, std::string>> read(const std::string &path);
}
}
}
