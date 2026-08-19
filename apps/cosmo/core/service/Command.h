/*
 *  Arstro cosmo_core — Command: the ONE way anything gets into the service (R-SVC-2).
 *
 *  A front end never calls EditSession, never mutates an EditParams, never opens a file.
 *  It builds a Command and dispatches it. A behaviour a front end can reach that no
 *  Command expresses is a defect in this enum, not a licence to reach past it.
 *
 *  The struct is the truth and the text form is GENERATED from it (R-SVC-5) — one parser,
 *  one formatter, shared by the CLI, `--script` files, the control socket and the journal.
 *  Two representations maintained by hand would drift the first time one grew a field, and
 *  this project already learned that lesson with a log line nobody had run.
 *
 *  Note what does NOT need its own command: curves, the colour mixer, colour grading and every
 *  scalar are already addressable through `set`, because `EditParamsIO` names them
 *  (`set curve=0,0;0.5,0.62;1,1`, `set mixer0=…`, `set grade0=210,18,-4`, `set rotation=1.5`).
 *  `set mask=<blob>` APPENDS a mask, which is why `mask set <i>` / `mask delete <i>` exist —
 *  addressing an existing one by index is the single thing the params codec cannot express.
 *
 *  The grammar is deliberately flat and line-oriented, because every consumer of it is a
 *  shell, a file, or a socket:
 *
 *      project open /tmp/japan18.cmp
 *      project save
 *      import /photos/a.RAF /photos/b.RAF
 *      select 3
 *      select next
 *      set exposure=1.2 temp=7000
 *      bypass 3 on
 *      group new "Tokyo"
 *      undo
 *      preset apply "Portrait/Soft Skin"
 *      export --outdir /tmp/out --format jpg --quality 90 --long-edge 2048
 *      settings set cpuPercent=25 previewEdge=1600
 *      screen home
 *      state print
 *      wait load.finished --timeout 120s
 *      quit
 */
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace cosmo
{
    struct Command
    {
        enum class Kind
        {
            None,
            ProjectOpen,     // path
            ProjectNew,      // path
            ProjectSave,     // path (empty = current)
            ProjectClose,
            Import,          // paths[]
            Select,          // index = node id
            SelectNext,
            SelectPrev,
            Set,             // fields: EditParams key=value
            Bypass,          // index = node, flag = on
            GroupNew,        // name
            GroupUngroup,    // index = node
            GroupRename,     // index = node, name
            Delete,          // index = node (-1 = the current selection)
            MaskSet,         // index = mask, fields (feather / inverted / geometry / adjust.*)
            MaskDelete,      // index = mask
            Undo,
            Redo,
            PresetApply,     // name
            PresetSave,      // name
            Export,          // path = outDir, fields: format/quality/long-edge
            SettingsSet,     // fields: cpuPercent / previewEdge / threads / useGpu
            Screen,          // name = "home" | "editor"
            StatePrint,      // flag = json
            UiDump,          // flag = json, name = root, fields: visible / depth
            Wait,            // name = condition, index = timeout ms
            Quit
        };

        Kind kind = Kind::None;
        std::string path;
        std::string name;
        int index = -1;
        bool flag = false;
        std::vector<std::string> paths;                                // Import
        std::vector<std::pair<std::string, std::string>> fields;       // Set / SettingsSet / Export

        bool valid() const { return kind != Kind::None; }
        /** Value for `key` in `fields`, or `fallback`. */
        std::string field(const std::string &key, const std::string &fallback = std::string()) const;
    };

    /** Parse one line. Returns a Command with `kind == None` and fills `err` on failure.
     *  Blank lines and `#` comments parse to None with an empty `err`, so a script file can
     *  be commented — callers distinguish by whether `err` is empty. */
    Command parseCommand(const std::string &line, std::string &err);

    /** The inverse, for the journal and for round-trip tests. */
    std::string formatCommand(const Command &c);

    /** Every command name the grammar accepts, for `--help` and for the R-SVC-9 coverage
     *  test that fails when a behaviour has no command. */
    const std::vector<std::string> &commandNames();
}
}
