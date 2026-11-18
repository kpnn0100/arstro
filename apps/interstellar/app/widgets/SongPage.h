/*
 *  interstellar_v1 — SongPage: Edit's fourth tab (R-SONG-7). A Solaris song this project hosts, as a
 *  DAW project inside Interstellar: Solaris's OWN window — its song bar and transport, its lanes, its
 *  mixer, its devices, its piano roll — drawn in the page's frame, below Interstellar's top bar.
 *
 *  The page draws nothing of the song itself: it holds an EmbeddedView the host made for the song
 *  (AppHooks::songView) and gives it the page's frame in device pixels. Its edits go out as
 *  `song run <song> …` (the host binds that), so each is one step of the song's own history and
 *  what an agent could have sent. With no song hosted it says how to bring one in.
 *
 *  It covers the monitor and the transport (it is the whole body below the top bar): a song's
 *  window needs the room, and the song has its own transport.
 */
#pragma once
#include "../Theme.h"
#include "../../../cosmo/widgets/PillButton.h"
#include "../../core/service/AppModel.h"
#include "EmbeddedView.h"
#include <functional>
#include <memory>
#include <string>

namespace arstro
{
namespace interstellar_v1
{
    class SongPage : public artboard::Segment
    {
    public:
        SongPage();
        void bind(const interstellar::AppModel &m);
        void layout();

        /** The view showing `song` (null = none). The page keeps it until another is shown. */
        void show(std::shared_ptr<EmbeddedView> view, const std::string &song);
        const std::string &song() const { return mSong; }
        EmbeddedView *view() const { return mView.get(); }
        /** The App's drawn UI scale: the frame in device pixels is the page's world rect times it. */
        void setDeviceScale(double s) { mScale = s; }
        /** The page's frame, device pixels (what the view is given; a pointer inside it is the view's). */
        artboard::Rect deviceFrame() const;
        /** The empty state's sentence — what the page says with no view (for tests). */
        std::string emptyText() const;
        bool hasSongs() const { return mHasSongs; }

        std::function<void()> onImport;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        std::shared_ptr<EmbeddedView> mView;
        std::string mSong;
        bool mHasSongs = false, mNoWindow = false;
        double mScale = 1.0, mNowMs = 0.0;
        std::shared_ptr<cosmo_v2::PillButton> mImport;

    public:
        /** The host could not make a window for the songs it hosts (no Solaris window in this build). */
        void setNoWindow(bool on) { mNoWindow = on; }
    };
}
}
