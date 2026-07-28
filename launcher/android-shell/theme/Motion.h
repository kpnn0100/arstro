/*
 *  arstro-android-shell — Motion (M2.2): shell-named motion tokens over the AB-6 primitives.
 *
 *  Thin aliases over `artboard::motion::` (the named durations + Spring settle-speed presets from
 *  AB-6, FR-31) and the five named cubic-bezier easings, given Android-surface names so shell code
 *  reads intent ("panel open") not a raw token. No new motion mechanism — everything drives the
 *  existing AnimatedProperty / Spring, so reduced-motion (FR-4e) is honoured for free.
 */
#pragma once
#include "artboard/artboard.h"

namespace arstro
{
namespace androidshell
{
namespace motion
{
    namespace ab = artboard::motion;

    // Durations (ms), named for shell use (plan §2.2 motion tokens).
    constexpr double kPressMs = ab::kDurationShort2;       // 100 — button/tile press response
    constexpr double kStateMs = ab::kDurationShort4;       // 200 — control state transitions
    constexpr double kPanelCloseMs = ab::kDurationMedium1; // 250 — shade/panel close
    constexpr double kPanelOpenMs = ab::kDurationMedium3;  // 350 — shade/panel open, tile morph

    // Easings (the five AB-6 cubic-beziers) named by role.
    constexpr artboard::Easing kStandard = artboard::Easing::Standard;
    constexpr artboard::Easing kStandardAccel = artboard::Easing::StandardAccel;    // exits
    constexpr artboard::Easing kEmphasizedDecel = artboard::Easing::EmphasizedDecel; // entrances (panels)

    // Spring settle-speed presets (omega) for followers.
    constexpr double kSpatialFast = ab::kSpatialFast;       // snappy position/size (press, drag catch-up)
    constexpr double kSpatialDefault = ab::kSpatialDefault; // default position/size glide
    constexpr double kEffectsDefault = ab::kEffectsDefault; // colour/opacity blends

} // namespace motion
} // namespace androidshell
} // namespace arstro
