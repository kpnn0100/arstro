/*
 *  Cosmo by arstro — SettingsPanel: app/engine settings. Preview quality (render
 *  resolution — speed vs detail) and CPU thread count for the multicore engine.
 *  Both map directly onto the RenderService / parallel layer.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>
#include <memory>

namespace arstro
{
namespace cosmo
{
    class SettingsPanel : public artboard::Segment
    {
    public:
        SettingsPanel(const artboard::Theme &theme, const artboard::Color &accent);

        std::function<void(int)> onPreviewEdge;  // preview render long-edge in px
        std::function<void(int)> onThreads;      // engine threads (0 = auto)

        void layout(double w, double h);

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        artboard::Color mAccent;
        std::shared_ptr<artboard::ComboBox> mQuality;
        std::shared_ptr<artboard::ComboBox> mThreads;
        double mRowY[2] = {0, 0};
    };
}
}
