/*
 *  D-12 fixture: is OMP_NUM_THREADS still readable when main() sets it?
 *
 *  linux_main.cpp pins nested OpenMP teams to one thread (R-CPU-2c) with
 *  g_setenv("OMP_NUM_THREADS", "1", FALSE) as the first statement of main().
 *  libgomp, however, parses the environment in an ELF constructor that runs at
 *  library-load time — before main — so by then the value is already fixed.
 *
 *      gcc -fopenmp -O1 -o /tmp/omp_env_order omp_env_order.c
 *      /tmp/omp_env_order                   # expect: team = every core (the bug)
 *      OMP_NUM_THREADS=1 /tmp/omp_env_order # expect: team = 1  (what was intended)
 *
 *  Exits 1 when setenv-from-main failed to bind, so it can be asserted in CI.
 */
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    const int before = omp_get_max_threads();
    setenv("OMP_NUM_THREADS", "1", 0);   /* 0 = don't overwrite, matching g_setenv(..., FALSE) */
    const int after = omp_get_max_threads();

    int team = 0;
    #pragma omp parallel
    {
        #pragma omp master
        team = omp_get_num_threads();
    }

    printf("before setenv: omp_get_max_threads() = %d\n", before);
    printf("after  setenv: omp_get_max_threads() = %d\n", after);
    printf("actual parallel-region team size = %d  (machine has %d cores)\n",
           team, omp_get_num_procs());

    if (team > 1)
    {
        printf("FAIL: setenv() from main() did not bind the team size\n");
        return 1;
    }
    printf("PASS: team pinned to 1\n");
    return 0;
}
