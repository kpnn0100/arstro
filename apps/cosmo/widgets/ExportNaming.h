/*
 *  cosmo_v2 by arstro — ExportNaming: the ONE function that names an exported file
 *  (R-EXPORT-3a, D-72).
 *
 *  It exists because the name was computed twice — once by the host's writer and once by the
 *  Export dialog's preview line — and neither asked whether the file was already there. With the
 *  dialog's defaults (same as source, JPEG, no prefix, no subfolder) a JPEG's output path was the
 *  JPEG itself, and the export replaced the original. Two rules now hold for every destination:
 *
 *    1. the output is never the source file  → the stem gets "_edit";
 *    2. the output is never an existing file → "-1", "-2", … until the name is free.
 *
 *  Header-only and free of every toolkit so the dialog (a widget) and ExportWriter (the host)
 *  call the same code: a preview that shows one name while another is written is the same
 *  defect one step later. The only I/O is the `exists` probe, which a test can replace.
 */
#pragma once
#include "ExportDialog.h"
#include <filesystem>
#include <functional>
#include <string>

namespace arstro
{
namespace cosmo_v2
{
namespace exportNaming
{
    using Exists = std::function<bool(const std::string &)>;

    inline bool onDisk(const std::string &path)
    {
        std::error_code ec;
        return std::filesystem::exists(std::filesystem::u8path(path), ec);
    }

    /** True when both strings name one file. Lexical (no symlink chase) unless both exist. */
    inline bool samePath(const std::string &a, const std::string &b)
    {
        if (a.empty() || b.empty()) return false;
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path pa = fs::u8path(a), pb = fs::u8path(b);
        if (fs::exists(pa, ec) && fs::exists(pb, ec) && fs::equivalent(pa, pb, ec)) return true;
        return fs::absolute(pa, ec).lexically_normal() == fs::absolute(pb, ec).lexically_normal();
    }

    inline std::string folderOf(const std::string &p)
    {
        const auto s = p.find_last_of('/');
        return s == std::string::npos ? std::string() : p.substr(0, s);
    }
    inline std::string fileOf(const std::string &p)
    {
        const auto s = p.find_last_of('/');
        return s == std::string::npos ? p : p.substr(s + 1);
    }
    inline std::string stemOf(const std::string &name)
    {
        const auto d = name.find_last_of('.');
        return d == std::string::npos || d == 0 ? name : name.substr(0, d);
    }
    inline std::string extensionFor(const std::string &format)
    {
        return format == "PNG" ? ".png" : format == "TIFF" ? ".tif" : ".jpg";
    }

    /** `<dir>/<stem><ext>`, moved off the source (rule 1) and, when `avoidExisting`, off every
     *  existing file (rule 2). */
    inline std::string freeName(const std::string &dir, std::string stem, const std::string &ext,
                                const std::string &sourcePath, bool avoidExisting,
                                const Exists &exists = onDisk)
    {
        std::string path = dir + "/" + stem + ext;
        if (samePath(path, sourcePath))
        {
            stem += "_edit";
            path = dir + "/" + stem + ext;
        }
        if (!avoidExisting) return path;
        // Bounded: a folder holding ten thousand exports of one photo is not a case to loop on.
        for (int n = 1; n < 10000 && (exists ? exists(path) : onDisk(path)); ++n)
            path = dir + "/" + stem + "-" + std::to_string(n) + ext;
        return path;
    }

    /** The folder an export of `sourcePath` lands in for `req` (never empty). */
    inline std::string outputDir(const ExportDialog::Request &req, const std::string &sourcePath)
    {
        std::string dir = req.sameAsSource ? folderOf(sourcePath) : req.destination;
        if (dir.empty()) dir = folderOf(sourcePath);
        if (dir.empty()) dir = ".";
        if (req.useSubfolder && !req.subfolder.empty()) dir += "/" + req.subfolder;
        return dir;
    }

    /** Where `sourcePath`'s export is written for `req`. `fallbackName` names a photo with no
     *  source path. Never the source, never an existing file (R-EXPORT-3a). */
    inline std::string outputPath(const ExportDialog::Request &req, const std::string &sourcePath,
                                  const std::string &fallbackName, const Exists &exists = onDisk)
    {
        std::string base = sourcePath.empty() ? fallbackName : fileOf(sourcePath);
        if (base.empty()) base = "export";
        base = stemOf(base);
        return freeName(outputDir(req, sourcePath), (req.usePrefix ? req.prefix : std::string()) + base,
                        extensionFor(req.format), sourcePath, true, exists);
    }
}
}
}
