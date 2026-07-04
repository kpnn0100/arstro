/*
 *  Cosmo by arstro — ToneCurvePanel: the tone-curve section. An interactive
 *  CurveEditor plus an animated text log/linear toggle. Forwards changes via
 *  callbacks. Responsive via layout(w,h).
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "../widgets/CurveEditor.h"
#include "../widgets/TextToggle.h"
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace arstro
{
namespace cosmo
{
    class ToneCurvePanel : public artboard::Segment
    {
    public:
        ToneCurvePanel(const artboard::Theme &theme, const artboard::Color &accent);

        std::function<void(const std::vector<std::pair<float, float>> &)> onCurveChange;
        std::function<void(bool)> onLogChange;

        void setCurve(const std::vector<std::pair<float, float>> &pts) { mCurve->setPoints(pts); }
        void setLog(bool log) { mLogToggle->setOn(log); }
        void setHistogram(std::vector<float> bins) { mCurve->setHistogram(std::move(bins)); }
        void layout(double w, double h);

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        artboard::Color mAccent;
        std::shared_ptr<CurveEditor> mCurve;
        std::shared_ptr<TextToggle> mLogToggle;
        std::shared_ptr<artboard::Button> mReset;
    };
}
}
