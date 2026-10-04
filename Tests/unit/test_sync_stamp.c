#include "unity.h"
#include <stdint.h>
#include "sync_stamp.h"

/*
 * SyncStamp in RTC ticks (1/4096 s) instead of whole ms (issue #82).
 * Expected values are worked by hand from the definitions:
 *   1 tick = 244.140625 us; 1 ms = 4.096 ticks; the 10-byte Sync packet is
 *   991 232 us on air at SF12/BW125 = 4059.99 ticks; a day is 353 894 400 ticks.
 */

void setUp(void) {}
void tearDown(void) {}

void test_a_time_on_air_in_us_is_the_nearest_tick(void)
{
    TEST_ASSERT_EQUAL_UINT32(4060u, SyncStamp_UsToTicks(991232u));
    TEST_ASSERT_EQUAL_UINT32(0u, SyncStamp_UsToTicks(121u));     /* 0.495 tick */
    TEST_ASSERT_EQUAL_UINT32(1u, SyncStamp_UsToTicks(123u));     /* 0.504 tick */
}

void test_a_ms_is_the_first_tick_not_before_it(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, SyncStamp_MsToTicks(0u));
    TEST_ASSERT_EQUAL_UINT32(33u, SyncStamp_MsToTicks(8u));       /* 32.768 */
    TEST_ASSERT_EQUAL_UINT32(12288u, SyncStamp_MsToTicks(3000u)); /* exact */
    TEST_ASSERT_EQUAL_UINT32(5u, SyncStamp_MsToTicks(1u));        /* 4.096 ticks: 4 ticks is only 0.977 ms */
}

/* The whole-ms interface round-trips: the floor ms of the first tick not
 * before a ms is that ms (a set's age carry and the silence timer use it). */
void test_the_floor_ms_of_the_ticks_of_a_ms_is_that_ms(void)
{
    for (uint32_t ms = 0u; ms < 200000u; ms += 3u) {
        TEST_ASSERT_EQUAL_UINT32(ms, RtcTicks_ToMs(SyncStamp_MsToTicks(ms)));
    }
    TEST_ASSERT_EQUAL_UINT32(86399999u, RtcTicks_ToMs(SyncStamp_MsToTicks(86399999u)));
}

void test_the_stamp_is_the_packet_start_rxdone_minus_the_air_time(void)
{
    TEST_ASSERT_EQUAL_UINT32(95940u, SyncStamp_FromRxDone(100000u, 991232u, 0u));
}

void test_the_rx_latency_is_subtracted_too(void)
{
    TEST_ASSERT_EQUAL_UINT32(95940u - 4u, SyncStamp_FromRxDone(100000u, 991232u, 977u));   /* 977 us = 4 ticks */
}

/* A packet received just after midnight started before it. */
void test_the_stamp_wraps_to_the_day_before(void)
{
    TEST_ASSERT_EQUAL_UINT32(RTC_DAY_TICKS - 4058u, SyncStamp_FromRxDone(2u, 991232u, 0u));
}

void test_the_error_of_a_stamp_on_time_is_zero(void)
{
    TEST_ASSERT_EQUAL_INT32(0, SyncStamp_ErrorUs(12288u, 3000u));
}

/* One tick late is 244.14 us; 12 ticks is 2929.69 us, rounded to the nearest us. */
void test_the_error_is_in_us_to_the_tick(void)
{
    TEST_ASSERT_EQUAL_INT32(244, SyncStamp_ErrorUs(12289u, 3000u));
    TEST_ASSERT_EQUAL_INT32(2930, SyncStamp_ErrorUs(12288u + 12u, 3000u));
    TEST_ASSERT_EQUAL_INT32(-977, SyncStamp_ErrorUs(12288u - 4u, 3000u));   /* -976.56 */
}

/* Expected arrivals are exact schedule ms, not ticks: 3001 ms is 12292.096 ticks. */
void test_a_non_tick_expected_arrival_is_not_rounded(void)
{
    TEST_ASSERT_EQUAL_INT32(-23, SyncStamp_ErrorUs(12292u, 3001u));   /* -0.096 tick = -23.44 us */
}

/* Stamp 4 ticks before midnight (0.98 ms), expected 100 ms after it. */
void test_the_error_across_midnight_is_taken_the_short_way(void)
{
    TEST_ASSERT_EQUAL_INT32(-100977, SyncStamp_ErrorUs(RTC_DAY_TICKS - 4u, 100u));
    /* and the other way round: stamp just after midnight, expected just before it */
    TEST_ASSERT_EQUAL_INT32(100977, SyncStamp_ErrorUs(4u, 86400000u - 100u));
}

void test_the_error_in_ms_rounds_to_the_nearest_away_from_zero(void)
{
    TEST_ASSERT_EQUAL_INT32(0, SyncStamp_UsToMs(499));
    TEST_ASSERT_EQUAL_INT32(1, SyncStamp_UsToMs(500));
    TEST_ASSERT_EQUAL_INT32(2, SyncStamp_UsToMs(1500));
    TEST_ASSERT_EQUAL_INT32(-1, SyncStamp_UsToMs(-500));
    TEST_ASSERT_EQUAL_INT32(-2, SyncStamp_UsToMs(-1500));
    TEST_ASSERT_EQUAL_INT32(50, SyncStamp_UsToMs(50057));
}

/* A whole-ms stamp, as the old interface gave, comes out at most one tick
 * late: the error of a packet on time reads 0 to 244 us. */
void test_a_whole_ms_stamp_converted_to_ticks_reads_under_one_tick_late(void)
{
    for (uint32_t ms = 0u; ms < 100000u; ms += 7u) {
        int32_t err = SyncStamp_ErrorUs(SyncStamp_MsToTicks(ms), ms);
        TEST_ASSERT_TRUE_MESSAGE(err >= 0 && err <= 244, "not within one tick late");
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_time_on_air_in_us_is_the_nearest_tick);
    RUN_TEST(test_a_ms_is_the_first_tick_not_before_it);
    RUN_TEST(test_the_floor_ms_of_the_ticks_of_a_ms_is_that_ms);
    RUN_TEST(test_the_stamp_is_the_packet_start_rxdone_minus_the_air_time);
    RUN_TEST(test_the_rx_latency_is_subtracted_too);
    RUN_TEST(test_the_stamp_wraps_to_the_day_before);
    RUN_TEST(test_the_error_of_a_stamp_on_time_is_zero);
    RUN_TEST(test_the_error_is_in_us_to_the_tick);
    RUN_TEST(test_a_non_tick_expected_arrival_is_not_rounded);
    RUN_TEST(test_the_error_across_midnight_is_taken_the_short_way);
    RUN_TEST(test_the_error_in_ms_rounds_to_the_nearest_away_from_zero);
    RUN_TEST(test_a_whole_ms_stamp_converted_to_ticks_reads_under_one_tick_late);
    return UNITY_END();
}
