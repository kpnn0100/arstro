// solaris_ntwb tests — the web face's adapter over an in-memory bridge (R-SVC-7).
//
// The REAL SolarisService behind the REAL NtwbAdapter and ntwb::Client, the host played by a fake on
// the other end of an ntwb::MemoryTransport (core/Ntwb's own test pattern): a browser's call becomes a
// command line through the one grammar, its reply carries the command's output or its refusal, every
// event is relayed under its own name with its --watch line, and the `model` state IS what
// `state print --json` prints. Then `solaris-cc ntwb install` into a scratch data dir: the manifest,
// the API, the web UI and what it borrows from cosmo — never the user's $XDG_DATA_HOME.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "AppModelCodec.h"
#include "Command.h"
#include "Event.h"
#include "NtwbAdapter.h"
#include "SolarisService.h"
#include "ntwb/Client.h"
#include "ntwb/Protocol.h"
#include "ntwb/Transport.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef SOLARIS_CC
#error "SOLARIS_CC (the solaris-cc binary) must be defined by the build"
#endif

using namespace arstro;
using ntwb::Json;
namespace fs = std::filesystem;

namespace
{
    int passed = 0;
    void pass(const char *name)
    {
        std::printf("[PASS] %s\n", name);
        ++passed;
    }

    fs::path scratch(const char *sub)
    {
        const char *t = std::getenv("SOLARIS_TEST_DIR");
        fs::path d = (t ? fs::path(t) : fs::temp_directory_path() / "solaris_ntwb_tests") / "ntwb" / sub;
        fs::remove_all(d);
        fs::create_directories(d);
        return d;
    }

    std::string slurp(const fs::path &p)
    {
        std::ifstream f(p, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    Json parse(const std::string &text)
    {
        std::string err;
        Json j = Json::parse(text, err);
        assert(err.empty());
        return j;
    }

    /** Arstro Remote, played on the other end of the memory transport. */
    struct FakeHost
    {
        std::shared_ptr<ntwb::MemoryTransport> t;
        ntwb::Decoder d;
        std::vector<ntwb::Frame> frames;
        void drain()
        {
            char buf[65536];
            long n;
            while ((n = t->recv(buf, sizeof buf, 0)) > 0) d.feed(buf, (size_t)n);
            ntwb::Frame f;
            while (d.next(f)) frames.push_back(f);
        }
        void send(const Json &msg) { t->send(ntwb::encodeJson(msg)); }
        void call(const std::string &id, const std::string &method, const Json &params, bool notify = false)
        {
            Json m = Json::object();
            m.set("t", notify ? "notify" : "call");
            if (!notify) m.set("id", id);
            m.set("method", method);
            m.set("params", params);
            m.set("client", "c1");
            send(m);
        }
        const Json *result(const std::string &id) const
        {
            for (const auto &f : frames)
                if (!f.blob && f.json["t"].asString() == "result" && f.json["id"].asString() == id) return &f.json;
            return nullptr;
        }
        const Json *lastState(const std::string &key) const
        {
            for (auto it = frames.rbegin(); it != frames.rend(); ++it)
                if (!it->blob && it->json["t"].asString() == "state" && it->json["key"].asString() == key) return &it->json["data"];
            return nullptr;
        }
        std::vector<const Json *> events(const std::string &name) const
        {
            std::vector<const Json *> out;
            for (const auto &f : frames)
                if (!f.blob && f.json["t"].asString() == "event" && f.json["name"].asString() == name) out.push_back(&f.json["data"]);
            return out;
        }
    };

    Json line(const std::string &l)
    {
        Json p = Json::object();
        p.set("line", l);
        return p;
    }

    void test_a_call_is_a_command_line_and_the_state_is_the_model()
    {
        fs::current_path(scratch("adapter"));
        auto ends = ntwb::MemoryTransport::pair();
        ntwb::Client client({"solaris", "1"});
        client.setTransport(ends.first);
        FakeHost host{ends.second};
        solaris::SolarisService svc;
        solaris_cli::NtwbAdapter adapter(svc, client);
        adapter.start();
        std::string err;
        assert(client.connect(err));
        host.send(parse(R"({"t":"welcome","ntwb":"1.1.0","session":"main","host":{"name":"h","version":"1"},"clients":["c1"]})"));
        assert(client.poll(0) && client.welcomed());

        double now = 0;
        auto step = [&] {
            assert(client.poll(0));
            svc.pump();
            adapter.tick(now += 100);
            host.drain();
        };
        host.call("h1", "command", line("project new web.slp --bpm 96"));
        step();
        const Json *r = host.result("h1");
        assert(r && (*r)["ok"].asBool() && (*r)["data"]["revision"].asInt() > 0);
        // every event relayed under its own name, carrying the --watch line and its fields
        const auto opened = host.events("project.opened");
        assert(opened.size() == 1);
        assert((*opened[0])["line"].asString() == "[evt] project.opened path=web.slp strips=1 clips=0");
        assert((*opened[0])["fields"]["path"].asString() == "web.slp");
        // the model state, pushed on the revision: what the page draws
        const Json *m = host.lastState("model");
        assert(m && (*m)["screen"].asString() == "project" && (*m)["bpm"].asNumber() == 96.0);

        host.call("h2", "command", line("clip add --instrument drums --at 0 --length 8"));
        host.call("h3", "command", line("set ch_2.gain=-6"));
        host.call("h4", "command", line("get ch_2.gain"));
        step();
        assert(host.result("h2") && (*host.result("h2"))["ok"].asBool());
        assert((*host.result("h4"))["data"]["output"].asString() == svc.output()); // the command's own print
        assert(svc.output().find("-6") != std::string::npos);
        // a fader drag: notifies, no reply awaited, and the model follows
        host.call("", "command", line("set ch_2.pan=0.5"), true);
        step();
        bool sawPan = false;
        for (const auto &s : host.lastState("model")->operator[]("strips").items())
            if (s["id"].asString() == "ch_2") sawPan = s["pan"].asNumber() == 0.5 && s["gain"].asNumber() == -6.0;
        assert(sawPan);

        // a refusal is a failed call with the service's sentence — and the command.rejected event
        host.call("h5", "command", line("flurb"));
        host.call("h6", "command", line("wait 2"));
        host.call("h7", "nope", Json::object());
        step();
        assert(!(*host.result("h5"))["ok"].asBool() && (*host.result("h5"))["error"].asString().find("unknown command: flurb") != std::string::npos);
        assert(host.events("command.rejected").size() == 1);
        assert(!(*host.result("h6"))["ok"].asBool() && (*host.result("h6"))["error"].asString().find("wait is the caller's loop") == 0);
        assert((*host.result("h7"))["error"].asString() == "no method named nope");

        // the web's model IS the CLI's dump: `model` == `state print --json`, parsed — but for the
        // revision, which a dispatch moves AFTER it printed (state print included)
        host.call("h8", "command", line("state print --json"));
        host.call("h9", "model", Json::object());
        host.call("h10", "commands", Json::object());
        step();
        auto unrevised = [](Json j) { assert(j.erase("revision")); return j; };
        const Json printed = unrevised(parse((*host.result("h8"))["data"]["output"].asString()));
        assert(unrevised((*host.result("h9"))["data"]) == printed);
        assert(unrevised(*host.lastState("model")) == printed);
        assert((*host.result("h9"))["data"]["revision"].asInt() == svc.model().revision);
        assert((*host.result("h10"))["data"].size() == solaris::commandSpecs().size());

        // the transport has its own key, so a playhead moves with no model per frame
        const Json *t = host.lastState("transport");
        assert(t && !(*t)["playing"].asBool() && (*t)["peaks"].has("ch_2"));

        // nothing the adapter sent breaks the protocol (NTWB-06)
        for (const auto &f : host.frames)
            if (!f.blob) assert(ntwb::validate(f.json, ntwb::A2H).empty());
        pass("a browser's call is a command line; its reply is the output or the refusal; events relayed; the model state IS state print --json");
    }

    void test_install_writes_the_app_into_a_scratch_data_dir()
    {
        const fs::path data = scratch("install");
        const std::string cc = SOLARIS_CC;
        assert(std::system(("\"" + cc + "\" ntwb install --data-dir \"" + data.string() + "\" > \"" + (data / "install.log").string() + "\"").c_str()) == 0);
        const fs::path app = data / "solaris";
        const Json manifest = parse(slurp(app / "ntwb.json"));
        assert(manifest["id"].asString() == "solaris" && manifest["ntwb"].asString() == ntwb::kVersion);
        assert(manifest["exec"].size() == 3 && manifest["exec"].at(1).asString() == "ntwb" && manifest["exec"].at(2).asString() == "serve");
        assert(fs::canonical(manifest["exec"].at(0).asString()) == fs::canonical(cc));
        assert(manifest["web"].asString() == "web" && manifest["api"].asString() == "api.json" && manifest["single"].asBool());
        assert(slurp(app / "api.json") == solaris_cli::NtwbAdapter::apiDescription().dumpPretty());
        for (const char *f : {"index.html", "icon.svg", "css/tokens.css", "css/base.css", "js/main.js", "js/core/signal.js", "js/core/dom.js",
                              "fonts/Roboto-Regular.ttf", "fonts/JetBrainsMono-Regular.ttf", "fonts/OFL-Roboto.txt"})
            assert(fs::is_regular_file(app / "web" / f));
        // the borrowed files are cosmo's, unchanged
        assert(slurp(app / "web" / "js" / "core" / "signal.js") == slurp(fs::path(COSMO_SOURCE_DIR) / "web" / "js" / "core" / "signal.js"));
        assert(std::system(("\"" + cc + "\" ntwb uninstall --data-dir \"" + data.string() + "\" >> \"" + (data / "install.log").string() + "\"").c_str()) == 0);
        assert(!fs::exists(app));
        pass("ntwb install writes the manifest, the API, the web UI and cosmo's borrowed core and fonts; uninstall removes them");
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_a_call_is_a_command_line_and_the_state_is_the_model();
    test_install_writes_the_app_into_a_scratch_data_dir();
    std::printf("solaris_ntwb: %d passed\n", passed);
    return 0;
}
