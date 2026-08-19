/*
 *  Arstrobench by arstro — SystemPanel: the machine the score belongs to (R-SYS-1).
 *
 *  Three fixed rows -- chip, memory, operating system. The row count is fixed and the
 *  panel is sized to hold all three, so this panel structurally cannot overflow its box
 *  (R-UI-1); if a fourth row is ever added, it needs the clip-and-scroll treatment first.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "../core/SystemInfo.h"

namespace arstro
{
namespace arstrobench
{
    class SystemPanel : public artboard::Segment
    {
    public:
        SystemPanel();

        void enter(double finalY, double delayMs, double nowMs);
        void setInfo(const SystemInfo &info) { mInfo = info; }
        const SystemInfo &info() const { return mInfo; }

        /** Baseline (local y) of row `i` in [0,3) — the layout's one source of truth, so
         *  the paint and the tests read the same numbers. */
        static double rowBaseline(int i);
        static constexpr int kRows = 3;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        SystemInfo mInfo;
    };
}
}
