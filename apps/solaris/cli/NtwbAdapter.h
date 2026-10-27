/*
 *  solaris-cc — NtwbAdapter: Solaris as an app of Arstro Remote (R-SVC-7; cosmo's worked example,
 *  apps/cosmo/cli/NtwbAdapter.h; the skill arstro.ntwb.implement).
 *
 *  `solaris-cc ntwb serve` runs THE service — the same SolarisService the window and the CLI drive —
 *  with no window, and this adapter puts it on the NTWB bridge, so the song can be seen and edited in
 *  a browser while the engine, the files and the audio device stay on this machine.
 *
 *  Seam rule, as ControlServer's and solaris-cc's: THIS CLASS HOLDS NO BEHAVIOUR. It translates:
 *
 *      NTWB call `command {line}`        ->  SolarisService::dispatchText   (the one grammar, docs/API.md)
 *      AppModel (modelToJson)            ->  NTWB state `model`             (on every revision, ≤ 25/s)
 *      transport + meters                ->  NTWB state `transport`         (while they move, ≤ 20/s)
 *      what moves with no revision       ->  NTWB state `live`              (when it changed, ≤ 25/s): EVERY value
 *                                            the window's widgets read every frame and `pump()` moves without an edit
 *                                            (the model's `!stable` fields, AppModelCodec.cpp) — the page merges it
 *                                            over `model`, so nothing the native shows live freezes on the web
 *      Event                             ->  NTWB event `<eventName>`       {line: formatEvent(e), fields}
 *      NTWB call `browse {path, kinds?}` ->  the host's folder lister       (the same `listDir` the service's browser
 *                                            uses — the machine's files, for the page's pickers: App's onPick*)
 *      NTWB call `video.frame` / `video.thumb {file, t, w, h}` -> the host's video decoder (App's onVideoFrame /
 *                                            onVideoThumb hooks — the Video window's picture and the video lane's
 *                                            thumbnails), a JPEG blob (stream of the same name) to the CALLING page alone
 *
 *  `wait` is refused: it would sleep this loop (the bridge's pings included) and a browser waits by
 *  watching events. The model is the same JSON `state print --json` prints, so a page draws exactly
 *  what an agent reads (R-SVC-7: "the service is the model; the web draws it"). MVVM: what a page
 *  shows and how (zoom, scroll, an open panel) is the page's own and never sent.
 *
 *  The API it serves is `apiDescription()`, generated from the event table, committed as
 *  docs/ntwb-api.json (ctest solaris_ntwb_api_current) and installed as the manifest's `api`.
 */
#pragma once
#include "ntwb/Client.h"
#include "ntwb/Json.h"
#include "WebAudio.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris
{
    class SolarisService;
    struct AppModel;
    struct BrowserEntry;
}
namespace solaris_cli
{
    /** A frame of a picture (App's VideoImage): width × height RGBA8, rows top first; `time` the frame's own seconds. */
    struct VideoPicture
    {
        int width = 0, height = 0;
        double time = -1;
        std::vector<uint8_t> rgba;
    };

    class NtwbAdapter
    {
    public:
        /** The host's folder lister (`solaris_host::listDir`, what `SolarisService::Host::listDir` is bound to). */
        using Lister = std::function<bool(const std::string &path, std::vector<solaris::BrowserEntry> &out, std::string &err)>;
        /** App's VideoFrameSource: the frame showing `t` seconds into `file`, fitted to boxW × boxH. */
        using VideoSource = std::function<bool(const std::string &file, double t, int boxW, int boxH, VideoPicture &out)>;
        /** RGBA8 → JPEG (the host's encoder: GdkPixbuf, as interstellar-cc and cosmo-cc encode). */
        using JpegEncoder = std::function<bool(const uint8_t *rgba, int w, int h, int quality, std::string &out)>;

        /** What the host gives the adapter that the service does not have: without one, its method is refused, never
         *  guessed. `videoFrame` (video.frame) and `videoThumb` (video.thumb) are two decoders (a thumbnail's seek never costs the window's frames —
         *  App.h onVideoThumb). */
        struct Hooks
        {
            Lister listDir;
            VideoSource videoFrame, videoThumb;
            JpegEncoder jpeg;
            /** R-SVC-7: the "web" driver's hub (WebAudio.h) - what the engine plays, for the pages that `audio.listen`. */
            std::shared_ptr<WebAudioHub> audio;
        };

        NtwbAdapter(solaris::SolarisService &svc, ntwb::Client &client, Hooks hooks);
        NtwbAdapter(solaris::SolarisService &svc, ntwb::Client &client, Lister listDir = {});

        /** Install the call handlers and the event subscription. Call once, before connect(). */
        void start();
        /** After every pump: publish a changed model, a moving transport and what moves live. Never blocks. */
        void tick(double wallMs);

        /** The Solaris API as NTWB describes an app's API (ntwb spec: APP_API). */
        static ntwb::Json apiDescription();
        /** The model as `state print --json` prints it (peaks and position included). */
        static ntwb::Json modelJson(const solaris::AppModel &m);
        /** What moves without an edit: the transport, the master's and every strip's meters. */
        static ntwb::Json transportJson(const solaris::AppModel &m);
        /** EVERY value that moves with no revision, at the model's own paths (AppModelCodec's `!stable` fields):
         *    transport  {playing, recording, position, tempo, tempoId, latencyMs, masterPeak, masterRms, masterHold,
         *                masterClip, driver, xruns, countingIn, recorded, cpu, cpuPeak, swaps, devices[], launched[]}
         *    strips     {<strip id>: {peak, rms, hold, clip, cpu}}
         *    readings   {<device id>: [value, …]}   strips[].devices[] and masterDevices[] that measure (R-FX-17), in order
         *    bindings   [live, …]                   bindings[i].live, in the model's order (R-MIX-16)
         *    loudness   {momentary, shortTerm, integrated, truePeak, correlation, spectrum}
         *    video      {time};  audition {file, playing, progress};  midiMessages
         *  A page overlays it on `model` path by path (js/vm/app.js). */
        static ntwb::Json liveJson(const solaris::AppModel &m);
        /** A call, answered as the bridge would answer it — exposed for the tests. */
        ntwb::Json onCall(const ntwb::Call &call);
        /** `browse {path, kinds?}`: a folder of the board — `{path, parent, entries: [{name, path, kind, size?}]}`,
         *  folders first, then files, each by name (case folded); dot-files hidden. `kind` is the host's (dir · song ·
         *  audio · midi · other), with `other` named where Solaris opens it: video (a picture `video add` takes),
         *  preset (.slpreset), sfz (an instrument's definition, R-INST-9). `kinds` keeps only those kinds (and the
         *  folders); `path` empty = the home folder; `parent` null at the root. */
        ntwb::Json browse(const ntwb::Json &params) const;

        static constexpr double kModelMinIntervalMs = 40.0;     // ≤ 25 model pushes a second
        static constexpr double kTransportMinIntervalMs = 50.0; // ≤ 20 transport pushes a second
        static constexpr double kLiveMinIntervalMs = 40.0;      // ≤ 25 live pushes a second
        static constexpr int kVideoQuality = 85;                // a picture's JPEG quality (Interstellar's frame's)
        static constexpr int kVideoMaxEdge = 3840;              // a box larger than 4K is refused
        static constexpr double kAudioChunkS = 0.02;            // the page's sound in blobs of >= 20 ms (or what is there after 40 ms)

    private:
        ntwb::Json runCommand(const std::string &line);
        ntwb::Json video(const ntwb::Call &call, bool thumb);

        solaris::SolarisService &mSvc;
        ntwb::Client &mClient;
        Hooks mHooks;
        long long mSentRevision = -1;
        double mLastModelMs = -1e9;
        std::string mSentTransport; // the last `transport` sent, as text: sent again only when it differs
        double mLastTransportMs = -1e9;
        std::string mSentLive;      // … and the last `live`
        double mLastLiveMs = -1e9;
        long long mVideoSeq = 0;
        // R-SVC-7: the pages that listen (`audio.listen`), and the PCM sent to them on the stream `audio`
        std::set<std::string> mListeners;
        long long mAudioSeq = 0;
        double mLastAudioMs = -1e9;
        void sendAudio(double wallMs);
    };
}
}
