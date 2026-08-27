/*
 *  Arstro cosmo_core — AppModel: the entire observable state of cosmo, as plain data.
 *
 *  R-SVC-3. This is the ONE way state leaves the service. A front end renders the model
 *  and reacts to events; it computes nothing it could be told. That is what makes a GUI
 *  and a CLI "look at the same thing" checkable rather than hoped for: `cosmo-cc state
 *  print` dumps exactly what the window is drawing from.
 *
 *  Three rules keep it honest:
 *
 *  1. **No pixels.** A preview frame is 5-20 MB and a full-res one ~100 MB. Frames are
 *     referenced by slot + size + a monotonic `frameSeq`; the bytes stay in the engine and
 *     the view fetches them through RenderService as it always did.
 *  2. **No Artboard types, no presentation.** Positions, easing, hover, scroll offsets and
 *     transition phase are the VIEW's business (R-SVC-4). The model says a load is 6 of 18;
 *     the view knows the bar eases toward it. R-G-1 is untouched by any of this.
 *  3. **`revision` increments on every change.** A view that has already drawn revision N
 *     can skip work; a test can assert that a command changed something.
 *
 *  Deliberately a struct of values, not an interface: it is copied into a snapshot for the
 *  CLI, diffed in tests, and formatted to text by AppModelCodec. Nothing here has
 *  behaviour.
 */
#pragma once
#include "../AppSettings.h"
#include "../ProjectStore.h"
#include "engine/EditParams.h"
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace cosmo
{
    /** Which surface is up. Matches App::Screen's meaning, minus the transition phases,
     *  which are presentation (R-SVC-4). */
    enum class Screen
    {
        Splash,
        Home,
        Loading,
        Editor
    };

    const char *screenName(Screen s);

    /** One row of the home grid (R-HOME). A projection of ProjectStore's RecentEntry, so
     *  the view never reads the store itself. */
    struct RecentModel
    {
        std::string name;
        std::string path;
        std::string firstImagePath;
        int photoCount = 0;
        long long sizeBytes = 0;
        long long lastOpened = 0;
    };

    /** One node of the group tree, flattened in tree order with its parent's index. The
     *  view draws the filmstrip and the breadcrumb from this; `slot < 0 && pending` is a
     *  placeholder that has not decoded yet (R-LOADUX-1/2). */
    struct NodeModel
    {
        int node = -1;          // EditSession node id — the handle commands use
        int parent = -1;        // index into AppModel::nodes, -1 for a root child
        int depth = 0;
        bool group = false;
        std::string name;
        int slot = -1;          // engine slot, -1 while pending or failed
        bool pending = false;   // placeholder awaiting pixels
        bool failed = false;    // decode failed: reads as missing, not as a stall
        bool bypass = false;    // R-BYPASS
        bool selected = false;
    };

    /** R-LOADUX-4. Three numbers, not one, because a count of finished entries is not progress
     *  when one entry takes nine seconds: five workers claim together and finish together, so
     *  `done` alone sat at 0 for 9.1 s and then leapt by five (D-22). `started` is the earliest
     *  honest signal there is — a RAW decode reports no internal progress, so cosmo says what it
     *  knows and lets the view animate the uncertainty rather than inventing a percentage. */
    struct LoadModel
    {
        bool active = false;
        int done = 0;           // entries applied, in order
        int started = 0;        // entries a worker has claimed; >= done
        int total = 0;
        std::string status;     // "Loading  DSCF5186.RAF" — what, not how far
        std::string stage;      // "reading" | "decoding" | "saving" | "" — the named phase
        int workers = 0;        // the pool the budget allotted (R-CPU-4)
        /** In flight right now: claimed but not yet applied. The view draws this slice of the
         *  bar as work rather than as emptiness, which is what stops it reading as a stall. */
        int inFlight() const { return started > done ? started - done : 0; }

        /** Summed sub-image progress of the in-flight entries, 0..inFlight() (D-24). One RAF
         *  decode is ~8.3 s and 90% of it is a single `dcraw_process()`, so `done` alone could
         *  not move for nine seconds at a stretch; LibRaw reports its own demosaic iterations
         *  and this is their sum. */
        double partial = 0.0;
        /** What the bar should actually fill to: finished entries plus the fraction of the ones
         *  being worked on. Monotonic within a load, and never past 1. */
        double fraction() const
        {
            if (total <= 0) return 0.0;
            const double f = ((double)done + partial) / (double)total;
            return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
        }
        /** The deepest stage any in-flight entry has reached — "demosaicing", "reading raw" —
         *  so the status line can say what is happening, not just to which file. */
        std::string entryStage;
    };

    struct ExportModel
    {
        bool active = false;
        int done = 0;
        int total = 0;
        int failures = 0;
        std::string name;       // the image currently being written
        std::string outDir;
    };

    struct HistoryModel
    {
        int nodes = 0;          // size of the branching DAG for the edit target
        int current = -1;
        bool canUndo = false;
        bool canRedo = false;
        std::string lastLabel;  // History::describeChange of the newest step
    };

    /** What the CPU budget resolved to, so it is inspectable rather than inferred — the
     *  whole point of R-CPU-4 as amended (D-11/D-12 shipped because nobody could see it). */
    struct BudgetModel
    {
        int percent = 50;
        int cores = 0;
        int total = 0;
        int engineThreads = 0;
        int decodeWorkers = 0;
        int peakDecode = 0;
        /** R-MEM-4: what the engine's two pixel pools actually hold, and how many times
         *  eviction has forced a re-decode. Measured on the render worker and published
         *  here, because "memory is bounded" is the kind of claim this project has already
         *  shipped once without anyone ever reading the number back (R-CPU-4). */
        size_t residentBytes = 0;
        int rehydrations = 0;
    };

    struct AppModel
    {
        unsigned revision = 0;

        Screen screen = Screen::Home;
        std::string projectPath;    // "" = no project open
        std::string projectName;
        bool dirty = false;

        std::vector<RecentModel> recents;
        std::vector<NodeModel> nodes;

        int selectedNode = -1;      // the node the user picked
        int currentSlot = -1;       // the image on the stage, -1 if none
        int editGroup = -1;         // >= 0 while a group rather than an image is the edit target
        int imageCount = 0;

        /** The edit target's params as the panels should show them — `effectiveEditParams`,
         *  which honours bypass on ancestors but not on the target itself. Deliberately not
         *  `effectiveParams`: those two differ on purpose (see EditEngine's invariants). */
        EditParams params;
        /** The target's OWN params — `curParams()`. Both are in the model because the panels
         *  need both and they differ: a slider shows the stacked reach (`params`) while an edit
         *  is applied to the item's own value (`ownParams`). `hasEditTarget` is false when
         *  nothing is selected, which is what `curParams()` returning nullptr used to say — a
         *  view cannot read a null through a value model, so the flag carries it (R-SVC-3). */
        EditParams ownParams;
        bool hasEditTarget = false;

        HistoryModel history;
        LoadModel load;
        ExportModel exports;
        AppSettings settings;
        BudgetModel budget;

        /** Metadata only — see rule 1. `frameSeq` rises each time a preview lands, so a view
         *  knows to re-fetch and a test can wait for one. **The service is what polls** (S4c):
         *  `tryAcquire` MOVES the frame out, so exactly one owner may call it, and while that
         *  owner was the view no headless front end could tell a preview had arrived (D-21).
         *  `pump()` acquires; `takeFrame()` hands it on. */
        /** The SELECTED photo's full-resolution pixel size, {0,0} when nothing is selected.
         *
         *  Not the frame's size — that is the rendered preview of the *framed* (cropped,
         *  rotated) image and so cannot answer "what shape is the original". A crop aspect
         *  ratio needs exactly this: `EditParams`' crop is normalised 0..1 per axis, so a
         *  requested 16:9 is `cropW/cropH = 16/9 * sourceHeight/sourceWidth`. Without it a UI
         *  can only assume a square, which is what `XformPanel` did — and said so in a comment
         *  rather than being able to fix it (R-CROP-1). */
        int sourceWidth = 0, sourceHeight = 0;

        /** R-INFO: the last photo whose metadata was asked for, and what it said. Filled by the
         *  `metadata` command and left alone otherwise — reading a file to print its ISO should
         *  not happen every time somebody arrows through the filmstrip, so this is on demand and
         *  not part of the selection.
         *
         *  Label/value pairs rather than typed fields: the set differs per format, and the panel's
         *  job is to show what the file carries. `metadataNode` says which photo they belong to, so
         *  a view can tell "no metadata" from "metadata for the photo you were last looking at". */
        int metadataNode = -1;
        std::string metadataName;
        std::vector<std::pair<std::string, std::string>> metadata;

        int frameSlot = -1;
        int frameWidth = 0, frameHeight = 0;
        unsigned frameSeq = 0;
        /** Which pyramid level the last frame came from, 0 = full (R-PREVIEW-2), and that
         *  level's long edge. A view needs both: the level to know whether a refinement is
         *  still coming, the edge because a coarse frame is drawn into the SAME canvas rect
         *  as a full one and is therefore upscaled on the way to the screen.
         *
         *  MACHINE-DEPENDENT, so excluded from the stable dump: two services given the same
         *  commands on a fast and a slow box are in the same *state* while showing different
         *  levels — which is the entire point of R-PREVIEW-2, and would otherwise break
         *  R-SVC-9's "identical commands, identical stable text". */
        int frameLevel = 0;
        int frameLevelEdge = 0;
        /** True while the settle-and-refine walk still has a step to take (R-PREVIEW-3), so
         *  a view can say "sharpening" rather than guessing from the level. */
        bool refining = false;
        /** How many levels the pyramid has, and the interactive latency budget in ms. Both
         *  are stable — they are the CONTRACT, not the outcome. */
        int previewLevels = 1;
        double interactiveBudgetMs = 0.0;
        /** The measured cost the level choice is actually made from, ms per megapixel — 0
         *  until the first frame lands. R-PREVIEW-6 asks for the budget to be readable back
         *  rather than asserted; this is that number. Machine-dependent, so unstable. */
        double msPerMegapixel = 0.0;

        std::string lastError;      // the most recent rejected command or failure
        bool gpuAvailable = false;
        bool gpuActive = false;
    };
}
}
