/*!
 * \file      bench_config.h
 *
 * \brief     Bench hook: Build Overrides and Build ID, force-included in
 *            every C file of both cores.
 *
 * \details   Both .cproject files pass this header with -include to the C
 *            compiler of every configuration, so it is seen before any
 *            source line.
 *
 *            The bench tool (tools/bench, ADR 0001 there) builds from its
 *            own copy of the tree and writes a generated bench_overrides.h
 *            next to this file, for that build only:
 *
 * \code
 *   #define BENCH_BUILD_ID "a1b2c3d-dirty-5e6f"
 *   #undef  TX_RAMP_MS
 *   #define TX_RAMP_MS 5u
 * \endcode
 *
 *            The #undef lets an override replace a -D of the build
 *            configuration (BENCH_RTC_START_S). A define in a source file is
 *            overridable only when it is wrapped in #ifndef.
 *
 *            bench_overrides.h is gitignored and never exists in the repo:
 *            a normal build (CubeIDE, host tests) gets BENCH_BUILD_ID "dev"
 *            and no override.
 *
 *            BENCH_BUILD_ID is logged in the BOOT line of both cores. It is
 *            one ArcLog value, so it contains no spaces.
 *
 * \author    Simon R.C. Langlais ( Celium )
 */
#ifndef BENCH_CONFIG_H
#define BENCH_CONFIG_H

#if __has_include("bench_overrides.h")
#  include "bench_overrides.h"
#endif

#ifndef BENCH_BUILD_ID
#  define BENCH_BUILD_ID  "dev"
#endif

#endif /* BENCH_CONFIG_H */
