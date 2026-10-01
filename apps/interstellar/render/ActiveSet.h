/*
 *  interstellar_render — ActiveSet: which video clips are live at timeline time t, which source
 *  frame each one reads, and how much each one weighs.
 *
 *  It takes plain spans rather than the project model on purpose: the model is being written in
 *  parallel, and a render path that reached into it would be untestable until the model settled
 *  and would be free to read fields nobody meant it to (R-RENDER-2 wants a pure function of what
 *  it is handed).
 *
 *  ── The one thing it must get right (R-TL-4) ──
 *
 *  A transition HOLDS the outgoing clip past its out-point for the transition's duration, reading
 *  its handles, with the outgoing weight going 1 -> 0, the incoming 0 -> 1, and the two SUMMING TO
 *  1. Two clips that merely abut leave nothing to dissolve FROM: the first version computed the
 *  weights correctly and still faded the incoming clip up over black, because by the time the
 *  transition started the outgoing clip had already ended and was not in the set at all.
 *
 *  Source frames are picked by NEAREST NEIGHBOUR, floor(localTime * fps), and that is stated
 *  rather than dressed up as interpolation. A held clip keeps counting into its handles; a source
 *  with no handles is the decoder's to clamp (it freezes on the last frame rather than going black).
 */
#pragma once
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
namespace render
{
    /** One clip on the timeline. `at` is where it starts on the timeline; `in`/`out` are its source
     *  range in seconds; `speed` > 0 (a clip with speed <= 0 is ignored — reverse and freeze are
     *  temporal effects, R-FX-2, not a sign on a number). Higher `trackOrder` draws on top. */
    struct ClipSpan
    {
        std::string id;
        int trackOrder = 0;
        bool audio = false;
        double at = 0, in = 0, out = 0, speed = 1.0;
    };

    /** A transition from `clipA` (outgoing) to `clipB` (incoming). It starts at clipB's `at` and
     *  lasts `dur` seconds. `linear` is the dissolve — a LINEAR alpha ramp; `linear == false` is a
     *  smoothstep ease, whose weights still sum to 1. */
    struct TransitionSpan
    {
        std::string clipA, clipB;
        double dur = 0;
        bool linear = true;
    };

    struct Active
    {
        std::string id;
        double localTime = 0;        // seconds into the SOURCE: (t - at) * speed + in
        long long sourceFrame = 0;   // floor(localTime * fps), nearest neighbour
        double weight = 1.0;         // transition weight; 1 outside any transition
        bool held = false;           // live only because a transition holds it past its out-point
        double progress = 0;         // 0..1 through the clip's own timeline span (>1 while held)
        /** Set on the INCOMING clip of a transition when the outgoing clip is the entry right
         *  before it in this list. Copy it to `Layer::dissolveWithPrevious`, so the composite mixes
         *  the pair against one base and the weights really do sum to 1 on screen. */
        bool dissolveWithPrevious = false;
    };

    /** The video clips live at `t`, bottom track first (then by `at`, so an outgoing clip sits
     *  under its incoming partner on the same track). Audio clips are excluded. A transition whose
     *  clips cannot both be found among the video spans is ignored rather than half-applied. */
    std::vector<Active> activeAt(const std::vector<ClipSpan> &clips,
                                 const std::vector<TransitionSpan> &transitions, double t, double fps);
}
}
}
