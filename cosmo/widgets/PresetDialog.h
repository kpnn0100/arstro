/*
 *  Cosmo by arstro — PresetDialog: a modal, centred popup that lets the user pick
 *  WHICH preset categories to include before a save / export / import. It shows a
 *  "Select all" master toggle plus one checkbox row per category, and a Cancel /
 *  Confirm footer. While open it is modal (draws in the overlay pass on top of
 *  everything; a click outside the card cancels). The host sizes it to cover the
 *  root so the card can be centred and clicks located.
 *
 *  It is preset-format agnostic: the caller supplies rows of {key,label,checked}
 *  and gets back the keys that are ticked on Confirm. Categories map to the generic
 *  .apf envelope (see ImageProcessing EditParamsApf).
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo
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
        void hide() { mOpen = false; }
        bool isOpen() const { return mOpen; }

    protected:
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return mOpen; }  // modal

    private:
        artboard::Rect cardRect() const;
        double rowTop(int i) const;      // top y of category row i (card space resolved)
        artboard::Rect selectAllRect() const;
        artboard::Rect confirmRect() const;
        artboard::Rect cancelRect() const;
        bool allChecked() const;

        artboard::Color mAccent;
        bool mOpen = false;
        std::string mTitle, mConfirmLabel;
        std::vector<Row> mRows;
        std::function<void(std::vector<std::string>)> mOnConfirm;
    };
}
}
