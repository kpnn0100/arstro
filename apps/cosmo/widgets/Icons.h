/*
 *  cosmo_v2 by arstro — small line icons matching the glyphs used
 *  in the Figma export (lucide-react: ChevronRight, ChevronDown, PanelLeft,
 *  Save, Upload, Download, RefreshCw, RotateCcw, FolderOpen, Image, X,
 *  Ban, Check, CheckCircle). Artboard has no icon library or SVG-path loader,
 *  so these are hand-drawn approximations of each
 *  glyph's silhouette using plain IRenderTarget path primitives -- not a HAL
 *  change, just composition (per the platform-independence rule's first
 *  preference: express new capability with existing primitives). At the
 *  10-14px sizes these render at, the recognizable shape reads correctly even
 *  though they are not literal copies of Lucide's bezier control points.
 *
 *  Every function fits `box` (local coordinates) and leaves the target's path
 *  state consumed — each call is self-contained (beginPath/.../strokePath). Most
 *  are stroke-only; `pipette` also fills its head, because its source does and
 *  because a hollow head is what makes an eyedropper read as a pen.
 *
 *  ── Two of these are TRACED from a supplied SVG, not approximated ──
 *
 *  `pipette` and `deleteBin` come from artwork the user chose (svgrepo's
 *  "color-picker-dropper-colour" and "delete-2"). Since Artboard has no SVG
 *  loader — and adding one to draw two glyphs would be the wrong trade — they
 *  are transcribed into this file's normalized 0..1 box by dividing each source
 *  coordinate by its viewBox (32 and 24 respectively). The cubic control points
 *  are carried across as they are, so these are the artwork rather than an
 *  impression of it, and the arithmetic that produced each number is written
 *  beside it so the next person can check it against the file.
 */
#pragma once
#include "../../../core/Artboard/src/render/RenderTarget.h"
#include "../../../core/Artboard/src/core/Geometry.h"

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
    /** A rubbish bin — the Mask panel's delete button. Traced from the supplied
     *  `delete-2` artwork (viewBox 24, stroke 2, round joins): a straight-sided can with
     *  rounded bottom corners and a rounded lid handle, which is a different silhouette from
     *  lucide's tapered Trash2 that used to be here. */
    void deleteBin(artboard::IRenderTarget &t, const artboard::Rect &box, const artboard::Color &c, double strokeWidth = 1.1);
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
    /** A pipette / eyedropper — the white-balance picker's tool button (R-WB-1). Traced from
     *  the supplied `color-picker-dropper-colour` artwork (viewBox 32): a filled head, a
     *  crossbar at the neck, and a thin barrel down to a tip at the lower left — the tip points
     *  at what it will sample, which is the orientation every eyedropper cursor has had since
     *  MacPaint. The source is a filled path with the barrel hollowed out; at 10-14 px that
     *  hollow barrel is under a pixel wide, so it is drawn as the stroke it visually is. */
    void pipette(artboard::IRenderTarget &t, const artboard::Rect &box, const artboard::Color &c,
                 double strokeWidth = 1.2);
}
}
}
