/*
 *  ntwb_tests — the C++ side of NTWB, checked the Arstro way: plain asserts, one printf per
 *  test, registered with ctest. The protocol-table test is the one that matters most: it is
 *  what makes this library's second copy of the protocol provably the same as the first.
 */
#include "ntwb/Client.h"
#include "ntwb/Json.h"
#include "ntwb/Protocol.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

using namespace arstro::ntwb;

static int gFailed = 0;
#define CHECK(cond)                                                                       \
    do                                                                                    \
    {                                                                                     \
        if (!(cond))                                                                      \
        {                                                                                 \
            std::printf("  CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__);       \
            ++gFailed;                                                                    \
        }                                                                                 \
    } while (0)

static void pass(const char *name, int before)
{
    std::printf("[%s] %s\n", gFailed == before ? "PASS" : "FAIL", name);
}

static void test_json_roundtrip()
{
    const int before = gFailed;
    std::string err;
    const std::string text = R"({"t":"call","id":"w1","n":42,"x":0.1,"neg":-3.5e2,"u":"aé😀","l":[true,false,null,{}],"s":"q\"\\\n"})";
    Json j = Json::parse(text, err);
    CHECK(err.empty());
    CHECK(j["n"].isInt() && j["n"].asInt() == 42);
    CHECK(!j["x"].isInt() && j["x"].asNumber() == 0.1);
    CHECK(j["neg"].asNumber() == -350.0);
    CHECK(j["u"].asString() == "a\xc3\xa9\xf0\x9f\x98\x80");
    CHECK(j["l"].size() == 4 && j["l"].at(2).isNull() && j["l"].at(3).isObject());
    Json back = Json::parse(j.dump(), err);
    CHECK(err.empty() && back == j);
    CHECK(Json(0.1).dump() == "0.1" && Json(7).dump() == "7");
    for (const char *bad : {"{", "[1,]", "{\"a\" 1}", "nul", "\"x", "1 2", "[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]"})
    {
        Json::parse(bad, err);
        CHECK(!err.empty());
    }
    pass("json_roundtrip", before);
}

static unsigned dirsOf(const Json &list)
{
    unsigned d = 0;
    for (const auto &x : list.items())
    {
        const std::string s = x.asString();
        d |= s == "a2h" ? A2H : s == "h2a" ? H2A : s == "c2h" ? C2H : s == "h2c" ? H2C : 0;
    }
    return d;
}

/** NR-2: every message, direction, field, type and required flag here equals the vendored
 *  catalogue Arstro Remote generates from its spec.py - and nothing extra exists. */
static void test_table_matches_the_vendored_spec()
{
    const int before = gFailed;
    std::ifstream f(NTWB_SPEC_JSON);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    const Json spec = Json::parse(ss.str(), err);
    CHECK(err.empty());
    CHECK(spec["version"].asString() == kVersion);
    CHECK(spec["max_frame"].asInt() == (long long)kMaxFrame && spec["max_blob_header"].asInt() == (long long)kMaxBlobHeader);
    CHECK(spec["frames"]["json"]["type"].asInt() == kFrameJson && spec["frames"]["blob"]["type"].asInt() == kFrameBlob);
    std::set<std::string> seen;
    for (const auto &m : spec["messages"].members())
    {
        const MessageSpec *mine = findMessage(m.first);
        if (!mine) { std::printf("  missing message %s\n", m.first.c_str()); ++gFailed; continue; }
        seen.insert(m.first);
        if (mine->dirs != dirsOf(m.second["dir"])) { std::printf("  directions of %s differ\n", m.first.c_str()); ++gFailed; }
        const Json &fields = m.second["fields"];
        if (fields.size() != mine->fields.size()) { std::printf("  field count of %s differs\n", m.first.c_str()); ++gFailed; }
        for (const auto &fs : mine->fields)
        {
            const Json &sf = fields[fs.name];
            if (!sf.isObject() || sf["type"].asString() != fs.type || sf["required"].asBool() != fs.required)
            {
                std::printf("  field %s.%s differs from the spec\n", m.first.c_str(), fs.name);
                ++gFailed;
            }
        }
    }
    CHECK(seen.size() == messages().size());                 // no message the spec does not have
    const Json &bh = spec["blob_header"]["fields"];
    CHECK(bh.size() == blobHeaderFields().size());
    for (const auto &fs : blobHeaderFields())
        CHECK(bh[fs.name]["type"].asString() == fs.type && bh[fs.name]["required"].asBool() == fs.required);
    CHECK(spec["environment"].size() == environment().size());
    for (const auto &e : environment()) CHECK(spec["environment"].has(e));
    pass("table_matches_the_vendored_spec", before);
}

static Json msg(const std::string &text)
{
    std::string err;
    Json j = Json::parse(text, err);
    CHECK(err.empty());
    return j;
}

static void test_validation_rejects_what_the_host_rejects()
{
    const int before = gFailed;
    CHECK(validate(msg(R"({"t":"call","id":"1","method":"add","params":{}})"), C2H).empty());
    struct Case { const char *m; Dir d; const char *why; } cases[] = {
        {R"({"t":"nope"})", A2H, "unknown message type"},
        {R"({"t":"call","id":"1"})", C2H, "needs field 'method'"},
        {R"({"t":"call","id":"1","method":"a","extra":1})", C2H, "has no field 'extra'"},
        {R"({"t":"call","id":"1","method":"a","client":"c1"})", C2H, "set by the host"},
        {R"({"t":"result","id":"1","ok":false})", A2H, "needs field 'error'"},
        {R"({"t":"hello","ntwb":"2.0.0","app":"x","version":"1"})", A2H, "not compatible"},
        {R"({"t":"state","key":"Bad Key","data":1})", A2H, "state.key must be"},
        {R"({"t":"welcome","ntwb":"1.0.0","session":"s","host":{},"clients":[]})", A2H, "not allowed"},
        {R"({"t":"log","level":"loud","msg":"x"})", A2H, "must be one of"},
    };
    for (const auto &c : cases)
    {
        const std::string why = validate(msg(c.m), c.d);
        if (why.find(c.why) == std::string::npos) { std::printf("  %s -> '%s'\n", c.m, why.c_str()); ++gFailed; }
    }
    pass("validation_rejects_what_the_host_rejects", before);
}

static void test_framing_survives_any_split()
{
    const int before = gFailed;
    const Json m = msg(R"({"t":"event","name":"added","data":{"n":2}})");
    Json h = Json::object();
    h.set("stream", "preview");
    h.set("mime", "image/jpeg");
    const uint8_t bytes[] = {0, 1, 2, 255};
    const std::string wire = encodeJson(m) + encodeBlob(h, bytes, sizeof bytes);
    Decoder d;
    std::vector<Frame> got;
    for (char c : wire)                                     // one byte at a time
    {
        d.feed(&c, 1);
        Frame f;
        while (d.next(f)) got.push_back(f);
    }
    CHECK(got.size() == 2);
    CHECK(!got[0].blob && got[0].json == m);
    CHECK(got[1].blob && got[1].json == h && got[1].data == std::string("\0\1\2\xff", 4));
    Decoder bad;
    const char junk[] = {9, 0, 0, 0, 0};
    bad.feed(junk, sizeof junk);
    Frame f;
    CHECK(!bad.next(f) && !bad.error().empty());
    pass("framing_survives_any_split", before);
}

/** A fake host on the other end of a MemoryTransport. */
struct FakeHost
{
    std::shared_ptr<MemoryTransport> t;
    Decoder d;
    std::vector<Frame> frames;
    void drain()
    {
        char buf[4096];
        long n;
        while ((n = t->recv(buf, sizeof buf, 0)) > 0) d.feed(buf, (size_t)n);
        Frame f;
        while (d.next(f)) frames.push_back(f);
    }
    void send(const std::string &text) { t->send(encodeJson(msg(text))); }
    const Frame *last(const std::string &type)
    {
        for (auto it = frames.rbegin(); it != frames.rend(); ++it)
            if (!it->blob && it->json["t"].asString() == type) return &*it;
        return nullptr;
    }
};

static void test_client_serves_calls_over_a_transport()
{
    const int before = gFailed;
    auto ends = MemoryTransport::pair();
    Client c({"cosmo", "9.9", {"blobs"}, "", "tok123"});
    c.setTransport(ends.first);
    FakeHost host{ends.second};
    std::vector<std::string> notes, opened;
    c.onCall = [&](const Call &call, bool &) -> Json {
        if (call.method == "add") return Json(call.params["a"].asInt() + call.params["b"].asInt());
        throw MethodError("no method named " + call.method);
    };
    c.onNotify = [&](const Call &call) { notes.push_back(call.method + "@" + call.client); };
    c.onClientOpen = [&](const std::string &id) { opened.push_back(id); };

    std::string err;
    CHECK(c.connect(err));
    host.drain();
    const Frame *hello = host.last("hello");
    CHECK(hello && hello->json["app"].asString() == "cosmo" && hello->json["token"].asString() == "tok123" &&
          validate(hello->json, A2H).empty());

    host.send(R"({"t":"welcome","ntwb":"1.0.0","session":"s1","host":{"name":"h","version":"1"},"clients":["c1"]})");
    host.send(R"({"t":"client.open","client":"c2"})");
    host.send(R"({"t":"call","id":"h1","method":"add","params":{"a":2,"b":40},"client":"c1"})");
    host.send(R"({"t":"call","id":"h2","method":"nope","client":"c2"})");
    host.send(R"({"t":"notify","method":"drag","params":{},"client":"c2"})");
    host.send(R"({"t":"ping","n":7})");
    CHECK(c.poll(0));
    CHECK(c.welcomed() && c.session() == "s1" && c.clients().size() == 2 && opened.size() == 1);
    host.drain();
    bool sawOk = false, sawErr = false, sawPong = false;
    for (const auto &f : host.frames)
    {
        const Json &j = f.json;
        if (j["t"].asString() == "result" && j["id"].asString() == "h1") sawOk = j["ok"].asBool() && j["data"].asInt() == 42;
        if (j["t"].asString() == "result" && j["id"].asString() == "h2")
            sawErr = !j["ok"].asBool() && j["error"].asString() == "no method named nope";
        if (j["t"].asString() == "pong") sawPong = j["n"].asInt() == 7;
    }
    CHECK(sawOk && sawErr && sawPong);
    CHECK(notes.size() == 1 && notes[0] == "drag@c2");

    CHECK(c.state("model", Json::object()));
    CHECK(!c.state("Bad Key", 1));                          // refused before sending (NTWB-06)
    CHECK(c.lastError().find("state.key") != std::string::npos);
    const uint8_t px[] = {1, 2, 3};
    Json meta = Json::object();
    meta.set("seq", 1);
    CHECK(c.blob("preview", "image/jpeg", px, 3, meta, "c1"));
    CHECK(c.event("load.progress", Json::object()));
    host.drain();
    const Frame &lastFrame = host.frames.back();
    CHECK(lastFrame.json["t"].asString() == "event");
    bool sawBlob = false;
    for (const auto &f : host.frames)
        if (f.blob) sawBlob = f.json["stream"].asString() == "preview" && f.data == "\1\2\3" && f.json["client"].asString() == "c1";
    CHECK(sawBlob);

    host.send(R"({"t":"bye","reason":"stopping"})");
    CHECK(!c.poll(0));
    CHECK(c.lastError().find("bye") != std::string::npos);
    pass("client_serves_calls_over_a_transport", before);
}

static void test_client_reports_a_refused_hello()
{
    const int before = gFailed;
    auto ends = MemoryTransport::pair();
    Client c({"cosmo", "1", {}, "", ""});
    c.setTransport(ends.first);
    std::string err;
    CHECK(c.connect(err));
    ends.second->send(encodeJson(msg(R"({"t":"error","error":"wrong token"})")));
    ends.second->close();
    CHECK(!c.poll(0));
    CHECK(c.lastError().find("wrong token") != std::string::npos);
    pass("client_reports_a_refused_hello", before);
}

int main()
{
    test_json_roundtrip();
    test_table_matches_the_vendored_spec();
    test_validation_rejects_what_the_host_rejects();
    test_framing_survives_any_split();
    test_client_serves_calls_over_a_transport();
    test_client_reports_a_refused_hello();
    std::printf("%s: %d failed\n", gFailed ? "FAILED" : "ok", gFailed);
    return gFailed ? 1 : 0;
}
