// solaris_ntwb tests — the web face's adapter over an in-memory bridge (R-SVC-7).
//
// The REAL SolarisService behind the REAL NtwbAdapter and ntwb::Client, the host played by a fake on
// the other end of an ntwb::MemoryTransport (core/Ntwb's own test pattern): a browser's call becomes a
// command line through the one grammar, its reply carries the command's output or its refusal, every
// event is relayed under its own name with its --watch line, and the `model` state IS what
// `state print --json` prints. `browse` lists a folder with the host's own lister (the pickers').
// Then `solaris-cc ntwb install` into a scratch data dir: the manifest, the API, the web UI, the
// window's words and what BORROW borrows from cosmo — never the user's $XDG_DATA_HOME.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "AppModelCodec.h"
#include "Command.h"
#include "Event.h"
#include "Machine.h"
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
#ifndef SOLARIS_SOURCE_DIR
#error "SOLARIS_SOURCE_DIR (apps/solaris) must be defined by the build"
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

        // `live`: EVERY value that moves with no revision, at the model's own paths — equal to the model's own values
        const Json *live = host.lastState("live");
        assert(live);
        const Json full = parse(solaris::modelToJson(svc.model(), false).dump());
        for (const char *k : {"position", "tempo", "tempoId", "masterPeak", "masterRms", "masterHold", "masterClip", "xruns", "countingIn",
                              "recorded", "cpu", "cpuPeak", "swaps", "devices", "launched", "playing"})
        {
            if (!((*live)["transport"][k] == full["transport"][k]))
                std::printf("live.transport.%s = %s, model: %s\n", k, (*live)["transport"][k].dump().c_str(), full["transport"][k].dump().c_str());
            assert((*live)["transport"][k] == full["transport"][k]);
        }
        for (const auto &s : full["strips"].items())
            for (const char *k : {"peak", "rms", "hold", "clip", "cpu"}) assert((*live)["strips"][s["id"].asString()][k] == s[k]);
        for (const char *k : {"momentary", "shortTerm", "integrated", "truePeak", "correlation", "spectrum"}) assert((*live)["loudness"][k] == full["loudness"][k]);
        assert((*live)["video"]["time"] == full["video"]["time"] && (*live)["midiMessages"] == full["midiMessages"]);
        assert((*live)["audition"]["progress"] == full["audition"]["progress"]);
        // … and sent again only when it changed
        const size_t liveSent = [&] { size_t n = 0; for (const auto &f : host.frames) n += !f.blob && f.json["t"].asString() == "state" && f.json["key"].asString() == "live"; return n; }();
        step();
        step();
        const size_t liveAfter = [&] { size_t n = 0; for (const auto &f : host.frames) n += !f.blob && f.json["t"].asString() == "state" && f.json["key"].asString() == "live"; return n; }();
        assert(liveAfter == liveSent);

        // nothing the adapter sent breaks the protocol (NTWB-06)
        for (const auto &f : host.frames)
            if (!f.blob) assert(ntwb::validate(f.json, ntwb::A2H).empty());
        pass("a browser's call is a command line; its reply is the output or the refusal; events relayed; the model state IS state print --json; "
             "`live` carries every value that moves with no revision, at its model path, sent only when it changed");
    }

    void touch(const fs::path &p, const std::string &bytes = "x")
    {
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << bytes;
    }

    void test_browse_lists_a_folder_of_the_machine_with_the_hosts_lister()
    {
        const fs::path root = scratch("browse");
        const fs::path d = root / "Songs";
        touch(d / "b song.slp", "song!");
        touch(d / "Kick.WAV", "RIFF");
        touch(d / "riff.mid");
        touch(d / "clip.mp4");
        touch(d / "pad.slpreset");
        touch(d / "piano.sfz");
        touch(d / "notes.txt");
        touch(d / ".hidden.slp");
        fs::create_directories(d / "zeta");
        fs::create_directories(d / "Alpha");

        auto ends = ntwb::MemoryTransport::pair();
        ntwb::Client client({"solaris", "1"});
        client.setTransport(ends.first);
        solaris::SolarisService svc;
        solaris_cli::NtwbAdapter adapter(svc, client, solaris_host::listDir); // the lister `ntwb serve` binds
        auto call = [&](const Json &params) {
            ntwb::Call c;
            c.method = "browse";
            c.params = params;
            return adapter.onCall(c);
        };
        Json p = Json::object();
        p.set("path", (d / ".." / "Songs").string()); // a path as typed: made canonical
        const Json r = call(p);
        assert(r["path"].asString() == fs::weakly_canonical(d).string());
        assert(r["parent"].asString() == fs::weakly_canonical(root).string());
        // folders first, then files — each by name, case folded; dot-files hidden; the kinds Solaris opens named
        std::vector<std::string> names, kinds;
        for (const auto &e : r["entries"].items())
        {
            names.push_back(e["name"].asString());
            kinds.push_back(e["kind"].asString());
        }
        const std::vector<std::string> wantNames = {"Alpha", "zeta", "b song.slp", "clip.mp4", "Kick.WAV", "notes.txt", "pad.slpreset", "piano.sfz", "riff.mid"};
        const std::vector<std::string> wantKinds = {"dir", "dir", "song", "video", "audio", "other", "preset", "sfz", "midi"};
        assert(names == wantNames && kinds == wantKinds);
        for (const auto &e : r["entries"].items())
        {
            assert(e["path"].asString() == (fs::weakly_canonical(d) / e["name"].asString()).string());
            assert(e["kind"].asString() == "dir" ? !e.has("size") : e.has("size"));
        }
        assert(r["entries"].at(2)["size"].asInt() == 5);
        // `kinds` keeps those files, and every folder (a picker walks through them)
        Json k = Json::array();
        k.push("song");
        p.set("kinds", k);
        const Json songs = call(p);
        assert(songs["entries"].size() == 3 && songs["entries"].at(2)["name"].asString() == "b song.slp");
        // the root has no parent; a file is no folder — said, never guessed
        Json rootP = Json::object();
        rootP.set("path", "/");
        assert(call(rootP)["parent"].isNull());
        bool refused = false;
        try
        {
            Json f = Json::object();
            f.set("path", (d / "riff.mid").string());
            call(f);
        }
        catch (const ntwb::MethodError &e) { refused = std::string(e.what()).find("is not a folder") != std::string::npos; }
        assert(refused);
        // a face with no lister refuses browse
        solaris_cli::NtwbAdapter bare(svc, client);
        refused = false;
        try
        {
            ntwb::Call c;
            c.method = "browse";
            c.params = p;
            bare.onCall(c);
        }
        catch (const ntwb::MethodError &e) { refused = std::string(e.what()).find("no folder lister") != std::string::npos; }
        assert(refused);
        // the API lists it (the host refuses a method it does not list, NTWB-07)
        assert(solaris_cli::NtwbAdapter::apiDescription()["methods"].has("browse"));
        pass("browse lists a folder of the machine with the host's lister: folders first, by name, the kinds Solaris opens");
    }

    void test_a_video_frame_is_a_jpeg_to_the_calling_page_alone()
    {
        auto ends = ntwb::MemoryTransport::pair();
        ntwb::Client client({"solaris", "1"});
        client.setTransport(ends.first);
        FakeHost host{ends.second};
        solaris::SolarisService svc;
        // the host's pieces, faked: a decoder that fits a 4:3 picture to the box, and an "encoder" that says what it got
        std::vector<std::string> asked;
        auto decoder = [&asked](const char *which) {
            return [&asked, which](const std::string &file, double t, int bw, int bh, solaris_cli::VideoPicture &out) {
                asked.push_back(std::string(which) + " " + file + " " + std::to_string((int)(t * 10)) + " " + std::to_string(bw) + "x" + std::to_string(bh));
                if (file == "gone.mp4") return false;
                out.width = std::min(bw, bh * 4 / 3);
                out.height = out.width * 3 / 4;
                out.time = t;
                out.rgba.assign((size_t)out.width * out.height * 4, 200);
                return true;
            };
        };
        solaris_cli::NtwbAdapter::Hooks hooks;
        hooks.videoFrame = decoder("frame");
        hooks.videoThumb = decoder("thumb");
        hooks.jpeg = [](const uint8_t *rgba, int w, int h, int q, std::string &out) {
            out = "JPEG " + std::to_string(w) + "x" + std::to_string(h) + " q" + std::to_string(q) + " " + std::to_string((int)rgba[0]);
            return true;
        };
        solaris_cli::NtwbAdapter adapter(svc, client, hooks);
        adapter.start();
        std::string err;
        assert(client.connect(err));
        host.send(parse(R"({"t":"welcome","ntwb":"1.1.0","session":"main","host":{"name":"h","version":"1"},"clients":["c1"]})"));
        assert(client.poll(0) && client.welcomed());
        Json p = Json::object();
        p.set("file", "/m/clip.mp4");
        p.set("t", 1.5);
        p.set("w", 400);
        p.set("h", 200);
        host.call("v1", "video.frame", p);
        host.call("v2", "video.thumb", p);
        Json bad = p;
        bad.set("file", "gone.mp4");
        host.call("v3", "video.frame", bad);
        Json noBox = p;
        noBox.erase("w");
        host.call("v4", "video.frame", noBox);
        assert(client.poll(0));
        host.drain();
        // each decoder its own, as the window's (App.h onVideoThumb)
        assert(asked.size() == 3 && asked[0] == "frame /m/clip.mp4 15 400x200" && asked[1] == "thumb /m/clip.mp4 15 400x200");
        const Json *r1 = host.result("v1");
        assert(r1 && (*r1)["ok"].asBool() && (*r1)["data"]["w"].asInt() == 266 && (*r1)["data"]["h"].asInt() == 199 && (*r1)["data"]["time"].asNumber() == 1.5);
        assert((*host.result("v3"))["ok"].asBool() && (*host.result("v3"))["data"]["empty"].asBool()); // cannot read it: no frame, said
        assert(!(*host.result("v4"))["ok"].asBool() && (*host.result("v4"))["error"].asString().find("params.w") != std::string::npos);
        // the pictures went to the calling page alone, each on its stream, with what it shows
        int blobs = 0;
        for (const auto &f : host.frames)
            if (f.blob)
            {
                ++blobs;
                assert(f.json["client"].asString() == "c1" && f.json["mime"].asString() == "image/jpeg");
                assert(f.json["stream"].asString() == (blobs == 1 ? "video.frame" : "video.thumb"));
                assert(f.json["meta"]["file"].asString() == "/m/clip.mp4" && f.json["meta"]["w"].asInt() == 266);
                assert(f.data == "JPEG 266x199 q85 200");
                assert(ntwb::validateBlobHeader(f.json, ntwb::A2H).empty());
            }
        assert(blobs == 2);
        // without the host's pieces it is refused, never guessed
        solaris_cli::NtwbAdapter bare(svc, client);
        bool refused = false;
        try
        {
            ntwb::Call c;
            c.method = "video.frame";
            c.params = p;
            bare.onCall(c);
        }
        catch (const ntwb::MethodError &e) { refused = std::string(e.what()).find("no video decoder") != std::string::npos; }
        assert(refused);
        const Json api = solaris_cli::NtwbAdapter::apiDescription();
        assert(api["methods"].has("video.frame") && api["methods"].has("video.thumb") && api["streams"].has("video.frame") && api["state"].has("live"));
        pass("video.frame / video.thumb: the host's two decoders, a JPEG to the calling page alone; unreadable said, no box refused");
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
                              "js/core/motion.js", "js/ui/controls.js", "js/ui/modal.js", "css/dialogs.css", "lang/vi.strings",
                              "fonts/Roboto-Regular.ttf", "fonts/JetBrainsMono-Regular.ttf", "fonts/OFL-Roboto.txt"})
            assert(fs::is_regular_file(app / "web" / f));
        // every borrowed file is cosmo's, unchanged, at the same path (BORROW: js/core/*.js, js/ui/*.js)
        int borrowedCount = 0;
        for (const char *dir : {"js/core", "js/ui"})
            for (const auto &e : fs::directory_iterator(fs::path(COSMO_SOURCE_DIR) / "web" / dir))
                if (e.path().extension() == ".js")
                {
                    assert(slurp(app / "web" / dir / e.path().filename()) == slurp(e.path()));
                    ++borrowedCount;
                }
        assert(borrowedCount >= 9);
        // web/files.json: every installed file — what kit/slot.js mounts
        const Json files = parse(slurp(app / "web" / "files.json"));
        bool listsMain = false, listsSignal = false;
        for (const auto &f : files.items())
        {
            listsMain = listsMain || f.asString() == "js/main.js";
            listsSignal = listsSignal || f.asString() == "js/core/signal.js";
            assert(fs::is_regular_file(app / "web" / f.asString()));
        }
        assert(listsMain && listsSignal);
        // the window's words: the tables it compiles in (R-ACCESS-2)
        assert(slurp(app / "web" / "lang" / "vi.strings") == slurp(fs::path(SOLARIS_SOURCE_DIR) / "lang" / "vi.strings"));
        // a BORROW naming a file cosmo does not have fails the install, naming it — and leaves the install as it was
        const fs::path web = scratch("borrow-web");
        fs::copy(fs::path(SOLARIS_SOURCE_DIR) / "web", web, fs::copy_options::recursive);
        std::ofstream(web / "BORROW", std::ios::app) << "js/ui/no-such-file.js\n";
        const std::string log = (data / "bad.log").string();
        assert(std::system(("\"" + cc + "\" ntwb install --data-dir \"" + data.string() + "\" --web-dir \"" + web.string() + "\" > \"" + log + "\" 2>&1").c_str()) != 0);
        assert(slurp(log).find("js/ui/no-such-file.js") != std::string::npos);
        assert(fs::is_regular_file(app / "web" / "js" / "main.js"));
        assert(std::system(("\"" + cc + "\" ntwb uninstall --data-dir \"" + data.string() + "\" >> \"" + (data / "install.log").string() + "\"").c_str()) == 0);
        assert(!fs::exists(app));
        pass("ntwb install writes the manifest, the API, the web UI, its words and what BORROW borrows from cosmo; a missing borrow fails, named");
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_a_call_is_a_command_line_and_the_state_is_the_model();
    test_browse_lists_a_folder_of_the_machine_with_the_hosts_lister();
    test_a_video_frame_is_a_jpeg_to_the_calling_page_alone();
    test_install_writes_the_app_into_a_scratch_data_dir();
    std::printf("solaris_ntwb: %d passed\n", passed);
    return 0;
}
