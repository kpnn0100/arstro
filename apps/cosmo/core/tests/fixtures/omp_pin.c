/*
 *  D-12 fixture, part 2: the mechanism that DOES work.
 *
 *  omp_env_order.c shows that OMP_NUM_THREADS set from main() binds nothing. This shows
 *  the replacement: the OpenMP thread count is a PER-THREAD internal control variable, so
 *  a worker can pin its own nested teams with omp_set_num_threads(1) — resolved by dlsym,
 *  so the caller links no OpenMP at all. That is what ProjectLoader's per-worker start
 *  hook does (apps/cosmo/OmpPin.cpp), and thread 0 below is the unpinned control that
 *  proves the pin is what makes the difference.
 *
 *      gcc -fopenmp -O1 -o /tmp/omp_pin omp_pin.c -ldl -lpthread && /tmp/omp_pin
 *
 *  Expected on an N-core machine: threads 1..3 report team size 1, thread 0 reports N.
 *  Exits 1 if a pinned thread did not get a team of 1.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <omp.h>
#include <pthread.h>
#include <stdio.h>

static int gBad = 0;

static void *worker(void *arg)
{
    const long id = (long)arg;
    void (*set)(int) = (void (*)(int))dlsym(RTLD_DEFAULT, "omp_set_num_threads");
    if (!set) { printf("  thread %ld: omp_set_num_threads NOT found\n", id); return 0; }
    if (id != 0) set(1);                 /* thread 0 stays unpinned, as the control */

    int team = 0;
    #pragma omp parallel
    {
        #pragma omp master
        team = omp_get_num_threads();
    }
    printf("  thread %ld (%s): team size = %d\n", id, id == 0 ? "control, unpinned" : "pinned", team);
    if (id != 0 && team != 1) gBad = 1;
    return 0;
}

int main(void)
{
    printf("machine cores = %d\n", omp_get_num_procs());
    pthread_t t[4];
    for (long i = 0; i < 4; ++i) pthread_create(&t[i], 0, worker, (void *)i);
    for (int i = 0; i < 4; ++i) pthread_join(t[i], 0);
    printf(gBad ? "FAIL: a pinned thread did not get a team of 1\n" : "PASS: pinned threads got a team of 1\n");
    return gBad;
}
