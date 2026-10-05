/*
 *  interstellar_core — Command: the ONE way anything gets into the service (R-SVC-1, R-SVC-2).
 *
 *  The GUI, `interstellar-cc`, a script file and an agent all hand the service the same thing: a
 *  line in the grammar of `docs/project-format.md` §8, parsed here into a tagged Command. The GUI
 *  has no privileged path — it dispatches TEXT, through the same parser (R-G-4).
 *
 *  **The grammar is a TABLE, and everything reads it.** `commandSpecs()` is the parser's input, the
 *  formatter's input, `interstellar-cc api`'s input, and the committed `docs/api.json`'s source
 *  (R-API-1). Cosmo's parser is a hand-written if-chain beside a hand-kept name list; the two agree
 *  because a test makes them, and the usage strings live only in a comment. Here a command that is
 *  not in the table cannot be parsed, and a command that is in it cannot be undocumented.
 *
 *  Shape of a line:
 *
 *      <verb words…> <positional…> [--flag value | --flag=value | --switch]…
 *      set <address>=<value> …
 *
 *  An unknown verb or flag is REJECTED, naming it and the nearest candidates (R-SVC-3): a command
 *  that accepts a key it does not understand and reports success is worse than one that crashes.
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
            // project
            ProjectNew, ProjectOpen, ProjectSave, ProjectClose,
            // rack — the hosted Cosmo project (R-RACK)
            ColourWorking, RackImport, RackAdd, RackGroupNew, RackDuplicate, RackFrame, RackRename, RackSelect,
            // the address space
            Set, Get, Eval, Revert,
            // versions (R-VER)
            TimelineNew, TimelineList, TimelineOpen, TimelinePin, TimelineUnpin, TimelineFreeze,
            TimelineThaw, TimelineRebase, TimelineDiff, TimelineDelete,
            // arrangement (R-TL)
            TrackAdd, ClipAdd, ClipTrim, ClipSplit, ClipMove, ClipDelete, ClipRoll, ClipSlip,
            ClipSpeed, ClipSelect, TransitionAdd, MarkerAdd, ClipCopy, ClipPaste,
            // the image-processing stack (R-FX-5)
            EffectAdd, EffectRemove, EffectMove,
            // temporal effects (R-FX-2)
            FxAdd, FxDelete,
            // audio subset (R-AUD-2)
            AudioTrackAdd, AudioClipAdd,
            // edit, settings, presets (R-EDIT, R-SET) — cosmo's File/Edit/Settings/Preset menus
            Undo, Redo, GradeCopy, GradePaste, RackUngroup, SettingsSet, PresetApply, PresetSave,
            PresetImport, RackRemove, Capture,
            // transport
            Playhead, Play, Pause,
            // delivery (R-RENDER)
            Render, RenderCancel, ExportStill, LutExport, InterchangeExport, InterchangeImport,
            Shuttle, Mark, SourceView, SourcePlayhead, EditTarget, EditInsert, EditOverwrite, MulticamNew, MulticamAngle,
            ProxyMake, ProxyRemove, ProxyUse, MediaOffline, MediaRelink, ViewMatte, TrackWindow, TrackCancel,
            StillGrab, StillApply, StillDelete, ViewWipe, NodeSerial, NodeParallel, NodeRemove,
            ProjectAutosave, ProjectRecover, RenderPresetSave, RenderPresetDelete, RenderPresetList,
            // the graded preview cache (R-PLAY-1)
            CacheBuild, CacheClear,
            // keyframes (R-ANIM)
            KeyAdd, KeyRemove, KeySet, KeyClear, KeyShift, KeyCopy, KeyPaste,
            // introspection
            StatePrint, Api, Lint, Wait, Quit
        };

        Kind kind = Kind::None;
        std::vector<std::string> args;                               // positionals after the verb
        std::vector<std::pair<std::string, std::string>> flags;      // --name value; a switch = "1"
        std::vector<std::pair<std::string, std::string>> fields;     // `set`: address=value pairs

        bool valid() const { return kind != Kind::None; }
        std::string arg(size_t i, const std::string &fallback = std::string()) const;
        bool has(const std::string &flag) const;
        std::string flag(const std::string &name, const std::string &fallback = std::string()) const;
    };

    /** One row of the grammar. `flags` entries are `name` (a switch) or `name=<hint>` (valued). */
    struct CommandSpec
    {
        Command::Kind kind;
        std::string verb;               // "timeline pin"
        std::string args;               // "<tl>" — the positional hint, for usage and the API doc
        int minArgs = 0, maxArgs = 0;   // positional count; -1 = unbounded
        std::vector<std::string> flags;
        std::string summary;
        std::string requirement;        // the R- id that asked for it
        bool fieldArgs = false;         // positionals are address=value pairs (`set`)
    };

    const std::vector<CommandSpec> &commandSpecs();
    const CommandSpec *specFor(Command::Kind k);

    /** The usage line for one row: `timeline pin <tl> [--commit <c>]`. */
    std::string usageOf(const CommandSpec &s);

    /** Parse one line. Blank lines and `;` / `#` comments parse to None with an EMPTY `err`, so a
     *  script reads naturally — a caller distinguishes failure by `err`. */
    Command parseCommand(const std::string &line, std::string &err);

    /** The inverse; `parseCommand(formatCommand(c))` reproduces `c` (the journal relies on it). */
    std::string formatCommand(const Command &c);

    /** Up to three known words nearest to `word` by edit distance, for "did you mean". */
    std::vector<std::string> nearest(const std::string &word, const std::vector<std::string> &pool);

    /** Split a line into tokens, honouring "double quotes". Shared with the address parser. */
    std::vector<std::string> tokenize(const std::string &line);

    /** Quote a token if it needs it (space, quote, `;`, `#`, or empty). */
    std::string quoteToken(const std::string &s);
}
}
