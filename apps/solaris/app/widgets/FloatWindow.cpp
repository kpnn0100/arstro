#include "FloatWindow.h"
#include "DevicePanel.h"
#include "PianoRoll.h"
#include "AutomationPanel.h"
#include "../../../interstellar/app/widgets/Glyphs.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;
    namespace glyph = interstellar_v1::glyph;

    FloatWindow::FloatWindow(std::string key, std::shared_ptr<Segment> content) : mKey(std::move(key)), mContent(std::move(content))
    {
        clipToBounds = true;
        opacity.set(0.0);
        addChild(mContent);
    }

    void FloatWindow::open()
    {
        mWanted = true;
        raise();
    }

    void FloatWindow::close() { mWanted = false; }

    void FloatWindow::layout()
    {
        mContent->x.set(1.0);
        mContent->y.set(kTitleH);
        mContent->width.set(std::max(0.0, width.value() - 2.0));
        mContent->height.set(std::max(0.0, height.value() - kTitleH - 1.0));
    }

    void FloatWindow::advance(double nowMs)
    {
        if (mWanted != mApplied)
        {
            mAppear.animateTo(mWanted ? 1.0 : 0.0, mWanted ? motion::kModalOpenMs : motion::kModalCloseMs, Easing::EaseOutCubic, nowMs);
            mApplied = mWanted;
        }
        mAppear.update(nowMs);
        opacity.set(mAppear.value());
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    bool FloatWindow::handleGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Move:
            mHover.setHovered(closeRect().contains(local) ? 0 : -1);
            return true;
        case Gesture::Type::Down:
            raise(); // touched: to the front
            mDragging = false;
            mGrab = local;
            return true;
        case Gesture::Type::DragStart:
            mDragging = titleRect().contains(mGrab); // only the title bar moves the window
            [[fallthrough]];
        case Gesture::Type::Drag:
            if (mDragging && parent())
            {
                // the pointer is the animation; the window stays inside the layer
                const Point inParent = parent()->toLocal(g.pos);
                const double W = parent()->width.value(), H = parent()->height.value();
                x.set(std::clamp(inParent.x - mGrab.x, 0.0, std::max(0.0, W - width.value())));
                y.set(std::clamp(inParent.y - mGrab.y, 0.0, std::max(0.0, H - kTitleH)));
            }
            return true;
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
            mDragging = false;
            return true;
        case Gesture::Type::Click:
            if (closeRect().contains(local)) { close(); if (onClosed) onClosed(); }
            return true;
        default:
            return true;
        }
    }

    void FloatWindow::onPaint(IRenderTarget &t) const
    {
        const double W = width.value(), H = height.value();
        drawRoundedRect(t, Rect{0.5, 0.5, W - 1.0, H - 1.0}, radius::control(), Paint::filledStroked(palette::popover(), palette::border(), 1.0));
        drawRoundedRect(t, Rect{1.0, 1.0, W - 2.0, kTitleH - 1.0}, radius::control(), Paint::filled(palette::secondary()));
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0, kTitleH - 0.5); t.lineTo(W, kTitleH - 0.5); t.strokePath();
        const Rect c = closeRect();
        t.setFill(palette::foreground());
        t.drawText(textfit::ellipsize(t, mTitle, std::max(0.0, c.x - 2.0 * space::padX()), 11.0, font::sansMedium()), space::padX(),
                   textfit::baseline(kTitleH * 0.5, 11.0), 11.0, font::sansMedium());
        const double hv = mHover.amount(0);
        if (hv > 0.001) drawRoundedRect(t, c, radius::control(), Paint::filled(palette::hoverWash(hv)));
        const Color xc = lerpColor(palette::mutedForeground(), palette::foreground(), hv);
        glyph::line(t, c.x + 6.0, c.y + 6.0, c.right() - 6.0, c.bottom() - 6.0, xc, 1.3);
        glyph::line(t, c.right() - 6.0, c.y + 6.0, c.x + 6.0, c.bottom() - 6.0, xc, 1.3);
    }

    // ── the layer ───────────────────────────────────────────────────────────────────────────

    FloatWindow &WindowLayer::place(std::shared_ptr<FloatWindow> w, double width, double height)
    {
        // a new window: cascade from the top right, inside the layer
        const double W = this->width.value(), H = this->height.value();
        const double ww = std::min(width, std::max(160.0, W - 13.0)), hh = std::min(height, std::max(120.0, H - 13.0));
        const double off = 26.0 * (mCascade++ % 6);
        w->width.set(ww);
        w->height.set(hh);
        w->x.set(std::clamp(W - ww - 19.5 - off, 0.0, std::max(0.0, W - ww)));
        w->y.set(std::clamp(13.0 + off, 0.0, std::max(0.0, H - hh)));
        addChild(w);
        mWindows[w->key()] = w;
        return *w;
    }

    FloatWindow &WindowLayer::openDevice(const std::string &dv)
    {
        const std::string key = "dev:" + dv;
        FloatWindow *w = window(key);
        if (!w)
        {
            auto panel = std::make_shared<DevicePanel>(dv);
            panel->onCommand = [this](const std::string &l) { return onCommand ? onCommand(l) : false; };
            panel->onMenu = [this](std::vector<cosmo_v2::ContextMenu::Item> items, Point world) { if (onMenu) onMenu(std::move(items), world); };
            panel->onRename = [this](const std::string &cur, Point world, std::function<void(const std::string &)> done) {
                if (onRename) onRename(cur, world, std::move(done));
            };
            panel->onOpenPattern = [this](const std::string &pt) { openRoll(pt); };
            panel->onCopy = [this](const std::string &text) { if (onCopy) onCopy(text); };
            panel->idsAmount = [this] { return idsAmount ? idsAmount() : 0.0; };
            panel->bind(mModel, mInteracting);
            w = &place(std::make_shared<FloatWindow>(key, panel), 390.0, 520.0);
        }
        w->open();
        bind(mModel, mInteracting); // its title, now
        return *w;
    }

    FloatWindow &WindowLayer::openRoll(const std::string &pt)
    {
        const std::string key = "roll:" + pt;
        FloatWindow *w = window(key);
        if (!w)
        {
            auto roll = std::make_shared<PianoRoll>(pt);
            roll->onCommand = [this](const std::string &l) { return onCommand ? onCommand(l) : false; };
            roll->onMenu = [this](std::vector<cosmo_v2::ContextMenu::Item> items, Point world) { if (onMenu) onMenu(std::move(items), world); };
            roll->bind(mModel);
            w = &place(std::make_shared<FloatWindow>(key, roll), 720.0, 460.0);
        }
        w->open();
        bind(mModel, mInteracting);
        return *w;
    }

    FloatWindow &WindowLayer::openAutomation(const std::string &au)
    {
        const std::string key = "auto:" + au;
        FloatWindow *w = window(key);
        if (!w)
        {
            auto panel = std::make_shared<AutomationPanel>(au);
            panel->onCommand = [this](const std::string &l) { return onCommand ? onCommand(l) : false; };
            panel->onRename = [this](const std::string &cur, Point world, std::function<void(const std::string &)> done) {
                if (onRename) onRename(cur, world, std::move(done));
            };
            panel->bind(mModel);
            w = &place(std::make_shared<FloatWindow>(key, panel), 422.5, 390.0);
        }
        w->open();
        bind(mModel, mInteracting);
        return *w;
    }

    AutomationPanel *WindowLayer::automationPanel(const std::string &au) const
    {
        FloatWindow *w = window("auto:" + au);
        return w ? static_cast<AutomationPanel *>(&w->content()) : nullptr;
    }

    PianoRoll *WindowLayer::roll(const std::string &pt) const
    {
        FloatWindow *w = window("roll:" + pt);
        return w ? static_cast<PianoRoll *>(&w->content()) : nullptr;
    }

    FloatWindow *WindowLayer::window(const std::string &key) const
    {
        const auto it = mWindows.find(key);
        return it == mWindows.end() ? nullptr : it->second.get();
    }

    DevicePanel *WindowLayer::devicePanel(const std::string &dv) const
    {
        FloatWindow *w = window("dev:" + dv);
        return w ? static_cast<DevicePanel *>(&w->content()) : nullptr;
    }

    bool WindowLayer::isOpen(const std::string &key) const
    {
        const FloatWindow *w = window(key);
        return w && w->isOpen();
    }

    void WindowLayer::close(const std::string &key)
    {
        if (FloatWindow *w = window(key)) w->close();
    }

    void WindowLayer::bind(const solaris::AppModel &m, bool interacting)
    {
        mModel = m;
        mInteracting = interacting;
        for (auto &kv : mWindows)
        {
            FloatWindow &w = *kv.second;
            if (kv.first.rfind("dev:", 0) == 0)
            {
                auto &panel = static_cast<DevicePanel &>(w.content());
                panel.bind(m, interacting);
                // titled by the device and its strip; a device removed by anyone takes its window with it
                std::string title = kv.first.substr(4);
                for (const auto &s : m.strips)
                    for (const auto &d : s.devices)
                        if (d.id == panel.device()) title = d.label + " \xE2\x80\x94 " + s.name;
                for (const auto &d : m.masterDevices)
                    if (d.id == panel.device()) title = d.label + " \xE2\x80\x94 Master";
                w.setTitle(title);
                if (!panel.present() && w.isOpen()) w.close();
            }
            else if (kv.first.rfind("roll:", 0) == 0)
            {
                auto &roll = static_cast<PianoRoll &>(w.content());
                roll.bind(m);
                w.setTitle(roll.title());
                if (!roll.present() && w.isOpen()) w.close(); // a pattern deleted by anyone takes its roll with it
            }
            else if (kv.first.rfind("auto:", 0) == 0)
            {
                auto &panel = static_cast<AutomationPanel &>(w.content());
                panel.bind(m);
                w.setTitle(panel.title());                    // a rename from anywhere retitles it
                if (!panel.present() && w.isOpen()) w.close(); // an automation deleted by anyone takes its window with it
            }
        }
    }

    void WindowLayer::layout()
    {
        const double W = width.value(), H = height.value();
        for (auto &kv : mWindows)
        {
            FloatWindow &w = *kv.second;
            // a window resized out of the layer comes back inside it (R4)
            w.x.set(std::clamp(w.x.value(), 0.0, std::max(0.0, W - w.width.value())));
            w.y.set(std::clamp(w.y.value(), 0.0, std::max(0.0, H - FloatWindow::kTitleH)));
            w.layout();
            if (kv.first.rfind("dev:", 0) == 0) static_cast<DevicePanel &>(w.content()).layout();
            else if (kv.first.rfind("roll:", 0) == 0) static_cast<PianoRoll &>(w.content()).layout();
            else if (kv.first.rfind("auto:", 0) == 0) static_cast<AutomationPanel &>(w.content()).layout();
        }
    }

    std::string WindowLayer::samplerAt(Point world) const
    {
        // the topmost open window under the point decides
        const auto &kids = children();
        for (auto it = kids.rbegin(); it != kids.rend(); ++it)
        {
            auto *w = static_cast<FloatWindow *>(it->get());
            if (!w->isOpen() || !w->localBounds().contains(w->toLocal(world))) continue;
            if (w->key().rfind("dev:", 0) != 0) return std::string();
            const auto &panel = static_cast<const DevicePanel &>(w->content());
            return panel.takesSample() ? panel.device() : std::string();
        }
        return std::string();
    }

    void WindowLayer::setSampleHint(const std::string &dv)
    {
        for (auto &kv : mWindows)
            if (kv.first.rfind("dev:", 0) == 0)
            {
                auto &panel = static_cast<DevicePanel &>(kv.second->content());
                panel.setDropHint(!dv.empty() && panel.device() == dv);
            }
    }

    bool WindowLayer::contextClick(Point world)
    {
        // the topmost open window under the point decides
        const auto &kids = children();
        for (auto it = kids.rbegin(); it != kids.rend(); ++it)
        {
            auto *w = static_cast<FloatWindow *>(it->get());
            if (!w->isOpen() || !w->localBounds().contains(w->toLocal(world))) continue;
            if (w->key().rfind("dev:", 0) == 0) return static_cast<DevicePanel &>(w->content()).contextClick(world);
            if (w->key().rfind("roll:", 0) == 0) return static_cast<PianoRoll &>(w->content()).contextClick(world);
            return false;
        }
        return false;
    }
}
}
