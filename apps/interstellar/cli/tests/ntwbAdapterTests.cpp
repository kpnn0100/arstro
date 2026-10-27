// interstellar_ntwb tests — the web face's adapter over an in-memory bridge (R-SVC-4).
//
// The REAL InterstellarService behind the REAL NtwbAdapter and ntwb::Client, the host played by a fake
// on the other end of an ntwb::MemoryTransport (core/Ntwb's own test pattern, as Solaris's
// solaris_ntwb): a browser's call becomes a command line through the one grammar, its reply carries the
// command's output or its refusal, every event is relayed under its own name with its --watch line,
// the `model` state IS what `state print --json` prints, and a `frame` goes to the page that asked
// for it alone; so do what the window gets through AppHooks beyond the model — `thumb` (the
// asynchronous thumbnailer and its epoch), `peaks`, `still`, `capture` — and `browse` lists a folder
// for the page's picker. Then `interstellar-cc ntwb install` into a scratch data dir: the manifest, the
// API, the web UI and every file web/BORROW lists of cosmo's (a missing one fails the install by name)
// — never the user's $XDG_DATA_HOME, settings or recents.
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
#ifndef INTERSTELLAR_SOURCE_DIR
#error "INTERSTELLAR_SOURCE_DIR (apps/interstellar) must be defined by the build"
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

    /** The service, the client, the adapter and the fake host, connected and welcomed. */
    struct Rig
    {
        fs::path dir;
        std::pair<std::shared_ptr<ntwb::MemoryTransport>, std::shared_ptr<ntwb::MemoryTransport>> ends;
        ntwb::Client client{{"interstellar", "1"}};
        FakeHost host;
        cosmo::ThreadBudget budget{50};
        interstellar::InterstellarService svc;
        interstellar_cli::NtwbAdapter adapter;
        double now = 1e12;
        Rig(const char *name, interstellar_cli::NtwbPictures pics, interstellar_cli::JpegEncoder jpeg = fakeJpeg)
            : dir(scratch(name)), ends(ntwb::MemoryTransport::pair()), host{ends.second}, svc(budget, scratchHost(dir)),
              adapter(svc, client, std::move(jpeg), std::move(pics))
        {
            fs::current_path(dir);
            client.setTransport(ends.first);
            adapter.start();
            std::string err;
            assert(client.connect(err));
            host.send(parse(R"({"t":"welcome","ntwb":"1.1.0","session":"main","host":{"name":"h","version":"1"},"clients":["c1"]})"));
            assert(client.poll(0) && client.welcomed());
        }
        void step()
        {
            assert(client.poll(0));
            svc.pump(now += 100);
            adapter.tick(now);
            host.drain();
        }
    };

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


    void test_pictures_go_to_the_page_that_asked()
    {
        // a thumbnailer as the GTK host's: the first ask queues (false), the decode lands later and
        // the epoch rises; the second ask has it
        unsigned epoch = 0;
        int asked = 0;
        std::string lastMedia;
        interstellar_cli::NtwbPictures pics;
        pics.thumbnail = [&](const std::string &media, double t, int edge, interstellar::Raster &out) {
            lastMedia = media;
            if (asked++ == 0) { ++epoch; return false; }   // queued; "decoded" by the next tick
            const int w = edge, h = edge / 2;
            out.allocate(w, h, 128);
            (void)t;
            return true;
        };
        pics.thumbnailEpoch = [&] { return epoch; };
        pics.fit = [](const interstellar::Raster &in, int edge, interstellar::Raster &out) {
            const double s = (double)edge / std::max(in.width, in.height);
            out.allocate(std::max(1, (int)(in.width * s)), std::max(1, (int)(in.height * s)), 7);
        };
        pics.png = [](const uint8_t *rgba, int w, int h, std::string &out) {
            if (!rgba) return false;
            out = "PNG " + std::to_string(w) + "x" + std::to_string(h);
            return true;
        };
        Rig r("pictures", pics);
        r.step();
        const Json *th0 = r.host.lastState("thumbs");
        assert(th0 && (*th0)["epoch"].asInt() == 0);

        Json tp = Json::object();
        tp.set("path", "media/a.mov");
        tp.set("t", 1.5);
        tp.set("edge", 96);
        r.host.call("t1", "thumb", tp);
        r.step();
        const Json *t1 = r.host.result("t1");
        assert(t1 && (*t1)["ok"].asBool() && !(*t1)["data"]["ready"].asBool());
        assert(r.host.blobs("thumb").empty() && lastMedia == "media/a.mov");   // the model's spelling: the host resolves it
        // the decode landed: `thumbs` rises, the page asks again and gets it, alone, with its key
        assert((*r.host.lastState("thumbs"))["epoch"].asInt() == 1);
        r.host.call("t2", "thumb", tp);
        r.step();
        const Json *t2 = r.host.result("t2");
        assert((*t2)["data"]["ready"].asBool() && (*t2)["data"]["w"].asInt() == 96 && (*t2)["data"]["h"].asInt() == 48);
        const auto blobs = r.host.blobs("thumb");
        assert(blobs.size() == 1);
        const Json &meta = blobs[0]->json["meta"];
        assert(blobs[0]->json["client"].asString() == "c1" && blobs[0]->json["mime"].asString() == "image/jpeg");
        assert(meta["path"].asString() == "media/a.mov" && meta["t"].asNumber() == 1.5 && meta["edge"].asInt() == 96);
        assert(meta["epoch"].asInt() == 1 && meta["w"].asInt() == 96 && meta["h"].asInt() == 48);
        assert(blobs[0]->data == "JPEG 96x48");
        Json bad = Json::object();
        r.host.call("t3", "thumb", bad);
        r.step();
        assert(!(*r.host.result("t3"))["ok"].asBool());

        // peaks: none computed for a file nobody cut — ready: false, with the epoch to wait on
        Json pp = Json::object();
        pp.set("media", "nowhere.wav");
        pp.set("perSecond", 25);
        r.host.call("p1", "peaks", pp);
        // a still that does not exist is empty; a capture with nothing open says why, in the window's words
        Json sp = Json::object();
        sp.set("id", "st_404");
        r.host.call("s1", "still", sp);
        r.host.call("c1", "capture", Json::object());
        r.step();
        const Json *p1 = r.host.result("p1");
        assert((*p1)["ok"].asBool() && !(*p1)["data"]["ready"].asBool() && (*p1)["data"].has("peaksEpoch"));
        assert((*r.host.result("s1"))["ok"].asBool() && (*r.host.result("s1"))["data"]["empty"].asBool());
        assert(!(*r.host.result("c1"))["ok"].asBool() &&
               (*r.host.result("c1"))["error"].asString() == "Nothing to copy: no frame under the playhead");
        // a lossless frame for the scopes: format png, refused for anything but jpeg / png
        Json fp = Json::object();
        fp.set("format", "gif");
        r.host.call("f1", "frame", fp);
        r.step();
        assert(!(*r.host.result("f1"))["ok"].asBool() && (*r.host.result("f1"))["error"].asString() == "frame: format is jpeg or png");
        assert(r.host.blobs("still").empty() && r.host.blobs("capture").empty());
        for (const auto &fr : r.host.frames)
            if (!fr.blob) assert(ntwb::validate(fr.json, ntwb::A2H).empty());
        pass("thumb / peaks / still / capture: each picture to the page that asked, a still not decoded yet answers ready:false and `thumbs` rises when it lands");
    }

    void test_without_a_thumbnailer_the_calls_say_so()
    {
        Rig r("nothumbs", interstellar_cli::NtwbPictures{});
        Json tp = Json::object();
        tp.set("path", "a.mov");
        r.host.call("t1", "thumb", tp);
        r.host.call("c1", "capture", Json::object());
        r.step();
        assert(!(*r.host.result("t1"))["ok"].asBool() && (*r.host.result("t1"))["error"].asString().find("no thumbnailer") != std::string::npos);
        assert(!(*r.host.result("c1"))["ok"].asBool() && (*r.host.result("c1"))["error"].asString().find("no PNG encoder") != std::string::npos);
        assert(r.host.lastState("thumbs") == nullptr);   // no epoch to publish
        pass("a build with no thumbnailer or PNG encoder refuses thumb / capture naming why");
    }

    void test_browse_lists_a_folder_by_kind()
    {
        Rig r("browse", interstellar_cli::NtwbPictures{});
        const fs::path d = r.dir / "pick";
        fs::create_directories(d / "Sub");
        for (const char *f : {"b.isp", "A.mov", "c.wav", "grade.cube", "cut.fcpxml", "subs.srt", "look.apf", "still.PNG", "notes.txt", ".hidden.isp"})
            std::ofstream(d / f) << "x";
        Json p = Json::object();
        p.set("path", d.string());
        r.host.call("b1", "browse", p);
        Json q = Json::object();
        q.set("path", (d / "Sub" / "..").string());
        Json kinds = Json::array();
        kinds.push("project");
        q.set("kinds", kinds);
        r.host.call("b2", "browse", q);
        Json bad = Json::object();
        bad.set("path", (d / "b.isp").string());
        r.host.call("b3", "browse", bad);
        r.step();
        const Json &all = (*r.host.result("b1"))["data"];
        assert(all["path"].asString() == fs::weakly_canonical(d).string() && all["parent"].asString() == fs::weakly_canonical(d).parent_path().string());
        std::vector<std::string> got;
        for (const auto &e : all["entries"].items()) got.push_back(e["name"].asString() + ":" + e["kind"].asString());
        const std::vector<std::string> want = {"Sub:dir", "A.mov:video", "b.isp:project", "c.wav:audio", "cut.fcpxml:timeline",
                                               "grade.cube:lut", "look.apf:preset", "still.PNG:image", "subs.srt:captions"};
        assert(got == want);   // folders first, then by name; dot-files and kinds nobody asked for left out
        for (const auto &e : all["entries"].items()) assert(e["kind"].asString() == "dir" || e["size"].asInt() == 1);
        const Json &projects = (*r.host.result("b2"))["data"];
        assert(projects["path"].asString() == fs::weakly_canonical(d).string());   // the folder, normalised
        assert(projects["entries"].size() == 2 && projects["entries"].at(1)["name"].asString() == "b.isp");
        assert(!(*r.host.result("b3"))["ok"].asBool() && (*r.host.result("b3"))["error"].asString().find("is not a folder") != std::string::npos);
        assert(std::string(interstellar_cli::NtwbAdapter::kindOf("notes.txt")) == "file");
        pass("browse lists a folder of the machine: folders first, then the kinds asked for, by name; a file is not a folder");
    }

    /** What web/BORROW lists, as the installer reads it. */
    std::vector<std::string> borrowed(const fs::path &webDir)
    {
        std::vector<std::string> out;
        std::ifstream f(webDir / "BORROW");
        std::string l;
        while (std::getline(f, l))
        {
            if (const auto h = l.find('#'); h != std::string::npos) l.erase(h);
            const auto a = l.find_first_not_of(" \t\r"), b = l.find_last_not_of(" \t\r");
            if (a != std::string::npos) out.push_back(l.substr(a, b - a + 1));
        }
        return out;
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
        for (const char *f : {"index.html", "icon.svg", "css/tokens.css", "css/base.css", "js/main.js", "js/shell.js", "js/model/session.js",
                              "js/vm/app.js", "js/edit/screen.js", "js/edit/menus.js", "js/kit/slot.js", "js/kit/cmd.js",
                              "js/dialogs/picker.js", "fonts/Roboto-Regular.ttf", "fonts/JetBrainsMono-Regular.ttf", "fonts/OFL-Roboto.txt"})
            assert(fs::is_regular_file(app / "web" / f));
        assert(!fs::exists(app / "web" / "BORROW"));   // the installer's list, not a page file
        // every borrowed file is cosmo's, unchanged, at the same relative path
        const fs::path webSrc = fs::path(INTERSTELLAR_SOURCE_DIR) / "web";
        const auto list = borrowed(webSrc);
        assert(std::find(list.begin(), list.end(), "js/core/signal.js") != list.end());
        for (const auto &b : list)
            assert(slurp(app / "web" / b) == slurp(fs::path(COSMO_SOURCE_DIR) / "web" / b));

        // a BORROW that names a file cosmo does not have fails the install BY NAME, and leaves the
        // installed page as it was
        const fs::path broken = scratch("install_broken_web");
        fs::copy(webSrc, broken, fs::copy_options::recursive);
        std::ofstream(broken / "BORROW", std::ios::app) << "\njs/ui/no_such_module.js\n";
        const fs::path errLog = data / "broken.log";
        assert(std::system(("\"" + cc + "\" ntwb install --data-dir \"" + data.string() + "\" --web-dir \"" + broken.string() + "\" > \"" +
                            errLog.string() + "\" 2>&1").c_str()) != 0);
        assert(slurp(errLog).find("js/ui/no_such_module.js") != std::string::npos);
        assert(fs::is_regular_file(app / "web" / "js" / "main.js"));
        assert(std::system(("\"" + cc + "\" ntwb uninstall --data-dir \"" + data.string() + "\" >> \"" + (data / "install.log").string() + "\"").c_str()) == 0);
        assert(!fs::exists(app));
        pass("ntwb install writes the manifest, the API, the web UI and every file web/BORROW lists (a missing one fails by name); uninstall removes them");
    }
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_a_call_is_a_command_line_and_the_state_is_the_model();
    test_frame_without_an_encoder_says_so();
    test_pictures_go_to_the_page_that_asked();
    test_without_a_thumbnailer_the_calls_say_so();
    test_browse_lists_a_folder_by_kind();
    test_install_writes_the_app_into_a_scratch_data_dir();
    std::printf("interstellar_ntwb: %d passed\n", passed);
    return 0;
}
