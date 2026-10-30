/*
 *  Arstro Mcp — Server. The protocol, and only the protocol: see Server.h for why no app
 *  behaviour may live here.
 */
#include "mcp/Server.h"
#include <cmath>
#include <exception>
#include <istream>
#include <ostream>

namespace arstro
{
namespace mcp
{
    ToolResult ToolResult::text(std::string t)
    {
        ToolResult r;
        r.addText(std::move(t));
        return r;
    }

    ToolResult ToolResult::error(std::string why)
    {
        ToolResult r = text(std::move(why));
        r.isError = true;
        return r;
    }

    ToolResult &ToolResult::addText(std::string t)
    {
        Content c;
        c.type = Content::Type::Text;
        c.text = std::move(t);
        content.push_back(std::move(c));
        return *this;
    }

    ToolResult &ToolResult::addImage(std::string bytes, std::string mime)
    {
        Content c;
        c.type = Content::Type::Image;
        c.bytes = std::move(bytes);
        c.mime = std::move(mime);
        content.push_back(std::move(c));
        return *this;
    }

    Server::Server(Info info) : mInfo(std::move(info)) {}

    void Server::add(Tool tool)
    {
        for (Tool &t : mTools)
            if (t.name == tool.name) { t = std::move(tool); return; }
        mTools.push_back(std::move(tool));
    }

    const std::vector<std::string> &Server::protocolVersions()
    {
        static const std::vector<std::string> v = {"2025-06-18", "2025-03-26", "2024-11-05"};
        return v;
    }

    std::string Server::base64(const std::string &bytes)
    {
        static const char *k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve((bytes.size() + 2) / 3 * 4);
        size_t i = 0;
        for (; i + 2 < bytes.size(); i += 3)
        {
            const unsigned v = ((unsigned char)bytes[i] << 16) | ((unsigned char)bytes[i + 1] << 8) |
                               (unsigned char)bytes[i + 2];
            out += k[(v >> 18) & 63]; out += k[(v >> 12) & 63]; out += k[(v >> 6) & 63]; out += k[v & 63];
        }
        if (i < bytes.size())
        {
            unsigned v = (unsigned char)bytes[i] << 16;
            if (i + 1 < bytes.size()) v |= (unsigned char)bytes[i + 1] << 8;
            out += k[(v >> 18) & 63];
            out += k[(v >> 12) & 63];
            out += i + 1 < bytes.size() ? k[(v >> 6) & 63] : '=';
            out += '=';
        }
        return out;
    }

    std::string Server::validate(const Json &schema, const Json &args)
    {
        if (!args.isObject()) return "arguments must be an object";
        const Json &props = schema["properties"];
        for (const auto &kv : args.members())
        {
            if (!props.has(kv.first))
            {
                std::string accepted;
                for (const auto &p : props.members()) accepted += (accepted.empty() ? "" : ", ") + p.first;
                return "unknown argument: " + kv.first + " (accepted: " + (accepted.empty() ? "none" : accepted) + ")";
            }
            const std::string type = props[kv.first]["type"].asString();
            const Json &v = kv.second;
            bool ok = true;
            if (type == "string") ok = v.isString();
            else if (type == "integer") ok = v.isNumber() && std::floor(v.asNumber()) == v.asNumber();
            else if (type == "number") ok = v.isNumber();
            else if (type == "boolean") ok = v.isBool();
            else if (type == "array") ok = v.isArray();
            else if (type == "object") ok = v.isObject();
            if (!ok) return "argument " + kv.first + " must be " + (type == "integer" ? "an " : "a ") + type;
        }
        for (const Json &r : schema["required"].items())
            if (!args.has(r.asString())) return "missing required argument: " + r.asString();
        return std::string();
    }

    Json Server::callTool(const Json &params, int &code, std::string &message)
    {
        const std::string name = params["name"].asString();
        const Tool *tool = nullptr;
        for (const Tool &t : mTools)
            if (t.name == name) tool = &t;
        if (!tool)
        {
            code = kInvalidParams;
            message = "unknown tool: " + name;
            return Json();
        }
        const Json args = params.has("arguments") && !params["arguments"].isNull() ? params["arguments"] : Json::object();

        ToolResult r;
        const std::string bad = validate(tool->inputSchema, args);
        if (!bad.empty()) r = ToolResult::error(name + ": " + bad);
        else
        {
            // An app's handler is the app's code; whatever it throws is reported to the agent as
            // that tool's failure rather than taking the server — and the agent's session — down.
            try { r = tool->call(args); }
            catch (const std::exception &e) { r = ToolResult::error(name + ": " + e.what()); }
            catch (...) { r = ToolResult::error(name + ": failed"); }
        }

        Json content = Json::array();
        for (const Content &c : r.content)
        {
            Json o = Json::object();
            if (c.type == Content::Type::Text)
            {
                o.set("type", "text");
                o.set("text", c.text);
            }
            else
            {
                o.set("type", "image");
                o.set("data", base64(c.bytes));
                o.set("mimeType", c.mime);
            }
            content.push(std::move(o));
        }
        Json out = Json::object();
        out.set("content", std::move(content));
        out.set("isError", r.isError);
        return out;
    }

    Json Server::dispatch(const std::string &method, const Json &params, bool &isError, int &code, std::string &message)
    {
        isError = false;
        if (method == "initialize")
        {
            std::string version = protocolVersions().front();
            const std::string asked = params["protocolVersion"].asString();
            for (const std::string &v : protocolVersions())
                if (v == asked) version = v;
            Json caps = Json::object();
            Json tools = Json::object();
            tools.set("listChanged", false);
            caps.set("tools", std::move(tools));
            Json info = Json::object();
            info.set("name", mInfo.name);
            info.set("version", mInfo.version);
            Json out = Json::object();
            out.set("protocolVersion", version);
            out.set("capabilities", std::move(caps));
            out.set("serverInfo", std::move(info));
            if (!mInfo.instructions.empty()) out.set("instructions", mInfo.instructions);
            return out;
        }
        if (method == "ping") return Json::object();
        if (method == "tools/list")
        {
            Json list = Json::array();
            for (const Tool &t : mTools)
            {
                Json o = Json::object();
                o.set("name", t.name);
                o.set("description", t.description);
                o.set("inputSchema", t.inputSchema);
                list.push(std::move(o));
            }
            Json out = Json::object();
            out.set("tools", std::move(list));
            return out;
        }
        if (method == "tools/call")
        {
            Json r = callTool(params, code, message);
            isError = code != 0;
            return r;
        }
        isError = true;
        code = kMethodNotFound;
        message = "method not found: " + method;
        return Json();
    }

    std::string Server::handle(const std::string &line)
    {
        auto reply = [](const Json &id, Json result) {
            Json o = Json::object();
            o.set("jsonrpc", "2.0");
            o.set("id", id);
            o.set("result", std::move(result));
            return o.dump();
        };
        auto error = [](const Json &id, int code, const std::string &message) {
            Json e = Json::object();
            e.set("code", code);
            e.set("message", message);
            Json o = Json::object();
            o.set("jsonrpc", "2.0");
            o.set("id", id);
            o.set("error", std::move(e));
            return o.dump();
        };

        std::string err;
        const Json msg = Json::parse(line, err);
        if (!err.empty()) return error(Json(), kParseError, "parse error: " + err);
        if (!msg.isObject()) return error(Json(), kInvalidRequest, "a message must be a JSON object (batches are not part of MCP)");

        const bool notification = !msg.has("id");
        const Json id = msg["id"];
        const std::string method = msg["method"].asString();
        if (msg["jsonrpc"].asString() != "2.0" || method.empty())
            return notification ? std::string() : error(id, kInvalidRequest, "not a JSON-RPC 2.0 request");

        if (notification)
        {
            // `notifications/initialized`, `notifications/cancelled` … take no reply. A tool call
            // here runs to completion before the next line is read, so there is nothing to cancel.
            if (method == "notifications/initialized") mInitialized = true;
            return std::string();
        }
        if (!msg.has("result") && !msg.has("error"))
        {
            bool isError = false;
            int code = 0;
            std::string message;
            Json result = dispatch(method, msg["params"], isError, code, message);
            if (isError) return error(id, code, message);
            return reply(id, std::move(result));
        }
        return std::string();   // a response to something we never asked: nothing to say
    }

    int Server::serve(std::istream &in, std::ostream &out)
    {
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.find_first_not_of(" \t") == std::string::npos) continue;
            const std::string r = handle(line);
            if (!r.empty()) out << r << '\n' << std::flush;
        }
        return 0;
    }
}
}
