#include "Modal.h"
#include "../App.h"
#include "BaseCatalog.h"
#include <cmath>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>

namespace genesis
{
namespace ui
{
    namespace
    {
        constexpr double kCardW = 460.0;
        constexpr double kRowH = 30.0;

        std::vector<std::string> genesisFilesIn(const std::string &dir)
        {
            std::vector<std::string> out;
            DIR *d = ::opendir(dir.c_str());
            if (!d) return out;
            while (dirent *e = ::readdir(d))
            {
                const std::string n = e->d_name;
                if (n.size() > 8 && n.compare(n.size() - 8, 8, ".genesis") == 0)
                    out.push_back(dir + "/" + n);
            }
            ::closedir(d);
            std::sort(out.begin(), out.end());
            return out;
        }

        std::string baseName(const std::string &path)
        {
            const size_t s = path.find_last_of('/');
            return s == std::string::npos ? path : path.substr(s + 1);
        }
    }

    Modal::Modal(App &app) : mApp(app)
    {
        inputTransparent = false;
        mName = std::make_shared<artboard::TextBox>(theme().textBox);
        mName->focusable = true;
        mName->height.set(26.0);
        mName->visible = false;
        addChild(mName);

        mOk = std::make_shared<artboard::Button>("OK", theme().button);
        mOk->height.set(26.0);
        mOk->focusable = true;
        mOk->visible = false;
        addChild(mOk);

        mCancel = std::make_shared<artboard::Button>("Cancel", theme().button);
        mCancel->height.set(26.0);
        mCancel->focusable = true;
        mCancel->visible = false;
        mCancel->onClick = [this] { close(); };
        addChild(mCancel);
    }

    artboard::Rect Modal::cardRect() const
    {
        const double w = std::min(kCardW, width.value() - 40.0);
        const double rows = (double)std::max<size_t>(mChoices.size(), mLines.size());
        const double bodyH = mMode == Mode::Report ? std::min(280.0, 30.0 + rows * 18.0)
                                                   : std::min(240.0, 40.0 + rows * kRowH);
        const double h = std::min(height.value() - 40.0, 118.0 + bodyH);
        // Rises 10px as it fades in — the reveal is motivated (it announces itself), short,
        // and eased; reduced motion collapses it via the Property itself.
        const double rise = (1.0 - mReveal.value()) * 10.0;
        return {(width.value() - w) * 0.5, (height.value() - h) * 0.5 + rise, w, h};
    }

    void Modal::rebuild()
    {
        mChoice = 0;
        mChoices.clear();
        mLines.clear();
        mName->visible = false;
        mOk->visible = mMode != Mode::Report;
        mCancel->visible = true;
        mCancel->text = mMode == Mode::Report ? "Close" : "Cancel";

        App *a = &mApp;
        switch (mMode)
        {
        case Mode::New:
            mTitle = "New component";
            for (const auto &b : bases())
                mChoices.push_back(b.name);
            mName->visible = true;
            mName->text = "MyComponent";
            mName->placeholder = "Component name";
            mName->caretToEnd();
            mOk->text = "Create";
            mOk->onClick = [a, this] {
                if (mChoice < 0 || mChoice >= (int)mChoices.size()) return;
                a->newDocument(mChoices[(size_t)mChoice], mName->text);
                close();
            };
            break;
        case Mode::Browse:
        {
            mTitle = "Open component";
            mChoices = genesisFilesIn(".");
            for (const auto &extra : genesisFilesIn("samples"))
                mChoices.push_back(extra);
            mOk->text = "Open";
            mOk->onClick = [a, this] {
                if (mChoice < 0 || mChoice >= (int)mChoices.size()) return;
                a->openDocument(mChoices[(size_t)mChoice]);
                close();
            };
            break;
        }
        case Mode::SaveAs:
            mTitle = "Save component as";
            mName->visible = true;
            mName->text = mApp.doc().name + ".genesis";
            mName->placeholder = "file.genesis";
            mName->caretToEnd();
            mOk->text = "Save";
            mOk->onClick = [a, this] {
                a->saveDocumentAs(mName->text);
                close();
            };
            break;
        case Mode::Report:
            mOk->visible = false;
            break;
        case Mode::None:
            break;
        }
    }

    void Modal::openNew() { mMode = Mode::New; mOpen = true; rebuild(); mReveal.animateTo(1.0, artboard::motion::kDurationShort4, artboard::Easing::EmphasizedDecel, mNowMs); }
    void Modal::openBrowse() { mMode = Mode::Browse; mOpen = true; rebuild(); mReveal.animateTo(1.0, artboard::motion::kDurationShort4, artboard::Easing::EmphasizedDecel, mNowMs); }
    void Modal::openSaveAs() { mMode = Mode::SaveAs; mOpen = true; rebuild(); mReveal.animateTo(1.0, artboard::motion::kDurationShort4, artboard::Easing::EmphasizedDecel, mNowMs); }

    void Modal::openReport(const std::string &title, const std::vector<std::string> &lines, StatusLevel level)
    {
        mMode = Mode::Report;
        mOpen = true;
        rebuild();
        mTitle = title;
        mLines = lines;
        mLevel = level;
        mReveal.animateTo(1.0, artboard::motion::kDurationShort4, artboard::Easing::EmphasizedDecel, mNowMs);
    }

    void Modal::close()
    {
        // Fades out rather than vanishing; isOpen() drops immediately so it stops taking input.
        mOpen = false;
        mReveal.animateTo(0.0, artboard::motion::kDurationShort3, artboard::Easing::EaseInCubic, mNowMs);
        mName->visible = false;
        mOk->visible = false;
        mCancel->visible = false;
    }

    void Modal::layout(double w, double h)
    {
        width.set(w);
        height.set(h);
        const artboard::Rect c = cardRect();
        const double pad = 16.0;
        mName->x.set(c.x + pad);
        mName->y.set(c.bottom() - 74.0);
        mName->width.set(c.w - pad * 2.0);
        const double bw = 92.0;
        mOk->x.set(c.right() - pad - bw);
        mOk->y.set(c.bottom() - 40.0);
        mOk->width.set(bw);
        mCancel->x.set(c.right() - pad - bw * 2.0 - 8.0);
        mCancel->y.set(c.bottom() - 40.0);
        mCancel->width.set(bw);
    }

    void Modal::advance(double nowMs)
    {
        mNowMs = nowMs;
        mReveal.update(nowMs);
        mHover.advance(nowMs);
        layout(width.value(), height.value());
        Segment::advance(nowMs);
    }

    bool Modal::handleGesture(const artboard::Gesture &g, const artboard::Point &p)
    {
        if (!mOpen) return false;
        const artboard::Rect c = cardRect();
        const bool inside = c.contains(p);
        if (g.type == artboard::Gesture::Type::Move)
        {
            const double listY = c.y + 52.0;
            const int i = (int)std::floor((p.y - listY) / kRowH);
            mHover.setHovered(inside && i >= 0 && i < (int)mChoices.size() ? i : -1);
            return true;
        }
        if (g.type == artboard::Gesture::Type::Click)
        {
            if (!inside)
            {
                close();   // click the scrim to dismiss
                return true;
            }
            const double listY = c.y + 52.0;
            const int i = (int)std::floor((p.y - listY) / kRowH);
            if (i >= 0 && i < (int)mChoices.size())
            {
                mChoice = i;
                return true;
            }
        }
        // Swallow everything else while open: a modal is modal.
        return artboard::Segment::handleGesture(g, p) || inside;
    }

    void Modal::onPaint(artboard::IRenderTarget &t) const
    {
        const double reveal = mReveal.value();
        if (reveal <= 0.001) return;
        const double w = width.value(), h = height.value();

        // Scrim: dims the app so the dialog is unmistakably the focus.
        artboard::drawRoundedRect(t, {0, 0, w, h}, 0.0,
                                  artboard::Paint::filled(artboard::Color{0, 0, 0, 0.55 * reveal}));

        const artboard::Rect c = cardRect();
        artboard::drawElevation(t, c, radius::panel(), 18.0);
        artboard::drawRoundedRect(t, c, radius::panel(),
                                  artboard::Paint::filledStroked(palette::popover(), palette::border(), 1.0));

        const double pad = 16.0;
        artboard::Color titleColor = palette::foreground();
        if (mMode == Mode::Report)
            switch (mLevel)
            {
            case StatusLevel::Good: titleColor = palette::success(); break;
            case StatusLevel::Warn: titleColor = palette::warning(); break;
            case StatusLevel::Bad: titleColor = palette::destructive(); break;
            default: break;
            }
        drawFitted(t, mTitle, c.x + pad, c.y + 28.0, c.w - pad * 2.0, type::title(), titleColor,
                   font::sansSemiBold());

        t.save();
        t.clipRect(c.x, c.y + 40.0, c.w, c.h - 96.0);
        double y = c.y + 52.0;
        for (int i = 0; i < (int)mChoices.size(); ++i, y += kRowH)
        {
            const bool sel = i == mChoice;
            const double hover = mHover.amount(i);
            if (sel)
                artboard::drawRoundedRect(t, {c.x + 8.0, y, c.w - 16.0, kRowH - 3.0}, radius::hairline(),
                                          artboard::Paint::filled(palette::selectedWash(1.0)));
            else if (hover > 0.01)
                artboard::drawRoundedRect(t, {c.x + 8.0, y, c.w - 16.0, kRowH - 3.0}, radius::hairline(),
                                          artboard::Paint::filled(palette::hoverWash(hover)));
            const std::string label = mMode == Mode::Browse ? baseName(mChoices[(size_t)i]) : mChoices[(size_t)i];
            // Two columns that never overlap: the name takes a fixed share, the summary the
            // rest, and each is ellipsized into its own share (design rule R5).
            const double nameW = std::min(150.0, (c.w - pad * 2.0) * 0.38);
            const double summaryW = c.w - pad * 2.0 - nameW - 12.0;
            drawFitted(t, label, c.x + pad, centreBaseline(y, kRowH - 3.0, type::small()), nameW,
                       type::small(), sel ? palette::foreground() : palette::secondaryForeground(),
                       sel ? font::sansMedium() : font::sans());
            if (mMode == Mode::New)
                if (const BaseDef *b = findBase(mChoices[(size_t)i]))
                    drawFittedRight(t, b->summary, c.x + pad + nameW + 12.0,
                                    centreBaseline(y, kRowH - 3.0, type::micro()), summaryW,
                                    type::micro(), palette::mutedForeground(), font::sans());
        }
        for (const auto &line : mLines)
        {
            const bool indent = !line.empty() && line[0] == ' ';
            drawFitted(t, line, c.x + pad + (indent ? 10.0 : 0.0), y + 12.0, c.w - pad * 2.0,
                       type::small(), indent ? palette::mutedForeground() : palette::foreground(),
                       indent ? font::mono() : font::sans());
            y += 18.0;
        }
        if (mMode == Mode::Browse && mChoices.empty())
            drawFitted(t, "No .genesis files here. Use New to start one.", c.x + pad, y + 14.0,
                       c.w - pad * 2.0, type::small(), palette::mutedForeground(), font::sans());
        t.restore();
    }
}
}
