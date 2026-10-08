/**
 * @file
 * @ingroup unit_tests
 * @brief Asks whether the running CPU has the AVX2 and the AVX-512 tier, for
 *        the x86 vector tests.
 *
 * A test calls the routines of a tier only when its probe returns 1. A build
 * that has no way to ask gets 0, and the tests skip the tier rather than run
 * it on a CPU that may not have it.
 */
#ifndef LAME_TEST_CPU_PROBE_H
#define LAME_TEST_CPU_PROBE_H

#include "util.h"

/**
 * @brief Checks whether the running CPU has AVX2.
 * @return 1 if it has, 0 if not or if the build cannot ask.
 */
static inline int
have_avx2(void)
{
#if defined( HAVE_AVX2_INTRINSICS )
# if defined( __AVX2__ )
    return 1;
# elif defined( LAME_CPU_SUPPORTS )
    return __builtin_cpu_supports("avx2") != 0;
# else
    return 0;
# endif
#else
    return 0;
#endif
}

/**
 * @brief Checks whether the running CPU has the AVX-512 subsets that the tier
 *        needs.
 *
 * The check requires all four subsets, because the kernels use all four. A
 * CPU with only the foundation subset would fault on the others.
 *
 * @return 1 if it has, 0 if not or if the build cannot ask.
 */
static inline int
have_avx512(void)
{
#if defined( HAVE_AVX512_INTRINSICS )
# if defined( __AVX512F__ ) && defined( __AVX512VL__ ) \
  && defined( __AVX512BW__ ) && defined( __AVX512DQ__ )
    return 1;
# elif defined( LAME_CPU_SUPPORTS_AVX512 )
    return __builtin_cpu_supports("avx512f") != 0
        && __builtin_cpu_supports("avx512vl") != 0
        && __builtin_cpu_supports("avx512bw") != 0
        && __builtin_cpu_supports("avx512dq") != 0;
# else
    return 0;
# endif
#else
    return 0;
#endif
}

#endif /* LAME_TEST_CPU_PROBE_H */
