/*
 *  mcp_tests — the protocol, end to end over strings: what a client sends, what it reads back.
 *  Plain assert() like ntwb_tests; no framework.
 */
#include "mcp/Server.h"
#include "mcp/ServiceTools.h"
#include <cassert>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <vector>

using arstro::mcp::Json;
using arstro::mcp::Server;
using arstro::mcp::Tool;
using arstro::mcp::ToolResult;

namespace
{
    Json parse(const std::string &s)
    {
        std::string err;
        Json j = Json::parse(s, err);
        assert(err.empty() && "the server only ever writes valid JSON");
        return j;
    }

    Server makeServer()
    {
        Server s({"demo", "1.0", "read demo_api first"});
        Tool echo;
        echo.name = "demo_echo";
        echo.description = "say it back";
        echo.inputSchema = parse(R"({"type":"object","properties":{"text":{"type":"string"},"times":{"type":"integer"},"loud":{"type":"boolean"}},"required":["text"]})");
        echo.call = [](const Json &a) {
            std::string out;
            for (long long i = 0; i < (a.has("times") ? a["times"].asInt() : 1); ++i) out += a["text"].asString();
            return ToolResult::text(a["loud"].asBool() ? out + "!" : out);
        };
        s.add(echo);

        Tool pic;
        pic.name = "demo_picture";
        pic.description = "an image";
        pic.inputSchema = parse(R"({"type":"object","properties":{}})");
        pic.call = [](const Json &) { return ToolResult::text("here").addImage(std::string("\xff\xd8\xff", 3), "image/jpeg"); };
        s.add(pic);

        Tool boom;
        boom.name = "demo_boom";
        boom.description = "throws";
        boom.inputSchema = parse(R"({"type":"object","properties":{}})");
        boom.call = [](const Json &) -> ToolResult { throw std::runtime_error("disk on fire"); };
        s.add(boom);
        return s;
    }

    void test_handshake_and_list()
    {
        Server s = makeServer();
        Json r = parse(s.handle(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"t","version":"0"}}})"));
        assert(r["id"].asInt() == 1);
        assert(r["result"]["protocolVersion"].asString() == "2025-03-26" && "the client's revision is honoured when we speak it");
        assert(r["result"]["serverInfo"]["name"].asString() == "demo");
        assert(r["result"]["capabilities"].has("tools"));
        assert(r["result"]["instructions"].asString() == "read demo_api first");

        r = parse(s.handle(R"({"jsonrpc":"2.0","id":"a","method":"initialize","params":{"protocolVersion":"1999-01-01"}})"));
        assert(r["id"].asString() == "a" && "a string id comes back a string");
        assert(r["result"]["protocolVersion"].asString() == Server::protocolVersions().front());

        assert(s.handle(R"({"jsonrpc":"2.0","method":"notifications/initialized"})").empty() &&
               "a notification takes no reply");

        r = parse(s.handle(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})"));
        const Json &tools = r["result"]["tools"];
        assert(tools.size() == 3);
        assert(tools.at(0)["name"].asString() == "demo_echo");
        assert(tools.at(0)["inputSchema"]["required"].at(0).asString() == "text");

        r = parse(s.handle(R"({"jsonrpc":"2.0","id":3,"method":"ping"})"));
        assert(r["result"].isObject() && r["result"].members().empty());
        std::printf("[PASS] handshake_and_list\n");
    }

    void test_calls_and_validation()
    {
        Server s = makeServer();
        Json r = parse(s.handle(R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"demo_echo","arguments":{"text":"ab","times":2,"loud":true}}})"));
        assert(!r["result"]["isError"].asBool());
        assert(r["result"]["content"].at(0)["type"].asString() == "text");
        assert(r["result"]["content"].at(0)["text"].asString() == "abab!");

        // arstro.rule §5: a key nobody declared is refused, by name — never accepted and ignored.
        r = parse(s.handle(R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"demo_echo","arguments":{"text":"x","tims":2}}})"));
        assert(r["result"]["isError"].asBool());
        assert(r["result"]["content"].at(0)["text"].asString().find("unknown argument: tims") != std::string::npos);

        r = parse(s.handle(R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"demo_echo","arguments":{"times":2}}})"));
        assert(r["result"]["isError"].asBool());
        assert(r["result"]["content"].at(0)["text"].asString().find("missing required argument: text") != std::string::npos);

        r = parse(s.handle(R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"demo_echo","arguments":{"text":"x","times":"2"}}})"));
        assert(r["result"]["isError"].asBool() && "a string is not an integer");
        r = parse(s.handle(R"({"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"demo_echo","arguments":{"text":"x","times":1.5}}})"));
        assert(r["result"]["isError"].asBool() && "1.5 is not an integer");

        // A throwing tool is that tool's failure, reported — not the server's death.
        r = parse(s.handle(R"({"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"demo_boom"}})"));
        assert(r["result"]["isError"].asBool());
        assert(r["result"]["content"].at(0)["text"].asString() == "demo_boom: disk on fire");

        // Images travel base64 with their type.
        r = parse(s.handle(R"({"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"demo_picture","arguments":{}}})"));
        assert(r["result"]["content"].at(1)["type"].asString() == "image");
        assert(r["result"]["content"].at(1)["data"].asString() == "/9j/");
        assert(r["result"]["content"].at(1)["mimeType"].asString() == "image/jpeg");

        // An unknown tool is a protocol error (the caller named something that does not exist).
        r = parse(s.handle(R"({"jsonrpc":"2.0","id":11,"method":"tools/call","params":{"name":"nope"}})"));
        assert(r["error"]["code"].asInt() == Server::kInvalidParams);
        std::printf("[PASS] calls_and_validation\n");
    }

    void test_errors_and_stream()
    {
        Server s = makeServer();
        Json r = parse(s.handle("{not json"));
        assert(r["error"]["code"].asInt() == Server::kParseError && r["id"].isNull());
        r = parse(s.handle(R"([{"jsonrpc":"2.0","id":1,"method":"ping"}])"));
        assert(r["error"]["code"].asInt() == Server::kInvalidRequest);
        r = parse(s.handle(R"({"jsonrpc":"2.0","id":12,"method":"resources/list"})"));
        assert(r["error"]["code"].asInt() == Server::kMethodNotFound);

        assert(Server::base64("") == "" && Server::base64("f") == "Zg==" && Server::base64("fo") == "Zm8=" &&
               Server::base64("foo") == "Zm9v" && Server::base64("foob") == "Zm9vYg==");

        // serve(): one reply per request line, none for a notification or a blank line.
        std::istringstream in("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}\r\n\n"
                              "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n"
                              "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\"}\n");
        std::ostringstream out;
        assert(s.serve(in, out) == 0);
        assert(out.str() == "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\n{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{}}\n");
        std::printf("[PASS] errors_and_stream\n");
    }

    // A service's MCP face comes from ONE function that runs one line: the helper must route
    // `api --json` / `state print --json` to it, run a script in order, stop at a refusal naming
    // the line, and hand back only the events this call caused.
    void test_text_service_tools()
    {
        std::vector<std::string> seen;
        std::string pending = "[evt] stale\n";
        arstro::mcp::TextService svc;
        svc.app = "fake";
        svc.runLine = [&](const std::string &line, std::string &out, std::string &err) {
            seen.push_back(line);
            if (line == "boom") { err = "boom: no such command"; return false; }
            out = "did " + line + "\n";
            pending += "[evt] ran " + line + "\n";
            return true;
        };
        svc.takeEvents = [&]() { std::string e = pending; pending.clear(); return e; };
        Server s({"fake", "0", ""});
        arstro::mcp::addTextServiceTools(s, svc);
        assert(s.tools().size() == 3 && s.tools()[0].name == "fake_api" && s.tools()[2].name == "fake_state");

        Json r = parse(s.handle(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"fake_api"}})"));
        assert(seen.back() == "api --json" && !r["result"]["isError"].asBool());
        r = parse(s.handle(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"fake_state","arguments":{"stable":true}}})"));
        assert(seen.back() == "state print --json --stable");

        pending = "[evt] stale\n";
        r = parse(s.handle(R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"fake_run","arguments":{"script":"a\nb"}}})"));
        const std::string text = r["result"]["content"].at(0)["text"].asString();
        assert(text == "did a\ndid b\n\n[evt] ran a\n[evt] ran b\n" && "in order, printed then this call's events only");

        seen.clear();
        r = parse(s.handle(R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"fake_run","arguments":{"script":"a\nboom\nc"}}})"));
        assert(r["result"]["isError"].asBool());
        assert(r["result"]["content"].at(0)["text"].asString().find("refused at line 2: boom") == 0);
        assert(seen.size() == 2 && "nothing after a refusal runs");
        std::printf("[PASS] text_service_tools\n");
    }
}

int main()
{
    test_handshake_and_list();
    test_calls_and_validation();
    test_errors_and_stream();
    test_text_service_tools();
    std::printf("\nAll mcp tests passed.\n");
    return 0;
}
