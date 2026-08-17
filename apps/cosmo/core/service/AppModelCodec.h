/*
 *  Arstro cosmo_core — AppModelCodec: the AppModel as text.
 *
 *  This is what `cosmo-cc state print` emits and what R-SVC-9's equality check compares: a
 *  project opened by the CLI and the same project opened by clicking must dump the SAME
 *  text. That is the whole "both front ends look at the same thing" claim, reduced to a
 *  diff.
 *
 *  Two properties it must keep:
 *
 *  - **Deterministic.** Fixed key order, fixed float formatting. A dump that varies run to
 *    run cannot be a golden file.
 *  - **State only, never presentation.** `revision`, `frameSeq` and anything else that
 *    changes merely because time passed are excluded by `Options::stable`, which is what a
 *    comparison uses. A GUI that has animated for 400 ms longer than the CLI is not a
 *    difference in state (R-SVC-4).
 */
#pragma once
#include "AppModel.h"
#include <string>

namespace arstro
{
namespace cosmo
{
    struct ModelDumpOptions
    {
        /** Drop fields that legitimately differ between two front ends showing the same
         *  state: revision, frameSeq, the measured peak, timings. On for comparisons. */
        bool stable = false;
        bool json = false;
        /** Include the full EditParams block. Off by default because it is long and most
         *  assertions are about the tree and the selection. */
        bool params = false;
    };

    std::string formatModel(const AppModel &m, const ModelDumpOptions &o = ModelDumpOptions{});
}
}
