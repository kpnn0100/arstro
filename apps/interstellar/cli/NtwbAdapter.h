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
 *      Event                        ->  NTWB event `<eventName>` {line: formatEvent(e), fields}
 *      NTWB call `frame {…}`        ->  renderFrame / renderSourceFrame -> a JPEG blob to THAT page
 *
 *  Why `frame` is a call and not a pushed stream: what the monitor shows is the window's VIEW state
 *  (the Cut tab shows the timeline at the playhead, the Grade tab a rack source's reference frame,
 *  App.cpp's monitor fetch) — it is not in the AppModel, so two pages on two tabs show two pictures.
 *  A page asks for the picture it shows when the model says it changed (frameSeq, the playhead, the
 *  revision) and gets it alone (MVVM, NTWB-11; cosmo's `before`/`uncropped` are the same pattern).
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
}
namespace interstellar_cli
{
    /** RGBA8 (straight, top-down) -> JPEG bytes; false when it cannot. Injected so the adapter (and
     *  its tests) need no codec: `ntwb serve` passes GdkPixbuf's, which the host already links. */
    using JpegEncoder = std::function<bool(const uint8_t *rgba, int w, int h, int quality, std::string &out)>;

    class NtwbAdapter
    {
    public:
        NtwbAdapter(interstellar::InterstellarService &svc, ntwb::Client &client, JpegEncoder jpeg = {});

        /** Install the call handlers and the event subscription. Call once, before connect(). */
        void start();
        /** After every pump: publish a changed model and a moving transport. Never blocks. */
        void tick(double wallMs);

        /** The Interstellar API as NTWB describes an app's API (ntwb spec: APP_API). */
        static ntwb::Json apiDescription();
        /** The model as `state print --json` prints it. */
        static ntwb::Json modelJson(const interstellar::AppModel &m);
        /** What moves with no edit: the playhead, the shuttle, the source viewer's playhead, the meters. */
        static ntwb::Json transportJson(const interstellar::AppModel &m);
        /** A call, answered as the bridge would answer it — exposed for the tests. */
        ntwb::Json onCall(const ntwb::Call &call);

        static constexpr double kModelMinIntervalMs = 40.0;        // ≤ 25 model pushes a second
        static constexpr double kModelPlayingIntervalMs = 500.0;   // while playing the revision moves every
                                                                   // frame; the playhead goes as `transport`
        static constexpr double kTransportMinIntervalMs = 40.0;    // ≤ 25 transport pushes a second
        static constexpr int kFrameQuality = 82;
        static constexpr int kDefaultEdge = 960, kMinEdge = 64, kMaxEdge = 3840;

    private:
        ntwb::Json runCommand(const std::string &line);
        ntwb::Json frame(const ntwb::Call &call);

        interstellar::InterstellarService &mSvc;
        ntwb::Client &mClient;
        JpegEncoder mJpeg;
        long long mSentRevision = -1;
        double mLastModelMs = -1e9;
        std::string mSentTransport; // the last `transport` sent, as text: sent again only when it differs
        double mLastTransportMs = -1e9;
        long long mFrameSeq = 0;    // the adapter's count of `frame` blobs (meta.seq), not the model's frameSeq
    };
}
}
