/*
 *  interstellar_core — Timeline: the cut, and only the cut (R-CUT).
 *
 *  Every operation here is reachable as a `Command`, so a sequence can be cut from a shell with
 *  no display and the whole of R-CUT-3 is provable by one committed script (R-CUT-7).
 *
 *  A clip carries no colour and this class never touches any: it references a rack node and the
 *  colour is that node's (R-COSMO-2). `Project::fieldIsColour` is what refuses one.
 */
#pragma once
#include "Project.h"
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
    class Timeline
    {
    public:
        explicit Timeline(Project &p) : mP(p) {}

        Track *addTrack(bool audio, const std::string &name, int order, std::string &err);
        Clip *addClip(const std::string &trackRef, const std::string &src, double in, double out,
                      double at, const std::string &name, std::string &err);
        bool trim(const std::string &clipRef, bool head, double t, std::string &err);
        /** Split at an absolute time: two clips, new ids, never reused (R-FMT-5). */
        bool split(const std::string &clipRef, double at, std::string &err);
        bool move(const std::string &clipRef, double at, const std::string &trackRef, std::string &err);
        /** Roll the edit point between two adjacent clips: one's out and the other's in move
         *  together, so the cut moves and the sequence length does not. */
        bool roll(const std::string &clipA, const std::string &clipB, double dt, std::string &err);
        /** Slip: the source window slides inside a clip whose timeline position is unchanged. */
        bool slip(const std::string &clipRef, double dt, std::string &err);
        bool remove(const std::string &clipRef, bool ripple, std::string &err);
        Transition *addTransition(const std::string &clipA, const std::string &clipB,
                                  const std::string &kind, double dur, std::string &err);

        /** The next and previous cut point from `t` — every clip edge, in order. What
         *  `playhead next-cut` uses, and what a view snaps to. */
        double nextCut(double t) const;
        double prevCut(double t) const;
        std::vector<double> cutPoints() const;

    private:
        Project &mP;
    };
}
}
