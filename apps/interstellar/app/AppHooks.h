/*
 *  interstellar_v1 — AppHooks: the ONLY seam between the front end and the service.
 *
 *  The App draws from `model()` and from nothing else, and it reports intent as TEXT command
 *  lines in the grammar of docs/project-format.md §8 through `dispatch` — the same lines the CLI,
 *  a script file and an agent send. So the GUI cannot do anything a script cannot (R-G-4), and
 *  a second front end needs nothing from this one except the grammar. The frame bytes come
 *  through `renderFrame`, because the model carries no pixels (AppModel rule 1).
 *
 *  The service is NOT linked into the app library: in this build there is no service at all,
 *  which is the point — the integrator wires these three functions to it, and the tests wire
 *  them to a fake that records every line.
 *
 *  `thumbnail` is an OPTIONAL fourth hook, added by the front end and documented in NOTES.md as a
 *  contract extension: the model carries no pixels, so a Home card's cover (RecentModel::coverPath)
 *  and a rack source's filmstrip cell (RackNodeModel::media at ::frame) have nowhere else to come
 *  from. Left empty, both draw an honest placeholder plate — nothing breaks. It is called at most
 *  once per (path, time) and its result cached, so the host should answer from a cache or a
 *  proxy rather than decoding a 4K frame on the UI thread.
 */
#pragma once
#include "../core/Raster.h"
#include "../core/service/AppModel.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar_v1
{
    struct AppHooks
    {
        std::function<const interstellar::AppModel &()> model;
        std::function<bool(const std::string &commandLine, std::string &err)> dispatch;      // project-format.md §8
        std::function<bool(double t, int proxyEdge, interstellar::Raster &out)> renderFrame;  // composited frame at t

        /** OPTIONAL (contract extension): a still of `mediaPath` at `t` seconds, about `edge` px on
         *  its long side. Empty = placeholders. */
        std::function<bool(const std::string &mediaPath, double t, int edge, interstellar::Raster &out)> thumbnail;
        /** OPTIONAL: a counter the host raises whenever a still it could not give at once has
         *  landed. A host that decodes thumbnails off the UI thread returns false from `thumbnail`
         *  until then; the app re-asks for what it is missing when this moves (D-5, D-6). */
        std::function<unsigned()> thumbnailEpoch;
        /** OPTIONAL: one rack source graded alone at `t` seconds (t < 0 = its reference frame) —
         *  what the Grade monitor shows, since Grade has no transport and no playhead (R-UI-3,
         *  amended) and the ref-frame slider previews before it commits (R-RACK-3). Empty = the
         *  Grade monitor falls back to `renderFrame` at the playhead. */
        std::function<bool(const std::string &bind, double t, int proxyEdge, interstellar::Raster &out)> renderSource;
        /** OPTIONAL: put what the monitor shows, at full resolution, on the system clipboard
         *  (R-UI-11 "Copy Frame"); `bind` empty = the timeline at the playhead. Empty = the menu
         *  offers only "Save Frame…". */
        std::function<bool(const std::string &bind, std::string &err)> copyFrame;
        /** R-AUD-7 (optional): a file's waveform envelope — peak of |L|,|R| per 1/perSecond s — once the
         *  service has it (false until then; `model.peaksEpoch` rises when one lands). */
        std::function<bool(const std::string &media, std::vector<float> &peaks, double &perSecond)> audioPeaks;
    };
}
}
