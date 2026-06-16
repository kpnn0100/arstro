/*
 *  Pulsar by arstro — the app: three OscillatorPanels (OSC1/2/3) of identical
 *  design, laid out left-to-right. OSC2 is snapped to the right edge of OSC1 and
 *  OSC3 to the right edge of OSC2 (Artboard's Segment snap), so the rack stays
 *  glued together — move OSC1 and the others follow.
 *
 *  Platform-free: only Artboard's Segment/IRenderTarget. (Audio + macro/LFO are
 *  reserved for a later pass.)
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include "OscillatorPanel.h"
#include <array>
#include <memory>

namespace arstro
{
namespace pulsar
{
    class PulsarApp
    {
    public:
        PulsarApp(double width, double height);

        void render(artboard::IRenderTarget &target, double nowMs);
        void pointer(int kind, double x, double y, int button, double timeMs);

    private:
        double mW, mH;
        artboard::Color mTitleColor;
        std::shared_ptr<artboard::Segment> mRoot;
        std::array<std::shared_ptr<OscillatorPanel>, 3> mOsc;
        artboard::GestureRecognizer mRecognizer;
    };
}
}
