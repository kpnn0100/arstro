/*
 *  Cosmo by arstro — NtwbAdapter implementation. See the header for the seam rule; every
 *  method below either parses into a Command, reads the model, or moves pixels.
 */
#include "NtwbAdapter.h"
#include "EditControls.h"
#include "core/service/AppModelCodec.h"
#include "core/service/Command.h"
#include "core/service/Event.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace arstro
{
namespace cosmo_v2
{
    using ntwb::Json;
    namespace fs = std::filesystem;

    namespace
    {
        std::string lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        const char *kindOf(const fs::path &p)
        {
            static const char *kImages[] = {".jpg", ".jpeg", ".png", ".tif", ".tiff", ".raf", ".cr2", ".cr3", ".nef",
                                            ".arw", ".dng", ".orf", ".rw2", ".pef", ".srw", ".nrw", ".raw"};
            const std::string ext = lower(p.extension().string());
            if (ext == ".cmp" || ext == ".cosmoproj") return "project";
            for (const char *e : kImages)
                if (ext == e) return "image";
            return nullptr;
        }

        Json histogramJson(const HistogramData &h)
        {
            // All 256 bins and the frame's own max, because the window's HistogramWidget plots
            // 256 normalised by log1p(maxCount) - the web view draws the same curve only if it
            // gets the same numbers (R-NTWB-5). It used to sum them to 64, which read as a
            // coarser, differently-scaled histogram next to the window's. ~4 KB per frame.
            auto bins = [](const std::array<uint32_t, HistogramData::kBins> &a) {
                Json out = Json::array();
                for (int i = 0; i < HistogramData::kBins; ++i) out.push((long long)a[i]);
                return out;
            };
            Json j = Json::object();
            j.set("r", bins(h.r));
            j.set("g", bins(h.g));
            j.set("b", bins(h.b));
            j.set("lum", bins(h.lum));
            j.set("max", (long long)h.maxCount);
            return j;
        }
    }

    NtwbAdapter::NtwbAdapter(cosmo::CosmoService &svc, ntwb::Client &client, JpegEncoder jpeg,
                             std::map<std::string, std::string> grammarHints, PreDispatch pre, CoverDecoder cover)
        : mSvc(svc), mClient(client), mJpeg(std::move(jpeg)), mHints(std::move(grammarHints)),
          mPre(std::move(pre)), mCover(std::move(cover))
    {
    }

    Json NtwbAdapter::modelJson(const cosmo::AppModel &m)
    {
        cosmo::ModelDumpOptions o;
        o.json = true;
        o.params = true;
        std::string err;
        Json j = Json::parse(cosmo::formatModel(m, o), err);
        if (!err.empty())                 // the codec writes JSON by hand; say so if it ever breaks
        {
            j = Json::object();
            j.set("codecError", err);
        }
        return j;
    }

    void NtwbAdapter::start()
    {
        // R-SVC-5: an Event IS the log line — so it is also the NTWB event, named by the same
        // dotted name, carrying the same line. One text form on every channel.
        mSvc.subscribe([this](const cosmo::Event &e) {
            Json d = Json::object();
            d.set("line", cosmo::formatEvent(e));
            d.set("text", e.text);
            d.set("a", e.a);
            d.set("b", e.b);
            d.set("c", e.c);
            d.set("ms", e.ms);
            mClient.event(cosmo::eventName(e.kind), d);
        });
        mClient.onCall = [this](const ntwb::Call &c, bool &) { return onCall(c); };
        mClient.onNotify = [this](const ntwb::Call &c) {
            // A drag sends `set` ~60 times a second as notifies (no reply to wait for); a
            // rejection still lands in lastError and emits command.rejected, so nothing is lost.
            try { onCall(c); } catch (const std::exception &) {}
        };
        mClient.onClientOpen = [this](const std::string &client) {
            // A late client is complete from `ready` (state `model`); the picture is a blob,
            // which the host does not retain, so hand it the newest one directly.
            sendPreview(client);
        };
    }

    void NtwbAdapter::sendPreview(const std::string &client)
    {
        if (mLastPreview.empty()) return;
        // A live stream: a slow browser skips to the newest frame instead of queueing old ones.
        mClient.blob("preview", "image/jpeg", (const uint8_t *)mLastPreview.data(), mLastPreview.size(),
                     mLastPreviewMeta, client, /*coalesce=*/true);
    }

    void NtwbAdapter::tick(double wallMs)
    {
        RenderService::Frame f;
        if (mSvc.takeFrame(f) && f.width > 0 && !f.rgba.empty())
        {
            std::string jpg;
            if (mJpeg(f.rgba.data(), f.width, f.height, kPreviewQuality, jpg))
            {
                Json meta = Json::object();
                meta.set("seq", (long long)++mFrameSeq);
                meta.set("slot", mSvc.model().frameSlot);
                meta.set("w", f.width);
                meta.set("h", f.height);
                meta.set("level", f.level);
                meta.set("levelEdge", f.levelEdge);
                meta.set("ms", f.ms);
                meta.set("hist", histogramJson(f.hist));
                mLastPreview = std::move(jpg);
                mLastPreviewMeta = meta;
                sendPreview("");
            }
        }
        const cosmo::AppModel &m = mSvc.model();
        if (m.revision != mSentRevision && wallMs - mLastModelMs >= kModelMinIntervalMs)
        {
            mSentRevision = m.revision;
            mLastModelMs = wallMs;
            mClient.state("model", modelJson(m));
        }
    }

    Json NtwbAdapter::runCommand(const std::string &line)
    {
        std::string err;
        const cosmo::Command c = cosmo::parseCommand(line, err);
        if (!err.empty()) throw ntwb::MethodError(err);
        using K = cosmo::Command::Kind;
        if (c.kind == K::None) return Json::object();                           // blank / comment
        // Front-end verbs: the service accepts them and does nothing (R-SVC-6), because
        // printing and waiting belong to whoever drives it. Over NTWB, printing is the
        // `model` state and waiting is watching events — say so rather than pretend.
        if (c.kind == K::StatePrint) return modelJson(mSvc.model());
        if (c.kind == K::Wait) throw ntwb::MethodError("wait is a front-end verb here - watch the events instead");
        if (c.kind == K::UiDump) throw ntwb::MethodError("ui dump needs the cosmo window (cosmo --control)");
        if (c.kind == K::Quit) throw ntwb::MethodError("stop the app from Arstro Remote (Apps) instead");
        if (mPre && !mPre(c, err)) throw ntwb::MethodError(err.empty() ? "rejected by the host" : err);
        if (!mSvc.dispatch(c)) throw ntwb::MethodError(mSvc.model().lastError);
        Json r = Json::object();
        r.set("revision", (long long)mSvc.model().revision);
        return r;
    }

    int NtwbAdapter::sendThumbs(const std::string &client, int onlyNode)
    {
        int sent = 0;
        for (const auto &n : mSvc.model().nodes)
        {
            if (n.slot < 0 || (onlyNode >= 0 && n.node != onlyNode)) continue;
            // TECHNICAL DEBT (R-SVC-2): thumbnails are pixels, which the model deliberately
            // never carries, and there is no service accessor for them yet — so this reads
            // through session(), exactly as the GTK host's filmstrip does. S4's cleanup of
            // session() callers must give both of them one.
            const auto *t = mSvc.session().thumbForSlot(n.slot);
            if (!t || t->rgba.empty()) continue;
            std::string jpg;
            if (!mJpeg(t->rgba.data(), t->w, t->h, kThumbQuality, jpg)) continue;
            Json meta = Json::object();
            meta.set("node", n.node);
            meta.set("slot", n.slot);
            meta.set("w", t->w);
            meta.set("h", t->h);
            if (mClient.blob("thumb", "image/jpeg", (const uint8_t *)jpg.data(), jpg.size(), meta, client)) ++sent;
        }
        return sent;
    }

    bool NtwbAdapter::sendFrameTo(const char *stream, const RenderService::Frame &f, const std::string &client)
    {
        std::string jpg;
        if (f.width <= 0 || f.rgba.empty() || !mJpeg(f.rgba.data(), f.width, f.height, kPreviewQuality, jpg)) return false;
        Json meta = Json::object();
        meta.set("slot", mSvc.model().currentSlot);
        meta.set("w", f.width);
        meta.set("h", f.height);
        meta.set("ms", f.ms);
        return mClient.blob(stream, "image/jpeg", (const uint8_t *)jpg.data(), jpg.size(), meta, client);
    }

    Json NtwbAdapter::before(const ntwb::Call &call)
    {
        // The session caches the geometry-only render per slot + geometry; the JPEG is cached
        // here on the same key, so a view toggling Before <-> After costs one encode, not one
        // per toggle.
        const cosmo::AppModel &m = mSvc.model();
        const EditParams &p = m.params;
        const std::string key = std::to_string(m.currentSlot) + "|" + std::to_string(p.cropX) + "," + std::to_string(p.cropY) +
                                "," + std::to_string(p.cropW) + "," + std::to_string(p.cropH) + "|" + std::to_string(p.rotation) +
                                "|" + std::to_string(p.quarterTurns) + "|" + std::to_string(p.lensDistortion) + "," +
                                std::to_string(p.lensCA) + "," + std::to_string(p.lensVignette);
        Json r = Json::object();
        if (key != mBeforeKey || mBeforeJpeg.empty())
        {
            RenderService::Frame f;
            if (!mSvc.renderBefore(f)) throw ntwb::MethodError("no photo is on the stage");
            std::string jpg;
            if (!mJpeg(f.rgba.data(), f.width, f.height, kPreviewQuality, jpg)) throw ntwb::MethodError("could not encode the Before frame");
            mBeforeKey = key;
            mBeforeJpeg = std::move(jpg);
            mBeforeW = f.width;
            mBeforeH = f.height;
        }
        Json meta = Json::object();
        meta.set("slot", m.currentSlot);
        meta.set("w", mBeforeW);
        meta.set("h", mBeforeH);
        mClient.blob("before", "image/jpeg", (const uint8_t *)mBeforeJpeg.data(), mBeforeJpeg.size(), meta, call.client);
        r.set("w", mBeforeW);
        r.set("h", mBeforeH);
        return r;
    }

    Json NtwbAdapter::uncropped(const ntwb::Call &call)
    {
        RenderService::Frame f;
        if (!mSvc.renderUncropped(f)) throw ntwb::MethodError("no photo is on the stage");
        if (!sendFrameTo("uncropped", f, call.client)) throw ntwb::MethodError("could not encode the uncropped frame");
        Json r = Json::object();
        r.set("w", f.width);
        r.set("h", f.height);
        return r;
    }

    Json NtwbAdapter::cover(const ntwb::Call &call)
    {
        const std::string path = call.params["path"].asString();
        if (path.empty()) throw ntwb::MethodError("cover needs params.path (an image file)");
        const int edge = std::clamp((int)call.params["edge"].asInt(kCoverEdge), 32, 2048);
        if (!mCover) throw ntwb::MethodError("this host cannot decode covers");
        const std::string key = std::to_string(edge) + "|" + path;
        auto it = mCovers.find(key);
        if (it == mCovers.end())
        {
            Cover c;
            if (!mCover(path, edge, c.jpeg, c.w, c.h)) throw ntwb::MethodError("cannot read " + path);
            if (mCovers.size() >= kCoverCache) mCovers.erase(mCovers.begin());
            it = mCovers.emplace(key, std::move(c)).first;
        }
        Json meta = Json::object();
        meta.set("path", path);
        meta.set("w", it->second.w);
        meta.set("h", it->second.h);
        mClient.blob("cover", "image/jpeg", (const uint8_t *)it->second.jpeg.data(), it->second.jpeg.size(), meta,
                     call.client);
        Json r = Json::object();
        r.set("w", it->second.w);
        r.set("h", it->second.h);
        return r;
    }

    Json NtwbAdapter::browse(const Json &params) const
    {
        std::string path = params["path"].asString();
        if (path.empty())
        {
            const char *home = std::getenv("HOME");
            path = home ? home : "/";
        }
        std::error_code ec;
        fs::path p = fs::weakly_canonical(fs::path(path), ec);
        if (ec || !fs::is_directory(p, ec)) throw ntwb::MethodError(path + " is not a folder");
        Json dirs = Json::array(), files = Json::array();
        std::vector<fs::directory_entry> entries;
        for (auto it = fs::directory_iterator(p, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::directory_iterator(); it.increment(ec))
            entries.push_back(*it);
        if (ec && entries.empty()) throw ntwb::MethodError("cannot read " + p.string() + ": " + ec.message());
        std::sort(entries.begin(), entries.end(), [](const fs::directory_entry &a, const fs::directory_entry &b) {
            return lower(a.path().filename().string()) < lower(b.path().filename().string());
        });
        for (const auto &e : entries)
        {
            const std::string name = e.path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            std::error_code ec2;
            Json j = Json::object();
            j.set("name", name);
            j.set("path", e.path().string());
            if (e.is_directory(ec2)) { j.set("kind", "dir"); dirs.push(j); continue; }
            const char *k = kindOf(e.path());
            if (!k) continue;
            j.set("kind", k);
            j.set("size", (long long)e.file_size(ec2));
            files.push(j);
        }
        Json out = Json::object();
        out.set("path", p.string());
        out.set("parent", p.has_parent_path() && p != p.root_path() ? Json(p.parent_path().string()) : Json());
        Json all = Json::array();
        for (const auto &d : dirs.items()) all.push(d);
        for (const auto &f : files.items()) all.push(f);
        out.set("entries", all);
        return out;
    }

    Json NtwbAdapter::onCall(const ntwb::Call &call)
    {
        const std::string &m = call.method;
        if (m == "command")
        {
            if (!call.params["line"].isString()) throw ntwb::MethodError("command needs params.line (a command line)");
            return runCommand(call.params["line"].asString());
        }
        if (m == "model") return modelJson(mSvc.model());
        if (m == "controls") return controlsJson();
        if (m == "commands")
        {
            Json out = Json::array();
            for (const auto &n : cosmo::commandNames())
            {
                Json c = Json::object();
                c.set("name", n);
                auto it = mHints.find(n);
                c.set("args", it == mHints.end() ? std::string() : it->second);
                out.push(c);
            }
            return out;
        }
        if (m == "thumbs")
        {
            const int node = call.params.has("node") ? (int)call.params["node"].asInt(-1) : -1;
            Json r = Json::object();
            r.set("sent", sendThumbs(call.client, node));
            return r;
        }
        if (m == "frame")
        {
            sendPreview(call.client);
            Json r = Json::object();
            r.set("sent", !mLastPreview.empty());
            return r;
        }
        if (m == "browse") return browse(call.params);
        if (m == "before") return before(call);
        if (m == "uncropped") return uncropped(call);
        if (m == "cover") return cover(call);
        throw ntwb::MethodError("no method named " + m);
    }

    Json NtwbAdapter::controlsJson()
    {
        Json sections = Json::array();
        for (const auto &sec : editControls())
        {
            Json s = Json::object();
            s.set("title", sec.title);
            s.set("whiteBalancePicker", sec.whiteBalancePicker);
            Json controls = Json::array();
            for (const auto &c : sec.controls)
            {
                Json j = Json::object();
                j.set("label", c.label);
                j.set("field", c.field);
                j.set("min", c.min);
                j.set("max", c.max);
                // Samples of track -> engine, computed by the SAME functions the window uses, so
                // the browser interpolates and never re-derives (mired temperature, EV/80 ...).
                Json stops = Json::array();
                for (int i = 0; i < kStops; ++i)
                {
                    const double t = c.min + (c.max - c.min) * i / (kStops - 1);
                    Json pair = Json::array();
                    pair.push(t);
                    pair.push(c.toEngine ? c.toEngine(t) : t);
                    stops.push(pair);
                }
                j.set("stops", stops);
                if (c.rampFrom || c.rampTo)
                {
                    char buf[16];
                    std::snprintf(buf, sizeof buf, "#%06X", c.rampFrom);
                    j.set("rampFrom", buf);
                    std::snprintf(buf, sizeof buf, "#%06X", c.rampTo);
                    j.set("rampTo", buf);
                }
                controls.push(j);
            }
            s.set("controls", controls);
            sections.push(s);
        }
        return sections;
    }

    Json NtwbAdapter::apiDescription(const std::map<std::string, std::string> &grammarHints)
    {
        auto P = [](const char *type, bool required, const char *doc) {
            Json p = Json::object();
            p.set("type", type);
            p.set("required", required);
            p.set("doc", doc);
            return p;
        };
        auto method = [](const char *doc, Json params, const char *result) {
            Json m = Json::object();
            m.set("doc", doc);
            m.set("params", params);
            m.set("result", result);
            return m;
        };
        Json methods = Json::object();
        {
            std::string grammar = "one line of the cosmo command grammar (R-SVC-5): ";
            bool first = true;
            for (const auto &n : cosmo::commandNames())
            {
                auto it = grammarHints.find(n);
                grammar += (first ? "" : " | ") + n + (it != grammarHints.end() && !it->second.empty() ? " " + it->second : "");
                first = false;
            }
            Json p = Json::object();
            p.set("line", P("str", true, grammar.c_str()));
            methods.set("command", method("dispatch one command to the service; `state print` returns the model, "
                                          "`wait`/`ui dump`/`quit` are refused (front-end verbs)",
                                          p, "{revision} - or the model for `state print`; a rejection fails with lastError"));
        }
        methods.set("model", method("the whole AppModel as JSON, params included", Json::object(), "the model"));
        methods.set("controls", method("the Basic/Detail slider catalogue; `stops` sample track -> engine units",
                                       Json::object(), "[{title, whiteBalancePicker, controls: [{label, field, min, max, stops, rampFrom?, rampTo?}]}]"));
        methods.set("commands", method("the command grammar: every command name with its argument hint",
                                       Json::object(), "[{name, args}]"));
        {
            Json p = Json::object();
            p.set("node", P("int", false, "only this node (default: every decoded image)"));
            methods.set("thumbs", method("send filmstrip thumbnails to the caller on the `thumb` stream", p, "{sent}"));
        }
        methods.set("frame", method("send the newest preview to the caller on the `preview` stream", Json::object(), "{sent}"));
        methods.set("before", method("render the current photo with its geometry only (the Before of Before / Split) and "
                                     "send it to the caller alone on the `before` stream", Json::object(), "{w, h}"));
        methods.set("uncropped", method("render the current photo with every edit but its crop (what a view that crops "
                                        "shows) and send it to the caller alone on the `uncropped` stream",
                                        Json::object(), "{w, h}"));
        {
            Json p = Json::object();
            p.set("path", P("str", true, "an image file of the board (a recent project's firstImagePath)"));
            p.set("edge", P("int", false, "longest edge in pixels (default 480, 32..2048)"));
            methods.set("cover", method("send a small JPEG of an image to the caller alone on the `cover` stream "
                                        "(the home screen's project covers)", p, "{w, h}"));
        }
        {
            Json p = Json::object();
            p.set("path", P("str", false, "folder to list (default: the home folder)"));
            methods.set("browse", method("list a folder of the board: sub-folders, projects (.cmp) and images",
                                         p, "{path, parent, entries: [{name, path, kind: dir|project|image, size?}]}"));
        }

        Json events = Json::object();
        for (int k = 0; k <= (int)cosmo::Event::Kind::CommandRejected; ++k)
        {
            Json e = Json::object();
            e.set("doc", "the service's event (R-SVC-3); `line` is its formatEvent() text");
            e.set("data", "{line, text, a, b, c, ms}");
            events.set(cosmo::eventName((cosmo::Event::Kind)k), e);
        }
        Json state = Json::object();
        {
            Json s = Json::object();
            s.set("doc", "the AppModel (formatModel JSON with params), pushed on every revision (at most 25/s)");
            s.set("data", "object");
            state.set("model", s);
        }
        Json streams = Json::object();
        {
            Json s = Json::object();
            s.set("doc", "the rendered preview of the current image, on every new frame");
            s.set("mime", "image/jpeg");
            s.set("meta", "{seq, slot, w, h, level, levelEdge, ms, hist: {r, g, b, lum: 256 bins each, max}}");
            streams.set("preview", s);
            Json t = Json::object();
            t.set("doc", "a filmstrip thumbnail (answer to `thumbs`)");
            t.set("mime", "image/jpeg");
            t.set("meta", "{node, slot, w, h}");
            streams.set("thumb", t);
            auto one = [](const char *doc, const char *meta) {
                Json x = Json::object();
                x.set("doc", doc);
                x.set("mime", "image/jpeg");
                x.set("meta", meta);
                return x;
            };
            streams.set("before", one("the Before render, to the client that asked (answer to `before`)", "{slot, w, h}"));
            streams.set("uncropped", one("the uncropped render, to the client that asked (answer to `uncropped`)",
                                         "{slot, w, h, ms}"));
            streams.set("cover", one("a recent project's cover, to the client that asked (answer to `cover`)", "{path, w, h}"));
        }
        Json api = Json::object();
        api.set("ntwb", ntwb::kVersion);
        api.set("app", "cosmo");
        api.set("methods", methods);
        api.set("events", events);
        api.set("state", state);
        api.set("streams", streams);
        return api;
    }
}
}
