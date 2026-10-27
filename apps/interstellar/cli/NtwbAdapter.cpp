/*
 *  interstellar-cc — NtwbAdapter implementation. See the header for the seam rule: every method
 *  below either hands a line to the service, reads its model and events, or asks it for a picture.
 */
#include "NtwbAdapter.h"
#include "AppModelCodec.h"
#include "Command.h"
#include "Event.h"
#include "InterstellarService.h"
#include "FrameSelector.h"
#include "Raster.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace arstro
{
namespace interstellar_cli
{
    using ntwb::Json;

    namespace
    {
        /** The service's JSON (interstellar::Json, writer-only) as the bridge's: one codec writes it,
         *  the other reads it — the same text `state print --json` prints. */
        Json reparse(const std::string &text)
        {
            std::string err;
            Json j = Json::parse(text, err);
            if (!err.empty())
            {
                j = Json::object();
                j.set("codecError", err);
            }
            return j;
        }

        /** A deep (16-bit) picture as the 8-bit one a JPEG carries. */
        const std::vector<uint8_t> &bytes8(const interstellar::Raster &r, std::vector<uint8_t> &tmp)
        {
            if (!r.rgba.empty() || r.rgba16.empty()) return r.rgba;
            tmp.resize(r.rgba16.size());
            for (size_t i = 0; i < tmp.size(); ++i) tmp[i] = (uint8_t)(r.rgba16[i] >> 8);
            return tmp;
        }

        std::string lower(std::string s)
        {
            for (char &c : s) c = (char)std::tolower((unsigned char)c);
            return s;
        }

        bool oneOf(const std::string &ext, std::initializer_list<const char *> list)
        {
            for (const char *e : list)
                if (ext == e) return true;
            return false;
        }

        /** The `kinds` a `browse` asks for: every listed kind by default ("file" only when named). */
        bool wanted(const ntwb::Json &kinds, const std::string &kind)
        {
            if (!kinds.isArray() || kinds.size() == 0) return kind != "file";
            for (const auto &k : kinds.items())
                if (k.isString() && (k.asString() == kind || k.asString() == "any")) return true;
            return false;
        }
    }

    NtwbAdapter::NtwbAdapter(interstellar::InterstellarService &svc, ntwb::Client &client, JpegEncoder jpeg, NtwbPictures pictures)
        : mSvc(svc), mClient(client), mJpeg(std::move(jpeg)), mPics(std::move(pictures))
    {
    }

    const char *NtwbAdapter::kindOf(const std::string &fileName)
    {
        // the same words as the GTK pickers' filters (linux_main.cpp: projects .isp, footage, LUTs
        // .cube, EDL/FCPXML/OTIO, SRT, presets .apf); moving footage is the core's own list
        if (interstellar::looksLikeVideo(fileName)) return "video";
        const auto dot = fileName.find_last_of('.');
        const std::string ext = dot == std::string::npos ? std::string() : lower(fileName.substr(dot + 1));
        if (ext == "isp") return "project";
        if (oneOf(ext, {"jpg", "jpeg", "png", "tif", "tiff", "exr", "dng", "bmp", "webp", "heic", "cr2", "cr3", "nef", "arw", "raf",
                        "orf", "rw2", "pef", "srw"}))
            return "image";
        if (oneOf(ext, {"wav", "aif", "aiff", "mp3", "m4a", "aac", "flac", "ogg", "opus"})) return "audio";
        if (ext == "cube") return "lut";
        if (oneOf(ext, {"edl", "fcpxml", "xml", "otio"})) return "timeline";
        if (ext == "srt") return "captions";
        if (ext == "apf") return "preset";
        return "file";
    }

    Json NtwbAdapter::modelJson(const interstellar::AppModel &m) { return reparse(interstellar::modelToJson(m).dump()); }

    Json NtwbAdapter::transportJson(const interstellar::AppModel &m)
    {
        Json t = Json::object();
        t.set("playing", m.playing);
        t.set("playhead", m.playhead);
        t.set("playheadFrame", m.playheadFrame);
        t.set("duration", m.duration);
        t.set("shuttle", m.shuttle);
        t.set("frameSeq", (long long)m.frameSeq);
        t.set("sourceView", m.sourceView);
        t.set("sourcePlayhead", m.sourcePlayhead);
        t.set("soundPlaying", m.soundPlaying);
        Json peak = Json::array();
        peak.push(m.meterPeakL);
        peak.push(m.meterPeakR);
        t.set("meterPeak", peak);
        Json rms = Json::array();
        rms.push(m.meterRmsL);
        rms.push(m.meterRmsR);
        t.set("meterRms", rms);
        t.set("meterClip", m.meterClip);
        return t;
    }

    void NtwbAdapter::start()
    {
        // R-SVC-2: an Event IS the log line — so it is also the NTWB event, named by the same dotted
        // name, carrying the same line and its declared fields. One text form on every channel.
        mSvc.subscribe([this](const interstellar::Event &e) {
            Json fields = Json::object();
            for (const auto &kv : e.fields) fields.set(kv.first, kv.second);
            Json d = Json::object();
            d.set("line", interstellar::formatEvent(e));
            d.set("fields", fields);
            mClient.event(interstellar::eventName(e.kind), d);
        });
        mClient.onCall = [this](const ntwb::Call &c, bool &) { return onCall(c); };
        // a scrub sends `playhead <t>` as notifies (no reply awaited); a refusal still lands in
        // model.lastError and the command.rejected event, so nothing is lost
        mClient.onNotify = [this](const ntwb::Call &c) {
            try { onCall(c); } catch (const std::exception &) {}
        };
    }

    void NtwbAdapter::tick(double wallMs)
    {
        const interstellar::AppModel &m = mSvc.model();
        const double every = m.playing ? kModelPlayingIntervalMs : kModelMinIntervalMs;
        if (m.revision != mSentRevision && wallMs - mLastModelMs >= every)
        {
            mSentRevision = m.revision;
            mLastModelMs = wallMs;
            mClient.state("model", modelJson(m));
        }
        // the playhead and the meters move every frame while playing: their own small key, sent when
        // they changed, so a page's playhead follows without the whole model per frame
        if (wallMs - mLastTransportMs >= kTransportMinIntervalMs)
        {
            const Json t = transportJson(m);
            std::string text = t.dump();
            if (text != mSentTransport)
            {
                mSentTransport = std::move(text);
                mLastTransportMs = wallMs;
                mClient.state("transport", t);
            }
        }
        // a still the thumbnailer decoded off this loop has landed: pages re-ask for what they are
        // missing (App::render's thumbnailEpoch check, D-5/D-6)
        if (mPics.thumbnailEpoch)
        {
            const long long ep = (long long)mPics.thumbnailEpoch();
            if (ep != mSentThumbEpoch)
            {
                mSentThumbEpoch = ep;
                Json j = Json::object();
                j.set("epoch", ep);
                mClient.state("thumbs", j);
            }
        }
    }

    Json NtwbAdapter::runCommand(const std::string &line)
    {
        using K = interstellar::Command::Kind;
        std::string err;
        const interstellar::Command c = interstellar::parseCommand(line, err);
        if (err.empty() && c.kind == K::Wait)
            throw ntwb::MethodError("wait is the caller's loop - watch the events (frame.ready, render.finished) instead");
        if (err.empty() && c.kind == K::Quit)
            throw ntwb::MethodError("quit is Arstro Remote's - stop the session from Apps (arstro-remote apps stop interstellar)");
        if (err.empty() && c.kind == K::None)
        {
            Json r = Json::object();
            r.set("output", "");
            r.set("revision", (long long)mSvc.model().revision);
            return r; // a blank line or a comment: nothing ran
        }
        if (!mSvc.dispatchText(line, err)) throw ntwb::MethodError(err);
        Json r = Json::object();
        r.set("output", mSvc.output());
        r.set("revision", (long long)mSvc.model().revision);
        return r;
    }

    Json NtwbAdapter::frame(const ntwb::Call &call)
    {
        const Json &p = call.params;
        const interstellar::AppModel &m = mSvc.model();
        const std::string source = p["source"].isString() ? p["source"].asString() : std::string();
        // the timeline at its playhead by default; a source at its reference frame (t < 0) by default
        const double t = p["t"].isNumber() ? p["t"].asNumber() : (source.empty() ? m.playhead : -1.0);
        const int edge = (int)std::clamp<long long>(p["edge"].isNumber() ? p["edge"].asInt() : kDefaultEdge, kMinEdge, kMaxEdge);
        // png = the frame's exact 8-bit pixels, for a page whose scopes must read what the native's
        // scopesOf reads (App::showScopes); jpeg (the default) for everything that only looks
        const std::string format = p["format"].isString() ? p["format"].asString() : std::string("jpeg");
        if (format != "jpeg" && format != "png") throw ntwb::MethodError("frame: format is jpeg or png");
        if (format == "png" && !mPics.png) throw ntwb::MethodError("this build has no PNG encoder (interstellar-cc needs gdk-pixbuf for a lossless frame)");
        if (!mJpeg) throw ntwb::MethodError("this build has no image encoder (interstellar-cc needs gdk-pixbuf for pictures)");

        const auto t0 = std::chrono::steady_clock::now();
        interstellar::Raster r;
        const bool ok = source.empty() ? mSvc.renderFrame(t, edge, r) : mSvc.renderSourceFrame(source, t, edge, r);
        Json out = Json::object();
        if (!ok || r.width <= 0 || r.height <= 0)
        {
            out.set("empty", true); // nothing to show here (no timeline, no clip at t, an unknown source)
            out.set("lastError", m.lastError);
            return out;
        }
        std::vector<uint8_t> tmp;
        const std::vector<uint8_t> &px = bytes8(r, tmp);
        std::string jpeg;
        const bool encoded = format == "png" ? mPics.png(px.data(), r.width, r.height, jpeg) : mJpeg(px.data(), r.width, r.height, kFrameQuality, jpeg);
        if (!encoded) throw ntwb::MethodError("the frame could not be encoded");
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

        Json meta = Json::object();
        meta.set("seq", ++mFrameSeq);
        meta.set("t", t);
        meta.set("source", source);
        meta.set("w", r.width);
        meta.set("h", r.height);
        meta.set("edge", edge);
        meta.set("frameSeq", (long long)m.frameSeq);
        meta.set("ms", ms);
        meta.set("format", format);
        // the picture this page asked for goes to this page alone (another page may be on another tab)
        mClient.blob("frame", format == "png" ? "image/png" : "image/jpeg", (const uint8_t *)jpeg.data(), jpeg.size(), meta, call.client, false);
        out.set("seq", mFrameSeq);
        out.set("w", r.width);
        out.set("h", r.height);
        out.set("bytes", (long long)jpeg.size());
        out.set("ms", ms);
        return out;
    }

    Json NtwbAdapter::thumb(const ntwb::Call &call)
    {
        const Json &p = call.params;
        if (!p["path"].isString() || p["path"].asString().empty()) throw ntwb::MethodError("thumb needs params.path (a media file as the model names it)");
        if (!mPics.thumbnail) throw ntwb::MethodError("this build has no thumbnailer (interstellar-cc needs FFmpeg for stills)");
        if (!mJpeg) throw ntwb::MethodError("this build has no image encoder (interstellar-cc needs gdk-pixbuf for pictures)");
        const std::string path = p["path"].asString();
        const double t = p["t"].isNumber() ? p["t"].asNumber() : 0.0;
        const int edge = (int)std::clamp<long long>(p["edge"].isNumber() ? p["edge"].asInt() : kThumbDefaultEdge, kThumbMinEdge, kThumbMaxEdge);
        const long long epoch = mPics.thumbnailEpoch ? (long long)mPics.thumbnailEpoch() : 0;
        Json out = Json::object();
        out.set("epoch", epoch);
        interstellar::Raster r;
        if (!mPics.thumbnail(path, t, edge, r) || r.empty())
        {
            out.set("ready", false);   // queued (or a file that cannot be decoded): ask again when `thumbs` rises
            return out;
        }
        std::vector<uint8_t> tmp;
        const std::vector<uint8_t> &px = bytes8(r, tmp);
        std::string jpeg;
        if (!mJpeg(px.data(), r.width, r.height, kThumbQuality, jpeg)) throw ntwb::MethodError("the thumbnail could not be encoded");
        Json meta = Json::object();
        meta.set("path", path);
        meta.set("t", t);
        meta.set("edge", edge);
        meta.set("epoch", epoch);
        meta.set("w", r.width);
        meta.set("h", r.height);
        mClient.blob("thumb", "image/jpeg", (const uint8_t *)jpeg.data(), jpeg.size(), meta, call.client, false);
        out.set("ready", true);
        out.set("w", r.width);
        out.set("h", r.height);
        out.set("bytes", (long long)jpeg.size());
        return out;
    }

    Json NtwbAdapter::peaks(const ntwb::Call &call)
    {
        const Json &p = call.params;
        if (!p["media"].isString() || p["media"].asString().empty()) throw ntwb::MethodError("peaks needs params.media (a clip's media as the model names it)");
        std::vector<float> pk;
        double per = 0.0;
        Json out = Json::object();
        if (!mSvc.audioPeaks(p["media"].asString(), pk, per) || per <= 0.0)
        {
            out.set("ready", false);   // not computed yet: model.peaksEpoch rises when it lands
            out.set("peaksEpoch", (long long)mSvc.model().peaksEpoch);
            return out;
        }
        // a coarser envelope when the page asks for one (a whole-file overview): the peak of each
        // group of k values, so a short spike is never lost — transport size, not a new envelope
        const double want = p["perSecond"].isNumber() ? p["perSecond"].asNumber() : 0.0;
        const int k = want > 0.0 && want < per ? std::max(1, (int)std::ceil(per / want)) : 1;
        Json arr = Json::array();
        for (size_t i = 0; i < pk.size(); i += (size_t)k)
        {
            float m = 0.0f;
            for (size_t j = i; j < std::min(pk.size(), i + (size_t)k); ++j) m = std::max(m, pk[j]);
            arr.push(std::round((double)m * 1e4) / 1e4);   // 1/10000 of full scale: -80 dB, more than a waveform draws
        }
        out.set("ready", true);
        out.set("perSecond", per / k);
        out.set("peaks", arr);
        return out;
    }

    Json NtwbAdapter::still(const ntwb::Call &call)
    {
        const Json &p = call.params;
        if (!p["id"].isString() || p["id"].asString().empty()) throw ntwb::MethodError("still needs params.id (a still's id, model.stills[].id)");
        if (!mJpeg) throw ntwb::MethodError("this build has no image encoder (interstellar-cc needs gdk-pixbuf for pictures)");
        const std::string id = p["id"].asString();
        const int edge = (int)std::clamp<long long>(p["edge"].isNumber() ? p["edge"].asInt() : kStillDefaultEdge, 0, kMaxEdge);
        interstellar::Raster full;
        Json out = Json::object();
        if (!mSvc.stillPicture(id, full) || full.empty())
        {
            out.set("empty", true);
            return out;
        }
        interstellar::Raster shallow, fitted;
        interstellar::toShallow(full, shallow);
        const interstellar::Raster *r = &shallow;
        if (edge > 0 && mPics.fit && std::max(shallow.width, shallow.height) > edge)
        {
            mPics.fit(shallow, edge, fitted);
            if (!fitted.empty()) r = &fitted;
        }
        std::string jpeg;
        if (!mJpeg(r->rgba.data(), r->width, r->height, kThumbQuality, jpeg)) throw ntwb::MethodError("the still could not be encoded");
        Json meta = Json::object();
        meta.set("id", id);
        meta.set("edge", edge);
        meta.set("w", r->width);
        meta.set("h", r->height);
        mClient.blob("still", "image/jpeg", (const uint8_t *)jpeg.data(), jpeg.size(), meta, call.client, false);
        out.set("w", r->width);
        out.set("h", r->height);
        out.set("bytes", (long long)jpeg.size());
        return out;
    }

    Json NtwbAdapter::capture(const ntwb::Call &call)
    {
        const Json &p = call.params;
        const std::string bind = p["bind"].isString() ? p["bind"].asString() : std::string();
        if (!mPics.png) throw ntwb::MethodError("this build has no PNG encoder (interstellar-cc needs gdk-pixbuf to copy a frame)");
        interstellar::Raster r;
        // the GTK host's own sentences (linux_main.cpp copyFrame)
        if (!mSvc.captureFrame(bind, r) || r.empty())
            throw ntwb::MethodError(bind.empty() ? "Nothing to copy: no frame under the playhead" : "Could not render " + bind + " to copy");
        std::vector<uint8_t> tmp;
        const std::vector<uint8_t> &px = bytes8(r, tmp);
        std::string png;
        if (!mPics.png(px.data(), r.width, r.height, png)) throw ntwb::MethodError("the frame could not be encoded");
        Json meta = Json::object();
        meta.set("seq", ++mFrameSeq);
        meta.set("bind", bind);
        meta.set("w", r.width);
        meta.set("h", r.height);
        mClient.blob("capture", "image/png", (const uint8_t *)png.data(), png.size(), meta, call.client, false);
        Json out = Json::object();
        out.set("seq", mFrameSeq);
        out.set("w", r.width);
        out.set("h", r.height);
        out.set("bytes", (long long)png.size());
        return out;
    }

    Json NtwbAdapter::browse(const ntwb::Call &call) const
    {
        namespace fs = std::filesystem;
        const Json &p = call.params;
        std::string path = p["path"].isString() ? p["path"].asString() : std::string();
        if (path.empty())
        {
            const char *home = std::getenv("HOME");
            path = home ? home : "/";
        }
        std::error_code ec;
        const fs::path dir = fs::weakly_canonical(fs::path(path), ec);
        if (ec || !fs::is_directory(dir, ec)) throw ntwb::MethodError(path + " is not a folder");
        std::vector<fs::directory_entry> entries;
        for (auto it = fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::directory_iterator(); it.increment(ec))
            entries.push_back(*it);
        if (ec && entries.empty()) throw ntwb::MethodError("cannot read " + dir.string() + ": " + ec.message());
        std::sort(entries.begin(), entries.end(), [](const fs::directory_entry &a, const fs::directory_entry &b) {
            return lower(a.path().filename().string()) < lower(b.path().filename().string());
        });
        Json dirs = Json::array(), files = Json::array();
        for (const auto &e : entries)
        {
            const std::string name = e.path().filename().string();
            if (name.empty() || name[0] == '.') continue;   // dot-files hidden, as a file chooser does
            std::error_code ec2;
            Json j = Json::object();
            j.set("name", name);
            j.set("path", e.path().string());
            if (e.is_directory(ec2))
            {
                j.set("kind", "dir");
                dirs.push(j);
                continue;
            }
            const std::string kind = kindOf(name);
            if (!wanted(p["kinds"], kind)) continue;
            j.set("kind", kind);
            j.set("size", (long long)e.file_size(ec2));
            files.push(j);
        }
        Json all = Json::array();
        for (const auto &d : dirs.items()) all.push(d);
        for (const auto &f : files.items()) all.push(f);
        Json out = Json::object();
        out.set("path", dir.string());
        out.set("parent", dir.has_parent_path() && dir != dir.root_path() ? Json(dir.parent_path().string()) : Json());
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
        if (m == "frame") return frame(call);
        if (m == "thumb") return thumb(call);
        if (m == "peaks") return peaks(call);
        if (m == "still") return still(call);
        if (m == "capture") return capture(call);
        if (m == "browse") return browse(call);
        if (m == "commands")
        {
            Json out = Json::array();
            for (const auto &s : interstellar::commandSpecs())
            {
                Json c = Json::object();
                c.set("name", s.verb);
                c.set("usage", interstellar::usageOf(s));
                c.set("summary", s.summary);
                out.push(c);
            }
            return out;
        }
        throw ntwb::MethodError("no method named " + m);
    }

    Json NtwbAdapter::apiDescription()
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
            Json p = Json::object();
            p.set("line", P("str", true,
                            "one line of the Interstellar command grammar (R-SVC-1) - every command is in "
                            "apps/interstellar/docs/API.md, and the `commands` method lists them"));
            methods.set("command", method("dispatch one command line to the service, as the window and interstellar-cc do; "
                                          "`wait` and `quit` are refused (watch the events; stop the session from Apps)",
                                          p, "{output, revision} - output is what the command printed; a refusal fails with its reason"));
        }
        methods.set("model", method("the whole AppModel as JSON (what `state print --json` prints)", Json::object(), "the model"));
        methods.set("commands", method("the command grammar: every command with its usage and summary", Json::object(),
                                       "[{name, usage, summary}]"));
        {
            Json p = Json::object();
            p.set("t", P("float", false, "seconds: the timeline's time (default: the playhead), or the source's (default: its reference frame)"));
            p.set("source", P("str", false, "a rack source's bind name: its own picture, as the Grade tab and the source viewer show it"));
            p.set("edge", P("int", false, "the long edge in pixels, 64-3840 (default 960)"));
            p.set("format", P("str", false, "jpeg (default) | png - the frame's exact 8-bit pixels, for scopes that must read what the window's do"));
            methods.set("frame", method("render the picture this page shows and send it to this page alone (stream `frame`)",
                                        p, "{seq, w, h, bytes, ms}, or {empty: true, lastError} when there is nothing to show"));
        }
        {
            Json p = Json::object();
            p.set("path", P("str", true, "a media file as the model names it (rack[].media, recents[].coverPath)"));
            p.set("t", P("float", false, "seconds into it (default 0)"));
            p.set("edge", P("int", false, "the long edge in pixels, 16-1024 (default 160)"));
            methods.set("thumb", method("a small still of a media file - a Home cover, a filmstrip cell, the reference strip "
                                        "(the window's thumbnail hook) - to this page alone (stream `thumb`); decoded off the "
                                        "loop, so a still not decoded yet answers ready: false: ask again when state `thumbs` rises",
                                        p, "{ready: true, epoch, w, h, bytes} | {ready: false, epoch}"));
        }
        {
            Json p = Json::object();
            p.set("media", P("str", true, "an audio clip's media as the model names it (clips[].media)"));
            p.set("perSecond", P("float", false, "a coarser envelope: at most this many peaks a second (default: the service's own)"));
            methods.set("peaks", method("a file's waveform envelope: the peak of |L|,|R| per 1/perSecond s (R-AUD-7); "
                                        "not computed yet = ready: false, and model.peaksEpoch rises when it lands",
                                        p, "{ready: true, perSecond, peaks: [0..1]} | {ready: false, peaksEpoch}"));
        }
        {
            Json p = Json::object();
            p.set("id", P("str", true, "a still's id (model.stills[].id)"));
            p.set("edge", P("int", false, "the long edge in pixels, 0 = its own size (default 320)"));
            methods.set("still", method("a gallery still's picture (R-CLR-4) to this page alone (stream `still`)",
                                        p, "{w, h, bytes} | {empty: true}"));
        }
        {
            Json p = Json::object();
            p.set("bind", P("str", false, "a rack source's bind name (Grade: its reference frame); default: the timeline at the playhead"));
            methods.set("capture", method("what the monitor shows, at full resolution, as a PNG to this page alone (stream `capture`) - "
                                          "Copy Frame (R-UI-11): the page puts it on its own clipboard",
                                          p, "{seq, w, h, bytes}; a refusal says why"));
        }
        {
            Json p = Json::object();
            p.set("path", P("str", false, "the folder to list (default: the home folder)"));
            p.set("kinds", P("list[str]", false, "the file kinds to list: project | video | image | audio | lut | timeline | captions | "
                                            "preset | file (anything else); default every kind but file; folders always"));
            methods.set("browse", method("list a folder of this machine for the page's file picker: sub-folders first, then the "
                                         "files of the kinds asked, by name; dot-files hidden",
                                         p, "{path, parent, entries: [{name, path, kind, size?}]}"));
        }

        Json events = Json::object();
        for (const auto &s : interstellar::eventSpecs())
        {
            Json e = Json::object();
            e.set("doc", s.summary);
            std::string data = "{line, fields: {";
            for (size_t i = 0; i < s.fields.size(); ++i) data += (i ? ", " : "") + s.fields[i];
            data += "}}";
            e.set("data", data);
            events.set(s.name, e);
        }
        Json state = Json::object();
        {
            Json s = Json::object();
            s.set("doc", "the AppModel (state print --json), pushed on every revision (at most 25/s; 2/s while playing)");
            s.set("data", "object");
            state.set("model", s);
            Json t = Json::object();
            t.set("doc", "what moves without an edit, pushed when it changed (at most 25/s)");
            t.set("data", "{playing, playhead, playheadFrame, duration, shuttle, frameSeq, sourceView, sourcePlayhead, soundPlaying, "
                          "meterPeak: [l, r], meterRms: [l, r], meterClip}");
            state.set("transport", t);
            Json th = Json::object();
            th.set("doc", "the thumbnailer's epoch: it rises whenever a still asked for with `thumb` was decoded - ask again "
                          "for what came back ready: false");
            th.set("data", "{epoch}");
            state.set("thumbs", th);
        }
        Json streams = Json::object();
        {
            Json f = Json::object();
            f.set("doc", "the picture a page asked for (answer to `frame`), to that page alone");
            f.set("mime", "image/jpeg | image/png (format png)");
            f.set("meta", "{seq, t, source, w, h, edge, frameSeq, ms, format}");
            streams.set("frame", f);
            Json th = Json::object();
            th.set("doc", "a still of a media file a page asked for (answer to `thumb`), to that page alone");
            th.set("mime", "image/jpeg");
            th.set("meta", "{path, t, edge, epoch, w, h}");
            streams.set("thumb", th);
            Json st = Json::object();
            st.set("doc", "a gallery still's picture a page asked for (answer to `still`), to that page alone");
            st.set("mime", "image/jpeg");
            st.set("meta", "{id, edge, w, h}");
            streams.set("still", st);
            Json cp = Json::object();
            cp.set("doc", "the monitor's picture at full resolution (answer to `capture`), to that page alone");
            cp.set("mime", "image/png");
            cp.set("meta", "{seq, bind, w, h}");
            streams.set("capture", cp);
        }
        Json api = Json::object();
        api.set("ntwb", ntwb::kVersion);
        api.set("app", "interstellar");
        api.set("methods", methods);
        api.set("events", events);
        api.set("state", state);
        api.set("streams", streams);
        return api;
    }
}
}
