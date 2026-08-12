#include "CanvasView.h"
#include "../App.h"
#include "Modal.h"
#include <cmath>

namespace genesis
{
namespace ui
{
    namespace
    {
        constexpr double kTransportH = 30.0;
        constexpr double kMinFrame = 6.0;   // small components (a 10px bar, a 22px box) are real sizes
        constexpr double kMaxFrame = 1600.0;
    }

    CanvasView::CanvasView(App &app) : mApp(app)
    {
        mDrive = std::make_shared<artboard::Slider>(theme().slider);
        mDrive->setRange(0.0, 1.0);
        mDrive->setValue(0.0);
        mDrive->focusable = true;
        App *a = &mApp;
        mDrive->onChange = [a](double v) {
            if (a->doc().base == "ProgressIndicator") a->runtime().setProgress(v);
            else if (a->doc().base == "Slider") a->runtime().setSliderValue(v);
        };
        addChild(mDrive);

        mReduced = std::make_shared<artboard::Checkbox>("Reduced motion", theme().checkbox);
        mReduced->focusable = true;
        addChild(mReduced);

        mHoverSim = std::make_shared<artboard::Checkbox>("Hover", theme().checkbox);
        mHoverSim->focusable = true;
        addChild(mHoverSim);

        refresh();
    }

    void CanvasView::refresh()
    {
        for (const auto &b : mTransport)
            b->visible = false;
        mTransport.clear();
        clearChildren();
        addChild(mDrive);
        addChild(mReduced);
        addChild(mHoverSim);

        App *a = &mApp;
        auto add = [&](const std::string &label, std::function<void(App &)> run) {
            auto b = std::make_shared<artboard::Button>(label, theme().button);
            b->height.set(24.0);
            b->focusable = true;
            b->onClick = [a, run] { run(*a); };
            addChild(b);
            mTransport.push_back(b);
        };

        const std::string &base = mApp.doc().base;
        if (base == "VisualLoop")
        {
            add("Start", [](App &app) { app.runtime().loopStart(); });
            add("Stop", [](App &app) { app.runtime().loopStop(); });
        }
        else if (base == "ProgressIndicator")
        {
            add("Indeterminate", [](App &app) {
                app.runtime().setIndeterminate(!app.runtime().document().shapes.empty() &&
                                               !app.previewOk() ? false : true);
            });
            add("Determinate", [](App &app) { app.runtime().setIndeterminate(false); });
        }
        else if (base == "Button")
        {
            add("Press", [](App &app) { app.runtime().setPressed(true); });
            add("Release", [](App &app) { app.runtime().setPressed(false); });
        }
        else if (base == "Checkbox")
        {
            add("Toggle", [](App &app) { app.runtime().toggleChecked(); });
        }
        add("Replay", [](App &app) { app.restartPreview(); });

        // The drive slider only means something for the two value-carrying bases.
        mDrive->visible = base == "ProgressIndicator" || base == "Slider";

        setFrameSize(mApp.doc().designW, mApp.doc().designH);
    }

    void CanvasView::setHoverSim(bool on) { mHoverSim->setChecked(on); }
    bool CanvasView::hoverSim() const { return mHoverSim->checked(); }

    void CanvasView::setFrameSize(double w, double h)
    {
        const double cw = std::min(kMaxFrame, std::max(kMinFrame, w));
        const double ch = std::min(kMaxFrame, std::max(kMinFrame, h));
        // Eased, so dragging the handle and typing a design size both read as motion (R1).
        mFrameW.animateTo(cw, artboard::motion::kDurationShort2, artboard::Easing::EaseOutCubic, mNowMs);
        mFrameH.animateTo(ch, artboard::motion::kDurationShort2, artboard::Easing::EaseOutCubic, mNowMs);
    }

    void CanvasView::layout(double w, double h)
    {
        width.set(w);
        height.set(h);
        const double pad = metrics::pad();
        double x = pad;
        const double ty = h - kTransportH + 3.0;
        for (const auto &b : mTransport)
        {
            const double bw = std::max(56.0, textWidth(b->text, type::body(), font::sansMedium()) + 20.0);
            b->x.set(x);
            b->y.set(ty);
            b->width.set(bw);
            x += bw + 6.0;
        }
        if (mDrive->visible)
        {
            const double sw = std::min(180.0, std::max(80.0, w - x - 260.0));
            mDrive->x.set(x + 16.0);
            mDrive->y.set(ty + 5.0);
            mDrive->width.set(sw);
            mDrive->height.set(14.0);
            x += sw + 26.0;
        }
        const double hoverW = 78.0, reducedW = 132.0;
        mHoverSim->x.set(std::max(x + 8.0, w - pad - reducedW - hoverW - 10.0));
        mHoverSim->y.set(ty + 2.0);
        mHoverSim->width.set(hoverW);
        mHoverSim->height.set(18.0);
        mReduced->x.set(w - pad - reducedW);
        mReduced->y.set(ty + 2.0);
        mReduced->width.set(reducedW);
        mReduced->height.set(18.0);
    }

    artboard::Rect CanvasView::stageRect() const
    {
        const double pad = metrics::pad();
        return {pad, pad + 18.0, width.value() - pad * 2.0,
                height.value() - pad * 2.0 - 18.0 - kTransportH};
    }

    artboard::Rect CanvasView::frameRect() const
    {
        const artboard::Rect s = stageRect();
        const double fw = std::min(mFrameW.value(), s.w - 24.0);
        const double fh = std::min(mFrameH.value(), s.h - 24.0);
        return {s.x + (s.w - fw) * 0.5, s.y + (s.h - fh) * 0.5, fw, fh};
    }

    artboard::Point CanvasView::handlePos() const
    {
        const artboard::Rect f = frameRect();
        return {f.right(), f.bottom()};
    }

    void CanvasView::advance(double nowMs)
    {
        mNowMs = nowMs;
        mFrameW.update(nowMs);
        mFrameH.update(nowMs);
        mHandleGlow.update(nowMs);
        mSelectFade.update(nowMs);
        artboard::setReducedMotion(mReduced->checked());
        mApp.runtime().setHovered(mHoverSim->checked());
        Segment::advance(nowMs);
    }

    bool CanvasView::handleGesture(const artboard::Gesture &g, const artboard::Point &p)
    {
        const artboard::Point handle = handlePos();
        const double d = std::hypot(p.x - handle.x, p.y - handle.y);
        const bool nearHandle = d <= metrics::handleHit();

        if (g.type == artboard::Gesture::Type::Move)
        {
            const double want = nearHandle ? 1.0 : 0.0;
            if (std::fabs(mHandleGlow.value() - want) > 0.01 && !mHandleGlow.isAnimating())
                mHandleGlow.animateTo(want, artboard::interaction::kHoverMs,
                                      artboard::Easing::EaseOutCubic, mNowMs);
            return true;
        }
        if (g.type == artboard::Gesture::Type::DragStart && nearHandle)
        {
            mResizing = true;
            mResizeW0 = mFrameW.value();
            mResizeH0 = mFrameH.value();
            return true;
        }
        if (g.type == artboard::Gesture::Type::Drag && mResizing)
        {
            // Direct manipulation: the pointer IS the animation, so this one path snaps.
            const double dx = (p.x - g.start.x) * 2.0;   // the frame is centred: both edges move
            const double dy = (p.y - g.start.y) * 2.0;
            mFrameW.set(std::min(kMaxFrame, std::max(kMinFrame, mResizeW0 + dx)));
            mFrameH.set(std::min(kMaxFrame, std::max(kMinFrame, mResizeH0 + dy)));
            return true;
        }
        if (g.type == artboard::Gesture::Type::Drop && mResizing)
        {
            mResizing = false;
            return true;
        }
        if (g.type == artboard::Gesture::Type::Click)
        {
            // Click inside the frame selects the topmost authored shape under the pointer.
            const artboard::Rect f = frameRect();
            const artboard::Point local{p.x - f.x, p.y - f.y};
            std::string hit;
            for (const auto &s : mApp.doc().shapes)
            {
                artboard::Segment *seg = mApp.runtime().segmentFor(s.id);
                if (!seg || seg->isFadedOut()) continue;
                const artboard::Point inSeg = seg->toLocal(local);
                const artboard::Rect b = seg->localBounds();
                if (inSeg.x >= b.x && inSeg.x <= b.right() && inSeg.y >= b.y && inSeg.y <= b.bottom())
                    hit = s.id;   // later shapes draw on top, so the last hit wins
            }
            mApp.selectShape(hit);
            return true;
        }
        return artboard::Segment::handleGesture(g, p);
    }

    void CanvasView::render(artboard::IRenderTarget &t, const artboard::Transform &parent) const
    {
        artboard::Segment::render(t, parent);
        // The previewed component is rendered here, inside the frame, AFTER the stage but
        // before the panel's own overlay pass — it is content, not chrome.
        if (!mApp.previewOk()) return;
        artboard::Segment *root = const_cast<App &>(mApp).runtime().root();
        if (!root) return;
        const artboard::Rect f = frameRect();
        const artboard::Transform world =
            parent.mul(localTransform()).mul(artboard::Transform::translation(f.x, f.y));
        t.save();
        t.setTransform(world);
        t.clipRect(0, 0, f.w, f.h);
        root->render(t, world);
        t.restore();
        root->renderOverlay(t, world);
    }

    void CanvasView::onPaint(artboard::IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        artboard::drawRoundedRect(t, {0, 0, w, h}, 0.0, artboard::Paint::filled(palette::background()));
        drawSectionTitle(t, "Preview", metrics::pad(), metrics::pad() + 10.0);

        const artboard::Rect s = stageRect();
        artboard::drawRoundedRect(t, s, radius::panel(),
                                  artboard::Paint::filledStroked(palette::stageBg(), palette::border(), 1.0));

        // A sparse grid gives the eye a sense of scale without competing with the component.
        t.save();
        t.clipRect(s.x, s.y, s.w, s.h);
        const double step = 32.0;
        for (double gx = s.x + step; gx < s.right(); gx += step)
            artboard::drawRoundedRect(t, {gx, s.y, 1, s.h}, 0.0, artboard::Paint::filled(palette::gridLine()));
        for (double gy = s.y + step; gy < s.bottom(); gy += step)
            artboard::drawRoundedRect(t, {s.x, gy, s.w, 1}, 0.0, artboard::Paint::filled(palette::gridLine()));
        t.restore();

        const artboard::Rect f = frameRect();
        artboard::drawRoundedRect(t, f, 0.0, artboard::Paint::stroked(palette::frameEdge(), 1.0));

        if (!mApp.previewOk())
        {
            // Error state: say what is wrong, in the place the component would have been.
            const std::string msg = mApp.previewError().empty() ? "The component has errors"
                                                                : mApp.previewError();
            drawFitted(t, "Preview unavailable", s.x + 16.0, s.y + s.h * 0.5 - 6.0, s.w - 32.0,
                       type::body(), palette::destructive(), font::sansMedium());
            drawFitted(t, msg, s.x + 16.0, s.y + s.h * 0.5 + 12.0, s.w - 32.0, type::small(),
                       palette::mutedForeground(), font::sans());
        }

        // Size readout, pinned under the frame so it reads as the frame's caption.
        char size[64];
        std::snprintf(size, sizeof size, "%d × %d", (int)std::lround(mFrameW.value()),
                      (int)std::lround(mFrameH.value()));
        drawFitted(t, size, f.x, f.bottom() + 16.0, std::max(60.0, f.w), type::micro(),
                   palette::mutedForeground(), font::mono());
    }

    void CanvasView::onOverlay(artboard::IRenderTarget &t) const
    {
        // The resize handle and the selection outline sit ON TOP of the previewed component,
        // so they are drawn in the overlay pass rather than fighting it for z-order — but a
        // modal owns the screen, and overlay content must not float above it.
        if (const_cast<App &>(mApp).modal()->coversApp())
            return;
        const artboard::Rect f = frameRect();
        const artboard::Point handle = handlePos();
        const double glow = mHandleGlow.value();
        artboard::drawRoundedRect(t, {handle.x - 5.0, handle.y - 5.0, 10.0, 10.0}, 2.0,
                                  artboard::Paint::filledStroked(
                                      lerpColor(palette::frameEdge(), palette::primary(), glow),
                                      palette::background(), 1.0));

        if (mApp.selectedShape().empty() || !mApp.previewOk()) return;
        artboard::Segment *seg = const_cast<App &>(mApp).runtime().segmentFor(mApp.selectedShape());
        if (!seg) return;
        const artboard::Rect b = seg->localBounds();
        const artboard::Transform toFrame =
            artboard::Transform::translation(f.x, f.y).mul(seg->worldTransform());
        const artboard::Point c[4] = {
            toFrame.apply({b.x, b.y}), toFrame.apply({b.right(), b.y}),
            toFrame.apply({b.right(), b.bottom()}), toFrame.apply({b.x, b.bottom()})};
        // Drawn as a polygon, not a rect, so it stays correct for a rotated/scaled shape.
        t.setStroke(palette::primary(), 1.0);
        t.beginPath();
        t.moveTo(c[0].x, c[0].y);
        for (int i = 1; i < 4; ++i)
            t.lineTo(c[i].x, c[i].y);
        t.closePath();
        t.strokePath();
        for (const auto &p : c)
            artboard::drawCircle(t, p.x, p.y, 2.5, artboard::Paint::filled(palette::primary()));
    }
}
}
