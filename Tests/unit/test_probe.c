#include "unity.h"
#include <stdint.h>
#include "arclog_capture.h"
#include "probe.h"

/*
 * The timing probe helper. Built twice:
 *  - test_probe (BENCH_PROBE): marks, the PROBE lines, the wrap of the 24-bit
 *    down-counter, the mark limit;
 *  - test_probe_off (production): the macros are empty statements and the
 *    helper is not even linked (the file defines no clock: a reference would
 *    fail the link).
 */

#ifdef BENCH_PROBE

/* A scripted down-counter: each read returns the next value. */
static uint32_t s_script[16];
static unsigned s_next;
static uint32_t s_hz = 4000000u;

uint32_t Probe_HostNow(void) { return s_script[s_next++]; }
uint32_t Probe_HostHz(void)  { return s_hz; }

static void script(const uint32_t *v, unsigned n)
{
    for (unsigned i = 0; i < n; i++) s_script[i] = v[i];
    s_next = 0;
}

void setUp(void) { ArcLog_CaptureReset(); s_hz = 4000000u; }
void tearDown(void) {}

/* 4 MHz: 4 ticks to the us. */
void test_one_line_per_mark_in_microseconds(void)
{
    const uint32_t v[] = { 0xFFFFFFu, 0xFFFFFFu - 2688u, 0xFFFFFFu - 2688u - 400u };
    script(v, 3);
    PROBE_START(p);
    PROBE_MARK(p, "settime");
    PROBE_MARK(p, "setdate");
    PROBE_LOG(p, "rtc_set");
    TEST_ASSERT_EQUAL_UINT32(2, ArcLog_CaptureCount());
    TEST_ASSERT_ARCLOG("PROBE tag=rtc_set seg=settime us=672 hz=4000000");
    TEST_ASSERT_ARCLOG("PROBE tag=rtc_set seg=setdate us=100 hz=4000000");
}

/* The counter counts down and wraps at 2^24: a segment across the wrap. */
void test_a_segment_across_the_counter_wrap(void)
{
    const uint32_t v[] = { 100u, 0xFFFFFFu - 155u };      /* 100 -> 0 -> 0xFFFFFF - 155: 100 + 156 ticks */
    script(v, 2);
    PROBE_START(p);
    PROBE_MARK(p, "wrapped");
    PROBE_LOG(p, "t");
    TEST_ASSERT_ARCLOG("PROBE tag=t seg=wrapped us=64 hz=4000000");   /* 256 ticks / 4 */
}

void test_the_clock_is_logged_not_assumed(void)
{
    s_hz = 32000000u;
    const uint32_t v[] = { 1000u, 200u };
    script(v, 2);
    PROBE_START(p);
    PROBE_MARK(p, "a");
    PROBE_LOG(p, "t");
    TEST_ASSERT_ARCLOG("PROBE tag=t seg=a us=25 hz=32000000");        /* 800 ticks / 32 */
}

void test_extra_marks_are_dropped(void)
{
    uint32_t v[16];
    for (unsigned i = 0; i < 12; i++) v[i] = 0xFFFFFFu - 40u * i;
    script(v, 12);
    PROBE_START(p);
    for (unsigned i = 0; i < 10; i++) PROBE_MARK(p, "m");
    PROBE_LOG(p, "t");
    TEST_ASSERT_EQUAL_UINT32(PROBE_MAX_MARKS, ArcLog_CaptureCount());
    TEST_ASSERT_ARCLOG("PROBE tag=t seg=m us=10 hz=4000000");
}

void test_nothing_is_logged_until_probe_log(void)
{
    const uint32_t v[] = { 5000u, 4000u };
    script(v, 2);
    PROBE_START(p);
    PROBE_MARK(p, "a");
    TEST_ASSERT_EQUAL_UINT32(0, ArcLog_CaptureCount());   /* the log is after the timed path */
    PROBE_LOG(p, "t");
    TEST_ASSERT_EQUAL_UINT32(1, ArcLog_CaptureCount());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_one_line_per_mark_in_microseconds);
    RUN_TEST(test_a_segment_across_the_counter_wrap);
    RUN_TEST(test_the_clock_is_logged_not_assumed);
    RUN_TEST(test_extra_marks_are_dropped);
    RUN_TEST(test_nothing_is_logged_until_probe_log);
    return UNITY_END();
}

#else   /* production */

void setUp(void) { ArcLog_CaptureReset(); }
void tearDown(void) {}

/* Same source lines as a probed path: they must compile to nothing. */
static int probed_path(int x)
{
    PROBE_START(p);
    x += 1;
    PROBE_MARK(p, "step");
    PROBE_LOG(p, "path");
    return x;
}

void test_the_probe_macros_vanish_without_bench_probe(void)
{
    TEST_ASSERT_EQUAL_INT(2, probed_path(1));
    TEST_ASSERT_EQUAL_UINT32(0, ArcLog_CaptureCount());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_probe_macros_vanish_without_bench_probe);
    return UNITY_END();
}

#endif
