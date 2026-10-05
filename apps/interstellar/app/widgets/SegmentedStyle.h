/*
 *  interstellar_v1 — the look of every segmented picker in the app (Deliver's codec row, Grade's
 *  Sources | Curves): cosmo's SegmentedControl, dressed once, so two pickers cannot drift apart.
 */
#pragma once
#include "../Theme.h"
#include "../../../cosmo/widgets/SegmentedControl.h"

namespace arstro
{
namespace interstellar_v1
{
    inline void styleSegmented(cosmo_v2::SegmentedControl &s)
    {
        using namespace artboard;
        s.containerBox = {Paint::filledStroked(palette::segmentedBg(), palette::border(), 1.0), radius::control()};
        s.idleSegBox = {Paint{}, radius::hairline()};
        s.activeSegBox = {Paint::filled(palette::primary()), radius::hairline()};
        s.edgeRadius = radius::control();
        s.idleText = {palette::mutedForeground(), 10.0, font::sans()};
        s.activeText = {palette::white(), 10.0, font::sans()};
        s.padding = 2.0;
    }
}
}
