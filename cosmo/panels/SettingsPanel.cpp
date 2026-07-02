#include "SettingsPanel.h"
#include "../Chrome.h"
#include "../CosmoTheme.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace
    {
        constexpr double kHeaderH = 34.0;
        constexpr double kPad = 12.0;
        constexpr double kLabelW = 72.0;
        const int kEdges[4] = {800, 1280, 1800, 2600};   // Draft / Standard / High / Max
        const int kThreads[5] = {0, 1, 2, 4, 8};         // Auto / 1 / 2 / 4 / 8
        const int kSteps[5] = {25, 50, 100, 250, 500};   // history depth
        const int kMerge[4] = {0, 250, 450, 900};        // Off / Fast / Normal / Slow (ms)
    }

    SettingsPanel::SettingsPanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        width.set(300.0);
        height.set(160.0);

        mQuality = std::make_shared<ComboBox>(theme.combo);
        mQuality->setOptions({"Draft", "Standard", "High", "Max"});
        mQuality->setSelectedIndex(2);  // High
        mQuality->onChange = [this](int i) { if (onPreviewEdge) onPreviewEdge(kEdges[i]); };
        addChild(mQuality);

        mThreads = std::make_shared<ComboBox>(theme.combo);
        mThreads->setOptions({"Auto", "1", "2", "4", "8"});
        mThreads->setSelectedIndex(0);  // Auto
        mThreads->onChange = [this](int i) { if (onThreads) onThreads(kThreads[i]); };
        addChild(mThreads);

        mHistorySteps = std::make_shared<ComboBox>(theme.combo);
        mHistorySteps->setOptions({"25", "50", "100", "250", "500"});
        mHistorySteps->setSelectedIndex(2);  // 100
        mHistorySteps->onChange = [this](int i) { if (onHistorySteps) onHistorySteps(kSteps[i]); };
        addChild(mHistorySteps);

        mHistoryMerge = std::make_shared<ComboBox>(theme.combo);
        mHistoryMerge->setOptions({"Off", "Fast", "Normal", "Slow"});
        mHistoryMerge->setSelectedIndex(2);  // Normal
        mHistoryMerge->onChange = [this](int i) { if (onHistoryCoalesce) onHistoryCoalesce(kMerge[i]); };
        addChild(mHistoryMerge);
    }

    void SettingsPanel::layout(double w, double h)
    {
        width.set(w);
        height.set(h);
        const double rowH = 30.0;
        const double cx = kLabelW, cw = w - kLabelW - kPad;
        auto place = [&](std::shared_ptr<ComboBox> &c, int row) {
            const double top = kHeaderH + 6.0 + row * rowH;
            mRowY[row] = top + rowH * 0.5 + 3.0;
            c->x.set(cx); c->y.set(top + (rowH - 24.0) * 0.5);
            c->width.set(cw > 40 ? cw : 40); c->height.set(24.0);
        };
        place(mQuality, 0);
        place(mThreads, 1);
        place(mHistorySteps, 2);
        place(mHistoryMerge, 3);
    }

    void SettingsPanel::onPaint(IRenderTarget &t) const
    {
        drawPanelChrome(t, width.value(), height.value(), "SETTINGS");
        t.setFill(palette::muted());
        t.drawText("preview", 12.0, mRowY[0], 10.0);
        t.drawText("threads", 12.0, mRowY[1], 10.0);
        t.drawText("history", 12.0, mRowY[2], 10.0);
        t.drawText("merge", 12.0, mRowY[3], 10.0);
        // honest acceleration note (multicore CPU, not GPU)
        t.setFill(palette::faint());
        t.drawText("acceleration: CPU, all cores", 12.0, height.value() - 14.0, 10.0);
    }
}
}
