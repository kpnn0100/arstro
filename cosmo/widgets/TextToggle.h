/*
 *  Cosmo by arstro — TextToggle: a text-only boolean toggle. The label is greyed
 *  out when off and animates to a bold, accent-coloured state when on (smooth
 *  transition). Used for the log/linear toggles ("log").
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>
#include <string>

namespace arstro
{
namespace cosmo
{
    class TextToggle : public artboard::Segment
    {
    public:
        TextToggle(const std::string &text, const artboard::Color &accent);

        std::function<void(bool)> onChange;
        bool on() const { return mOn; }
        void setOn(bool on);  // snap (animated), no callback

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &localPoint) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        std::string mText;
        artboard::Color mAccent;
        bool mOn = false;
        double mNowMs = 0.0;
        artboard::Property mAnim{0.0};  // 0 = off (grey), 1 = on (accent + bold)
    };
}
}
