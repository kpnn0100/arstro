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

    struct LoadModel
    {
        bool active = false;
        int done = 0;
        int total = 0;
        std::string status;     // "Loading  DSCF5186.RAF" — what, not how far
        int workers = 0;        // the pool the budget allotted (R-CPU-4)
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
        int frameSlot = -1;
        int frameWidth = 0, frameHeight = 0;
        unsigned frameSeq = 0;

        std::string lastError;      // the most recent rejected command or failure
        bool gpuAvailable = false;
        bool gpuActive = false;
    };
}
}
