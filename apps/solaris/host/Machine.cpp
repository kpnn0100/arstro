#include "Machine.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#ifdef SOLARIS_HAVE_PULSE
#include <pulse/pulseaudio.h>
#endif

namespace fs = std::filesystem;

namespace arstro
{
namespace solaris_host
{
    bool listDir(const std::string &path, std::vector<solaris::BrowserEntry> &out, std::string &err)
    {
        std::error_code ec;
        fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
        if (ec) { err = ec.message(); return false; }
        static const char *kAudio[] = {".wav", ".flac", ".mp3", ".ogg", ".oga", ".opus", ".aif", ".aiff", ".m4a", ".aac", ".wv"};
        for (const auto &e : it)
        {
            const std::string name = e.path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            solaris::BrowserEntry b;
            b.name = name;
            b.path = e.path().string();
            std::string ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if (e.is_directory(ec)) b.kind = "dir";
            else if (ext == ".slp") b.kind = "song";
            else if (std::find(std::begin(kAudio), std::end(kAudio), ext) != std::end(kAudio)) b.kind = "audio";
            else b.kind = "other";
            out.push_back(b);
        }
        return true;
    }

    void machinePaths(std::string &settings, std::string &recents)
    {
        const char *xc = std::getenv("XDG_CONFIG_HOME"), *xd = std::getenv("XDG_DATA_HOME"), *home = std::getenv("HOME");
        if (const char *s = std::getenv("SOLARIS_SETTINGS")) settings = s;
        else if (xc) settings = std::string(xc) + "/solaris/settings.txt";
        else if (home) settings = std::string(home) + "/.config/solaris/settings.txt";
        if (const char *r = std::getenv("SOLARIS_RECENTS")) recents = r;
        else if (xd) recents = std::string(xd) + "/solaris/recents";
        else if (home) recents = std::string(home) + "/.local/share/solaris/recents";
        std::error_code ec;
        if (!settings.empty()) fs::create_directories(fs::path(settings).parent_path(), ec);
        if (!recents.empty()) fs::create_directories(fs::path(recents).parent_path(), ec);
    }

#ifdef SOLARIS_HAVE_PULSE
    namespace
    {
        struct Query
        {
            std::vector<solaris::DeviceInfo> *out;
            int pending = 2;
            bool failed = false;
        };
        void onSink(pa_context *, const pa_sink_info *i, int eol, void *u)
        {
            auto *q = static_cast<Query *>(u);
            if (eol) { --q->pending; return; }
            q->out->push_back(solaris::DeviceInfo{i->name, i->description ? i->description : i->name, "out", i->sample_spec.channels, (int)i->sample_spec.rate});
        }
        void onSource(pa_context *, const pa_source_info *i, int eol, void *u)
        {
            auto *q = static_cast<Query *>(u);
            if (eol) { --q->pending; return; }
            if (i->monitor_of_sink != PA_INVALID_INDEX) return; // an output's monitor is not an input
            q->out->push_back(solaris::DeviceInfo{i->name, i->description ? i->description : i->name, "in", i->sample_spec.channels, (int)i->sample_spec.rate});
        }
    }
#endif

    bool listDevices(std::vector<solaris::DeviceInfo> &out, std::string &err)
    {
#ifndef SOLARIS_HAVE_PULSE
        (void)out;
        err = "this build has no sound-server support (libpulse)";
        return false;
#else
        pa_mainloop *ml = pa_mainloop_new();
        pa_context *ctx = pa_context_new(pa_mainloop_get_api(ml), "Solaris");
        bool ok = false;
        if (pa_context_connect(ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr) >= 0)
        {
            Query q{&out};
            bool asked = false;
            for (int spins = 0; spins < 2000; ++spins)
            {
                pa_mainloop_iterate(ml, 1, nullptr);
                const pa_context_state_t st = pa_context_get_state(ctx);
                if (st == PA_CONTEXT_FAILED || st == PA_CONTEXT_TERMINATED) break;
                if (st == PA_CONTEXT_READY && !asked)
                {
                    pa_operation_unref(pa_context_get_sink_info_list(ctx, onSink, &q));
                    pa_operation_unref(pa_context_get_source_info_list(ctx, onSource, &q));
                    asked = true;
                }
                if (asked && q.pending == 0) { ok = true; break; }
            }
        }
        if (!ok) err = std::string("the sound server did not answer: ") + pa_strerror(pa_context_errno(ctx));
        pa_context_disconnect(ctx);
        pa_context_unref(ctx);
        pa_mainloop_free(ml);
        return ok;
#endif
    }
}
}
