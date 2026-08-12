#include "ReactionsPanel.h"
#include "../App.h"
#include "BaseCatalog.h"
#include <cmath>

namespace genesis
{
namespace ui
{
    namespace
    {
        constexpr double kListW = 190.0;
        constexpr double kRowH = 28.0;
        constexpr double kHeadH = 34.0;
        constexpr double kScrubH = 26.0;
        constexpr double kStepH = 18.0;   // a step header owns its own row; tracks never sit under it
        constexpr double kChipW = 30.0;   // repeat / yoyo / delete, at the row's right edge
        constexpr double kChips = 3.0;

        std::string trackSummary(const Track &t)
        {
            std::string s = t.target + " → " + t.to;
            if (t.repeat == -1) s += "  ∞";
            else if (t.repeat > 0) s += "  ×" + std::to_string(t.repeat + 1);
            if (t.yoyo) s += " ⇄";
            return s;
        }
    }

    ReactionsPanel::ReactionsPanel(App &app) : mApp(app)
    {
        App *a = &mApp;

        mSignal = std::make_shared<artboard::ComboBox>(theme().combo);
        mSignal->height.set(22.0);
        mSignal->onChange = [a, this](int i) {
            if (Reaction *r = current())
            {
                const auto &sigs = findBase(a->doc().base)->signals;
                if (i >= 0 && i < (int)sigs.size()) r->signal = sigs[(size_t)i].name;
                a->documentChanged();
            }
        };
        addChild(mSignal);

        mCancel = std::make_shared<artboard::ComboBox>(theme().combo);
        mCancel->height.set(22.0);
        mCancel->setOptions({"restart", "ignore if running", "queue"});
        mCancel->onChange = [a, this](int i) {
            if (Reaction *r = current())
            {
                r->cancel = i == 1 ? Cancel::IgnoreIfRunning : (i == 2 ? Cancel::Queue : Cancel::Restart);
                a->documentChanged();
            }
        };
        addChild(mCancel);

        auto mkButton = [&](const char *label, std::function<void()> run) {
            auto b = std::make_shared<artboard::Button>(label, theme().button);
            b->height.set(22.0);
            b->focusable = true;
            b->onClick = std::move(run);
            addChild(b);
            return b;
        };
        mAddReaction = mkButton("+ Reaction", [a] {
            const BaseDef *base = findBase(a->doc().base);
            if (!base) return;
            // Default to the first EXPECTED signal that nothing handles yet — the thing the
            // author most likely came here to do.
            std::string want = base->signals.front().name;
            for (const auto &s : base->signals)
            {
                bool handled = false;
                for (const auto &r : a->doc().reactions)
                    if (r.signal == s.name) handled = true;
                if (s.expected && !handled) { want = s.name; break; }
            }
            Reaction r;
            r.signal = want;
            a->doc().reactions.push_back(r);
            a->selectReaction((int)a->doc().reactions.size() - 1);
            a->documentChanged();
        });
        mDeleteReaction = mkButton("Delete", [a] {
            auto &rs = a->doc().reactions;
            const int i = a->selectedReaction();
            if (i < 0 || i >= (int)rs.size()) return;
            rs.erase(rs.begin() + i);
            a->selectReaction(std::min(i, (int)rs.size() - 1));
            a->documentChanged();
        });
        mAddStep = mkButton("+ Step", [a, this] {
            if (Reaction *r = current())
            {
                r->steps.push_back(Step{});
                a->documentChanged();
            }
        });
        mAddTrack = mkButton("+ Track", [a, this] {
            Reaction *r = current();
            if (!r) return;
            if (r->steps.empty()) r->steps.push_back(Step{});
            Track t;
            // Prefill with a target that is already legal, so a new track is never born broken.
            for (const auto &s : a->doc().shapes)
                if (!s.animated.empty()) { t.target = s.id + "." + s.animated.front(); break; }
            if (t.target.empty())
                a->status("Mark a field animatable in the inspector first", StatusLevel::Warn);
            r->steps.back().tracks.push_back(t);
            a->documentChanged();
        });
        mFire = mkButton("Fire", [a, this] {
            if (const Reaction *r = current())
            {
                a->runtime().fire(r->signal);
                a->status("Fired " + r->signal, StatusLevel::Good);
            }
        });
        refresh();
    }

    /** One place that decides the track columns, so the captions, the fields and the
     *  hit-testing cannot drift apart when the panel is resized. */
    ReactionsPanel::Columns ReactionsPanel::columns(double panelW) const
    {
        Columns c;
        const double rightW = panelW - (kListW + metrics::pad()) - metrics::pad() - kChipW * kChips;
        c.gap = 6.0;
        c.easing = std::min(132.0, std::max(80.0, rightW * 0.20));
        c.ms = 54.0;
        c.delay = 48.0;
        c.from = std::max(50.0, rightW * 0.14);
        c.to = std::max(56.0, rightW * 0.16);
        c.target = std::max(78.0, rightW - c.easing - c.ms - c.delay - c.from - c.to - c.gap * 5.0);
        return c;
    }

    ReactionsPanel::Chip ReactionsPanel::chipAt(const artboard::Point &p, int &rowIndex) const
    {
        const double right = width.value() - metrics::pad();
        for (int i = 0; i < (int)mRows.size(); ++i)
        {
            const TrackRow &row = mRows[(size_t)i];
            if (p.y < row.y || p.y > row.y + kRowH - 2.0) continue;
            rowIndex = i;
            if (p.x >= right - kChipW) return Chip::Remove;
            if (p.x >= right - kChipW * 2.0) return Chip::Yoyo;
            if (p.x >= right - kChipW * kChips) return Chip::Repeat;
            return Chip::None;
        }
        rowIndex = -1;
        return Chip::None;
    }

    Reaction *ReactionsPanel::current()
    {
        auto &rs = mApp.doc().reactions;
        const int i = mApp.selectedReaction();
        return (i >= 0 && i < (int)rs.size()) ? &rs[(size_t)i] : nullptr;
    }
    const Reaction *ReactionsPanel::current() const
    {
        const auto &rs = mApp.doc().reactions;
        const int i = mApp.selectedReaction();
        return (i >= 0 && i < (int)rs.size()) ? &rs[(size_t)i] : nullptr;
    }

    void ReactionsPanel::rebuildRows()
    {
        for (const auto &r : mRows)
        {
            r.target->visible = false;
            r.from->visible = false;
            r.to->visible = false;
            r.ms->visible = false;
            r.delay->visible = false;
            r.easing->visible = false;
        }
        mRows.clear();
        const Reaction *r = current();
        if (!r) return;
        for (int si = 0; si < (int)r->steps.size(); ++si)
            for (int ti = 0; ti < (int)r->steps[(size_t)si].tracks.size(); ++ti)
            {
                const Track &t = r->steps[(size_t)si].tracks[(size_t)ti];
                TrackRow row;
                row.step = si;
                row.track = ti;
                auto field = [&](const std::string &value, const std::string &hint) {
                    auto b = std::make_shared<artboard::TextBox>(theme().textBox);
                    b->text = value;
                    b->placeholder = hint;
                    b->caretToEnd();
                    b->focusable = true;
                    b->height.set(20.0);
                    addChild(b);
                    return b;
                };
                row.target = field(t.target, "shape.field");
                row.from = field(t.from, "current");
                row.to = field(t.to, "to");
                row.ms = field(t.durationMs, "ms");
                row.delay = field(t.delayMs, "delay");
                row.easing = std::make_shared<artboard::ComboBox>(theme().combo);
                row.easing->height.set(20.0);
                row.easing->setOptions(easingNames());
                for (int e = 0; e < (int)easingNames().size(); ++e)
                    if (easingNames()[(size_t)e] == t.easing) row.easing->setSelectedIndex(e);
                App *a = &mApp;
                const int si2 = si, ti2 = ti;
                row.easing->onChange = [a, this, si2, ti2](int idx) {
                    Reaction *rr = current();
                    if (!rr || si2 >= (int)rr->steps.size()) return;
                    auto &tracks = rr->steps[(size_t)si2].tracks;
                    if (ti2 >= (int)tracks.size()) return;
                    tracks[(size_t)ti2].easing = easingNames()[(size_t)idx];
                    a->documentChanged();
                };
                addChild(row.easing);
                mRows.push_back(row);
            }
    }

    std::string ReactionsPanel::structureKey() const
    {
        std::string k = "r:" + std::to_string(mApp.selectedReaction()) + "/" +
                        std::to_string(mApp.doc().reactions.size());
        if (const Reaction *r = current())
            for (const auto &st : r->steps)
                k += "|" + std::to_string(st.tracks.size());
        return k;
    }

    void ReactionsPanel::syncValues()
    {
        const Reaction *r = current();
        if (!r) return;
        auto put = [](const std::shared_ptr<artboard::TextBox> &b, const std::string &v) {
            if (b->hasFocus() || b->text == v) return;
            b->text = v;
            b->caretToEnd();
        };
        for (auto &row : mRows)
        {
            if (row.step >= (int)r->steps.size()) continue;
            const auto &tracks = r->steps[(size_t)row.step].tracks;
            if (row.track >= (int)tracks.size()) continue;
            const Track &t = tracks[(size_t)row.track];
            put(row.target, t.target);
            put(row.from, t.from);
            put(row.to, t.to);
            put(row.ms, t.durationMs);
            put(row.delay, t.delayMs);
            for (int e = 0; e < (int)easingNames().size(); ++e)
                if (easingNames()[(size_t)e] == t.easing && row.easing->selectedIndex() != e)
                    row.easing->setSelectedIndex(e);
        }
    }

    void ReactionsPanel::refresh()
    {
        const BaseDef *base = findBase(mApp.doc().base);
        if (base)
        {
            std::vector<std::string> names;
            for (const auto &s : base->signals)
                names.push_back(s.name);
            mSignal->setOptions(names);
        }
        if (const Reaction *r = current())
        {
            const auto &sigs = base ? base->signals : std::vector<SignalDef>{};
            for (int i = 0; i < (int)sigs.size(); ++i)
                if (sigs[(size_t)i].name == r->signal) mSignal->setSelectedIndex(i);
            mCancel->setSelectedIndex(r->cancel == Cancel::IgnoreIfRunning ? 1
                                                                           : (r->cancel == Cancel::Queue ? 2 : 0));
        }
        // Keep the widgets (and the focus, and the caret) while the row structure is the
        // same — rebuilding would destroy the field being typed into.
        const std::string key = structureKey();
        if (key == mStructure && !mRows.empty())
        {
            syncValues();
            return;
        }
        mStructure = key;
        rebuildRows();
    }

    void ReactionsPanel::layout(double w, double h)
    {
        width.set(w);
        height.set(h);
        const double pad = metrics::pad();
        const double rightX = kListW + pad;
        const double rightW = w - rightX - pad;

        mAddReaction->x.set(pad);
        mAddReaction->y.set(h - pad - 22.0);
        mAddReaction->width.set(kListW * 0.55);
        mDeleteReaction->x.set(pad + kListW * 0.55 + 6.0);
        mDeleteReaction->y.set(h - pad - 22.0);
        mDeleteReaction->width.set(kListW * 0.45 - 6.0);

        mSignal->x.set(rightX);
        mSignal->y.set(pad - 2.0);
        mSignal->width.set(std::min(150.0, rightW * 0.3));
        mCancel->x.set(rightX + mSignal->width.value() + 8.0);
        mCancel->y.set(pad - 2.0);
        mCancel->width.set(std::min(140.0, rightW * 0.28));

        double bx = w - pad;
        for (auto *b : {&mFire, &mAddTrack, &mAddStep})
        {
            const double bw = std::max(56.0, textWidth((*b)->text, type::body(), font::sansMedium()) + 18.0);
            bx -= bw;
            (*b)->x.set(bx);
            (*b)->y.set(pad - 2.0);
            (*b)->width.set(bw);
            bx -= 6.0;
        }

        // Track columns: target | to | ms | easing — sized as fractions so they reflow.
        const double y0 = pad + kHeadH;
        double y = y0 - mListScroll;
        int lastStep = -1;
        const Columns c = columns(w);
        for (auto &row : mRows)
        {
            if (row.step != lastStep)   // reserve the step header's own row
            {
                lastStep = row.step;
                y += kStepH;
            }
            row.y = y;
            double x = rightX;
            auto place = [&](const std::shared_ptr<artboard::Segment> &seg, double cw) {
                seg->x.set(x); seg->y.set(y + 3.0); seg->width.set(cw);
                x += cw + c.gap;
            };
            place(row.target, c.target);
            place(row.from, c.from);
            place(row.to, c.to);
            place(row.ms, c.ms);
            place(row.delay, c.delay);
            place(row.easing, c.easing);
            y += kRowH;
        }
    }

    void ReactionsPanel::commit()
    {
        Reaction *r = current();
        if (!r) return;
        bool changed = false;
        for (const auto &row : mRows)
        {
            if (row.step >= (int)r->steps.size()) continue;
            auto &tracks = r->steps[(size_t)row.step].tracks;
            if (row.track >= (int)tracks.size()) continue;
            Track &t = tracks[(size_t)row.track];
            if (t.target != row.target->text) { t.target = row.target->text; changed = true; }
            if (t.to != row.to->text) { t.to = row.to->text; changed = true; }
            if (t.durationMs != row.ms->text) { t.durationMs = row.ms->text; changed = true; }
            if (t.from != row.from->text) { t.from = row.from->text; changed = true; }
            if (t.delayMs != row.delay->text) { t.delayMs = row.delay->text; changed = true; }
        }
        if (changed) mApp.documentChanged();
    }

    void ReactionsPanel::advance(double nowMs)
    {
        mNowMs = nowMs;
        mHover.advance(nowMs);
        for (const auto &row : mRows)
            if (row.target->hasFocus() || row.from->hasFocus() || row.to->hasFocus() ||
                row.ms->hasFocus() || row.delay->hasFocus())
            {
                commit();
                break;
            }
        const Reaction *r = current();
        mFire->enabled = r != nullptr;
        mAddStep->enabled = r != nullptr;
        mAddTrack->enabled = r != nullptr;
        mDeleteReaction->enabled = r != nullptr;
        Segment::advance(nowMs);
    }

    bool ReactionsPanel::handleGesture(const artboard::Gesture &g, const artboard::Point &p)
    {
        const double pad = metrics::pad();
        const double listTop = pad + 18.0;
        if (p.x < kListW)
        {
            const int i = (int)std::floor((p.y - listTop) / kRowH);
            if (g.type == artboard::Gesture::Type::Move)
            {
                mHover.setHovered(i >= 0 && i < (int)mApp.doc().reactions.size() ? i : -1);
                return true;
            }
            if (g.type == artboard::Gesture::Type::Click && i >= 0 && i < (int)mApp.doc().reactions.size())
            {
                mApp.selectReaction(i);
                return true;
            }
        }
        if (g.type == artboard::Gesture::Type::Click)
        {
            int rowIndex = -1;
            const Chip chip = chipAt(p, rowIndex);
            if (chip != Chip::None && rowIndex >= 0)
            {
                Reaction *r = current();
                const TrackRow &row = mRows[(size_t)rowIndex];
                if (r && row.step < (int)r->steps.size())
                {
                    auto &tracks = r->steps[(size_t)row.step].tracks;
                    if (row.track < (int)tracks.size())
                    {
                        Track &tr = tracks[(size_t)row.track];
                        if (chip == Chip::Repeat)
                        {
                            // Cycles 1x -> 2x -> 3x -> forever -> 1x. "Forever" has to be
                            // reachable without typing: it is what a spinner needs.
                            tr.repeat = tr.repeat == -1 ? 0 : (tr.repeat >= 2 ? -1 : tr.repeat + 1);
                        }
                        else if (chip == Chip::Yoyo)
                            tr.yoyo = !tr.yoyo;
                        else if (chip == Chip::Remove)
                        {
                            tracks.erase(tracks.begin() + row.track);
                            if (tracks.empty())
                                r->steps.erase(r->steps.begin() + row.step);   // no empty steps
                        }
                        mApp.documentChanged();
                        return true;
                    }
                }
            }
        }
        // The scrubber strip: drag to scrub the selected reaction's motion.
        const double scrubY = height.value() - pad - 22.0 - kScrubH;
        if (p.x >= kListW + pad && p.y >= scrubY && p.y <= scrubY + kScrubH)
        {
            if (g.type == artboard::Gesture::Type::Down || g.type == artboard::Gesture::Type::Drag ||
                g.type == artboard::Gesture::Type::DragStart)
            {
                const double x0 = kListW + pad;
                const double w = width.value() - x0 - pad;
                mScrubbing = true;
                mScrubT = std::min(1.0, std::max(0.0, (p.x - x0) / std::max(1.0, w)));
                if (const Reaction *r = current())
                    mApp.runtime().scrub(r->signal, mScrubT);   // replay to that point
                return true;
            }
            if (g.type == artboard::Gesture::Type::Drop)
            {
                mScrubbing = false;
                return true;
            }
        }
        return artboard::Segment::handleGesture(g, p);
    }

    void ReactionsPanel::onPaint(artboard::IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        const double pad = metrics::pad();
        artboard::drawRoundedRect(t, {0, 0, w, h}, 0.0, artboard::Paint::filled(palette::chromeBg()));
        artboard::drawRoundedRect(t, {0, 0, w, 1}, 0.0, artboard::Paint::filled(palette::border()));
        artboard::drawRoundedRect(t, {kListW, 0, 1, h}, 0.0, artboard::Paint::filled(palette::border()));

        drawSectionTitle(t, "Reactions", pad, pad + 8.0);

        const auto &rs = mApp.doc().reactions;
        const double listTop = pad + 18.0;
        double y = listTop;
        for (int i = 0; i < (int)rs.size(); ++i, y += kRowH)
        {
            if (y + kRowH > h - pad - 30.0) break;
            const bool sel = i == mApp.selectedReaction();
            const double hover = mHover.amount(i);
            if (sel)
                artboard::drawRoundedRect(t, {4, y, kListW - 8, kRowH - 3}, radius::hairline(),
                                          artboard::Paint::filled(palette::selectedWash(1.0)));
            else if (hover > 0.01)
                artboard::drawRoundedRect(t, {4, y, kListW - 8, kRowH - 3}, radius::hairline(),
                                          artboard::Paint::filled(palette::hoverWash(hover)));
            // Only claim a reaction is running when the preview is actually live.
            const bool running = mApp.previewOk() && mApp.runtime().isRunning(rs[(size_t)i].signal);
            if (running)
                artboard::drawCircle(t, 12.0, y + kRowH * 0.5 - 1.0, 3.0,
                                     artboard::Paint::filled(palette::success()));
            drawFitted(t, rs[(size_t)i].signal, running ? 22.0 : pad,
                       centreBaseline(y, kRowH - 3, type::small()), kListW - 40.0, type::small(),
                       sel ? palette::foreground() : palette::secondaryForeground(),
                       sel ? font::sansMedium() : font::sans());
            const int steps = (int)rs[(size_t)i].steps.size();
            drawFittedRight(t, std::to_string(steps) + (steps == 1 ? " step" : " steps"),
                            kListW - pad - 46.0, centreBaseline(y, kRowH - 3, type::micro()), 46.0,
                            type::micro(), palette::mutedForeground(), font::sans());
        }
        if (rs.empty())
        {
            drawFitted(t, "No reactions", pad, listTop + 14.0, kListW - pad * 2.0, type::small(),
                       palette::mutedForeground(), font::sans());
            drawFitted(t, "Add one to make it move.", pad, listTop + 30.0, kListW - pad * 2.0,
                       type::micro(), palette::mutedForeground(), font::sans());
        }

        const double rightX = kListW + pad;
        const double rightW = w - rightX - pad;
        const Reaction *r = current();
        if (!r)
        {
            drawFitted(t, "Select or add a reaction", rightX, h * 0.45, rightW, type::body(),
                       palette::mutedForeground(), font::sans());
            return;
        }

        // Column captions, aligned with the field row layout.
        const double y0 = pad + kHeadH;
        const double capY = y0 - 6.0;
        const Columns c = columns(w);
        {
            double cx = rightX;
            const char *caps[] = {"target", "from", "to", "ms", "delay", "easing"};
            const double widths[] = {c.target, c.from, c.to, c.ms, c.delay, c.easing};
            for (int i = 0; i < 6; ++i)
            {
                drawFitted(t, caps[i], cx, capY, widths[i], type::micro(),
                           palette::mutedForeground(), font::sans());
                cx += widths[i] + c.gap;
            }
            drawFitted(t, "repeat", w - pad - kChipW * kChips, capY, kChipW * kChips - 4.0,
                       type::micro(), palette::mutedForeground(), font::sans());
        }

        double ry = y0 - mListScroll;
        int lastStep = -1;
        for (const auto &row : mRows)
        {
            if (ry + kRowH > h - pad - 30.0 - kScrubH) break;
            if (row.step != lastStep)
            {
                lastStep = row.step;
                // Step separator on its OWN row, so the chain reads as "this, then that"
                // without ever sitting on top of a track.
                const std::string label =
                    row.step == 0 ? "step 1" : "then step " + std::to_string(row.step + 1);
                drawFitted(t, label, rightX, ry + 12.0, 120.0, type::micro(), palette::primary(),
                           font::sansMedium());
                const double lx = rightX + textWidth(label, type::micro(), font::sansMedium()) + 8.0;
                artboard::drawRoundedRect(t, {lx, ry + 8.0, std::max(0.0, w - pad - lx), 1.0}, 0.0,
                                          artboard::Paint::filled(palette::border()));
                ry += kStepH;
            }
            // The three chips at the row's right edge: repeat count, yoyo, remove.
            const Reaction *rr = current();
            if (rr && row.step < (int)rr->steps.size() &&
                row.track < (int)rr->steps[(size_t)row.step].tracks.size())
            {
                const Track &tr = rr->steps[(size_t)row.step].tracks[(size_t)row.track];
                const double right = w - pad;
                const double cy = ry + kRowH * 0.5 - 1.0;

                const std::string rep = tr.repeat == -1 ? "∞" : "×" + std::to_string(tr.repeat + 1);
                artboard::drawRoundedRect(t, {right - kChipW * kChips, ry + 2.0, kChipW - 4.0, kRowH - 8.0},
                                          radius::hairline(),
                                          artboard::Paint::filled(tr.repeat != 0 ? palette::primaryAlpha(0.28)
                                                                                 : palette::input()));
                drawFitted(t, rep, right - kChipW * kChips + 7.0, cy + 4.0, kChipW - 10.0, type::micro(),
                           tr.repeat != 0 ? palette::foreground() : palette::mutedForeground(),
                           font::monoMedium());

                artboard::drawRoundedRect(t, {right - kChipW * 2.0, ry + 2.0, kChipW - 4.0, kRowH - 8.0},
                                          radius::hairline(),
                                          artboard::Paint::filled(tr.yoyo ? palette::primaryAlpha(0.28)
                                                                          : palette::input()));
                drawFitted(t, "⇄", right - kChipW * 2.0 + 8.0, cy + 4.0, kChipW - 10.0, type::micro(),
                           tr.yoyo ? palette::foreground() : palette::mutedForeground(), font::sans());

                artboard::drawRoundedRect(t, {right - kChipW, ry + 2.0, kChipW - 4.0, kRowH - 8.0},
                                          radius::hairline(), artboard::Paint::filled(palette::input()));
                t.setStroke(palette::destructive(), 1.3);
                t.beginPath();
                const double xx = right - kChipW + (kChipW - 4.0) * 0.5;
                t.moveTo(xx - 3.5, cy - 3.5); t.lineTo(xx + 3.5, cy + 3.5);
                t.moveTo(xx + 3.5, cy - 3.5); t.lineTo(xx - 3.5, cy + 3.5);
                t.strokePath();
            }
            ry += kRowH;
        }
        if (mRows.empty())
            drawFitted(t, "No tracks yet — add one, then pick what it animates.", rightX, y0 + 14.0,
                       rightW, type::small(), palette::mutedForeground(), font::sans());

        // The scrubber. Local to this reaction, because the model is an event graph, not a
        // global timeline; the chip beside it names what can interrupt this reaction.
        const double scrubY = h - pad - 22.0 - kScrubH;
        artboard::drawRoundedRect(t, {rightX, scrubY + 8.0, rightW, 4.0}, radius::pill(),
                                  artboard::Paint::filled(palette::input()));
        artboard::drawRoundedRect(t, {rightX, scrubY + 8.0, rightW * mScrubT, 4.0}, radius::pill(),
                                  artboard::Paint::filled(palette::primary()));
        artboard::drawCircle(t, rightX + rightW * mScrubT, scrubY + 10.0, mScrubbing ? 6.0 : 4.5,
                             artboard::Paint::filled(palette::foreground()));
        char pct[24];
        std::snprintf(pct, sizeof pct, "%d%%", (int)std::lround(mScrubT * 100.0));
        drawFittedRight(t, pct, w - pad - 34.0, scrubY + 26.0, 34.0, type::micro(),
                        palette::mutedForeground(), font::mono());
        drawFitted(t, "scrub", rightX, scrubY + 26.0, 60.0, type::micro(),
                   palette::mutedForeground(), font::sans());
    }
}
}
