/*
 *  Arstro Ntwb — the NTWB 1.0.0 tables, validation and framing. See Protocol.h for why
 *  this table is tested against the vendored spec/api.json rather than trusted.
 */
#include "ntwb/Protocol.h"
#include <cctype>

namespace arstro
{
namespace ntwb
{
    const std::vector<MessageSpec> &messages()
    {
        static const std::vector<MessageSpec> m = {
            {"hello", A2H, {{"ntwb", "version", true}, {"app", "name", true}, {"version", "str", true},
                            {"token", "str", false}, {"pid", "int", false}, {"capabilities", "list[str]", false}}},
            {"welcome", H2A, {{"ntwb", "version", true}, {"session", "id", true}, {"host", "obj", true},
                              {"clients", "list[str]", true}}},
            {"ready", H2C, {{"ntwb", "version", true}, {"client", "id", true}, {"app", "obj", true},
                            {"state", "obj", true}}},
            {"status", H2C, {{"state", "enum:starting|running|stopped|failed", true}, {"detail", "str", false}}},
            {"client.open", H2A, {{"client", "id", true}, {"info", "obj", false}}},
            {"client.close", H2A, {{"client", "id", true}}},
            {"call", C2H | H2A, {{"id", "id", true}, {"method", "name", true}, {"params", "obj", false},
                                 {"client", "id", false}}},
            {"result", A2H | H2C, {{"id", "id", true}, {"ok", "bool", true}, {"data", "any", false},
                                   {"error", "str", false}}},
            {"notify", C2H | H2A, {{"method", "name", true}, {"params", "obj", false}, {"client", "id", false}}},
            {"event", A2H | H2C, {{"name", "name", true}, {"data", "any", false}, {"client", "id", false}}},
            {"state", A2H | H2C, {{"key", "name", true}, {"data", "any", true}}},
            {"log", A2H, {{"level", "enum:debug|info|warning|error", true}, {"msg", "str", true}}},
            {"ping", H2A, {{"n", "int", true}}},
            {"pong", A2H, {{"n", "int", true}}},
            {"error", H2A | H2C, {{"error", "str", true}, {"about", "str", false}}},
            {"bye", A2H | H2A, {{"reason", "str", false}}},
        };
        return m;
    }

    const std::vector<FieldSpec> &blobHeaderFields()
    {
        static const std::vector<FieldSpec> f = {
            {"stream", "name", true}, {"mime", "str", true}, {"meta", "obj", false}, {"client", "id", false},
            {"coalesce", "bool", false}};
        return f;
    }

    const std::vector<std::string> &environment()
    {
        static const std::vector<std::string> e = {"NTWB_SOCKET", "NTWB_TOKEN", "NTWB_APP_ID",
                                                   "NTWB_VERSION", "NTWB_HOST", "NTWB_DATA_DIR"};
        return e;
    }

    const MessageSpec *findMessage(const std::string &name)
    {
        for (const auto &m : messages())
            if (name == m.name) return &m;
        return nullptr;
    }

    const char *dirName(Dir d)
    {
        switch (d)
        {
        case A2H: return "a2h";
        case H2A: return "h2a";
        case C2H: return "c2h";
        case H2C: return "h2c";
        }
        return "?";
    }

    namespace
    {
        bool isId(const std::string &s)
        {
            if (s.empty() || s.size() > 64) return false;
            for (char c : s)
                if (!(std::isalnum((unsigned char)c) || c == '.' || c == '_' || c == ':' || c == '-')) return false;
            return true;
        }
        bool isName(const std::string &s)
        {
            if (s.empty() || s.size() > 64 || !(s[0] >= 'a' && s[0] <= 'z')) return false;
            for (char c : s)
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
            return true;
        }
        bool isVersion(const std::string &s, int *major)
        {
            int parts = 0, cur = -1, first = -1;
            for (size_t i = 0; i <= s.size(); ++i)
            {
                if (i == s.size() || s[i] == '.')
                {
                    if (cur < 0) return false;
                    if (parts == 0) first = cur;
                    ++parts;
                    cur = -1;
                }
                else if (s[i] >= '0' && s[i] <= '9') cur = (cur < 0 ? 0 : cur * 10) + (s[i] - '0');
                else return false;
            }
            if (major) *major = first;
            return parts == 3;
        }

        std::string checkType(const Json &v, const std::string &type, const std::string &where)
        {
            if (type == "any") return "";
            if (type.rfind("enum:", 0) == 0)
            {
                const std::string opts = type.substr(5);
                size_t start = 0;
                for (;;)
                {
                    const size_t bar = opts.find('|', start);
                    if (v.isString() && v.asString() == opts.substr(start, bar - start)) return "";
                    if (bar == std::string::npos) break;
                    start = bar + 1;
                }
                std::string list = opts;
                for (auto &c : list) if (c == '|') c = ',';
                return where + " must be one of " + list;
            }
            bool ok = false;
            if (type == "str") ok = v.isString();
            else if (type == "int") ok = v.isInt();
            else if (type == "num") ok = v.isNumber();
            else if (type == "bool") ok = v.isBool();
            else if (type == "obj") ok = v.isObject();
            else if (type == "list[str]")
            {
                ok = v.isArray();
                for (const auto &x : v.items()) ok = ok && x.isString();
            }
            else if (type == "id") ok = v.isString() && isId(v.asString());
            else if (type == "name") ok = v.isString() && isName(v.asString());
            else if (type == "version") ok = v.isString() && isVersion(v.asString(), nullptr);
            return ok ? "" : where + " must be " + type;
        }

        std::string checkFields(const Json &obj, const std::vector<FieldSpec> &fields, const std::string &what,
                                const char *skip)
        {
            if (!obj.isObject()) return what + " must be a JSON object";
            for (const auto &m : obj.members())
            {
                if (skip && m.first == skip) continue;
                bool known = false;
                for (const auto &f : fields) known = known || m.first == f.name;
                if (!known) return what + " has no field '" + m.first + "'";
            }
            for (const auto &f : fields)
            {
                if (!obj.has(f.name))
                {
                    if (f.required) return what + " needs field '" + f.name + "'";
                    continue;
                }
                const std::string e = checkType(obj[f.name], f.type, what + "." + f.name);
                if (!e.empty()) return e;
            }
            return "";
        }
    }

    std::string validate(const Json &msg, Dir dir)
    {
        if (!msg.isObject()) return "a message must be a JSON object";
        const Json &t = msg["t"];
        const MessageSpec *spec = t.isString() ? findMessage(t.asString()) : nullptr;
        if (!spec) return "unknown message type " + (t.isString() ? "'" + t.asString() + "'" : t.dump());
        if (!(spec->dirs & dir)) return "message '" + t.asString() + "' is not allowed " + dirName(dir);
        std::string e = checkFields(msg, spec->fields, t.asString(), "t");
        if (!e.empty()) return e;
        const std::string name = t.asString();
        if (name == "hello")
        {
            int major = -1;
            isVersion(msg["ntwb"].asString(), &major);
            if (major != kMajor) return std::string("this peer speaks NTWB ") + kVersion + "; version " +
                                        msg["ntwb"].asString() + " is not compatible";
        }
        if (name == "result" && !msg["ok"].asBool() && !msg.has("error")) return "result with ok=false needs field 'error'";
        if ((name == "call" || name == "notify") && dir == C2H && msg.has("client"))
            return name + ".client is set by the host, not by a client";
        return "";
    }

    std::string validateBlobHeader(const Json &hdr, Dir dir)
    {
        if (!(dir & (A2H | H2C))) return std::string("blobs are not allowed ") + dirName(dir);
        return checkFields(hdr, blobHeaderFields(), "blob", nullptr);
    }

    // ── framing ─────────────────────────────────────────────────────────────────────────
    namespace
    {
        void put32(std::string &out, uint32_t v)
        {
            out += (char)(v >> 24);
            out += (char)(v >> 16);
            out += (char)(v >> 8);
            out += (char)v;
        }
    }

    std::string encodeJson(const Json &msg)
    {
        const std::string payload = msg.dump();
        std::string out;
        out.reserve(payload.size() + 5);
        out += (char)kFrameJson;
        put32(out, (uint32_t)payload.size());
        out += payload;
        return out;
    }

    std::string encodeBlob(const Json &header, const uint8_t *data, size_t n)
    {
        const std::string h = header.dump();
        std::string out;
        out.reserve(7 + h.size() + n);
        out += (char)kFrameBlob;
        put32(out, (uint32_t)(2 + h.size() + n));
        out += (char)(h.size() >> 8);
        out += (char)h.size();
        out += h;
        out.append((const char *)data, n);
        return out;
    }

    void Decoder::feed(const char *p, size_t n)
    {
        if (mPos > 0 && mPos == mBuf.size()) { mBuf.clear(); mPos = 0; }
        else if (mPos > (1u << 20)) { mBuf.erase(0, mPos); mPos = 0; }
        mBuf.append(p, n);
    }

    bool Decoder::next(Frame &out)
    {
        if (!mError.empty() || mBuf.size() - mPos < 5) return false;
        const unsigned char *b = (const unsigned char *)mBuf.data() + mPos;
        const uint8_t type = b[0];
        const uint32_t len = ((uint32_t)b[1] << 24) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 8) | b[4];
        if (type != kFrameJson && type != kFrameBlob) { mError = "unknown frame type " + std::to_string(type); return false; }
        if (len > kMaxFrame) { mError = "frame too large (" + std::to_string(len) + " bytes)"; return false; }
        if (mBuf.size() - mPos - 5 < len) return false;
        const std::string payload = mBuf.substr(mPos + 5, len);
        mPos += 5 + len;
        std::string err;
        out = Frame();
        if (type == kFrameJson)
        {
            out.json = Json::parse(payload, err);
            if (!err.empty()) { mError = "message is not JSON: " + err; return false; }
            return true;
        }
        if (payload.size() < 2) { mError = "blob shorter than its header length"; return false; }
        const size_t hn = ((size_t)(unsigned char)payload[0] << 8) | (unsigned char)payload[1];
        if (hn > kMaxBlobHeader || 2 + hn > payload.size()) { mError = "bad blob header length"; return false; }
        out.blob = true;
        out.json = Json::parse(payload.substr(2, hn), err);
        if (!err.empty()) { mError = "blob header is not JSON: " + err; return false; }
        out.data = payload.substr(2 + hn);
        return true;
    }
}
}
