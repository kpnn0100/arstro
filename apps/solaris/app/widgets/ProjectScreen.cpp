#include "ProjectScreen.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <cstdio>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;

    ProjectScreen::ProjectScreen()
    {
        clipToBounds = true;
        mBar = std::make_shared<SongBar>();
        addChild(mBar);
    }

    void ProjectScreen::bind(const solaris::AppModel &m)
    {
        mModel = m;
        mBar->bind(m);
    }

    void ProjectScreen::layout()
    {
        mBar->x.set(0);
        mBar->y.set(0);
        mBar->width.set(width.value());
    }

    void ProjectScreen::onPaint(IRenderTarget &t) const
    {
        const double W = width.value(), H = height.value();
        drawRoundedRect(t, Rect{0, 0, W, H}, 0.0, Paint::filled(surface::stageBg()));
        const double x = 32.0, maxW = W - 64.0;
        double y = SongBar::kHeight + 40.0;
        auto line = [&](const std::string &s, double px, const char *fam, const Color &c) {
            if (y > H - 16.0) return;
            t.setFill(c);
            t.drawText(textfit::ellipsize(t, s, maxW, px, fam), x, y, px, fam);
            y += px + 9.0;
        };
        for (const auto &mx : mModel.mixers)
        {
            line(mx.name, 9.0, font::sansSemiBold(), palette::mutedForeground());
            for (const auto &id : mx.strips)
                for (const auto &s : mModel.strips)
                    if (s.id == id)
                    {
                        char buf[160];
                        std::snprintf(buf, sizeof buf, "%s  \xC2\xB7  %s  \xC2\xB7  %d clips  to %s", s.name.c_str(), s.kind.c_str(), s.clipCount, s.out.c_str());
                        line(buf, 11.0, font::sans(), s.audible ? palette::foreground() : palette::mutedForeground());
                    }
            y += 8.0;
        }
        char buf[96];
        std::snprintf(buf, sizeof buf, "%zu lanes  \xC2\xB7  %zu clips  \xC2\xB7  %zu patterns", mModel.lanes.size(), mModel.clips.size(), mModel.patterns.size());
        line(buf, 11.0, font::sans(), palette::mutedForeground());
        y += 8.0;
        line("The lanes, the browser and the mixer dock arrive next; until then every edit is a command (solaris-cc).", 10.0, font::sans(),
             palette::mutedForeground());
    }
}
}
