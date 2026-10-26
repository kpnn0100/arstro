// interstellar_ntwb tests — the web face's adapter over an in-memory bridge (R-SVC-4).
//
// The REAL InterstellarService behind the REAL NtwbAdapter and ntwb::Client, the host played by a fake
// on the other end of an ntwb::MemoryTransport (core/Ntwb's own test pattern, as Solaris's
// solaris_ntwb): a browser's call becomes a command line through the one grammar, its reply carries the
// command's output or its refusal, every event is relayed under its own name with its --watch line,
// the `model` state IS what `state print --json` prints, and a `frame` goes to the page that asked
// for it alone. Then `interstellar-cc ntwb install` into a scratch data dir: the manifest, the API,
// the web UI and what it borrows from cosmo — never the user's $XDG_DATA_HOME, settings or recents.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "AppModelCodec.h"
#include "Command.h"
#include "Event.h"
#include "InterstellarService.h"
#include "NtwbAdapter.h"
#include "core/ThreadBudget.h"
#include "core/decode/NativeImageDecoder.h"
#include "ntwb/Client.h"
#include "ntwb/Protocol.h"
#include "ntwb/Transport.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef INTERSTELLAR_CC
#error "INTERSTELLAR_CC (the interstellar-cc binary) must be defined by the build"
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
        const char *t = std::getenv("INTERSTELLAR_TEST_DIR");
        fs::path d = (t ? fs::path(t) : fs::temp_directory_path() / "interstellar_ntwb_tests") / sub;
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
        std::vector<const ntwb::Frame *> blobs(const std::string &stream) const
        {
            std::vector<const ntwb::Frame *> out;
            for (const auto &f : frames)
                if (f.blob && f.json["stream"].asString() == stream) out.push_back(&f);
            return out;
        }
    };

    Json line(const std::string &l)
    {
        Json p = Json::object();
        p.set("line", l);
        return p;
    }

    /** A host that touches nothing of the user's: settings, presets and recents in the scratch dir. */
    interstellar::InterstellarService::Host scratchHost(const fs::path &dir)
    {
        interstellar::InterstellarService::Host h;
        h.rackDecoder = [](std::shared_ptr<const interstellar::FrameSelector>) {
            return std::unique_ptr<cosmo::IImageDecoder>(new cosmo::NativeImageDecoder());
        };
        h.settingsPath = (dir / "settings.txt").string();
        h.presetDir = (dir / "presets").string();
        h.recentsPath = (dir / "recents").string();
        return h;
    }

    /** A stand-in encoder: the adapter's job is to route the picture, not to compress it. */
    bool fakeJpeg(const uint8_t *rgba, int w, int h, int, std::string &out)
    {
        if (!rgba || w <= 0 || h <= 0) return false;
        out = "JPEG " + std::to_string(w) + "x" + std::to_string(h);
        return true;
    }

    void test_a_call_is_a_command_line_and_the_state_is_the_model()
    {
        const fs::path dir = scratch("adapter");
        fs::current_path(dir);
        auto ends = ntwb::MemoryTransport::pair();
        ntwb::Client client({"interstellar", "1"});
        client.setTransport(ends.first);
        FakeHost host{ends.second};
        cosmo::ThreadBudget budget(50);
        interstellar::InterstellarService svc(budget, scratchHost(dir));
        interstellar_cli::NtwbAdapter adapter(svc, client, fakeJpeg);
        adapter.start();
        std::string err;
        assert(client.connect(err));
        host.send(parse(R"({"t":"welcome","ntwb":"1.1.0","session":"main","host":{"name":"h","version":"1"},"clients":["c1"]})"));
        assert(client.poll(0) && client.welcomed());

        double now = 1e12;   // the pump's clock only moves forward
        auto step = [&] {
            assert(client.poll(0));
            svc.pump(now += 100);
            adapter.tick(now);
            host.drain();
        };
        host.call("h1", "command", line("project new web.isp --fps 24 --res 640x360"));
        step();
        const Json *r = host.result("h1");
        assert(r && (*r)["ok"].asBool() && (*r)["data"]["revision"].asInt() > 0);
        for (int i = 0; i < 100 && svc.model().screen != interstellar::Screen::Edit; ++i) step();
        step();
        // every event relayed under its own name, carrying the --watch line and its fields
        const auto opened = host.events("project.opened");
        assert(opened.size() == 1);
        assert((*opened[0])["line"].asString().rfind("[evt] project.opened ", 0) == 0);
        assert((*opened[0])["fields"].has("path"));
        // the model state, pushed on the revision: what the page draws
        const Json *m = host.lastState("model");
        assert(m && (*m)["screen"].asString() == "edit" && (*m)["fps"].asNumber() == 24.0 && (*m)["width"].asInt() == 640);

        host.call("h2", "command", line("track add --kind video"));
        host.call("h3", "command", line("timeline list"));
        step();
        assert(host.result("h2") && (*host.result("h2"))["ok"].asBool());
        assert((*host.result("h3"))["data"]["output"].asString() == svc.output()); // the command's own print
        // a scrub or a quick edit: notifies, no reply awaited, and the model follows
        host.call("", "command", line("marker add web_mark --at 0"), true);
        step();
        bool sawMarker = false;
        for (const auto &mk : (*host.lastState("model"))["markers"].items()) sawMarker = sawMarker || mk.dump().find("web_mark") != std::string::npos;
        assert(sawMarker);

        // a refusal is a failed call with the service's sentence — and the command.rejected event
        host.call("h5", "command", line("flurb"));
        host.call("h6", "command", line("wait frame.ready"));
        host.call("h7", "command", line("quit"));
        host.call("h8", "nope", Json::object());
        step();
        assert(!(*host.result("h5"))["ok"].asBool() && (*host.result("h5"))["error"].asString().find("unknown command: flurb") != std::string::npos);
        assert(host.events("command.rejected").size() == 1);
        assert(!(*host.result("h6"))["ok"].asBool() && (*host.result("h6"))["error"].asString().find("wait is the caller's loop") == 0);
        assert(!(*host.result("h7"))["ok"].asBool() && (*host.result("h7"))["error"].asString().find("quit is Arstro Remote's") == 0);
        assert(!svc.quitRequested());
        assert((*host.result("h8"))["error"].asString() == "no method named nope");

        // the web's model IS the CLI's dump: `model` == `state print --json`, parsed — but for the
        // revision, which a dispatch moves AFTER it printed (state print included)
        host.call("h9", "command", line("state print --json"));
        host.call("h10", "model", Json::object());
        host.call("h11", "commands", Json::object());
        step();
        auto unrevised = [](Json j) { assert(j.erase("revision")); return j; };
        const Json printed = unrevised(parse((*host.result("h9"))["data"]["output"].asString()));
        assert(unrevised((*host.result("h10"))["data"]) == printed);
        assert((*host.result("h10"))["data"]["revision"].asInt() == (long long)svc.model().revision);
        assert((*host.result("h11"))["data"].size() == interstellar::commandSpecs().size());
        // the model state follows the revision (the last push is the model as it stands)
        step();
        assert(unrevised(*host.lastState("model")) == unrevised(interstellar_cli::NtwbAdapter::modelJson(svc.model())));

        // the transport has its own key, so a playhead moves with no model per frame
        const Json *t = host.lastState("transport");
        assert(t && !(*t)["playing"].asBool() && (*t).has("playhead") && (*t)["meterPeak"].size() == 2);

        // `frame`: the picture goes to the page that asked, alone, with its size; or "nothing here"
        Json fp = Json::object();
        fp.set("edge", 320);
        host.call("h12", "frame", fp);
        step();
        const Json *f = host.result("h12");
        assert(f && (*f)["ok"].asBool());
        const auto pictures = host.blobs("frame");
        if ((*f)["data"]["empty"].asBool())
            assert(pictures.empty());
        else
        {
            assert(pictures.size() == 1);
            const ntwb::Frame &b = *pictures[0];
            assert(b.json["client"].asString() == "c1" && b.json["mime"].asString() == "image/jpeg");
            const int w = (int)b.json["meta"]["w"].asInt(), hgt = (int)b.json["meta"]["h"].asInt();
            assert(w > 0 && hgt > 0 && std::max(w, hgt) <= 320);
            assert(b.data == "JPEG " + std::to_string(w) + "x" + std::to_string(hgt));
            assert((*f)["data"]["seq"].asInt() == b.json["meta"]["seq"].asInt());
        }

        // nothing the adapter sent breaks the protocol (NTWB-06)
        for (const auto &fr : host.frames)
            if (!fr.blob) assert(ntwb::validate(fr.json, ntwb::A2H).empty());
        pass("a browser's call is a command line; its reply is the output or the refusal; events relayed; the model state IS state print --json; a frame goes to its page");
    }

    void test_frame_without_an_encoder_says_so()
    {
        const fs::path dir = scratch("noencoder");
        fs::current_path(dir);
        auto ends = ntwb::MemoryTransport::pair();
        ntwb::Client client({"interstellar", "1"});
        client.setTransport(ends.first);
        FakeHost host{ends.second};
        cosmo::ThreadBudget budget(50);
        interstellar::InterstellarService svc(budget, scratchHost(dir));
        interstellar_cli::NtwbAdapter adapter(svc, client);   // a build with no codec
        adapter.start();
        std::string err;
        assert(client.connect(err));
        host.send(parse(R"({"t":"welcome","ntwb":"1.1.0","session":"main","host":{"name":"h","version":"1"},"clients":["c1"]})"));
        assert(client.poll(0));
        host.call("f1", "frame", Json::object());
        assert(client.poll(0));
        host.drain();
        assert(!(*host.result("f1"))["ok"].asBool() && (*host.result("f1"))["error"].asString().find("no image encoder") != std::string::npos);
        pass("a build with no image encoder refuses `frame` naming why, and nothing else changes");
    }

    void test_install_writes_the_app_into_a_scratch_data_dir()
    {
        const fs::path data = scratch("install");
        const std::string cc = INTERSTELLAR_CC;
        assert(std::system(("\"" + cc + "\" ntwb install --data-dir \"" + data.string() + "\" > \"" + (data / "install.log").string() + "\"").c_str()) == 0);
        const fs::path app = data / "interstellar";
        const Json manifest = parse(slurp(app / "ntwb.json"));
        assert(manifest["id"].asString() == "interstellar" && manifest["ntwb"].asString() == ntwb::kVersion);
        assert(manifest["exec"].size() == 3 && manifest["exec"].at(1).asString() == "ntwb" && manifest["exec"].at(2).asString() == "serve");
        assert(fs::canonical(manifest["exec"].at(0).asString()) == fs::canonical(cc));
        assert(manifest["web"].asString() == "web" && manifest["api"].asString() == "api.json");
        assert(!manifest["single"].asBool(true));                     // a session per project
        assert(manifest["capabilities"].size() == 1 && manifest["capabilities"].at(0).asString() == "blobs");
        assert(slurp(app / "api.json") == interstellar_cli::NtwbAdapter::apiDescription().dumpPretty());
        for (const char *f : {"index.html", "icon.svg", "css/tokens.css", "css/base.css", "js/main.js", "js/core/signal.js", "js/core/dom.js",
                              "js/model/session.js", "js/vm/edit.js", "fonts/Roboto-Regular.ttf", "fonts/JetBrainsMono-Regular.ttf",
                              "fonts/OFL-Roboto.txt"})
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
    test_frame_without_an_encoder_says_so();
    test_install_writes_the_app_into_a_scratch_data_dir();
    std::printf("interstellar_ntwb: %d passed\n", passed);
    return 0;
}
