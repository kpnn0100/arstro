/*
 *  Cosmo by arstro — PresetBar: a fixed row of three labelled buttons (SAVE,
 *  IMPORT, EXPORT) that snaps to the bottom of the right-hand edit column. Each
 *  fires its callback; SAVE/EXPORT first raise the category picker (PresetDialog),
 *  IMPORT opens a file then the picker. Purely a set of buttons — the host owns
 *  the preset logic and the file dialogs.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>

namespace arstro
{
namespace cosmo
{
    class PresetBar : public artboard::Segment
    {
    public:
        explicit PresetBar(const artboard::Color &accent);

        std::function<void()> onSave;
        std::function<void()> onImport;
        std::function<void()> onExport;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        artboard::Rect btnRect(int i) const;  // 0=save, 1=import, 2=export (local space)

        artboard::Color mAccent;
        int mPressed = -1;
    };
}
}
