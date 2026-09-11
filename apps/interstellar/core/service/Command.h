/*
 *  interstellar_core — Command: the ONE way anything gets into the service (R-SVC-2).
 *
 *  A front end never mutates the timeline, never touches an EditParams, never opens a file. It
 *  builds a Command and dispatches it. A behaviour a front end can reach that no Command
 *  expresses is a defect in this enum, not a licence to reach past it.
 *
 *  Note what needs NO command of its own: every scalar, every geometry value, every opacity and
 *  every rack parameter is reachable through `set <address>=<value>`, because ParamRegistry
 *  names them all and says which service owns each. That is the economy the address space buys
 *  — the enum stays readable while the parameter count runs into the hundreds.
 *
 *  The grammar is flat and line-oriented, because every consumer is a shell, a file or a socket:
 *
 *      project new /tmp/cut.isp --fps 24 --res 3840x2160
 *      rack import japan18.cmp
 *      set gr1.basic.exposure=0.2           (routed to the hosted CosmoService — R-COSMO-3)
 *      track add --kind video --name v0
 *      clip add --track v0 --src rack:cn_41 --in 12.4 --out 16.6 --at 0 --name clp_a
 *      clip split clp_a --at 2.0
 *      auto new ac_push --dur 2.0 --points 0=0,1=1 --ease easeInOut
 *      auto link ac_push -> gr1.basic.exposure --at 4.0 --from 0 --to 0.8
 *      bind gr1.opacity = clamp(gr1.basic.exposure / 2 + 0.5, 0, 1)
 *      eval gr1.opacity --at 4.5 --explain
 *      state print --stable
 *      quit
 */
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace interstellar
{
    struct Command
    {
        enum class Kind
        {
            None,
            ProjectNew, ProjectOpen, ProjectSave, ProjectClose,
            RackImport, RackNew, RackPin, RackUnpin, RackAdd, RackGroupNew, RackDuplicate,
            RackRename, RackSelect,
            Set, Get, Eval,
            TrackAdd, ClipAdd, ClipTrim, ClipSplit, ClipMove, ClipDelete, ClipRoll, ClipSlip,
            TransitionAdd, MarkerAdd, Rename,
            AutoNew, AutoPointSet, AutoLinkAdd, AutoUnlink, AutoLanes,
            BindSet, BindDelete, BindList,
            Playhead, Play, Pause, Render, ExportStill, Lint,
            SettingsSet, StatePrint, Api, Wait, Quit
        };

        Kind kind = Kind::None;
        std::string path;     // a file path
        std::string name;     // the primary subject: an object, an address, a condition
        std::string name2;    // a second subject: the second clip of a roll, a link's target
        std::string text;     // free text: an expression, a note
        int index = -1;
        double ms = 0.0;      // a time, where one is needed
        bool flag = false;
        std::vector<std::string> paths;
        std::vector<std::pair<std::string, std::string>> fields;

        bool valid() const { return kind != Kind::None; }
        std::string field(const std::string &key, const std::string &fallback = {}) const;
        double fieldNum(const std::string &key, double fallback) const;
    };

    /** Parse one line. `kind == None` with an empty `err` is a blank or `#`-commented line — a
     *  successful no-op, so a script file reads naturally. `kind == None` with a non-empty `err`
     *  is a rejection, and the rejection NAMES what it did not understand: a command that
     *  accepts a key it does not know and reports success is worse than one that crashes,
     *  because an agent believes the edit landed (R-SVC-6, cosmo's D-59). */
    Command parseCommand(const std::string &line, std::string &err);
    std::string formatCommand(const Command &c);

    /** Every command name the grammar accepts — walked by the coverage test, so a behaviour
     *  with no command fails a build rather than being noticed later by a human. */
    const std::vector<std::string> &commandNames();

    /** The argument hint for each name, GENERATED beside it rather than hand-maintained.
     *  Cosmo's `--help` is the experiment already run for the alternative: names generated and
     *  always right, hints hand-written and missing for 8 of 30. */
    struct CommandSpec { const char *name; const char *args; const char *what; };
    const std::vector<CommandSpec> &commandSpecs();
}
}
