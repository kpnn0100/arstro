#include "SongPage.h"
#include "TextFit.h"
#include <algorithm>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        constexpr double kBtnW = 136.0, kBtnH = 26.0;   // the 8/16/32 rhythm of an empty state (design rule §2.4)
    }

    SongPage::SongPage()
    {
        clipToBounds = true;
        mImport = std::make_shared<cosmo_v2::PillButton>("Import Song\xE2\x80\xA6");
        mImport->idleBox = {Paint::filledStroked(Color{0, 0, 0, 0}, palette::border(), 1.0), radius::control()};
        mImport->idleText = {palette::foreground(), 11.0, font::sans()};
        mImport->activeText = {palette::white(), 11.0, font::sans()};
        mImport->hoverEmphasis = palette::primary();
        mImport->onClick = [this] { if (onImport) onImport(); };
        addChild(mImport);
    }

    void SongPage::bind(const interstellar::AppModel &m)
    {
        mHasSongs = !m.songs.empty();
        // a song closed under the page (the project closed): its window goes with it
        if (mView)
        {
            bool still = false;
            for (const auto &g : m.songs) still = still || g.name == mSong;
            if (!still) { mView.reset(); mSong.clear(); }
        }
    }

    void SongPage::show(std::shared_ptr<EmbeddedView> view, const std::string &song)
    {
        if (mView && mView != view) mView->hidden();
        mView = std::move(view);
        mSong = mView ? song : std::string();
    }

    Rect SongPage::deviceFrame() const
    {
        const Point o = worldTransform().apply(Point{0.0, 0.0});
        return Rect{o.x * mScale, o.y * mScale, width.value() * mScale, height.value() * mScale};
    }

    std::string SongPage::emptyText() const
    {
        if (mHasSongs && mNoWindow) return "This build has no Solaris window.";
        if (mHasSongs) return "Double-click a song clip in Cut to open it here.";
        return "No song in this project yet.";
    }

    void SongPage::layout()
    {
        const double W = width.value(), H = height.value();
        mImport->x.set(std::round((W - kBtnW) * 0.5));
        mImport->y.set(std::round(H * 0.42 + 34.0));
        mImport->width.set(kBtnW);
        mImport->height.set(kBtnH);
        // the button is the empty state's: with a window up it neither draws nor takes a click
        const bool empty = !mView && !mHasSongs;
        mImport->visible = empty;
    }

    void SongPage::advance(double nowMs)
    {
        mNowMs = nowMs;
        Segment::advance(nowMs);
    }

    void SongPage::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(palette::background()));
        if (mView)
        {
            // the song's own window, in the page's frame: device pixels, clipped to it (its transforms are its own)
            const Rect f = deviceFrame();
            t.save();
            t.setTransform(Transform::identity());
            t.clipRect(f.x, f.y, f.w, f.h);
            mView->render(t, f.x, f.y, f.w, f.h, 1.0, mNowMs);
            t.restore();
            return;
        }
        const std::string s1 = emptyText();
        const std::string s2 = mHasSongs ? std::string("Its own window opens on this page \xE2\x80\x94 lanes, mixer, devices and piano roll, as in Solaris.")
                                         : std::string("A Solaris song lands on the timeline as a whole project, and opens here to edit \xE2\x80\x94 File \xE2\x96\xB8 Import Song\xE2\x80\xA6");
        const double cy = h * 0.42;
        t.setFill(palette::foreground());
        const std::string a = textfit::ellipsize(t, s1, std::max(0.0, w - 32.0), 13.0, font::sansMedium());
        t.drawText(a, (w - t.measureText(a, 13.0, font::sansMedium())) * 0.5, cy, 13.0, font::sansMedium());
        t.setFill(palette::mutedForeground());
        const std::string b = textfit::ellipsize(t, s2, std::max(0.0, w - 32.0), 11.0, font::sans());
        t.drawText(b, (w - t.measureText(b, 11.0, font::sans())) * 0.5, cy + 19.0, 11.0, font::sans());
    }
}
}
