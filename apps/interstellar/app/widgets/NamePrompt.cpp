#include "NamePrompt.h"
#include "TextFit.h"
#include <algorithm>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        constexpr double kCardW = 360.0;
        constexpr double kCardH = 168.0;
        constexpr double kPad = 16.0;     // the modal 8/16/32 rhythm
        constexpr double kFieldH = 28.0;
        constexpr double kBtnH = 28.0;
        constexpr double kBtnW = 88.0;
        constexpr double kRowH = 34.0;      // a labelled field row (multi-field mode)
        constexpr double kLabelW = 150.0;
        Color fade(Color c, double a) { c.a *= a; return c; }
    }

    NamePrompt::NamePrompt()
    {
        TextBoxStyle st;
        st.idle = {Paint::filledStroked(palette::input(), palette::border(), 1.0), radius::control()};
        st.focused = {Paint::filledStroked(palette::input(), palette::ring(), 1.0), radius::control()};
        st.text = {palette::foreground(), 12.0, font::sans()};
        st.placeholder = {palette::mutedForeground(), 12.0, font::sans()};
        st.caretColor = palette::primary();
        st.selectionColor = palette::primaryAlpha(0.38);
        mField = std::make_shared<TextBox>(st);
        mField->focusable = true;
        mField->opacity.set(0.0);
        addChild(mField);
        for (auto &f : mFields)
        {
            f = std::make_shared<TextBox>(st);
            f->focusable = true;
            f->opacity.set(0.0);
            f->visible = false;
            addChild(f);
        }
    }

    void NamePrompt::showFields(const std::string &title, const std::string &message,
                                const std::vector<std::pair<std::string, std::string>> &fields, const std::string &confirmLabel,
                                std::function<void(const std::vector<std::string> &)> onConfirm)
    {
        mMulti = true;
        mCount = std::min((int)fields.size(), kMaxFields);
        for (int i = 0; i < kMaxFields; ++i)
        {
            mLabels[i] = i < mCount ? fields[(size_t)i].first : std::string();
            mFields[i]->text = i < mCount ? fields[(size_t)i].second : std::string();
            mFields[i]->visible = i < mCount;
        }
        mField->visible = false;
        mOnConfirmFields = std::move(onConfirm);
        mTitle = title;
        mMessage = message;
        mConfirmLabel = confirmLabel;
        if (mCount > 0) { mFields[0]->selectAll(); mFields[0]->requestFocus(); }
        mOpen = true;
        mClosing = false;
        mStartPending = true;
        raise();
    }

    int NamePrompt::focusedField() const
    {
        for (int i = 0; i < mCount; ++i) if (mFields[i]->hasFocus()) return i;
        return 0;
    }

    double NamePrompt::cardH() const { return mMulti ? 64.0 + mCount * kRowH + kPad + kBtnH + kPad : kCardH; }

    void NamePrompt::show(const std::string &title, const std::string &message, const std::string &initial,
                          const std::string &confirmLabel, std::function<void(const std::string &)> onConfirm)
    {
        mMulti = false;
        mCount = 0;
        for (auto &f : mFields) f->visible = false;
        mField->visible = true;
        mTitle = title;
        mMessage = message;
        mConfirmLabel = confirmLabel;
        mOnConfirm = std::move(onConfirm);
        mField->text = initial;
        mField->selectAll();
        mField->requestFocus();
        mOpen = true;
        mClosing = false;
        mStartPending = true;   // a setter has no clock: advance starts the appear
        raise();
    }

    void NamePrompt::beginClose()
    {
        if (!mOpen || mClosing) return;
        mClosing = true;
        mAppear.animateTo(0.0, motion::kModalCloseMs, Easing::EaseOutCubic, mLastMs);
    }

    void NamePrompt::confirm()
    {
        if (!isOpen()) return;
        if (mMulti)
        {
            std::vector<std::string> v;
            for (int i = 0; i < mCount; ++i) v.push_back(mFields[i]->text);
            auto action = mOnConfirmFields;   // copy out, close, THEN act
            beginClose();
            if (action) action(v);
            return;
        }
        std::string name = mField->text;
        while (!name.empty() && name.back() == ' ') name.pop_back();
        while (!name.empty() && name.front() == ' ') name.erase(name.begin());
        if (name.empty()) return;   // nothing to create: stay open, the field says why by being empty
        auto action = mOnConfirm;   // copy out, close, THEN act
        beginClose();
        if (action) action(name);
    }

    void NamePrompt::cancel() { beginClose(); }

    bool NamePrompt::handleKey(const KeyEvent &e)
    {
        if (!isOpen()) return false;
        if (e.type == KeyEvent::Type::Down && e.keyCode == 27) { cancel(); return true; }
        if (e.type == KeyEvent::Type::Down && (e.keyCode == 13 || e.keyCode == 10)) { confirm(); return true; }
        if (mMulti)
        {
            const int i = focusedField();
            if (e.type == KeyEvent::Type::Down && e.keyCode == 9 && mCount > 0)   // Tab: the next field
            {
                const int n = (i + 1) % mCount;
                mFields[n]->requestFocus();
                mFields[n]->selectAll();
                return true;
            }
            if (i < mCount) mFields[i]->dispatchKey(e);
            return true;
        }
        mField->dispatchKey(e);
        return true;
    }

    Rect NamePrompt::cardRect() const
    {
        return Rect{(width.value() - kCardW) * 0.5, (height.value() - cardH()) * 0.5 - 24.0, kCardW, cardH()};
    }

    Rect NamePrompt::buttonRect(int i) const
    {
        const Rect c = cardRect();
        const double y = c.bottom() - kPad - kBtnH;
        const double right = c.right() - kPad;
        return i == 1 ? Rect{right - kBtnW, y, kBtnW, kBtnH} : Rect{right - 2 * kBtnW - 8.0, y, kBtnW, kBtnH};
    }

    void NamePrompt::layout()
    {
        const Rect c = cardRect();
        mField->x.set(c.x + kPad);
        mField->y.set(c.y + 72.0);
        mField->width.set(c.w - 2 * kPad);
        mField->height.set(kFieldH);
        for (int i = 0; i < kMaxFields; ++i)
        {
            mFields[i]->x.set(c.x + kPad + kLabelW);
            mFields[i]->y.set(c.y + 60.0 + i * kRowH);
            mFields[i]->width.set(std::max(0.0, c.w - 2 * kPad - kLabelW));
            mFields[i]->height.set(kFieldH);
        }
    }

    void NamePrompt::advance(double nowMs)
    {
        mLastMs = nowMs;
        if (mStartPending)
        {
            mAppear.animateTo(1.0, motion::kModalOpenMs, Easing::EaseOutCubic, nowMs);
            mStartPending = false;
        }
        mAppear.update(nowMs);
        if (mClosing && !mAppear.isAnimating() && mAppear.value() <= 0.001) { mOpen = false; mClosing = false; }
        mField->opacity.set(mAppear.value());
        for (auto &f : mFields) f->opacity.set(mAppear.value());
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        layout();
        Segment::advance(nowMs);
    }

    bool NamePrompt::handleGesture(const Gesture &g, const Point &local)
    {
        if (!isOpen()) return false;
        if (g.type == Gesture::Type::Move)
        {
            mHover.setHovered(buttonRect(0).contains(local) ? 0 : (buttonRect(1).contains(local) ? 1 : -1));
            return true;
        }
        if (g.type == Gesture::Type::Click)
        {
            if (buttonRect(0).contains(local)) { cancel(); return true; }
            if (buttonRect(1).contains(local)) { confirm(); return true; }
            if (!cardRect().contains(local)) { cancel(); return true; }   // click outside cancels
        }
        return true;   // a modal swallows everything while it is up
    }

    void NamePrompt::onPaint(IRenderTarget &t) const
    {
        const double a = mAppear.value();
        if (a <= 0.001) return;
        drawRoundedRect(t, Rect{0, 0, width.value(), height.value()}, 0.0, Paint::filled(surface::scrim(0.55 * a)));
        Rect c = cardRect();
        c.y += 8.0 * (1.0 - a);   // a small rise on entry, from the same eased amount
        drawRoundedRect(t, Rect{c.x, c.y + 6, c.w, c.h}, radius::control(), Paint::filled(Color{0, 0, 0, 0.35 * a}));
        drawRoundedRect(t, c, radius::control(), Paint::filledStroked(fade(palette::popover(), a), fade(palette::border(), a), 1.0));
        t.setFill(fade(palette::foreground(), a));
        t.drawText(textfit::ellipsize(t, mTitle, c.w - 2 * kPad, 14.0, font::sansSemiBold()), c.x + kPad, c.y + kPad + 14.0, 14.0, font::sansSemiBold());
        t.setFill(fade(palette::mutedForeground(), a));
        t.drawText(textfit::ellipsize(t, mMessage, c.w - 2 * kPad, 12.0, font::sans()), c.x + kPad, c.y + kPad + 38.0, 12.0, font::sans());
        if (mMulti)
            for (int i = 0; i < mCount; ++i)
            {
                t.setFill(fade(palette::mutedForeground(), a));
                t.drawText(textfit::ellipsize(t, mLabels[i], kLabelW - 8.0, 11.0, font::sans()), c.x + kPad,
                           textfit::baseline(c.y + 60.0 + i * kRowH + kFieldH * 0.5, 11.0), 11.0, font::sans());
            }

        for (int i = 0; i < 2; ++i)
        {
            Rect r = buttonRect(i);
            r.y += 8.0 * (1.0 - a);
            const double hv = mHover.amount(i);
            if (i == 1)
                drawRoundedRect(t, r, radius::control(), Paint::filled(fade(brighten(palette::primary(), interaction::kHoverFillLift * hv), a)));
            else
                drawRoundedRect(t, r, radius::control(), Paint::filledStroked(fade(palette::hoverWash(hv), a), fade(palette::border(), a), 1.0));
            const std::string label = i == 1 ? mConfirmLabel : std::string("Cancel");
            const char *fam = i == 1 ? font::sansSemiBold() : font::sans();
            const double tw = t.measureText(label, 12.0, fam);
            t.setFill(fade(i == 1 ? palette::primaryForeground() : palette::foreground(), a));
            t.drawText(label, r.x + (r.w - tw) * 0.5, textfit::baseline(r.y + r.h * 0.5, 12.0), 12.0, fam);
        }
    }
}
}
