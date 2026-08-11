/*
 *  cosmo_v2 by arstro — small stroke-only line icons matching the glyphs used
 *  in the Figma export (lucide-react: ChevronRight, ChevronDown, PanelLeft,
 *  Save, Upload, Download, RefreshCw, Trash2, RotateCcw, FolderOpen, Image, X,
 *  Ban, Check, CheckCircle). Artboard has no icon library or SVG-path loader,
 *  so these are hand-drawn approximations of each
 *  glyph's silhouette using plain IRenderTarget path primitives -- not a HAL
 *  change, just composition (per the platform-independence rule's first
 *  preference: express new capability with existing primitives). At the
 *  10-14px sizes these render at, the recognizable shape reads correctly even
 *  though they are not literal copies of Lucide's bezier control points.
 *
 *  Every function draws stroke-only, fit to `box` (local coordinates), and
 *  leaves the target's path state consumed (each call is self-contained:
 *  beginPath/.../strokePath).
 */
#pragma once
#include "../../Artboard/src/render/RenderTarget.h"
#include "../../Artboard/src/core/Geometry.h"

namespace arstro
{
namespace cosmo_v2
{
namespace icon
{
    void chevronRight(artboard::IRenderTarget &t, const artboard::Rect &box, const artboard::Color &c, double strokeWidth = 1.3);
    void chevronDown(artboard::IRenderTarget &t, const artboard::Rect &box, const artboard::Color &c, double strokeWidth = 1.3);
    void panelLeft(artboard::IRenderTarget &t, const artboard::Rect &box, const artboard::Color &c, double strokeWidth = 1.4);
    void save(artboard::IRenderTarget &t, const artboard::Rect &box, const artboard::Color &c, double strokeWidth = 1.2);
    void upload(artboard::IRenderTarget &t, const artboard::Rect &box, const artboard::Color &c, double strokeWidth = 1.2);
    void download(artboard::IRenderTarget &t, const artboard::Rect &box, const artboard::Color &c, double strokeWidth = 1.2);
    void refreshCw(artboard::IRenderTarget &t, const artboard::Rect &box, const artboard::Color &c, double strokeWidth = 1.2);
    void trash2(artboard::IRenderTarget &t, const artboard::Rect &box, const artboard::Color &c, double strokeWidth = 1.1);
    void rotateCcw(artboard::IRenderTarget &t, const artboard::Rect &box, const artboard::Color &c, double strokeWidth = 1.2);
    /** lucide FolderOpen — the Export modal's tree group rows + destination row (R-EXPORT). */
    void folder(artboard::IRenderTarget &t, const artboard::Rect &box, const artboard::Color &c, double strokeWidth = 1.2);
    /** lucide Image — the Export modal's tree image rows (R-EXPORT-2). */
    void image(artboard::IRenderTarget &t, const artboard::Rect &box, const artboard::Color &c, double strokeWidth = 1.2);
    /** lucide X — a dialog's close button (R-EXPORT-1). */
    void close(artboard::IRenderTarget &t, const artboard::Rect &box, const artboard::Color &c, double strokeWidth = 1.3);
    /** lucide Ban (circle + slash) — the "filter disabled" badge (R-BYPASS-4/5). */
    void ban(artboard::IRenderTarget &t, const artboard::Rect &box, const artboard::Color &c, double strokeWidth = 1.2);
    /** lucide Check — the per-file "written" tick in the export manifest (R-EXPORT-6). */
    void check(artboard::IRenderTarget &t, const artboard::Rect &box, const artboard::Color &c, double strokeWidth = 1.6);
    /** lucide CheckCircle — the export-complete confirmation (R-EXPORT-6 beat 3). */
    void checkCircle(artboard::IRenderTarget &t, const artboard::Rect &box, const artboard::Color &c, double strokeWidth = 1.4);
}
}
}
