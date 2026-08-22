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
 *  PER-THREAD internal control variable, so it must be called ON each thread that decodes.
 *
 *  ON EACH THREAD THAT DECODES, not on each pool worker — that distinction is D-41, and it
 *  is why this header no longer says "hence the ProjectLoader worker-start hook". Wiring
 *  the pin to that one hook covered the project load and nothing else: opening a single
 *  photo, opening a .cosmo, and the synchronous workspace load all decode on the GTK main
 *  thread, and `cosmo-cc info` decodes on its own. Measured on 16 cores, a bare decode took
 *  4.6 cores and 20 OS threads at cpuPercent=25 and 4.5 at 100% — identical, i.e. entirely
 *  outside the budget — while the pooled paths tracked it correctly. So the pin belongs to
 *  the DECODER now (`PinnedDecoder`), which no caller can forget to construct, and the
 *  worker hook stays as well: the failure is silent, so it is worth covering at both ends.
 *
 *  The symbol is looked up dynamically rather than linked, so cosmo_core and the app keep
 *  no OpenMP dependency and this is a no-op on a build whose LibRaw has no OpenMP at all
 *  (this Linux host's vendored libraw.a, for one). Host layer on purpose: dlsym, getenv and
 *  logging are not allowed in cosmo_core.
 */
#pragma once

namespace arstro
{
namespace cosmo_v2
{
    /** Size the CALLING thread's nested OpenMP teams to `teamSize`. Called by
     *  `PinnedDecoder` before every decode and by the pool's per-worker start hook. Cheap
     *  and repeatable — it is a store, and re-calling it is how a budget change takes effect
     *  on the next decode (R-CPU-3). No-op when no OpenMP runtime is loaded.
     *
     *  `teamSize` defaults to 1 because that is the POOL's answer: `decodeWorkers()` workers
     *  each opening a team of 1 is exactly the budget, and a second layer of parallelism
     *  inside one image would be pure oversubscription (R-CPU-2c).
     *
     *  It is a parameter rather than a constant because 1 is *not* the answer off the pool.
     *  A lone decode — the user opening one photo — has no outer parallelism to oversubscribe
     *  against, so pinning it to 1 spends one core of a budget that allows `total()`. Measured
     *  on 16 cores: hard-coding 1 fixed the budget violation and made opening a 24 MP ARW take
     *  1.85 s instead of 0.79 s, at 100% as much as at 25%. R-CPU-1 grants the user's share;
     *  it does not ask cosmo to leave it unused. So the caller says how wide, and
     *  `PinnedDecoder` gets that number from the budget. */
    void pinNestedOpenMPForThisThread(int teamSize = 1);

    /** One line for the startup log: whether the pin is available, via which runtime, and
     *  what it pins to. Checked at runtime because the last version of this was assumed and
     *  was wrong. */
    const char *ompPinStatus();

    /** How many distinct threads the pin has actually bound so far.
     *
     *  R-CPU-4 as amended wants honesty that is MEASURED. "Every decoding thread is pinned"
     *  is otherwise exactly the kind of claim that shipped twice already without ever being
     *  executed — and a thread-COUNT assertion would be inert wherever LibRaw has no OpenMP,
     *  whereas this count is the same on every platform. Read it back in `cosmo-cc backends`
     *  and in the load log. */
    int ompPinnedThreadCount();

    /** The user's own `OMP_NUM_THREADS` when they set a readable one, and 0 when they did
     *  not. Non-zero OVERRIDES every `teamSize` above, because R-CPU-2(c) says an explicit
     *  user value still wins and the previous version overrode it unconditionally. */
    int ompPinUserOverride();
}
}
