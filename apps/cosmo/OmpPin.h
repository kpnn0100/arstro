/*
 *  Cosmo by arstro — pin nested OpenMP parallelism, per thread, at runtime.
 *
 *  R-CPU-2(c) wants one decode worker to mean one core: LibRaw is built with OpenMP on
 *  several of the platforms cosmo ships to, so each worker would otherwise open its own
 *  team sized to the whole machine — 8 workers x 16 cores is 128 threads, which took the
 *  entire computer no matter how the pool was sized.
 *
 *  The original attempt set OMP_NUM_THREADS=1 at the top of main(). It never worked
 *  (D-12): libgomp parses the environment in a load-time constructor, so by the time
 *  main() runs the value has already been read and the assignment is seen by nobody.
 *  Proven by core/tests/fixtures/omp_env_order.c.
 *
 *  What does work is `omp_set_num_threads(1)` — but the OpenMP thread count is a
 *  PER-THREAD internal control variable, so it must be called ON each decode worker, not
 *  once on the main thread. Hence the ProjectLoader worker-start hook.
 *
 *  The symbol is looked up dynamically rather than linked, so cosmo_core and the app keep
 *  no OpenMP dependency and this is a no-op on a build whose LibRaw has no OpenMP at all
 *  (this Linux host's vendored libraw.a, for one). Host layer on purpose: dlsym and
 *  logging are not allowed in cosmo_core.
 */
#pragma once

namespace arstro
{
namespace cosmo_v2
{
    /** Pin the CALLING thread's nested OpenMP teams to one thread. Call from each decode
     *  worker before it decodes. No-op when no OpenMP runtime is loaded. */
    void pinNestedOpenMPForThisThread();

    /** One line for the startup log: whether the pin is available, and via which runtime.
     *  Checked at runtime because the last version of this was assumed and was wrong. */
    const char *ompPinStatus();
}
}
