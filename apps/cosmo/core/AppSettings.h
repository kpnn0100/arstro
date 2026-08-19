/*
 *  cosmo_core by arstro — AppSettings: the handful of engine preferences that must
 *  survive a restart (R-SETTINGS-4).
 *
 *  Turning GPU acceleration on, or picking a preview quality, is a statement about
 *  the machine, not about the session — having it silently revert on every launch
 *  makes the setting feel broken. These live in the same user config dir as the
 *  recent-projects index, in the same plain `key=value` shape the workspace format
 *  uses, so there is one file convention to understand across the app.
 *
 *  Platform-free (std::filesystem + fstream only), so it sits in cosmo_core beside
 *  ProjectStore rather than in the GTK host.
 */
#pragma once
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo
{
    struct AppSettings
    {
        int previewEdge = 1600;   // base preview render long edge, px (R-SETTINGS-1)
        int threads = 0;          // engine worker threads; 0 = auto (then the budget below applies)
        bool useGpu = false;      // GPU acceleration opt-in (R-GPU-3)
        int cpuPercent = 50;      // share of the machine's cores cosmo may schedule (R-CPU-1)
        /** UI scale, PERCENT of the design size (R-SCALE-1). 100 = the Figma sizes; below
         *  that the whole shell is drawn smaller so a small panel fits more of the app, above
         *  it larger for a dense display or a user who wants bigger controls.
         *
         *  Percent rather than a double on purpose: this file is plain `key=value` text a
         *  person may edit, and "90" cannot round-trip wrong the way "0.8999999" can. It is
         *  the ONLY presentation value in this struct, and it is here because it is a
         *  statement about the machine (R-SETTINGS-4) — the service stores and forwards it and
         *  never acts on it, since the layout it scales is the view's (R-SVC-3). */
        int uiScale = 100;

        /** Legal UI scales, in the order the settings row offers them. */
        static const std::vector<int> &uiScales();
        /** `percent` snapped to the nearest offered scale — a hand-edited 83 becomes 90
         *  rather than a size nothing was ever laid out or rendered at. */
        static int clampUiScale(int percent);

        /** Worker count for `percent` of this machine's logical cores (R-CPU-1).
         *
         *  A CPU budget is offered to the user as a percentage because that is the honest
         *  unit for "how much of my computer may this take", but it can only be ENFORCED as
         *  a thread count: no portable per-process CPU-time cap exists across Linux/Windows/
         *  Android, and throttling by sleeping would occupy the very cores it is sparing.
         *
         *  Rounds to nearest and never returns 0, so the smallest budget on the smallest
         *  machine still makes progress. `cap <= 0` means uncapped. */
        static int workersFor(int percent, int cap = 0);

        /** Path of the settings file inside ProjectStore::configDir(). */
        static std::string path();
        /** Load, falling back to the defaults above for anything missing or malformed —
         *  a corrupt or partial file must never stop the app from starting. */
        static AppSettings load();
        /** Persist. Returns false if the file could not be written. */
        bool save() const;
    };
}
}
