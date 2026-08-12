#include "HomeScreen.h"
#include "BaseCatalog.h"
#include <cmath>
#include <ctime>

namespace genesis
{
namespace ui
{
    namespace
    {
        constexpr double kSidebar = 296.0;
        constexpr double kCardW = 208.0;
        constexpr double kCardH = 132.0;
        constexpr double kCardGap = 14.0;
        constexpr double kHeaderH = 78.0;

        /** A tiny glyph per base, so the new-component cards are scannable without reading. */
        void drawBaseGlyph(artboard::IRenderTarget &t, const std::string &base, const artboard::Rect &r,
                           const artboard::Color &c)
        {
            const double cx = r.x + r.w * 0.5, cy = r.y + r.h * 0.5;
            if (base == "VisualLoop")
            {
                t.setStroke(c, 2.0);
                t.beginPath();
                const double rad = 13.0;
                t.moveTo(cx + rad, cy);
                t.cubicTo(cx + rad, cy - rad * 1.33, cx - rad, cy - rad * 1.33, cx - rad, cy);
                t.cubicTo(cx - rad, cy + rad * 1.33, cx + rad, cy + rad * 1.33, cx + rad * 0.2, cy + rad);
                t.strokePath();
            }
            else if (base == "ProgressIndicator")
            {
                artboard::drawRoundedRect(t, {cx - 22, cy - 3, 44, 6}, 3.0,
                                          artboard::Paint::filled(artboard::Color{c.r, c.g, c.b, 0.28}));
                artboard::drawRoundedRect(t, {cx - 22, cy - 3, 28, 6}, 3.0, artboard::Paint::filled(c));
            }
            else if (base == "Button")
                artboard::drawRoundedRect(t, {cx - 24, cy - 10, 48, 20}, radius::control(),
                                          artboard::Paint::filledStroked(
                                              artboard::Color{c.r, c.g, c.b, 0.22}, c, 1.5));
            else if (base == "Slider")
            {
                artboard::drawRoundedRect(t, {cx - 24, cy - 2, 48, 4}, 2.0,
                                          artboard::Paint::filled(artboard::Color{c.r, c.g, c.b, 0.3}));
                artboard::drawCircle(t, cx + 6, cy, 7.0, artboard::Paint::filled(c));
            }
            else
            {
                artboard::drawRoundedRect(t, {cx - 11, cy - 11, 22, 22}, radius::control(),
                                          artboard::Paint::stroked(c, 1.6));
                t.setStroke(c, 2.0);
                t.beginPath();
                t.moveTo(cx - 5, cy);
                t.lineTo(cx - 1, cy + 5);
                t.lineTo(cx + 6, cy - 5);
                t.strokePath();
            }
        }
    }

    HomeScreen::HomeScreen()
    {
        mOpen = std::make_shared<artboard::Button>("Open a component…", theme().button);
        mOpen->focusable = true;
        mOpen->height.set(32.0);
        mOpen->onClick = [this] { if (onOpen) onOpen(); };
        addChild(mOpen);
        mNowSeconds = (long long)std::time(nullptr);
    }

    void HomeScreen::setRecents(std::vector<RecentEntry> recents)
    {
        mRecents = std::move(recents);
        mNowSeconds = (long long)std::time(nullptr);
        rebuildCards();
    }

    double HomeScreen::sidebarW() const { return std::min(kSidebar, width.value() * 0.34); }

    void HomeScreen::rebuildCards()
    {
        mCards.clear();
        const double left = sidebarW() + metrics::pad() * 2.0;
        const double avail = std::max(kCardW, width.value() - left - metrics::pad() * 2.0);
        const int cols = std::max(1, (int)std::floor((avail + kCardGap) / (kCardW + kCardGap)));
        int index = 0;
        auto place = [&](Card c) {
            const int col = index % cols, row = index / cols;
            c.rect = {left + col * (kCardW + kCardGap), kHeaderH + row * (kCardH + kCardGap) - mScroll,
                      kCardW, kCardH};
            mCards.push_back(c);
            ++index;
        };
        for (const auto &r : mRecents)
        {
            Card c;
            c.isRecent = true;
            c.title = r.name.empty() ? r.path : r.name;
            c.subtitle = (r.base.empty() ? std::string("component") : r.base) + " · " +
                         Recents::relativeAge(r.modified, mNowSeconds) + (r.exists ? "" : " · missing");
            c.path = r.path;
            c.base = r.base;
            place(c);
        }
        for (const auto &b : bases())
        {
            Card c;
            c.title = "New " + b.name;
            c.subtitle = b.summary;
            c.base = b.name;
            place(c);
        }
    }

    void HomeScreen::layout(double w, double h)
    {
        width.set(w);
        height.set(h);
        const double pad = metrics::pad() * 2.0;
        mOpen->x.set(pad);
        mOpen->y.set(196.0);
        mOpen->width.set(sidebarW() - pad * 2.0);
        rebuildCards();
    }

    void HomeScreen::advance(double nowMs)
    {
        mNowMs = nowMs;
        mHover.advance(nowMs);
        mAppear.update(nowMs);
        if (!mAppear.isAnimating() && mAppear.value() < 1.0)
            mAppear.animateTo(1.0, artboard::motion::kDurationMedium2,
                              artboard::Easing::EmphasizedDecel, nowMs);
        Segment::advance(nowMs);
    }

    int HomeScreen::cardAt(const artboard::Point &p) const
    {
        for (int i = 0; i < (int)mCards.size(); ++i)
        {
            const artboard::Rect &r = mCards[(size_t)i].rect;
            if (p.x >= r.x && p.x <= r.right() && p.y >= r.y && p.y <= r.bottom())
                return i;
        }
        return -1;
    }

    bool HomeScreen::handleGesture(const artboard::Gesture &g, const artboard::Point &p)
    {
        if (g.type == artboard::Gesture::Type::Move)
        {
            mHover.setHovered(cardAt(p));
            return true;
        }
        if (g.type == artboard::Gesture::Type::Drag)
        {
            const int rows = ((int)mCards.size() + 2) / 3;
            const double contentH = kHeaderH + rows * (kCardH + kCardGap);
            const double maxScroll = std::max(0.0, contentH - height.value() + 40.0);
            mScroll = std::min(maxScroll, std::max(0.0, mScroll - (p.y - g.start.y) * 0.4));
            rebuildCards();
            return true;
        }
        if (g.type == artboard::Gesture::Type::Click)
        {
            const int i = cardAt(p);
            if (i < 0) return true;
            const Card &c = mCards[(size_t)i];
            if (c.isRecent)
            {
                if (onOpenRecent) onOpenRecent(c.path);
            }
            else if (onNewComponent)
                onNewComponent(c.base);
            return true;
        }
        return artboard::Segment::handleGesture(g, p);
    }

    void HomeScreen::onPaint(artboard::IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        const double appear = mAppear.value();
        artboard::drawRoundedRect(t, {0, 0, w, h}, 0.0, artboard::Paint::filled(palette::background()));

        // ── sidebar ──
        const double sb = sidebarW();
        const double pad = metrics::pad() * 2.0;
        artboard::drawRoundedRect(t, {0, 0, sb, h}, 0.0, artboard::Paint::filled(palette::railBg()));
        artboard::drawRoundedRect(t, {sb - 1, 0, 1, h}, 0.0, artboard::Paint::filled(palette::border()));

        const double sz = 34.0, sp = -0.03 * sz;
        const double stemW = t.measureText("genesis", sz, font::sansSemiBold(), sp);
        const double wy = 84.0 + (1.0 - appear) * 6.0;
        artboard::Color fg = palette::foreground();
        fg.a *= appear;
        t.setFill(fg);
        t.drawText("genesis", pad, wy, sz, font::sansSemiBold(), sp);
        artboard::Color dotc = palette::primary();
        dotc.a *= appear;
        t.setFill(dotc);
        t.drawText(".", pad + stemW, wy, sz, font::sansSemiBold(), sp);
        drawFitted(t, "Animation designer for Artboard", pad, wy + 22.0, sb - pad * 2.0, type::small(),
                   palette::mutedForeground(), font::sans());
        drawFitted(t, "Components you author here export as C++ that", pad, wy + 54.0,
                   sb - pad * 2.0, type::micro(), palette::mutedForeground(), font::sans());
        drawFitted(t, "depends on artboard alone — no Genesis runtime.", pad, wy + 68.0,
                   sb - pad * 2.0, type::micro(), palette::mutedForeground(), font::sans());
        drawFitted(t, "Genesis 0.1 · arstro", pad, h - 22.0, sb - pad * 2.0, type::micro(),
                   palette::mutedForeground(), font::sans());

        // ── header ──
        const double left = sb + metrics::pad() * 2.0;
        drawSectionTitle(t, mRecents.empty() ? "Start" : "Recent", left, 40.0);
        const std::string count = mRecents.empty()
                                      ? std::string("Pick a base to begin")
                                      : std::to_string(mRecents.size()) +
                                            (mRecents.size() == 1 ? " component" : " components");
        drawFitted(t, count, left, 58.0, w - left - metrics::pad(), type::small(),
                   palette::mutedForeground(), font::sans());

        // ── cards ──
        t.save();
        t.clipRect(sb, kHeaderH - 18.0, w - sb, h - kHeaderH + 18.0);
        for (int i = 0; i < (int)mCards.size(); ++i)
        {
            const Card &c = mCards[(size_t)i];
            if (c.rect.bottom() < 0 || c.rect.y > h) continue;
            const double hover = mHover.amount(i);
            // Cards stagger in: each is 24ms behind the last, so the grid assembles rather
            // than appearing all at once.
            const double stagger = std::min(1.0, std::max(0.0, appear * 1.6 - (double)i * 0.06));
            if (stagger <= 0.004) continue;
            artboard::Rect r = c.rect;
            r.y += (1.0 - stagger) * 10.0;

            artboard::Color fill = c.isRecent ? palette::card() : palette::muted();
            fill.a *= stagger;
            artboard::Color edge = hover > 0.01 ? palette::primaryAlpha(0.55 * hover) : palette::border();
            edge.a *= stagger;
            artboard::drawRoundedRect(t, r, radius::control(),
                                      artboard::Paint::filledStroked(fill, edge, 1.0));
            if (hover > 0.01)
                artboard::drawRoundedRect(t, r, radius::control(),
                                          artboard::Paint::filled(palette::hoverWash(hover * stagger)));

            artboard::Color glyph = c.isRecent ? palette::secondaryForeground() : palette::primary();
            glyph.a *= stagger;
            drawBaseGlyph(t, c.base, {r.x, r.y + 8.0, r.w, 62.0}, glyph);

            artboard::Color title = palette::foreground();
            title.a *= stagger;
            drawFitted(t, c.title, r.x + 12.0, r.y + 94.0, r.w - 24.0, type::body(), title,
                       font::sansMedium());
            artboard::Color sub = palette::mutedForeground();
            sub.a *= stagger;
            drawFitted(t, c.subtitle, r.x + 12.0, r.y + 112.0, r.w - 24.0, type::micro(), sub,
                       font::sans());
        }
        t.restore();
    }
}
}
