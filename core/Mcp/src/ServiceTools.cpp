/*
 *  Arstro Mcp — ServiceTools. See the header: a service's MCP face, from one function that runs
 *  one line.
 */
#include "mcp/ServiceTools.h"
#include <sstream>

namespace arstro
{
namespace mcp
{
    namespace
    {
        Json schema(const char *text)
        {
            std::string err;
            return Json::parse(text, err);
        }
    }

    void addTextServiceTools(Server &server, const TextService &svc)
    {
        TextService s = svc;   // the tools outlive the caller's struct
        auto events = [s]() { return s.takeEvents ? s.takeEvents() : std::string(); };

        Tool api;
        api.name = s.app + "_api";
        api.description = "The " + s.app + " API document, generated from the code: every command with its "
                          "grammar, every event, every model field and address. Read it before writing commands.";
        api.inputSchema = schema(R"({"type":"object","properties":{}})");
        api.call = [s, events](const Json &) {
            std::string out, err;
            if (!s.runLine("api --json", out, err)) return ToolResult::error("api: " + err);
            events();
            return ToolResult::text(out);
        };
        server.add(api);

        Tool run;
        run.name = s.app + "_run";
        run.description = "Run " + s.app + " command lines (newline-separated) in order, stopping at the first "
                          "refusal; each line runs to completion. Returns what the lines printed and the event "
                          "lines they caused; a refusal is an error naming the line and why. " + s.runHint;
        run.inputSchema = schema(R"({"type":"object","properties":{"script":{"type":"string","description":"one command per line; '#' starts a comment"}},"required":["script"]})");
        run.call = [s, events](const Json &a) {
            std::istringstream in(a["script"].asString());
            std::string line, printed, err;
            int n = 0;
            events();   // only what THIS call caused
            while (std::getline(in, line))
            {
                ++n;
                std::string out;
                if (!s.runLine(line, out, err))
                    return ToolResult::error("refused at line " + std::to_string(n) + ": " + line + "\n" + err +
                                             "\n" + printed + events());
                printed += out;
            }
            const std::string evts = events();
            const std::string text = printed + (printed.empty() || evts.empty() ? "" : "\n") + evts;
            return ToolResult::text(text.empty() ? "ok" : text);
        };
        server.add(run);

        Tool state;
        state.name = s.app + "_state";
        state.description = "The " + s.app + " model as JSON (`state print --json`); stable=true omits what changes "
                            "with time, so two dumps of the same state compare equal.";
        state.inputSchema = schema(R"({"type":"object","properties":{"stable":{"type":"boolean"}}})");
        state.call = [s, events](const Json &a) {
            std::string out, err;
            if (!s.runLine(a["stable"].asBool() ? "state print --json --stable" : "state print --json", out, err))
                return ToolResult::error("state: " + err);
            events();
            return ToolResult::text(out);
        };
        server.add(state);
    }
}
}
