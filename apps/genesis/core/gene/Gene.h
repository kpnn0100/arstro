/*
 *  Genesis — `genesis::gene` is an ALIAS of the shared `arstro::gene`, not a copy.
 *
 *  Gene was promoted to `core/Gene` on 2026-09-11 so Interstellar could bind a parameter to a
 *  calculation over other parameters without a second expression language existing in this
 *  repo (interstellar R-BIND-2). Genesis's 156 call sites spell `gene::` and are unchanged:
 *  this shim is the whole of the migration on this side.
 *
 *  It is a shim rather than a moved include because `apps/genesis/core` is on genesis's
 *  include path and `core/Gene/include` is not — so `#include "gene/Gene.h"` keeps resolving
 *  here, unambiguously, and the shared library stays free of any mention of Genesis.
 *
 *  An ALIAS and never a fork: this repo has already paid once for a copied file (genesis's own
 *  palette copy, which drifted), and `arstrobench` aliasing cosmo's token namespaces is the
 *  pattern that has cost nothing.
 */
#pragma once
#include "../../../../core/Gene/include/gene/Gene.h"

namespace genesis
{
    namespace gene = ::arstro::gene;
}
