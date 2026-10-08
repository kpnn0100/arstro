/*
 *  solaris_core — Command: the ONE way anything gets into the service (R-SVC-1, R-SVC-2, R-G-4).
 *
 *  The GUI, `solaris-cc`, a script file and an agent all hand the service the same thing: a TEXT
 *  line in the grammar of `docs/project-format.md` §12, parsed here into a tagged Command. A
 *  drag-and-drop in the UI is one of these lines; there is no other door.
 *
 *  **The grammar is a TABLE, and everything reads it**: `commandSpecs()` is the parser's input, the
 *  formatter's input, and the generated API document's source (R-API-1). A command not in the table
 *  cannot be parsed; a command in it cannot be undocumented.
 *
 *      <verb words…> <positional…> [--flag value | --flag=value | --switch]…
 *      set <address>=<value> …
 *
 *  An unknown verb or flag is REJECTED, naming it and the nearest candidates (R-SVC-3).
 */
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace solaris
{
    struct Command
    {
        enum class Kind
        {
            None,
            ProjectNew, ProjectOpen, ProjectSave, ProjectClose,
            Set, Get,
            MixerAdd, MixerDelete, MixerMove,
            StripAdd, StripDelete, StripMove, Route,
            SendAdd, SendDelete,
            DeviceAdd, DeviceRemove, DeviceMove,
            LaneAdd, LaneDelete,
            ClipAdd, ClipMove, ClipDuplicate, ClipUnique, ClipDelete,
            PatternNew, NoteAdd, NoteDelete,
            Render,
            MatrixPrint, Audit, StatePrint, Api
        };
        Kind kind = Kind::None;
        std::vector<std::string> args;                           // positionals after the verb
        std::vector<std::pair<std::string, std::string>> flags;  // --name value; a switch = "1"
        std::vector<std::pair<std::string, std::string>> fields; // `set`: address=value pairs

        std::string arg(size_t i, const std::string &fallback = std::string()) const;
        bool has(const std::string &flag) const;
        std::string flag(const std::string &name, const std::string &fallback = std::string()) const;
    };

    struct CommandSpec
    {
        Command::Kind kind;
        std::string verb;               // "clip add"
        std::string args;               // "<ac>" — the positional hint
        int minArgs = 0, maxArgs = 0;   // −1 = unbounded
        std::vector<std::string> flags; // "pre" (a switch) or "to=<target>" (valued)
        std::string summary;
        std::string requirement;        // the R- id that asked for it
        bool fieldArgs = false;         // `set`: positionals are address=value
    };

    const std::vector<CommandSpec> &commandSpecs();
    const CommandSpec *specFor(Command::Kind k);
    std::string usageOf(const CommandSpec &s);
    /** Parse one line. A blank line or a whole-line `#`/`;` comment is Kind::None with no error. */
    Command parseCommand(const std::string &line, std::string &err);
    std::string formatCommand(const Command &c);
    /** Up to three candidates from `pool` near `word` (prefix or small edit distance). */
    std::vector<std::string> nearest(const std::string &word, const std::vector<std::string> &pool);
    std::vector<std::string> tokenize(const std::string &line);
    std::string quoteToken(const std::string &s);
}
}
