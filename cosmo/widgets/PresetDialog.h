/*
 *  cosmo_v2 by arstro — PresetDialog: a modal, centred category picker shown before
 *  a preset Save / Export / Import (R-PRESETPICK). It lists a "Select all" master
 *  toggle plus one checkbox row per category, and a Cancel / Confirm footer, and
 *  returns the ticked keys on confirm. Ported from cosmo's PresetDialog, restyled
 *  to the cosmo_v2 theme and given a fade in/out (R-G-1: nothing pops).
 *
 *  It is preset-format agnostic: the caller supplies rows of {key,label,checked}
 *  (keys are the generic .apf category names from EditParamsApf) and gets back the
 *  ticked keys on Confirm. While open it is modal (draws in the overlay pass on top
 *  of everything; a click outside the card cancels).
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "HoverFade.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class PresetDialog : public artboard::Segment
    {
    public:
        struct Row { std::string key; std::string label; bool checked = true; };
        explicit PresetDialog(const artboard::Color &accent);

        /** Open with a title, the confirm-button label, and the category rows.
         *  onConfirm receives the keys that are ticked. */
        void show(const std::string &title, const std::string &confirmLabel,
                  std::vector<Row> rows, std::function<void(std::vector<std::string>)> onConfirm);
        /** True while the modal is capturing input (not while fading out). */
        bool isOpen() const { return mOpen && !mClosing; }

    protected:
        void advance(double nowMs) override;
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return mOpen && !mClosing; }  // modal

    private:
        void beginClose();
        artboard::Rect cardRect() const;
        double rowTop(int i) const;      // top y of category row i
        artboard::Rect selectAllRect() const;
        artboard::Rect confirmRect() const;
        artboard::Rect cancelRect() const;
        bool allChecked() const;
        void hitTargets(const artboard::Point &p, int &row, int &btn) const;  // row: -2 none/-1 selectAll/>=0 category; btn: -1/0 cancel/1 confirm

        artboard::Color mAccent;
        bool mOpen = false;
        bool mClosing = false;
        double mLastMs = 0.0;
        artboard::AnimatedProperty mAppear{0.0};
        std::string mTitle, mConfirmLabel;
        std::vector<Row> mRows;
        std::function<void(std::vector<std::string>)> mOnConfirm;
        // Per-item hover cross-fade (R-G-3). Rows: id 0 = Select-all, id i+1 = category i.
        // Buttons: id 0 = Cancel, id 1 = Confirm.
        HoverFade mRowHover;
        HoverFade mBtnHover;
    };
}
}
