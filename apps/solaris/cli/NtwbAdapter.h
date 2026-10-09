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
 *      NTWB call `command {line}`   ->  SolarisService::dispatchText    (the one grammar, docs/API.md)
 *      AppModel (modelToJson)       ->  NTWB state `model`              (on every revision, ≤ 25/s)
 *      transport + meters           ->  NTWB state `transport`          (while they move, ≤ 20/s)
 *      Event                        ->  NTWB event `<eventName>`        {line: formatEvent(e), fields}
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
#include <string>

namespace arstro
{
namespace solaris
{
    class SolarisService;
    struct AppModel;
}
namespace solaris_cli
{
    class NtwbAdapter
    {
    public:
        NtwbAdapter(solaris::SolarisService &svc, ntwb::Client &client);

        /** Install the call handlers and the event subscription. Call once, before connect(). */
        void start();
        /** After every pump: publish a changed model and a moving transport. Never blocks. */
        void tick(double wallMs);

        /** The Solaris API as NTWB describes an app's API (ntwb spec: APP_API). */
        static ntwb::Json apiDescription();
        /** The model as `state print --json` prints it (peaks and position included). */
        static ntwb::Json modelJson(const solaris::AppModel &m);
        /** What moves without an edit: the transport, the master's and every strip's meters. */
        static ntwb::Json transportJson(const solaris::AppModel &m);
        /** A call, answered as the bridge would answer it — exposed for the tests. */
        ntwb::Json onCall(const ntwb::Call &call);

        static constexpr double kModelMinIntervalMs = 40.0;     // ≤ 25 model pushes a second
        static constexpr double kTransportMinIntervalMs = 50.0; // ≤ 20 transport pushes a second

    private:
        ntwb::Json runCommand(const std::string &line);

        solaris::SolarisService &mSvc;
        ntwb::Client &mClient;
        long long mSentRevision = -1;
        double mLastModelMs = -1e9;
        std::string mSentTransport; // the last `transport` sent, as text: sent again only when it differs
        double mLastTransportMs = -1e9;
    };
}
}
