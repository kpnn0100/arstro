#include "ReactionsPanel.h"
#include "../App.h"
#include "BaseCatalog.h"
#include <cmath>
#include <cstdio>

namespace genesis
{
namespace ui
{
    namespace
    {
        constexpr double kListW = 190.0;
        constexpr double kRowH = 28.0;
        // The header band: the signal/cancel dropdowns and the +Step/+Track/Fire buttons sit at
        // `pad - 2` and are 22 tall, so they end at `pad + 20`. The column captions are drawn 6px
        // above the first track row, so the band has to leave room for BOTH — at 34 the captions'
        // ascenders touched the buttons' bottom edge with no air at all.
        constexpr double kHeadH = 42.0;
        constexpr double kScrubH = 26.0;
        constexpr double kStepH = 18.0;   // a step header owns its own row; tracks never sit under it
        constexpr double kChipW = 30.0;   // repeat / yoyo / delete, at the row's right edge
        constexpr double kChips = 3.0;

        /** The dropdown's list: every `artboard::Easing` plus `Custom`, the curve authored by the
         *  speed it enters and leaves at (G-25). `Custom` is deliberately NOT in `easingNames()` —
         *  that table maps names to real enumerators and nothing maps to this one — but it has to
         *  appear here, or a track using it reads as "Linear" and the first touch of the control
         *  silently throws the authored speeds away. */
        const std::vector<std::string> &easingOptions()
        {
            static const std::vector<std::string> all = [] {
                std::vector<std::string> v = easingNames();
                v.push_back("Custom");
                return v;
            }();
            return all;
        }

        /** Milliseconds, short enough for a step header: whole ms under a second, then seconds
         *  with one decimal, because "8500 ms" is harder to read at a glance than "8.5 s". */
        std::string fmtMs(double ms)
        {
            char buf[32];
            if (ms < 1000.0)
                std::snprintf(buf, sizeof buf, "%.0f ms", ms);
            else
                std::snprintf(buf, sizeof buf, "%.2g s", ms / 1000.0);
            return buf;
        }

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

        // G-26. A chain could repeat a TRACK but not itself, so "play an intro, then cycle
        // forever" was not expressible. The range lives on the reaction, so it belongs beside the
        // signal and the cancel policy rather than on any one step.
        mLoopFrom = std::make_shared<artboard::ComboBox>(theme().combo);
        mLoopFrom->height.set(22.0);
        mLoopFrom->onChange = [a, this](int i) {
            Reaction *r = current();
            if (!r) return;
            if (i <= 0)   // "no loop" — the off switch is this control, not a separate toggle
            {
                r->loopFrom = r->loopTo = -1;
            }
            else
            {
                r->loopFrom = i - 1;
                // Keep the range well-formed here rather than letting validate() scold about a
                // state the UI itself produced: dragging the start past the end drags the end.
                if (r->loopTo < r->loopFrom) r->loopTo = (int)r->steps.size() - 1;
                if (r->loopTo < r->loopFrom) r->loopTo = r->loopFrom;
            }
            a->documentChanged();
        };
        addChild(mLoopFrom);

        mLoopTo = std::make_shared<artboard::ComboBox>(theme().combo);
        mLoopTo->height.set(22.0);
        mLoopTo->onChange = [a, this](int i) {
            Reaction *r = current();
            if (!r || !r->loops()) return;
            r->loopTo = i;
            if (r->loopTo < r->loopFrom) r->loopFrom = r->loopTo;
            a->documentChanged();
        };
        addChild(mLoopTo);

        auto mkButton = [&](const char *label, std::function<void()> run) {
            auto b = std::make_shared<artboard::Button>(label, theme().button);
            b->height.set(22.0);
            b->focusable = true;
            b->onClick = std::move(run);
            addChild(b);
            return b;
        };
        mAddReaction = mkButton("+ Reaction", [a, this] {
            Shape *host = owner();
            if (!host)
            {
                a->status("Select an object to give it a reaction", StatusLevel::Warn);
                return;
            }
            const BaseDef *base = findBase(a->doc().base);
            if (!base) return;
            // Default to the first EXPECTED signal that nothing handles yet — the thing the
            // author most likely came here to do.
            std::string want = base->signals.front().name;
            for (const auto &s : base->signals)
            {
                bool handled = false;
                for (const auto &r : host->reactions)
                    if (r.signal == s.name) handled = true;
                if (s.expected && !handled) { want = s.name; break; }
            }
            Reaction r;
            r.signal = want;
            host->reactions.push_back(r);
            a->selectReaction((int)host->reactions.size() - 1);
            a->documentChanged();
        });
        mDeleteReaction = mkButton("Delete", [a, this] {
            Shape *host = owner();
            if (!host) return;
            const int i = a->selectedReaction();
            if (i < 0 || i >= (int)host->reactions.size()) return;
            host->reactions.erase(host->reactions.begin() + i);
            a->selectReaction(std::min(i, (int)host->reactions.size() - 1));
            a->documentChanged();
        });
        // The same ×1 → ×2 → ×3 → ∞ cycle the track repeat chip uses, so one idiom covers both
        // "this track repeats" and "this chain repeats" (G-26).
        mLoopCount = mkButton("x2", [a, this] {
            Reaction *r = current();
            if (!r || !r->loops()) return;
            r->loopCount = r->loopCount < 0 ? 2 : (r->loopCount >= 4 ? -1 : r->loopCount + 1);
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
            // Prefill with a field of THIS object, so a new track is never born broken — and
            // the target is bare, because the owner is the selected object.
            // Every object has animatable fields, so a new track always has a valid target and
            // there is no longer anything to mark first (G-21). `opacity` when the kind has it:
            // it is what an author reaches for most, and it is safe on any shape.
            const Shape *host = owner();
            if (host)
                for (const auto *fd : fieldsFor(host->kind))
                    if (fd->animatable && (t.target.empty() || std::string(fd->name) == "opacity"))
                        t.target = fd->name;
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

    double ReactionsPanel::Columns::total() const
    {
        double n = 0.0, sum = 0.0;
        for (double c : {target, from, to, ms, delay, easing})
            if (c > 0.0) { sum += c; n += 1.0; }
        return sum + (n > 1.0 ? (n - 1.0) * gap : 0.0);
    }

    /*  One place decides the track columns, so the captions, the fields and the hit-testing
     *  cannot drift apart when the panel is resized.
     *
     *  Narrow widths SHED COLUMNS rather than squeezing everything until the text collides:
     *  `delay` goes first, then `from`, then the chips, then `easing` — in reverse order of
     *  how often they are edited. What remains always fits, so nothing ever overlaps.
     */
    ReactionsPanel::Columns ReactionsPanel::columns(double panelW) const
    {
        Columns c;
        // The grip is taken off the top and never shed: without it the rows cannot be reordered
        // at all, and no other part of a row is draggable (G-23).
        const double avail =
            panelW - (kListW + metrics::pad()) - metrics::pad() - Columns::grip;

        c.easing = 112.0;
        c.ms = 52.0;
        c.delay = 46.0;
        c.from = 62.0;
        c.to = 68.0;
        c.target = 132.0;

        auto fits = [&] { return c.total() + (c.showChips ? kChipW * kChips : 0.0) <= avail; };
        if (!fits()) c.delay = 0.0;
        if (!fits()) c.from = 0.0;
        if (!fits()) c.showChips = false;
        if (!fits()) c.easing = std::max(64.0, c.easing - (c.total() - avail));
        if (!fits()) c.to = std::max(40.0, c.to - (c.total() - avail));
        if (!fits()) c.ms = std::max(34.0, c.ms - (c.total() - avail));
        if (!fits()) c.target = std::max(52.0, c.target - (c.total() - avail));

        // Whatever is left over goes to the target column, which is the one worth the room.
        const double slack = avail - (c.total() + (c.showChips ? kChipW * kChips : 0.0));
        if (slack > 0.0)
            c.target += slack;
        return c;
    }

    int ReactionsPanel::gripAt(const artboard::Point &p) const
    {
        const double x0 = kListW + metrics::pad();
        if (p.x < x0 || p.x > x0 + Columns::grip) return -1;
        for (int i = 0; i < (int)mRows.size(); ++i)
        {
            const TrackRow &row = mRows[(size_t)i];
            if (!row.target->visible) continue;          // a hidden row has no grip to grab
            if (p.y >= row.y && p.y <= row.y + kRowH - 2.0) return i;
        }
        return -1;
    }

    void ReactionsPanel::dropTargetAt(double localY, int &step, int &index) const
    {
        const Reaction *r = current();
        const int stepCount = r ? (int)r->steps.size() : 0;
        step = std::max(0, stepCount - 1);
        index = 0;
        if (!r || mRows.empty()) return;

        int lastStep = -1;
        for (const auto &row : mRows)
        {
            if (row.step != lastStep)
            {
                lastStep = row.step;
                // The step's own header band: a drop on the title means "first in this step".
                if (localY < row.y)
                {
                    step = row.step;
                    index = 0;
                    return;
                }
            }
            if (localY < row.y + kRowH * 0.5) { step = row.step; index = row.track; return; }
            if (localY < row.y + kRowH) { step = row.step; index = row.track + 1; return; }
        }
        // Past every row. Within a row's height of the last one it means "last in that step";
        // further down it means a NEW final step, which is how simultaneous becomes sequential.
        const TrackRow &last = mRows.back();
        step = localY > last.y + kRowH * 2.0 ? stepCount : last.step;
        index = step == stepCount ? 0 : last.track + 1;
    }

    ReactionsPanel::Chip ReactionsPanel::chipAt(const artboard::Point &p, int &rowIndex) const
    {
        rowIndex = -1;
        if (!columns(width.value()).showChips)
            return Chip::None;   // dropped at this width: nothing to hit
        const double right = width.value() - metrics::pad();
        for (int i = 0; i < (int)mRows.size(); ++i)
        {
            const TrackRow &row = mRows[(size_t)i];
            if (!row.target->visible) continue;   // a hidden row has no chips either
            if (p.y < row.y || p.y > row.y + kRowH - 2.0) continue;
            rowIndex = i;
            if (p.x >= right - kChipW) return Chip::Remove;
            if (p.x >= right - kChipW * 2.0) return Chip::Yoyo;
            if (p.x >= right - kChipW * kChips) return Chip::Repeat;
            return Chip::None;
        }
        return Chip::None;
    }

    /*  The panel is scoped to the SELECTED object: a reaction belongs to a shape, so
     *  selecting one shows its reactions and nothing else. With the owner implied, a track's
     *  target is just a field name.
     */
    Shape *ReactionsPanel::owner()
    {
        return mApp.doc().findShape(mApp.selectedShape());
    }
    const Shape *ReactionsPanel::owner() const
    {
        return mApp.doc().findShape(mApp.selectedShape());
    }

    Reaction *ReactionsPanel::current()
    {
        Shape *s = owner();
        const int i = mApp.selectedReaction();
        return (s && i >= 0 && i < (int)s->reactions.size()) ? &s->reactions[(size_t)i] : nullptr;
    }
    const Reaction *ReactionsPanel::current() const
    {
        const Shape *s = owner();
        const int i = mApp.selectedReaction();
        return (s && i >= 0 && i < (int)s->reactions.size()) ? &s->reactions[(size_t)i] : nullptr;
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
                row.easing->setOptions(easingOptions());
                for (int e = 0; e < (int)easingOptions().size(); ++e)
                    if (easingOptions()[(size_t)e] == t.easing) row.easing->setSelectedIndex(e);
                App *a = &mApp;
                const int si2 = si, ti2 = ti;
                row.easing->onChange = [a, this, si2, ti2](int idx) {
                    Reaction *rr = current();
                    if (!rr || si2 >= (int)rr->steps.size()) return;
                    auto &tracks = rr->steps[(size_t)si2].tracks;
                    if (ti2 >= (int)tracks.size()) return;
                    tracks[(size_t)ti2].easing = easingOptions()[(size_t)idx];
                    a->documentChanged();
                };
                addChild(row.easing);
                mRows.push_back(row);
            }
    }

    std::string ReactionsPanel::structureKey() const
    {
        std::string k = "o:" + mApp.selectedShape() + " r:" + std::to_string(mApp.selectedReaction());
        if (const Shape *host = owner())
            k += "/" + std::to_string(host->reactions.size());
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
            for (int e = 0; e < (int)easingOptions().size(); ++e)
                if (easingOptions()[(size_t)e] == t.easing && row.easing->selectedIndex() != e)
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
            // G-26. The step count changes as steps are added and removed, so both option lists
            // are rebuilt from the reaction each refresh rather than once at construction.
            std::vector<std::string> froms{"no loop"};
            std::vector<std::string> tos;
            for (size_t i = 0; i < r->steps.size(); ++i)
            {
                froms.push_back("from " + std::to_string(i + 1));
                tos.push_back("to " + std::to_string(i + 1));
            }
            mLoopFrom->setOptions(froms);
            mLoopTo->setOptions(tos.empty() ? std::vector<std::string>{"to 1"} : tos);
            mLoopFrom->setSelectedIndex(r->loops() ? r->loopFrom + 1 : 0);
            if (r->loops())
                mLoopTo->setSelectedIndex(std::min(r->loopTo, (int)r->steps.size() - 1));
            mLoopCount->text = r->loopCount < 0 ? "∞" : "x" + std::to_string(r->loopCount);
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

    // The list rectangles. `trackBottom` leaves room for the scrubber and the footer row; the
    // track rows themselves start below the signal/cancel header. These four are the only place
    // either list's box is defined.
    // The loop controls live in the FOOTER band, beside the scrubber — which is where they
    // belong: both answer "how does this reaction play". They were tried on their own line under
    // the header and that line has to come out of the track list, which is the thing this panel
    // exists for. At a 640px window the list viewport is ~65px, and taking 26 of them left a row
    // unreachable at every offset (the G-20 defect this codebase has already shipped once) and
    // shrank the "drop below the last row for a new step" target (G-23) to about four pixels.
    // The footer band is already there and already empty on this side.
    bool ReactionsPanel::loopLineShown() const { return true; }
    double ReactionsPanel::trackTop() const { return metrics::pad() + kHeadH; }
    double ReactionsPanel::trackBottom() const
    {
        return height.value() - metrics::pad() - 30.0 - kScrubH;
    }
    double ReactionsPanel::reactionTop() const { return metrics::pad() + 18.0; }
    double ReactionsPanel::reactionBottom() const { return height.value() - metrics::pad() - 30.0; }

    void ReactionsPanel::layout(double w, double h)
    {
        width.set(w);
        height.set(h);
        const double pad = metrics::pad();
        const double rightX = kListW + pad;
        const double rightW = w - rightX - pad;

        // Inside the list COLUMN, not the panel: the column ends at the divider (kListW), so the
        // two buttons share `kListW - 2*pad` between them. Sizing them off kListW itself put their
        // right edge a whole `pad` past the divider and into the track panel.
        const double listInner = kListW - pad * 2.0;
        mAddReaction->x.set(pad);
        mAddReaction->y.set(h - pad - 22.0);
        mAddReaction->width.set(listInner * 0.55);
        mDeleteReaction->x.set(pad + listInner * 0.55 + 6.0);
        mDeleteReaction->y.set(h - pad - 22.0);
        mDeleteReaction->width.set(listInner * 0.45 - 6.0);

        // The buttons claim their space first (they are fixed, and their labels must fit),
        // then the two dropdowns share whatever is left — so they can never collide.
        double bx = w - pad;
        for (auto *b : {&mFire, &mAddTrack, &mAddStep})
        {
            const double bw = std::max(52.0, textWidth((*b)->text, type::body(), font::sansMedium()) + 16.0);
            bx -= bw;
            (*b)->x.set(bx);
            (*b)->y.set(pad - 2.0);
            (*b)->width.set(bw);
            bx -= 6.0;
        }
        const double dropdownRoom = std::max(0.0, bx - 8.0 - rightX);
        const double signalW = std::min(150.0, dropdownRoom * 0.55);
        const double cancelW = std::min(140.0, dropdownRoom - signalW - 8.0);
        mSignal->x.set(rightX);
        mSignal->y.set(pad - 2.0);
        mSignal->width.set(std::max(0.0, signalW));
        mSignal->visible = signalW > 40.0;
        mCancel->x.set(rightX + signalW + 8.0);
        mCancel->y.set(pad - 2.0);
        mCancel->width.set(std::max(0.0, cancelW));
        mCancel->visible = cancelW > 40.0;

        // The loop line, beneath the signal/cancel line and left-aligned with it. The two controls
        // that are meaningless without a range hide with it rather than showing a dead "to 1".
        const Reaction *rNow = current();
        const bool looping = rNow && rNow->loops();
        // The footer band already carries the scrubber's "scrub" caption on the left and its
        // percentage on the right; the middle is empty, so the controls go between them rather
        // than on top of either.
        const double loopY = h - pad - 22.0;
        const double loopLeft = rightX + 64.0;               // clear of the "scrub" caption
        const double loopRight = w - pad - 40.0;             // clear of the "0%" readout
        const double kLoopFromW = 92.0, kLoopToW = 70.0, kLoopCountW = 36.0;
        const bool showLoop =
            rNow != nullptr && loopLineShown() && loopRight - loopLeft > kLoopFromW;
        double lx = loopLeft;
        mLoopFrom->x.set(lx);
        mLoopFrom->y.set(loopY);
        mLoopFrom->width.set(kLoopFromW);
        mLoopFrom->visible = showLoop;
        lx += kLoopFromW + 6.0;
        mLoopTo->x.set(lx);
        mLoopTo->y.set(loopY);
        mLoopTo->width.set(kLoopToW);
        mLoopTo->visible = showLoop && looping && lx + kLoopToW <= loopRight;
        lx += kLoopToW + 6.0;
        mLoopCount->x.set(lx);
        mLoopCount->y.set(loopY);
        mLoopCount->width.set(kLoopCountW);
        mLoopCount->visible = showLoop && looping && lx + kLoopCountW <= loopRight;

        // Track columns: target | to | ms | easing — sized as fractions so they reflow.
        const double y0 = trackTop();
        const double lastRowBottom = trackBottom();
        // Both lists are measured every layout, so the bars appear exactly when there is
        // something to reach and never otherwise (FR-47) — and each is measured against the
        // box its rows are actually placed in, so the last row is always reachable (G-20).
        {
            const Reaction *r = current();
            const double content = (double)mRows.size() * kRowH +
                                   (r ? (double)r->steps.size() * kStepH : 0.0);
            mTrackScroll.measure(lastRowBottom - y0, content);
            const Shape *host = owner();
            mReactionScroll.measure(reactionBottom() - reactionTop(),
                                    host ? (double)host->reactions.size() * kRowH : 0.0);
        }
        double y = y0 - mTrackScroll.offset();
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
            // A row outside the list area is HIDDEN, not merely skipped when painting — its
            // widgets are real Segments, so a half-scrolled row would otherwise draw over the
            // column captions above or the scrubber below. It is all-or-nothing because these
            // are text fields: half a field is not editable, so there is nothing to gain from
            // showing one. Every row is still REACHABLE — `mTrackScroll` is measured against
            // exactly this box, so the last row lands flush with `lastRowBottom` at full scroll.
            const bool visibleRow = y >= y0 && y + kRowH <= lastRowBottom;
            double x = rightX + Columns::grip;   // the grip owns the row's left edge (G-23)
            auto place = [&](const std::shared_ptr<artboard::Segment> &seg, double cw) {
                seg->visible = visibleRow && cw > 0.0;
                if (!seg->visible)
                    return;
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
        // A range needs steps to name, so the control is dead until there are some (G-26).
        mLoopFrom->enabled = r != nullptr && !r->steps.empty();
        mLoopTo->enabled = r != nullptr && r->loops();
        mLoopCount->enabled = r != nullptr && r->loops();
        Segment::advance(nowMs);
    }

    bool ReactionsPanel::handleGesture(const artboard::Gesture &g, const artboard::Point &p)
    {
        const double pad = metrics::pad();
        const double listTop = pad + 18.0;
        if (p.x < kListW)
        {
            const Shape *host = owner();
            const int count = host ? (int)host->reactions.size() : 0;
            const int i = (int)std::floor((p.y - listTop + mReactionScroll.offset()) / kRowH);
            if (g.type == artboard::Gesture::Type::Move)
            {
                mHover.setHovered(i >= 0 && i < count ? i : -1);
                return true;
            }
            if (g.type == artboard::Gesture::Type::Click && i >= 0 && i < count)
            {
                mApp.selectReaction(i);
                return true;
            }
        }
        // Dragging a track by its grip (G-23). Tested BEFORE the drag-to-scroll below, since a
        // grip press means "move this row", not "scroll the list".
        if (g.type == artboard::Gesture::Type::Down)
        {
            const int row = gripAt(p);
            if (row >= 0)
            {
                mDrag = TrackDrag{};
                mDrag.active = true;
                mDrag.row = row;
                mDrag.y = p.y;
                dropTargetAt(p.y, mDrag.step, mDrag.index);
                return true;
            }
        }
        if (mDrag.active && (g.type == artboard::Gesture::Type::Drag ||
                             g.type == artboard::Gesture::Type::DragStart ||
                             g.type == artboard::Gesture::Type::Move))
        {
            mDrag.y = p.y;
            dropTargetAt(p.y, mDrag.step, mDrag.index);
            return true;
        }
        if (mDrag.active && (g.type == artboard::Gesture::Type::Drop ||
                             g.type == artboard::Gesture::Type::Up ||
                             g.type == artboard::Gesture::Type::Click))
        {
            const TrackDrag d = mDrag;
            mDrag = TrackDrag{};
            Reaction *r = current();
            if (r && d.row >= 0 && d.row < (int)mRows.size())
            {
                const TrackRow &row = mRows[(size_t)d.row];
                // A drop that would not move the track is not a document change, so it never
                // lands in the undo history (G-23).
                if (Document::moveTrack(*r, row.step, row.track, d.step, d.index))
                {
                    mApp.documentChanged();
                    mApp.status("Moved track to step " + std::to_string(std::min(d.step, (int)r->steps.size() - 1) + 1),
                                StatusLevel::Good);
                }
            }
            return true;
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
        // Each list scrolls on its own side, by wheel and by drag, so nothing is ever
        // truncated out of reach (FR-47).
        const double scrubTop = height.value() - pad - 22.0 - kScrubH;
        if (g.type == artboard::Gesture::Type::Scroll)
        {
            ListScroll &target = p.x < kListW ? mReactionScroll : mTrackScroll;
            if (p.x >= kListW && p.y >= scrubTop)
                return false;                    // over the scrubber: not a list
            if (!target.wheel(g.delta.y))
                return false;                    // it all fits: let it bubble
            layout(width.value(), height.value());
            return true;
        }
        if (g.type == artboard::Gesture::Type::Drag)
        {
            if (p.x < kListW)
            {
                mReactionScroll.drag((p.y - g.start.y) * 0.4);
                return true;
            }
            if (p.y < scrubTop)
            {
                mTrackScroll.drag((p.y - g.start.y) * 0.4);
                layout(width.value(), height.value());
                return true;
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
                    if (const Shape *host = owner())
                        mApp.runtime().scrub(host->id, r->signal, mScrubT);   // replay to that point
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

        // The heading names the object, because the panel IS that object's reactions.
        const Shape *titleHost = owner();
        drawSectionTitle(t, titleHost ? titleHost->id : std::string("Reactions"), pad, pad + 8.0);
        if (titleHost)
            drawFitted(t, "reactions", pad + textWidth(titleHost->id, type::micro(), font::sansSemiBold(), 1.1) + 8.0,
                       pad + 8.0, kListW - pad * 2.0, type::micro(), palette::mutedForeground(), font::sans());

        const Shape *host = owner();
        static const std::vector<Reaction> kNone;
        const auto &rs = host ? host->reactions : kNone;
        const double listTop = reactionTop();
        const double listBottom = reactionBottom();
        t.save();
        t.clipRect(0, listTop - 2.0, kListW, listBottom - listTop + 2.0);
        double y = listTop - mReactionScroll.offset();
        for (int i = 0; i < (int)rs.size(); ++i, y += kRowH)
        {
            if (y + kRowH < listTop || y > listBottom) continue;
            const bool sel = i == mApp.selectedReaction();
            const double hover = mHover.amount(i);
            if (sel)
                artboard::drawRoundedRect(t, {4, y, kListW - 8, kRowH - 3}, radius::hairline(),
                                          artboard::Paint::filled(palette::selectedWash(1.0)));
            else if (hover > 0.01)
                artboard::drawRoundedRect(t, {4, y, kListW - 8, kRowH - 3}, radius::hairline(),
                                          artboard::Paint::filled(palette::hoverWash(hover)));
            // Only claim a reaction is running when the preview is actually live.
            const bool running = mApp.previewOk() && host &&
                                 mApp.runtime().isRunning(host->id, rs[(size_t)i].signal);
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
        t.restore();
        mReactionScroll.drawBar(t, {0, listTop - 2.0, kListW, listBottom - listTop + 2.0});

        if (!host)
        {
            drawFitted(t, "No object selected", pad, listTop + 14.0, kListW - pad * 2.0,
                       type::small(), palette::mutedForeground(), font::sans());
            drawFitted(t, "Pick one to see its reactions.", pad, listTop + 30.0, kListW - pad * 2.0,
                       type::micro(), palette::mutedForeground(), font::sans());
        }
        else if (rs.empty())
        {
            drawFitted(t, "No reactions", pad, listTop + 14.0, kListW - pad * 2.0, type::small(),
                       palette::mutedForeground(), font::sans());
            drawFitted(t, "Add one to make " + host->id + " move.", pad, listTop + 30.0,
                       kListW - pad * 2.0, type::micro(), palette::mutedForeground(), font::sans());
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

        // Column captions, aligned with the field row layout: 6px of air under the header buttons
        // (which end at pad + 20) and 8px above the first row, so the band separates the two.
        const double y0 = trackTop();
        const double capY = y0 - 8.0;
        const Columns c = columns(w);
        {
            double cx = rightX + Columns::grip;   // the captions sit over the FIELDS, past the grip
            const char *caps[] = {"target", "from", "to", "ms", "delay", "easing"};
            const double widths[] = {c.target, c.from, c.to, c.ms, c.delay, c.easing};
            for (int i = 0; i < 6; ++i)
            {
                if (widths[i] <= 0.0) continue;   // this column was dropped at this width
                drawFitted(t, caps[i], cx, capY, widths[i], type::micro(),
                           palette::mutedForeground(), font::sans());
                cx += widths[i] + c.gap;
            }
            if (c.showChips)
                drawFitted(t, "repeat", w - pad - kChipW * kChips, capY, kChipW * kChips - 4.0,
                           type::micro(), palette::mutedForeground(), font::sans());
        }

        // Clipped to the list box: a step separator may be scrolled half out of view, and
        // without this it would draw over the captions above or the scrubber below.
        const double trackClipTop = y0 - 2.0;
        const double trackClipBottom = trackBottom();
        t.save();
        t.clipRect(rightX - 2.0, trackClipTop, rightW + 4.0, trackClipBottom - trackClipTop);
        double ry = y0 - mTrackScroll.offset();
        int lastStep = -1;
        // Once per paint, not once per row: every row of a step reads the same entry.
        std::vector<Runtime::StepTiming> stepTimes;
        if (const Shape *host = owner())
            if (const Reaction *cr = current())
                stepTimes = mApp.runtime().stepTimings(host->id, cr->signal);
        for (const auto &row : mRows)
        {
            if (row.step != lastStep)
            {
                lastStep = row.step;
                // Step separator on its OWN row, so the chain reads as "this, then that"
                // without ever sitting on top of a track.
                const Reaction *lr = current();
                const bool inLoop = lr && lr->loops() && row.step >= lr->loopFrom &&
                                    row.step <= lr->loopTo;
                std::string label =
                    row.step == 0 ? "step 1" : "then step " + std::to_string(row.step + 1);
                // G-26. The range lives on the reaction, but it is the STEPS that repeat, so it
                // has to be legible here — a range you can set and not see is a range you will
                // set wrong. The last step of the range says where it goes back to and how often,
                // because that is the moment the jump happens.
                // Plain words and the arrow `trackSummary` already uses. A rounder glyph would
                // read better and the UI font does not have one — it drew as a tofu box, which
                // is worse than no marker at all.
                if (inLoop && row.step == lr->loopTo)
                    label += lr->loopFrom == lr->loopTo
                                 ? "  loop"
                                 : "  loop → " + std::to_string(lr->loopFrom + 1);
                drawFitted(t, label, rightX, ry + 12.0, 150.0, type::micro(), palette::primary(),
                           font::sansMedium());
                double lx = rightX + textWidth(label, type::micro(), font::sansMedium()) + 8.0;

                // G-27. A chain is authored as durations and delays but WATCHED as a timeline, and
                // the arithmetic between the two is where the surprises live — one delay buried in
                // one track moves every step after it. So each header says how long it takes and
                // when it starts, from the live values, so a `speed` param re-times these too.
                if (row.step < (int)stepTimes.size())
                {
                    const Runtime::StepTiming &tm = stepTimes[(size_t)row.step];
                    const std::string ms =
                        tm.endless ? "endless" : fmtMs(tm.durationMs) + " @ " + fmtMs(tm.startMs);
                    const double mw = textWidth(ms, type::micro(), font::mono()) + 4.0;
                    if (lx + mw < w - pad - 30.0)
                    {
                        // An endless step is not slow, it is a dead end — nothing after it ever
                        // runs — so it is coloured as the warning it is rather than as data.
                        drawFitted(t, ms, lx, ry + 12.0, mw, type::micro(),
                                   tm.endless ? palette::warning() : palette::mutedForeground(),
                                   font::mono());
                        lx += mw + 8.0;
                    }
                }
                const double lineW = std::max(0.0, w - pad - lx);
                artboard::drawRoundedRect(t, {lx, ry + 8.0, lineW, 1.0}, 0.0,
                                          artboard::Paint::filled(inLoop ? palette::primaryAlpha(0.5)
                                                                         : palette::border()));
                if (inLoop && row.step == lr->loopTo)
                {
                    const std::string n = lr->loopCount < 0 ? "∞" : "×" + std::to_string(lr->loopCount);
                    const double nw = textWidth(n, type::micro(), font::sansMedium()) + 4.0;
                    drawFitted(t, n, w - pad - nw, ry + 12.0, nw, type::micro(), palette::primary(),
                               font::sansMedium());
                }
                ry += kStepH;
            }
            // The grip: two short rules, the conventional "grab me" mark. It brightens while
            // this row is the one being dragged, so the pointer always has an anchor (G-23).
            if (row.target->visible)
            {
                const bool held = mDrag.active && mDrag.row == (int)(&row - &mRows[0]);
                const double gx = rightX + 3.0;
                artboard::Paint bar = artboard::Paint::filled(
                    held ? palette::primary() : palette::whiteAlpha(0.22));
                for (int k = 0; k < 2; ++k)
                    artboard::drawRoundedRect(t, {gx + (double)k * 4.0, ry + 8.0, 2.0, kRowH - 16.0},
                                              radius::pill(), bar);
            }
            // The three chips at the row's right edge: repeat count, yoyo, remove. Drawn only
            // when the row's fields are shown, so chips can never float beside a hidden row.
            const Reaction *rr = current();
            if (c.showChips && row.target->visible && rr && row.step < (int)rr->steps.size() &&
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
        // Where the dragged row would land, and a ghost of the row itself under the pointer.
        // Both inside the list clip, so neither can escape over the captions or the scrubber.
        if (mDrag.active && mDrag.row >= 0 && mDrag.row < (int)mRows.size())
        {
            double lineY = y0 - mTrackScroll.offset();
            int seen = -1;
            for (const auto &row : mRows)
            {
                if (row.step != seen) { seen = row.step; lineY += kStepH; }
                if (row.step == mDrag.step && row.track == mDrag.index) break;
                lineY += kRowH;
            }
            if (mDrag.step >= (int)(current() ? current()->steps.size() : 0))
                lineY += kStepH;   // a new final step gets its own header row first
            artboard::drawRoundedRect(t, {rightX, lineY - 1.5, rightW, 3.0}, radius::pill(),
                                      artboard::Paint::filled(palette::primary()));

            const TrackRow &row = mRows[(size_t)mDrag.row];
            const std::string label =
                row.target->text.empty() ? std::string("track") : row.target->text;
            const double gw = std::min(rightW, 150.0);
            // Opaque, not a wash: a drag ghost is the one thing allowed to sit over the rows, so
            // it has to be readable rather than let the label underneath bleed through.
            artboard::drawRoundedRect(t, {rightX + 10.0, mDrag.y - 10.0, gw, 20.0}, radius::hairline(),
                                      artboard::Paint::filledStroked(palette::popover(),
                                                                     palette::primary(), 1.0));
            drawFitted(t, label, rightX + 18.0, mDrag.y + 4.0, gw - 16.0, type::micro(),
                       palette::foreground(), font::sansMedium());
        }
        t.restore();
        if (mRows.empty())
            drawFitted(t, "No tracks yet — add one, then pick what it animates.", rightX, y0 + 14.0,
                       rightW, type::small(), palette::mutedForeground(), font::sans());

        mTrackScroll.drawBar(t, {rightX, trackClipTop, rightW, trackClipBottom - trackClipTop});

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
        // G-27: the chain's own length, beside the scrubber it scrubs. The per-step numbers say
        // where the time goes; this says how much there is.
        std::string scrubLabel = "scrub";
        if (const Shape *sh = owner())
            if (const Reaction *cr = current())
            {
                const auto times = mApp.runtime().stepTimings(sh->id, cr->signal);
                double total = 0.0;
                bool endless = false;
                for (const auto &st : times) { total += st.durationMs; endless = endless || st.endless; }
                if (!times.empty())
                    scrubLabel += endless ? "  endless" : "  " + fmtMs(total);
            }
        drawFitted(t, scrubLabel, rightX, scrubY + 26.0, 120.0, type::micro(),
                   palette::mutedForeground(), font::sans());
    }
}
}
