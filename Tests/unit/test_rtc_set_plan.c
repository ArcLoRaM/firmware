#include "unity.h"
#include <stdint.h>
#include "rtc_set_plan.h"

/* RTC model: the true time T (in units of 1/4 096 000 s, exact for both ms
 * and RTC ticks) and an old clock running off_u ahead of it. The platform
 * reads the old clock at an anchor (the MAC's snapshot), then exactly at a
 * tick edge, stands the calendar still for loss_us, restarts it at the
 * planned second and shifts it. The new clock must then read
 * target_ms - floor_ms(anchor) + old_clock(T): the sender's time. */
#define U_PER_MS    4096LL
#define U_PER_TICK  1000LL
#define U_PER_S     4096000LL
#define U_PER_DAY   (86400LL * U_PER_S)

static int64_t day_u(int64_t u) { return ((u % U_PER_DAY) + U_PER_DAY) % U_PER_DAY; }

typedef struct {
    int64_t new_clock_u;   /* new clock reading at true time t_check */
    int64_t want_u;        /* what it should read */
} Outcome;

/* anchor_u, edge_u: true times of the anchor read and of the edge read
 * (edge_u must be on a tick of the old clock). */
static Outcome set_clock(uint32_t target_ms, int64_t off_u, int64_t anchor_u, int64_t edge_u,
                         uint32_t loss_us)
{
    uint32_t anchor_ticks = (uint32_t)(day_u(anchor_u + off_u) / U_PER_TICK);
    uint32_t edge_ticks   = (uint32_t)(day_u(edge_u + off_u) / U_PER_TICK);

    RtcSetPlan_t plan  = RtcSetPlan_Make(target_ms, anchor_ticks, loss_us);
    int32_t      shift = RtcSetPlan_ShiftTicks(&plan, anchor_ticks, edge_ticks);

    int64_t restart_u = edge_u + (int64_t)loss_us * 4096 / 1000;
    int64_t t_check   = restart_u + 5 * U_PER_S;   /* after the shift took effect */
    Outcome o;
    o.new_clock_u = day_u((int64_t)plan.seconds * U_PER_S + (t_check - restart_u)
                          + (int64_t)shift * U_PER_TICK);
    uint32_t floor_ms = RtcTicks_ToMs(anchor_ticks);
    o.want_u = day_u((int64_t)target_ms * U_PER_MS - (int64_t)floor_ms * U_PER_MS
                     + day_u(t_check + off_u));
    return o;
}

static void assert_within_one_tick(Outcome o)
{
    int64_t d = o.new_clock_u - o.want_u;
    if (d > U_PER_DAY / 2) d -= U_PER_DAY;
    if (d < -U_PER_DAY / 2) d += U_PER_DAY;
    TEST_ASSERT_TRUE_MESSAGE(d >= -U_PER_TICK && d <= U_PER_TICK, "new clock off by more than one tick");
}

void setUp(void) {}
void tearDown(void) {}

void test_ticks_to_ms_floors(void)
{
    TEST_ASSERT_EQUAL_UINT32(0u, RtcTicks_ToMs(4u));          /* 0.977 ms */
    TEST_ASSERT_EQUAL_UINT32(1u, RtcTicks_ToMs(5u));          /* 1.221 ms */
    TEST_ASSERT_EQUAL_UINT32(86399999u, RtcTicks_ToMs(RTC_DAY_TICKS - 1u));
}

void test_elapsed_ticks_wrap_at_midnight(void)
{
    TEST_ASSERT_EQUAL_UINT32(10u, RtcTicks_Elapsed(RTC_DAY_TICKS - 4u, 6u));
    TEST_ASSERT_EQUAL_UINT32(0u, RtcTicks_Elapsed(123u, 123u));
}

/* The bench case of #55: a C2 at 00:00:03 sets itself to about 23:55:07,
 * 2.0 ms of HAL work between the snapshot and the write. */
void test_clock_reads_the_sender_time_after_the_write(void)
{
    int64_t off_u    = 3LL * U_PER_S + 471 * U_PER_MS + 333;   /* old clock: 00:00:03.471+ */
    int64_t anchor_u = 10 * U_PER_S + 17;
    int64_t edge_u   = anchor_u + 2 * U_PER_MS;
    edge_u          += U_PER_TICK - day_u(edge_u + off_u) % U_PER_TICK;   /* on a tick */
    assert_within_one_tick(set_clock(86107038u, off_u, anchor_u, edge_u, 180u));
}

void test_any_position_of_the_anchor_and_edge_within_their_ticks(void)
{
    for (int64_t a = 0; a < 2 * U_PER_TICK; a += 37) {
        for (int64_t gap_ticks = 1; gap_ticks < 400; gap_ticks += 53) {
            int64_t off_u    = 7 * U_PER_S + 129;
            int64_t anchor_u = 20 * U_PER_S + a;
            int64_t edge_u   = anchor_u + gap_ticks * U_PER_TICK;
            edge_u          -= day_u(edge_u + off_u) % U_PER_TICK;
            assert_within_one_tick(set_clock(43210987u, off_u, anchor_u, edge_u, 180u));
        }
    }
}

void test_sub_second_values_round_to_an_advance_or_a_delay(void)
{
    static const uint32_t targets[] = { 50000000u, 50000001u, 50000499u, 50000500u,
                                        50000501u, 50000998u, 50000999u };
    for (unsigned i = 0; i < sizeof targets / sizeof targets[0]; i++) {
        int64_t off_u  = 5 * U_PER_S;
        int64_t edge_u = 30 * U_PER_S + 4 * U_PER_TICK;
        RtcSetPlan_t p = RtcSetPlan_Make(targets[i], (uint32_t)((edge_u + off_u) / U_PER_TICK), 180u);
        TEST_ASSERT_TRUE(p.base_ticks >= -(int32_t)RTC_TICKS_PER_S / 2 - 1);
        TEST_ASSERT_TRUE(p.base_ticks <= (int32_t)RTC_TICKS_PER_S / 2 + 1);
        assert_within_one_tick(set_clock(targets[i], off_u, edge_u, edge_u, 180u));
    }
}

void test_the_shift_stays_within_one_second_up_to_the_anchor_window(void)
{
    uint32_t anchor = 1000u * RTC_TICKS_PER_S;
    for (uint32_t t = 50000000u; t < 50001000u; t += 7u) {
        RtcSetPlan_t p = RtcSetPlan_Make(t, anchor, 180u);
        int32_t s = RtcSetPlan_ShiftTicks(&p, anchor, anchor + RTC_SET_ANCHOR_MAX_TICKS);
        TEST_ASSERT_TRUE(s > -(int32_t)RTC_TICKS_PER_S && s < (int32_t)RTC_TICKS_PER_S);
    }
}

void test_a_set_across_midnight(void)
{
    int64_t off_u    = 0;
    int64_t anchor_u = U_PER_DAY - 3 * U_PER_MS;
    int64_t edge_u   = anchor_u + 4 * U_PER_MS;   /* after 00:00 on the old clock */
    edge_u          -= day_u(edge_u + off_u) % U_PER_TICK;
    assert_within_one_tick(set_clock(86399999u, off_u, anchor_u, edge_u, 180u));
    assert_within_one_tick(set_clock(250u, off_u, anchor_u, edge_u, 180u));
}

void test_a_second_rounded_up_past_midnight_writes_second_zero(void)
{
    RtcSetPlan_t p = RtcSetPlan_Make(86399800u, 0u, 180u);
    TEST_ASSERT_EQUAL_UINT32(0u, p.seconds);
    TEST_ASSERT_TRUE(p.base_ticks < 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ticks_to_ms_floors);
    RUN_TEST(test_elapsed_ticks_wrap_at_midnight);
    RUN_TEST(test_clock_reads_the_sender_time_after_the_write);
    RUN_TEST(test_any_position_of_the_anchor_and_edge_within_their_ticks);
    RUN_TEST(test_sub_second_values_round_to_an_advance_or_a_delay);
    RUN_TEST(test_the_shift_stays_within_one_second_up_to_the_anchor_window);
    RUN_TEST(test_a_set_across_midnight);
    RUN_TEST(test_a_second_rounded_up_past_midnight_writes_second_zero);
    return UNITY_END();
}
