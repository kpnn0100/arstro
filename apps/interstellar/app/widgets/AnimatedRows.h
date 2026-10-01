/*
 *  interstellar_v1 — AnimatedRows<T>: list insert / remove / reorder that TRAVELS (design rule §1).
 *
 *  "List insert/remove" is on the law's list of visible changes, and a self-drawn list that simply
 *  draws `model.rack[i]` at `i × rowH` breaks it the moment the model changes shape: a row added
 *  above shoves every row below it down in one frame, a deleted render job vanishes and its
 *  neighbours jump up. So each row is keyed by its model identity (a rack object id, a job id) and
 *  carries its own eased y and alpha:
 *
 *   * a row that moves (something was inserted above it) eases to its new y (200 ms);
 *   * a NEW row fades in at its slot — except on the very first sync, where there is nowhere to
 *     travel from and an entrance nobody asked for would be its own bug ("first placement sets");
 *   * a REMOVED row is kept as a ghost with a copy of its last data and fades out, then is
 *     dropped — so the list never deletes a picture in one frame.
 *
 *  Header-only and type-generic: rows are self-drawn by their owner, which iterates `rows()` and
 *  reads `y()`/`alpha()`. Hit-testing uses only live rows (`gone == false`).
 */
#pragma once
#include "../Theme.h"
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace interstellar_v1
{
    template <class T>
    class AnimatedRows
    {
    public:
        struct Row
        {
            std::string key;
            T data{};
            int index = -1;              // position in the current model order; -1 = ghost
            double yTarget = 0.0, aTarget = 1.0, yLast = 0.0, aLast = 1.0;
            artboard::AnimatedProperty y{0.0}, alpha{0.0};
            bool placed = false, gone = false;
            double liveY() const { return y.value(); }
            double liveAlpha() const { return alpha.value(); }
        };

        /** Re-key the list to `items` (display order). Row height is uniform. */
        void sync(const std::vector<std::pair<std::string, T>> &items, double rowH)
        {
            for (auto &r : mRows) { r.index = -1; }
            for (int i = 0; i < (int)items.size(); ++i)
            {
                Row *row = find(items[i].first);
                if (!row)
                {
                    mRows.emplace_back();
                    row = &mRows.back();
                    row->key = items[i].first;
                    row->placed = false;
                }
                row->data = items[i].second;
                row->index = i;
                row->yTarget = i * rowH;
                row->aTarget = 1.0;
                row->gone = false;
            }
            for (auto &r : mRows)
                if (r.index < 0) { r.gone = true; r.aTarget = 0.0; }
            mCount = (int)items.size();
            mSynced = true;
        }

        void advance(double nowMs)
        {
            for (auto &r : mRows)
            {
                if (!r.placed)
                {
                    r.y.set(r.yTarget);
                    r.yLast = r.yTarget;
                    if (mEverAdvanced && !artboard::reducedMotion())
                    {
                        r.alpha.set(0.0);
                        r.alpha.animateTo(1.0, motion::kCrossFadeMs, artboard::Easing::EaseOutCubic, nowMs);
                    }
                    else
                        r.alpha.set(1.0);
                    r.aLast = 1.0;
                    r.placed = true;
                }
                if (r.yTarget != r.yLast)
                {
                    r.y.animateTo(r.yTarget, motion::kSelectMs, artboard::Easing::EaseOutCubic, nowMs);
                    r.yLast = r.yTarget;
                }
                if (r.aTarget != r.aLast)
                {
                    r.alpha.animateTo(r.aTarget, motion::kCrossFadeMs, artboard::Easing::EaseOutCubic, nowMs);
                    r.aLast = r.aTarget;
                }
                r.y.update(nowMs);
                r.alpha.update(nowMs);
            }
            mRows.erase(std::remove_if(mRows.begin(), mRows.end(),
                                       [](const Row &r) { return r.gone && !r.alpha.isAnimating() && r.alpha.value() <= 0.001; }),
                        mRows.end());
            if (mSynced) mEverAdvanced = true;
        }

        bool moving() const
        {
            for (const auto &r : mRows)
                if (r.y.isAnimating() || r.alpha.isAnimating()) return true;
            return false;
        }

        std::vector<Row> &rows() { return mRows; }
        const std::vector<Row> &rows() const { return mRows; }
        int count() const { return mCount; }
        const Row *byIndex(int i) const
        {
            for (const auto &r : mRows) if (r.index == i) return &r;
            return nullptr;
        }

    private:
        Row *find(const std::string &key)
        {
            for (auto &r : mRows) if (r.key == key && !r.gone) return &r;
            for (auto &r : mRows) if (r.key == key) return &r;   // a ghost coming back
            return nullptr;
        }
        std::vector<Row> mRows;
        int mCount = 0;
        bool mSynced = false, mEverAdvanced = false;
    };
}
}
