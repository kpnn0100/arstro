/*
 *  Cosmo by arstro — ToneCurvePanel: the tone-curve section. An interactive
 *  CurveEditor plus a log/linear domain toggle. Forwards changes via callbacks.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "../widgets/CurveEditor.h"
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

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        artboard::Color mAccent;
        std::shared_ptr<CurveEditor> mCurve;
        std::shared_ptr<artboard::ToggleSwitch> mLogToggle;
    };
}
}
