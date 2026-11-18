/*
 *  interstellar_song_ui — Solaris's window inside Interstellar (R-SONG-7). See SongView.h.
 */
#include "SongView.h"
#include "SongHost.h"

#include "App.h"        // Solaris's front end: this target's include path is Solaris's app
#include "AppModel.h"   // … and its model
#include "Cursor.h"

#include <cmath>

namespace arstro
{
namespace interstellar
{
    namespace
    {
        namespace pal = ::arstro::cosmo_v2::palette;

        /** Solaris's colours while its window is at work, Interstellar's put back after (one accent slot, two apps). */
        struct SolarisColours
        {
            artboard::Color accent = pal::accentSlot();
            double contrast = pal::contrast();
            bool reduced = artboard::reducedMotion();
            SolarisColours() { solaris_ui::installSolarisAccent(); }
            ~SolarisColours()
            {
                pal::setAccent(accent);
                pal::setContrast(contrast);
                artboard::setReducedMotion(reduced);
            }
        };

        class SolarisView : public interstellar_v1::EmbeddedView
        {
        public:
            SolarisView(SongHost &host, SongViewHooks hooks) : mHost(host), mHooks(std::move(hooks))
            {
                SolarisColours c;
                solaris_ui::AppHooks h;
                h.model = [this]() -> const solaris::AppModel & { return mHost.model(); };
                h.dispatch = [this](const std::string &line, std::string &err) {
                    // Solaris's Home closes its song: here, the song stays hosted and the page gives way to Interstellar
                    if (line == "project close")
                    {
                        if (mHooks.onBack) mHooks.onBack();
                        return true;
                    }
                    if (!mHooks.dispatch) { err = "no service"; return false; }
                    return mHooks.dispatch(line, err);
                };
                mApp.reset(new solaris_ui::App(h, 960.0, 600.0));
                mApp->onQuit = [this] { if (mHooks.onBack) mHooks.onBack(); };
                mApp->onCopy = mHooks.onCopy;
                mApp->onPickFile = mHooks.onPickFile;
                mApp->onPickDirectory = mHooks.onPickDirectory;
            }

            void render(artboard::IRenderTarget &target, double x, double y, double w, double h, double alpha, double nowMs) override
            {
                SolarisColours c;
                if (std::fabs(w - mW) > 0.5 || std::fabs(h - mH) > 0.5)
                {
                    mW = w;
                    mH = h;
                    mApp->setSize(w, h);
                }
                mApp->setOrigin(x, y);
                if (alpha < 0.999) target.pushLayer(alpha);
                mApp->render(target, nowMs);
                if (alpha < 0.999) target.popLayer();
            }
            bool needsRedraw(double nowMs) const override { return mApp->needsRedraw(nowMs) || mHost.model().transport.playing; }
            void pointer(int kind, double x, double y, int button, double timeMs, bool alt, bool shift, bool ctrl) override
            {
                SolarisColours c;
                mApp->setOrigin(0.0, 0.0);   // the frame's own pixels: the App reads them from its origin
                mApp->pointer(kind, x, y, button, timeMs, alt, shift, ctrl);
            }
            void wheel(double x, double y, double notches, bool ctrl, bool shift, bool alt) override
            {
                SolarisColours c;
                mApp->setOrigin(0.0, 0.0);
                mApp->wheel(x, y, notches, ctrl, shift, alt);
            }
            bool key(const artboard::KeyEvent &e) override
            {
                SolarisColours c;
                return mApp->key(e);
            }
            std::string cursorName() const override { return solaris_ui::cursorName(mApp->cursor()); }
            std::string title() const override { return mHost.model().projectName; }
            void hidden() override
            {
                // the song's transport does not go on playing behind Interstellar's pages
                if (mHost.model().transport.playing && mHooks.dispatch)
                {
                    std::string err;
                    mHooks.dispatch("transport stop", err);
                }
            }

        private:
            SongHost &mHost;
            SongViewHooks mHooks;
            std::unique_ptr<solaris_ui::App> mApp;
            double mW = 0.0, mH = 0.0;
        };
    }

    std::shared_ptr<interstellar_v1::EmbeddedView> makeSongView(SongHost &host, SongViewHooks hooks)
    {
        return std::make_shared<SolarisView>(host, std::move(hooks));
    }
}
}
