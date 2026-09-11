/*
 *  interstellar_core — AppModel: the entire observable state of Interstellar, as plain data.
 *
 *  R-SVC-3. The ONE way state leaves the service. A front end renders the model and reacts to
 *  events; it computes nothing it could be told. Three rules keep it honest:
 *
 *  1. **No pixels.** A 4K frame is 33 MB. A frame is referenced by size + a monotonic `frameSeq`;
 *     the bytes stay with the service and a view fetches them through `takeFrame`.
 *  2. **No Artboard types, no presentation.** Easing, hover, scroll offsets and which frame the
 *     playhead is animating toward are the VIEW's business (R-SVC-4). The service says the
 *     playhead is at 4.271 s; the view knows how to travel there.
 *  3. **`revision` increments on every change**, so a view that has drawn revision N can skip
 *     work and a test can assert that a command changed something.
 *
 *  **R-COSMO-4 is AMENDED here — see the note on `rack` below.**
 */
#pragma once
#include "../Project.h"
#include "../RackAccess.h"
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
    enum class Workspace { Grade, Cut, Mix, Deliver };
    const char *workspaceName(Workspace w);

    struct TrackModel
    {
        NodeId id, name;
        bool audio = false;
        int order = 0;
        bool mute = false;
        double opacity = 1.0;
    };

    struct ClipModel
    {
        NodeId id, name, track;
        std::string src, srcName;      // srcName = the rack object's bind name, for display
        double at = 0, in = 0, out = 0, speed = 1.0, opacity = 1.0;
        double duration = 0;
        bool srcOffline = false;       // reads as missing, never as a stall (R-RACK-5)
    };

    struct LaneModel
    {
        std::string address;
        std::vector<NodeId> links;     // the autolinks on this address, in time order
    };

    struct BindingModel
    {
        std::string target, expr;
        std::vector<std::string> deps;
        bool broken = false;
        std::string brokenWhy;
    };

    /** One rack node, as a view needs it.
     *
     *  **AMENDED (implementation, 2026-09-11): this is a PROJECTION of the rack, not
     *  `cosmo::AppModel` carried by value.** R-COSMO-4 said the Cosmo model would be a member,
     *  to avoid a second copy of one fact (R-G-3). Carrying it turned out to force `cosmo_core`
     *  — and therefore GTK3, GdkPixbuf and LibRaw — onto `AppModel.h`, which every file in this
     *  library and every test includes. That made the core suite need a display-capable host to
     *  LINK, for state no view reads.
     *
     *  So the rack reaches the model through the `RackAccess` seam instead, and R-G-3 is
     *  satisfied a different way: **these fields are not a copy, they are a read.** Nothing here
     *  is writable, nothing is cached across a revision, and the authority is still the one
     *  hosted `CosmoService`. What the amendment costs is that a view wanting a Cosmo field this
     *  struct does not carry must ask for it to be added — which is the normal cost of a
     *  projection, and cheaper than a library-wide dependency. */
    struct RackNodeModel
    {
        std::string node;        // the Cosmo node id
        std::string bindName;    // Interstellar's (R-PARAM-2); Cosmo's own name may have spaces
        std::string cosmoName;
        int depth = 0;
        bool group = false, bypass = false, pending = false, failed = false;
        bool referenced = false; // a clip uses it. False is NOT an error (R-COSMO-10)
        double gradeWeight = 1.0;
    };

    struct RenderModel
    {
        bool active = false;
        int done = 0, total = 0, failures = 0;
        std::string outPath;
    };

    struct AppModel
    {
        unsigned revision = 0;

        std::string projectPath, projectName;
        bool dirty = false;
        Workspace workspace = Workspace::Cut;

        double fps = 24.0;
        int width = 3840, height = 2160;
        double duration = 0;

        // ── the rack (see RackNodeModel's note) ──
        std::vector<RackNodeModel> rack;
        bool rackPinned = false;
        std::string rackPinCommit, rackPath;

        std::vector<TrackModel> tracks;
        std::vector<ClipModel> clips;
        std::vector<LaneModel> lanes;
        std::vector<BindingModel> bindings;

        /** The resolved value of every address the current frame used (R-EVAL-3). A number the
         *  renderer used and nobody can read back is the R-CPU-4 mistake, and this app computes
         *  hundreds of them per frame. */
        std::vector<std::pair<std::string, double>> resolved;
        std::vector<std::string> lint;

        double playhead = 0;
        long long playheadFrame = 0;
        bool playing = false;

        // Frame METADATA only — never pixels (rule 1).
        int frameWidth = 0, frameHeight = 0, frameLayers = 0;
        unsigned frameSeq = 0;
        double frameMs = 0;        // MACHINE-DEPENDENT -> excluded from the stable dump

        RenderModel render;
        ProjectSettings settings;
        std::string lastError;
    };
}
}
