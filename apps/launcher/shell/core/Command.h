/*
 *  arstro-shell — Command and Event: the one way in and the one way out (R-CORE-2, R-CORE-3).
 *
 *  Both are described by a TABLE (commandSpecs / eventSpecs / settingSpecs). The parser, the
 *  formatter and the API document are all generated from those tables, so the document cannot
 *  name a command the shell does not have, and a new command cannot be reachable without being
 *  documented (arstro.rule §5).
 */
#pragma once
#include "Navigator.h"
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace shell
{
    enum class CommandKind
    {
        AppLaunch, AppClose, AppsRescan,
        NavHome, NavBack, NavRecents,
        TaskFocus, TaskConnected, TaskExited, TaskBackUnhandled,
        ShadeOpen, ShadeClose, ShadeToggle,
        Set,
        NotifyPost, NotifyDismiss, NotifyClear,
        Touch, Screen
    };

    /** How a command's arguments are written; one parser per shape, shared by every command. */
    enum class ArgShape { None, Id, Number, Fields, Touch, Size };

    struct CommandSpec
    {
        CommandKind kind;
        const char *name;   // the leading words, e.g. "app launch"
        ArgShape shape;
        const char *args;   // the grammar of what follows, for the API document
        const char *help;
    };

    using Fields = std::vector<std::pair<std::string, std::string>>;

    struct Command
    {
        CommandKind kind = CommandKind::NavHome;
        std::string id;      // ArgShape::Id
        int number = 0;      // ArgShape::Number
        Fields fields;       // ArgShape::Fields
        TouchSample touch;   // ArgShape::Touch
        double w = 0, h = 0; // ArgShape::Size
    };

    const std::vector<CommandSpec> &commandSpecs();
    /** Parse one text line. On failure returns false and `error` names what was not understood. */
    bool parseCommand(const std::string &line, Command &out, std::string &error);
    std::string formatCommand(const Command &c);

    struct Event
    {
        std::string name;
        Fields fields;
    };
    struct EventSpec
    {
        const char *name;
        const char *fields;
        const char *help;
    };
    const std::vector<EventSpec> &eventSpecs();
    /** `name key=value ...` — the line the journal records, a socket sends and a test matches. */
    std::string formatEvent(const Event &e);

    struct SettingSpec
    {
        const char *key;
        const char *type;   // "bool" | "number" | "color" | "text"
        const char *def;
        double min, max;    // number only
        const char *help;
    };
    const std::vector<SettingSpec> &settingSpecs();

    /** Quote a value for the text grammar when it holds a space, a quote or nothing at all. */
    std::string quoteValue(const std::string &v);
    /** Split a line into words, honouring "double quotes" and key="quoted value". */
    std::vector<std::string> splitWords(const std::string &line);
}
}
