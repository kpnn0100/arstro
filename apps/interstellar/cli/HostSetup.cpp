/*
 *  interstellar-cc — HostSetup: see HostSetup.h. Moved here unchanged from main.cpp so the
 *  `ntwb serve` face (NtwbMain.cpp) builds the same service.
 */
#include "HostSetup.h"
#ifdef INTERSTELLAR_HAVE_FFMPEG
#include "AudioSourceFFmpeg.h"
#include "FrameWriterFFmpeg.h"
#include "PackageWriter.h"
#include "HostFrameSource.h"
#include "PngWriter.h"
#include "VideoFrameDecoder.h"
#endif
#include "core/decode/NativeImageDecoder.h"
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#ifdef INTERSTELLAR_HAVE_TEXT
#include "../app/BurnText.h"
#endif

namespace arstro
{
namespace interstellar_cli
{
    using namespace arstro::interstellar;

    namespace
    {
        std::string recentsPath()
        {
            if (const char *x = std::getenv("INTERSTELLAR_RECENTS")) return x;
            if (const char *x = std::getenv("XDG_DATA_HOME")) return std::string(x) + "/interstellar/recents";
            if (const char *h = std::getenv("HOME")) return std::string(h) + "/.local/share/interstellar/recents";
            return std::string();
        }
    }

    InterstellarService::Host makeHost()
    {
        InterstellarService::Host h;
#ifdef INTERSTELLAR_HAVE_FFMPEG
        h.rackDecoder = [](std::shared_ptr<const FrameSelector> sel) {
            return std::unique_ptr<cosmo::IImageDecoder>(new interstellar_host::VideoFrameDecoder(std::move(sel)));
        };
        h.frameSource = [] { return std::unique_ptr<IFrameSource>(new interstellar_host::HostFrameSource()); };
        h.hasVendorDecoder = [](const std::string &ext) { return interstellar_host::vendorDecoderInstalled(ext); };   // R-MEDIA-1
#ifdef INTERSTELLAR_HAVE_TEXT
        h.drawText = [](interstellar::Raster &f, const std::vector<interstellar::OverlayText> &items) { return interstellar_v1::drawOverlayText(f, items); };   // R-DLV-1/2
#endif
        h.audioSource = [] { return std::unique_ptr<IAudioSource>(new interstellar_host::AudioSourceFFmpeg()); };
        h.frameWriter = [] { return std::unique_ptr<IFrameWriter>(new interstellar_host::FrameWriterFFmpeg()); };
        h.packageWriter = [] { return std::unique_ptr<IFrameWriter>(new interstellar_host::PackageWriter()); };   // R-DLV-4
        h.writeImage = [](const std::string &p, const Raster &r, std::string &err) {
            return interstellar_host::writePng(p, r, err);
        };
#else
        h.rackDecoder = [](std::shared_ptr<const FrameSelector>) {
            return std::unique_ptr<cosmo::IImageDecoder>(new cosmo::NativeImageDecoder());
        };
#endif
        h.recentsPath = recentsPath();
        // The same settings file and preset library the window uses, so `settings set` from a
        // shell and the Engine Settings chips are one setting.
        const char *xc = std::getenv("XDG_CONFIG_HOME"), *xd = std::getenv("XDG_DATA_HOME"), *home = std::getenv("HOME");
        if (xc) h.settingsPath = std::string(xc) + "/interstellar/settings.txt";
        else if (home) h.settingsPath = std::string(home) + "/.config/interstellar/settings.txt";
        if (const char *x = std::getenv("INTERSTELLAR_PRESETS")) h.presetDir = x;
        else if (xd) h.presetDir = std::string(xd) + "/interstellar/presets";
        else if (home) h.presetDir = std::string(home) + "/.local/share/interstellar/presets";
        return h;
    }
}
}
