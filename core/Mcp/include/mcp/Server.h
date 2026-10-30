/*
 *  Arstro Mcp — Server: an Arstro app as a Model Context Protocol server, so an agent can
 *  drive it the way a person drives its window.
 *
 *  MCP (modelcontextprotocol.io) is JSON-RPC 2.0, one message per line on stdio. An agent's
 *  client starts the server, asks `initialize`, lists the server's `tools`, and calls them.
 *  That is all this library does — and all it may do. **It holds no behaviour of any app**:
 *  every Arstro app already has ONE way in (a text command, parsed by its service) and ONE
 *  way out (its model and its events), and an MCP tool is a front end over exactly those,
 *  a peer of the window, the CLI, the control socket and the NTWB bridge (arstro.rule §1).
 *  So a tool here is a name, a schema and a function the APP supplies; the protocol is ours.
 *
 *  Two rules this enforces so no app has to remember them:
 *    - An argument the tool did not declare is REJECTED, naming it (arstro.rule §5: a key
 *      accepted and ignored is worse than a crash — the caller believes it landed).
 *    - A tool that fails says so with `isError` and the reason, never with an empty success.
 *
 *  Platform-free: `handle()` maps one message to one reply and touches no stream; the only
 *  OS call is in Stdio.cpp, which keeps stdout for the protocol alone.
 */
#pragma once
#include "ntwb/Json.h"
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace arstro
{
namespace mcp
{
    using Json = ntwb::Json;

    /** One block of a tool's answer: text, or an image the agent can look at. */
    struct Content
    {
        enum class Type { Text, Image };
        Type type = Type::Text;
        std::string text;   // Text
        std::string bytes;  // Image: the encoded file (JPEG/PNG), raw — base64 is the wire's business
        std::string mime;   // Image: "image/jpeg" | "image/png"
    };

    struct ToolResult
    {
        std::vector<Content> content;
        bool isError = false;

        static ToolResult text(std::string t);
        static ToolResult error(std::string why);
        ToolResult &addText(std::string t);
        ToolResult &addImage(std::string bytes, std::string mime);
    };

    struct Tool
    {
        std::string name;
        std::string description;
        /** A JSON Schema object: {"type":"object","properties":{…},"required":[…]}. Every
         *  property's "type" (string | integer | number | boolean) is checked before `call`
         *  runs, required ones must be present, and any key not under "properties" is refused. */
        Json inputSchema;
        std::function<ToolResult(const Json &args)> call;
    };

    class Server
    {
    public:
        struct Info
        {
            std::string name;          // "cosmo"
            std::string version;       // the app's
            std::string instructions;  // what an agent should read first; sent in `initialize`
        };

        explicit Server(Info info);

        /** Register a tool. A second tool of the same name replaces the first. */
        void add(Tool tool);
        const std::vector<Tool> &tools() const { return mTools; }

        /** One JSON-RPC message in, its reply out — the empty string for a notification
         *  (no `id`), which takes none. Never throws: a malformed line is a JSON-RPC error. */
        std::string handle(const std::string &line);

        /** Serve newline-delimited messages from `in` to `out` until EOF. Flushes after
         *  every reply, because the client is waiting for it. Returns 0. */
        int serve(std::istream &in, std::ostream &out);

        /** The protocol revisions this server speaks, newest first. `initialize` answers with
         *  the client's own revision when it is one of these, else the newest. */
        static const std::vector<std::string> &protocolVersions();

        /** Check `args` against `schema` as described on `Tool::inputSchema`; "" when valid. */
        static std::string validate(const Json &schema, const Json &args);

        static std::string base64(const std::string &bytes);

        // JSON-RPC 2.0 error codes.
        static constexpr int kParseError = -32700, kInvalidRequest = -32600, kMethodNotFound = -32601,
                             kInvalidParams = -32602, kInternalError = -32603;

    private:
        Json dispatch(const std::string &method, const Json &params, bool &isError, int &code, std::string &message);
        Json callTool(const Json &params, int &code, std::string &message);

        Info mInfo;
        std::vector<Tool> mTools;
        bool mInitialized = false;
    };

    /** Serve on the process's stdin/stdout. Before the first message it moves the process's
     *  own stdout onto stderr and keeps a private handle to the real one for the protocol,
     *  so a stray printf anywhere in an app (a decoder, a library) lands in the log instead
     *  of corrupting a JSON-RPC stream. The one OS-specific function in the library. */
    int serveStdio(Server &server);
}
}
