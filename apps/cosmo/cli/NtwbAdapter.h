/*
 *  Cosmo by arstro — NtwbAdapter: cosmo as an app of Arstro Remote (R-NTWB-1..6).
 *
 *  `cosmo-cc ntwb serve` runs THE service — the same CosmoService the window and the CLI
 *  drive — with no window, and this adapter puts it on the NTWB bridge, so its UI can live
 *  in a browser while the core, the files and the GPU stay on the board.
 *
 *  Seam rule, the same as ControlChannel's and cosmo-cc's: THIS CLASS HOLDS NO BEHAVIOUR.
 *  Every change still enters as a `Command` parsed by the one grammar (R-SVC-2/5), every
 *  observation still leaves as `AppModel` / `Event` (R-SVC-3). The adapter only translates:
 *
 *      NTWB call `command {line}`  ->  parseCommand -> CosmoService::dispatch
 *      AppModel (formatModel JSON) ->  NTWB state `model`         (on every revision)
 *      Event                       ->  NTWB event `<eventName>`   {line, text, a, b, c, ms}
 *      preview frame (RGBA)        ->  NTWB blob `preview`        (JPEG, + histogram meta)
 *      filmstrip thumbnail         ->  NTWB blob `thumb`          (JPEG, on request)
 *
 *  plus the two things a browser cannot do for itself: list a directory of the board
 *  (`browse`, for the open/import/export pickers) and learn the slider catalogue with its
 *  unit conversions pre-sampled (`controls`, EditControls.h). Encoding is injected: JPEG is
 *  a codec and codecs live in the host (R-SVC-7).
 *
 *  MVVM (R-NTWB-7): the model above is SHARED by every client of the session; what one
 *  client's VIEW needs and no other does travels to that client alone (the blob's `client`):
 *
 *      `before`      ->  blob `before`     the geometry-only Before render (Before / Split)
 *      `uncropped`   ->  blob `uncropped`  the photo with every edit but its crop (cropping)
 *      `cover`       ->  blob `cover`      a recent project's first photo (the home cards)
 *
 *  so one page comparing or cropping never changes the photo another page shows.
 *
 *  The Cosmo API it serves is described by `apiDescription()` — generated from the grammar
 *  (commandNames), the event names and the catalogue, committed as apps/cosmo/docs/ntwb-api.json
 *  and installed as the manifest's `api`, so Arstro Remote refuses anything else (NTWB-07).
 */
#pragma once
#include "core/service/CosmoService.h"
#include "ntwb/Client.h"
#include "ntwb/Json.h"
#include <functional>
#include <map>
#include <string>

namespace arstro
{
namespace cosmo_v2
{
    class NtwbAdapter
    {
    public:
        /** RGBA8 -> JPEG bytes. False (with `out` untouched) if the codec failed. */
        using JpegEncoder = std::function<bool(const uint8_t *rgba, int w, int h, int quality, std::string &out)>;

        /** The host's say in a command before the service gets it - cosmo-cc puts an `export`'s
         *  --format / --quality / --long-edge into its writer here, as its own `run` does.
         *  False (with `err`) rejects the line. */
        using PreDispatch = std::function<bool(const cosmo::Command &, std::string &err)>;
        /** A file -> a JPEG whose long edge is at most `edge` (the host's decoder, R-SVC-7). */
        using CoverDecoder = std::function<bool(const std::string &path, int edge, std::string &jpeg, int &w, int &h)>;

        NtwbAdapter(cosmo::CosmoService &svc, ntwb::Client &client, JpegEncoder jpeg,
                    std::map<std::string, std::string> grammarHints, PreDispatch pre = {},
                    CoverDecoder cover = {});

        /** Install the call handlers and the event subscription. Call once, before connect(). */
        void start();
        /** After every pump: publish a changed model, ship a new preview frame. Never blocks. */
        void tick(double wallMs);

        /** The Cosmo API as NTWB describes an app's API (ntwb spec: APP_API). */
        static ntwb::Json apiDescription(const std::map<std::string, std::string> &grammarHints);
        /** The slider catalogue with each conversion sampled at `kStops` track positions. */
        static ntwb::Json controlsJson();
        /** The model as JSON, params included (what state `model` carries). */
        static ntwb::Json modelJson(const cosmo::AppModel &m);

        static constexpr int kStops = 41;
        static constexpr int kPreviewQuality = 85;
        static constexpr int kThumbQuality = 80;
        static constexpr double kModelMinIntervalMs = 40.0;   // <= 25 model pushes a second
        static constexpr int kCoverEdge = 480;                // the home card's cover (App's onDecodeThumbnail)
        static constexpr size_t kCoverCache = 48;             // covers kept encoded, by path + edge

    private:
        ntwb::Json onCall(const ntwb::Call &call);
        ntwb::Json runCommand(const std::string &line);
        ntwb::Json browse(const ntwb::Json &params) const;
        int sendThumbs(const std::string &client, int onlyNode);
        void sendPreview(const std::string &client);
        /** Encode `f` and send it to `client` alone on `stream`; false if the codec failed. */
        bool sendFrameTo(const char *stream, const RenderService::Frame &f, const std::string &client);
        ntwb::Json before(const ntwb::Call &call);
        ntwb::Json uncropped(const ntwb::Call &call);
        ntwb::Json cover(const ntwb::Call &call);

        cosmo::CosmoService &mSvc;
        ntwb::Client &mClient;
        JpegEncoder mJpeg;
        std::map<std::string, std::string> mHints;
        PreDispatch mPre;
        CoverDecoder mCover;
        struct Cover { std::string jpeg; int w = 0, h = 0; };
        std::map<std::string, Cover> mCovers;   // "edge|path" -> encoded cover
        std::string mBeforeKey;                 // what the cached Before JPEG is of
        std::string mBeforeJpeg;
        int mBeforeW = 0, mBeforeH = 0;
        unsigned mSentRevision = ~0u;
        double mLastModelMs = -1e9;
        unsigned mFrameSeq = 0;
        std::string mLastPreview;        // the newest encoded preview, resent to a late client
        ntwb::Json mLastPreviewMeta;
    };
}
}
