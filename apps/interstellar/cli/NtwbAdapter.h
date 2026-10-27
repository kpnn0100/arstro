/*
 *  interstellar-cc — NtwbAdapter: Interstellar as an app of Arstro Remote (R-SVC-4; Solaris's
 *  apps/solaris/cli/NtwbAdapter.h and cosmo's are the worked examples; the skill arstro.ntwb.implement).
 *
 *  `interstellar-cc ntwb serve` runs THE service — the same InterstellarService the window and the
 *  CLI drive — with no window, and this adapter puts it on the NTWB bridge, so a project can be cut,
 *  graded and delivered from a browser while the media, the rack and the renders stay on this machine.
 *
 *  Seam rule, as interstellar-cc's: THIS CLASS HOLDS NO BEHAVIOUR. It translates:
 *
 *      NTWB call `command {line}`   ->  InterstellarService::dispatchText  (the one grammar, docs/API.md)
 *      AppModel (modelToJson)       ->  NTWB state `model`     (on every revision: ≤ 25/s, ≤ 2/s while playing)
 *      the transport and meters     ->  NTWB state `transport` (when they changed, ≤ 25/s)
 *      the thumbnailer's epoch      ->  NTWB state `thumbs`    {epoch} (when it rose)
 *      Event                        ->  NTWB event `<eventName>` {line: formatEvent(e), fields}
 *      NTWB call `frame {…}`        ->  renderFrame / renderSourceFrame -> a JPEG blob `frame` to THAT page
 *
 *  and, for what the native App gets through AppHooks (app/AppHooks.h) beyond the model — the same
 *  functions the GTK host binds them to (linux_main.cpp:498-512) — each picture to the page that asked:
 *
 *      `thumb {path, t, edge}`      ->  AppHooks::thumbnail / thumbnailEpoch (host/Thumbnailer, async):
 *                                       a JPEG blob `thumb`, or {ready: false} — ask again when `thumbs`
 *                                       rises, exactly as App::render re-asks when thumbnailEpoch moves
 *      `peaks {media, perSecond?}`  ->  InterstellarService::audioPeaks (AppHooks::audioPeaks)
 *      `still {id, edge?}`          ->  InterstellarService::stillPicture (AppHooks::stillPicture): blob `still`
 *      `capture {bind?}`            ->  InterstellarService::captureFrame (AppHooks::copyFrame's pixels):
 *                                       a PNG blob `capture` — the page puts it on ITS clipboard
 *      `browse {path?, kinds?}`     ->  a folder of this machine, for the page's file picker (the GTK
 *                                       file choosers of linux_main.cpp; cosmo's `browse`)
 *
 *  Why the pictures are calls and not pushed streams: what a view shows is the window's VIEW state
 *  (the Cut tab shows the timeline at the playhead, the Grade tab a rack source's reference frame,
 *  App.cpp's monitor fetch; which thumbnails a strip needs) — it is not in the AppModel, so two pages
 *  on two tabs show two pictures. A page asks for what it shows and gets it alone (MVVM, NTWB-11).
 *
 *  `wait` is refused (it would sleep this loop, the bridge's pings included — a page watches the
 *  events instead) and so is `quit` (the session is Arstro Remote's: it is stopped from Apps).
 *
 *  The API it serves is `apiDescription()`, generated from the event table, committed as
 *  docs/ntwb-api.json (ctest interstellar_ntwb_api_current) and installed as the manifest's `api`.
 */
#pragma once
#include "ntwb/Client.h"
#include "ntwb/Json.h"
#include <cstdint>
#include <functional>
#include <string>

namespace arstro
{
namespace interstellar
{
    class InterstellarService;
    struct AppModel;
    struct Raster;
}
namespace interstellar_cli
{
    /** RGBA8 (straight, top-down) -> JPEG bytes; false when it cannot. Injected so the adapter (and
     *  its tests) need no codec: `ntwb serve` passes GdkPixbuf's, which the host already links. */
    using JpegEncoder = std::function<bool(const uint8_t *rgba, int w, int h, int quality, std::string &out)>;

    /** What the GTK host binds in AppHooks beyond the service and the adapter cannot make itself,
     *  injected like the encoder so the adapter and its tests need no codec. Any may be empty: the
     *  call that needs it then says so (a build without FFmpeg has no thumbnailer). */
    struct NtwbPictures
    {
        /** AppHooks::thumbnail: a still of `media` (as the model spells it) at `t` s, ~`edge` px on
         *  its long side; false = not decoded yet (queued) — linux_main.cpp's Thumbnailer::get. */
        std::function<bool(const std::string &media, double t, int edge, interstellar::Raster &out)> thumbnail;
        /** AppHooks::thumbnailEpoch: rises whenever a queued still landed. */
        std::function<unsigned()> thumbnailEpoch;
        /** Fit a picture into `edge` px on its long side (host/Thumbnailer's boxDownscale). Empty =
         *  a still goes at its own size. */
        std::function<void(const interstellar::Raster &in, int edge, interstellar::Raster &out)> fit;
        /** RGBA8 -> PNG bytes: Copy Frame's lossless picture (R-UI-11). Empty = `capture` refused. */
        std::function<bool(const uint8_t *rgba, int w, int h, std::string &out)> png;
    };

    class NtwbAdapter
    {
    public:
        NtwbAdapter(interstellar::InterstellarService &svc, ntwb::Client &client, JpegEncoder jpeg = {}, NtwbPictures pictures = {});

        /** Install the call handlers and the event subscription. Call once, before connect(). */
        void start();
        /** After every pump: publish a changed model, a moving transport, a risen thumbnail epoch.
         *  Never blocks. */
        void tick(double wallMs);

        /** The Interstellar API as NTWB describes an app's API (ntwb spec: APP_API). */
        static ntwb::Json apiDescription();
        /** The model as `state print --json` prints it. */
        static ntwb::Json modelJson(const interstellar::AppModel &m);
        /** What moves with no edit: the playhead, the shuttle, the source viewer's playhead, the meters. */
        static ntwb::Json transportJson(const interstellar::AppModel &m);
        /** The kind `browse` gives a file name: project | video | image | audio | lut | timeline |
         *  captions | preset, or "file" for anything else (the picker's filters, linux_main.cpp). */
        static const char *kindOf(const std::string &fileName);
        /** A call, answered as the bridge would answer it — exposed for the tests. */
        ntwb::Json onCall(const ntwb::Call &call);

        static constexpr double kModelMinIntervalMs = 40.0;        // ≤ 25 model pushes a second
        static constexpr double kModelPlayingIntervalMs = 500.0;   // while playing the revision moves every
                                                                   // frame; the playhead goes as `transport`
        static constexpr double kTransportMinIntervalMs = 40.0;    // ≤ 25 transport pushes a second
        static constexpr int kFrameQuality = 82;
        static constexpr int kDefaultEdge = 960, kMinEdge = 64, kMaxEdge = 3840;
        static constexpr int kThumbQuality = 80, kThumbDefaultEdge = 160, kThumbMinEdge = 16, kThumbMaxEdge = 1024;
        static constexpr int kStillDefaultEdge = 320;

    private:
        ntwb::Json runCommand(const std::string &line);
        ntwb::Json frame(const ntwb::Call &call);
        ntwb::Json thumb(const ntwb::Call &call);
        ntwb::Json peaks(const ntwb::Call &call);
        ntwb::Json still(const ntwb::Call &call);
        ntwb::Json capture(const ntwb::Call &call);
        ntwb::Json browse(const ntwb::Call &call) const;

        interstellar::InterstellarService &mSvc;
        ntwb::Client &mClient;
        JpegEncoder mJpeg;
        NtwbPictures mPics;
        long long mSentRevision = -1;
        double mLastModelMs = -1e9;
        std::string mSentTransport; // the last `transport` sent, as text: sent again only when it differs
        double mLastTransportMs = -1e9;
        long long mSentThumbEpoch = -1;
        long long mFrameSeq = 0;    // the adapter's count of `frame` / `capture` blobs (meta.seq), not the model's frameSeq
    };
}
}
