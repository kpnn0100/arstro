/*
 *  interstellar_core — AppModel: the whole observable state of Interstellar, as plain data.
 *
 *  **This header is a FROZEN CONTRACT between the service and the UI.** The UI is built against it
 *  without linking the service at all: a widget reads `const AppModel &` and reports intent through
 *  a callback; the App turns that intent into a TEXT COMMAND LINE in the same grammar the CLI and an
 *  agent use (`docs/project-format.md` §8) and hands it to a `dispatch` hook. So the GUI cannot do
 *  anything a script cannot — R-G-4 holds by construction rather than by discipline.
 *
 *  Three rules keep it honest:
 *   1. **No pixels.** A frame is referenced by `frameSeq`; the bytes come through a render hook.
 *   2. **No presentation.** Easing, hover, scroll offsets, which tab is open — the view's business.
 *      The model says the playhead is at 4.271 s; the view knows how to travel there.
 *   3. **`revision` rises on every change**, so a view that has drawn revision N can skip work and a
 *      test can assert that a command changed something.
 *
 *  Adding a field is fine. Renaming or removing one is a contract change and needs both sides.
 */
#pragma once
#include "engine/EditParams.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace interstellar
{
    using NodeId = std::string;

    /** Which surface is up. Derived by the service from whether a project is open — a TAB is not
     *  here, because which tab is showing is presentation (rule 2). */
    enum class Screen { Home, Loading, Edit };

    /** One card on Home. A projection of the recents index, so the view never reads a file. */
    struct RecentModel
    {
        std::string name, path;
        std::string coverPath;        // first frame of the project's first source; "" = none
        int sourceCount = 0;
        long long sizeBytes = 0;
        long long lastOpened = 0;     // unix seconds
    };

    /** One node of the rack — the colour authority, hosted Cosmo — flattened in tree order. */
    struct RackNodeModel
    {
        int node = -1;                // the Cosmo node id
        NodeId rackObj;               // the #rackobj id (Interstellar's)
        std::string bindName;         // `gr1`, `s_day01` — what an ADDRESS spells (R-RACK-6)
        std::string cosmoName;        // Cosmo's own name; may contain spaces. For display.
        int parent = -1;              // index into AppModel::rack, -1 at the root
        int depth = 0;
        bool group = false;
        bool bypass = false;
        bool pending = false;         // pixels still decoding
        bool failed = false;          // offline: reads as MISSING, never as a stall (R-RACK-7)
        double weight = 1.0;          // grade weight, 0..1 — a continuous bypass (R-RACK-4)
        std::string media;            // source file
        double frame = 0.0;           // reference frame a video is graded on, seconds (R-RACK-3)
        bool video = false;
        int usedBy = 0;               // clips referencing it in the CURRENT timeline. 0 is not an error
        bool overridden = false;      // the current timeline has a #tlgrade on this node (R-VER)
        bool selected = false;        // in the selection (Shift = range, Ctrl = toggle — R-RACK-8)
        double mediaDuration = 0;     // seconds of source; 0 = a still, or not yet opened (R-RACK-3)
        double mediaFps = 0;          // the source's own frame rate once opened — a ref-frame step is 1/mediaFps (R-RACK-3)
        int sharesMedia = 0;          // OTHER sources on the same file (a variant and its original, R-RACK-5)
    };

    /** One timeline — a VERSION (R-VER). Ordered so a base precedes everything derived from it. */
    struct TimelineModel
    {
        NodeId id, name;
        NodeId base;                  // "" = a root
        int depth = 0;                // depth in the version tree, for indenting the switcher
        bool colourPinned = false;
        std::string pinCommit;        // when pinned
        bool cutFrozen = false;
        int danglingDeltas = 0;       // deltas whose target the base deleted — `rebase` reports them
        int overrides = 0;            // #tlset + #tlgrade count — "how far this version has diverged"
    };

    /** Where a node in the RESOLVED current timeline came from. Drawn differently, because "this
     *  clip is the base's" and "this clip is mine" ask the editor for different things. */
    enum class Provenance { Local, Inherited, Overridden, Dangling };

    struct TrackModel
    {
        NodeId id, name;
        bool audio = false;
        int order = 0;
        bool mute = false;
        double opacity = 1.0;
        double gain = 0.0;            // dB, audio tracks
        Provenance provenance = Provenance::Local;
    };

    struct ClipModel
    {
        NodeId id, name, track;
        NodeId src;                   // the #rackobj id (video) or a media path (audio)
        std::string srcName;          // the rack node's bind name, for the clip label
        double at = 0, in = 0, out = 0, speed = 1.0;
        double duration = 0;          // timeline seconds — derived, (out-in)/speed
        double opacity = 1.0;
        double gain = 0.0;            // audio clips, dB
        bool audio = false;
        bool offline = false;
        Provenance provenance = Provenance::Local;
    };

    struct TransitionModel
    {
        NodeId id, clipA, clipB;
        std::string kind;             // dissolve | dip
        double dur = 0.5;
    };

    struct MarkerModel
    {
        NodeId id, name;
        double at = 0;
        std::string note;
    };

    struct RenderJobModel
    {
        NodeId id;
        NodeId timeline;              // ALWAYS named — a render never implies "current" (R-RENDER-1)
        std::string timelineName;
        std::string outPath, format;  // h264 | h265 | prores | dnxhr | png-seq
        int done = 0, total = 0;
        int width = 0, height = 0;    // the output frame (R-RENDER-6)
        double fps = 0;               // the output rate the timeline is sampled at
        std::string spec;             // the whole spec in words: "H.265 10-bit · q18 · medium · 1920×1080 · 25 fps"
        std::string state;            // queued | running | done | failed | cancelled
        std::string error;
    };

    /** Engine settings, as Cosmo's Engine Settings dialog shows them (R-SET). Persisted by the
     *  service in the host's settings file; one CPU budget serves the hosted Cosmo rack and
     *  Interstellar's own render path. */
    struct SettingsModel
    {
        int cpuPercent = 50;          // share of the machine's cores the app may schedule (R-SET-2)
        int threads = 0;              // engine worker threads; 0 = auto (from cpuPercent)
        int previewEdge = 1600;       // monitor/preview render long edge, px; a cap, never an upscale
        bool useGpu = false;
        int uiScale = 100;            // percent of the design size — the host draws through it
        // ── measured, read-only ──
        bool gpuAvailable = false;
        int cores = 0, engineThreads = 0, decodeWorkers = 0;
    };

    /** One preset in the shared library (Cosmo's `.apf`), for the Preset menu. */
    struct PresetModel
    {
        std::string name;             // what `preset apply <name>` spells ("Portrait/Soft")
        std::string folder;
    };

    struct AppModel
    {
        unsigned revision = 0;
        Screen screen = Screen::Home;

        std::vector<RecentModel> recents;

        // ── the open project ──
        std::string projectPath, projectName;
        bool dirty = false;
        double fps = 24.0;
        int width = 1920, height = 1080;

        // ── Grade: the rack ──
        std::vector<RackNodeModel> rack;
        int selectedRack = -1;        // index into `rack`, -1 = none
        /** The selected node's params, as Cosmo's panels show them: `gradeParams` is the stacked
         *  reach (effective), `gradeOwnParams` is what an edit changes. Both, because they differ
         *  and a slider needs both — exactly cosmo's own model. */
        EditParams gradeParams;
        EditParams gradeOwnParams;
        bool hasGradeTarget = false;
        int sourceWidth = 0, sourceHeight = 0;

        // ── Cut: versions and the resolved current timeline ──
        std::vector<TimelineModel> timelines;
        NodeId currentTimeline;
        std::vector<TrackModel> tracks;
        std::vector<ClipModel> clips;
        std::vector<TransitionModel> transitions;
        std::vector<MarkerModel> markers;
        NodeId selectedClip;
        double duration = 0;

        // ── transport ──
        double playhead = 0;
        long long playheadFrame = 0;
        bool playing = false;

        // ── frame metadata. Never pixels (rule 1). MACHINE-DEPENDENT: excluded from stable dumps. ──
        unsigned frameSeq = 0;
        int frameWidth = 0, frameHeight = 0;

        // ── Deliver ──
        std::vector<RenderJobModel> renders;

        // ── Edit: one undo history across the rack and the project (R-EDIT-1) ──
        bool canUndo = false, canRedo = false;
        std::string undoLabel, redoLabel;   // "set a.basic.exposure", "clip move shotA"
        bool hasGradeClipboard = false;     // `grade copy` has something to paste
        std::string gradeClipboardFrom;     // the bind name it was copied from
        bool hasClipClipboard = false;      // `clip copy` has something to paste (R-TL-6)
        std::string clipClipboardFrom;      // the clip it was copied from

        // ── Settings + Preset ──
        SettingsModel settings;
        std::vector<PresetModel> presets;

        // ── failure, inspectable after the fact by a front end that was not listening ──
        std::string lastError;
    };
}
}
