#include "unity.h"
#include <stdint.h>
#include "tdma_table.h"
#include "compliance_engine.h"
#include "mac_state_machine.h"     /* SYNC_TX_BUDGET */

/*
 * The Sync profiles against the duty-cycle budget (ETSI 1 %, issues #34, #45).
 *
 * The real compliance engine and the real TDMA table, driven the way the TDMA
 * Machine drives them: each Sync Tx slot asks for slot_active_ms (2500 ms) of
 * credit and, once sent, gives back the difference to the real airtime of the
 * 10-byte Sync packet (991 ms at SF12/BW125, the figure of the Rx window
 * tests). The same file is built once per profile (sync_profile.h):
 *  - BRINGUP (3 Tx cells per phase, a cell every 3 s): it exhausts the
 *    credit, which is why no multi-hour run can use it; the host tests pin it;
 *  - DEV (a phase every 200 s, one packet): never denied, 0.496 % of the band;
 *  - PROD (a phase every 540 s, one packet, provisional): never denied;
 *  - the source default (no SYNC_PROFILE given) is DEV.
 */

#include "sync_profile.h"

#define FREQ_HZ      868300000u
#define TX_POWER_DBM 14
#define SYNC_TOA_MS  991u

static uint32_t s_now_ms;
static uint32_t tick(void) { return s_now_ms; }

typedef struct {
    uint32_t granted;
    uint32_t denied;
    uint32_t first_denied_ms;   /* from the first request; UINT32_MAX if none */
    uint32_t last_grant_gap_ms; /* between the last two grants */
} Outcome;

static uint32_t frame_ms(void)
{
    uint32_t f = 0;
    for (uint8_t i = 0; i < TdmaTable_PhaseCount(); i++) {
        f += TdmaTable_PhaseDuration_ms(TdmaTable_GetPhase(i));
    }
    return f;
}

/* A node sending in cells [first_cell, first_cell + SYNC_TX_BUDGET) of every
 * Sync phase of every frame, for `hours`. C3 starts at cell 0; a relaying C2
 * at cell 1 (it receives cell 0). */
static Outcome simulate(uint32_t first_cell, uint32_t hours)
{
    Outcome o = { 0, 0, UINT32_MAX, 0 };
    uint32_t last_grant = 0, t0 = 0;
    bool     have_t0 = false;

    TdmaTable_Init();
    s_now_ms = 0;
    ComplianceEngine_Init(NULL, tick);

    uint32_t frame = frame_ms();
    for (uint32_t k = 0; (uint64_t)k * frame < (uint64_t)hours * 3600000u; k++) {
        for (uint8_t i = 0; i < TdmaTable_PhaseCount(); i++) {
            const Phase_t *p = TdmaTable_GetPhase(i);
            uint32_t per_cell = p->slot_active_ms + p->gap_after_slot_ms;
            for (uint32_t c = first_cell; c < first_cell + SYNC_TX_BUDGET; c++) {
                s_now_ms = k * frame + TdmaTable_PhaseStartOffset_ms(i) + c * per_cell;
                if ((uint64_t)s_now_ms >= (uint64_t)hours * 3600000u) continue;   /* past the window */
                if (!have_t0) { t0 = s_now_ms; have_t0 = true; }
                ComplianceResult_t r = ComplianceEngine_RequestChannel(FREQ_HZ, p->slot_active_ms, TX_POWER_DBM);
                if (r == COMPLIANCE_GRANTED) {
                    ComplianceEngine_ReportTxDone(FREQ_HZ, SYNC_TOA_MS);
                    if (o.granted > 0) o.last_grant_gap_ms = s_now_ms - last_grant;
                    last_grant = s_now_ms;
                    o.granted++;
                } else {
                    if (o.denied == 0) o.first_denied_ms = s_now_ms - t0;
                    o.denied++;
                }
            }
        }
    }
    return o;
}

void setUp(void) {}
void tearDown(void) {}

#if SYNC_PROFILE != SYNC_PROFILE_BRINGUP

#if   SYNC_PROFILE == SYNC_PROFILE_DEV
#  define PHASE_MS      200000u
#  define PERCENT_X1000 496u      /* the C3's airtime, % x 1000, at most */
#else
#  define PHASE_MS      540000u
#  define PERCENT_X1000 190u
#endif

void test_the_schedule_has_the_profile_period_and_one_packet_per_phase(void)
{
    TdmaTable_Init();
    TEST_ASSERT_EQUAL_UINT32(PHASE_MS, TdmaTable_PhaseDuration_ms(TdmaTable_GetPhase(0)));
    TEST_ASSERT_EQUAL_UINT32(1u, SYNC_TX_BUDGET);               /* one transmission is one repetition */
    TEST_ASSERT_EQUAL_UINT32(2u * PHASE_MS, frame_ms());
}

/* A lost packet (k = 1) must not drop the node to COLD: the silence timeout
 * outlasts two periods. */
void test_the_silence_timeout_outlasts_one_lost_packet(void)
{
    TEST_ASSERT_TRUE(SYNC_SILENCE_TIMEOUT_MS > 2u * PHASE_MS);
}

void test_c3_is_never_denied_in_4_hours(void)
{
    Outcome o = simulate(0, 4);
    TEST_ASSERT_EQUAL_UINT32(0, o.denied);
    TEST_ASSERT_EQUAL_UINT32((4u * 3600000u + PHASE_MS - 1u) / PHASE_MS, o.granted);   /* one per phase */
    TEST_ASSERT_EQUAL_UINT32(PHASE_MS, o.last_grant_gap_ms);
}

void test_a_relaying_c2_is_never_denied_in_4_hours(void)
{
    Outcome o = simulate(1, 4);
    TEST_ASSERT_EQUAL_UINT32(0, o.denied);
}

/* Margin: the airtime is under 1 % of the time with room to spare. */
void test_airtime_is_under_one_percent_with_margin(void)
{
    Outcome o = simulate(0, 4);
    uint64_t airtime_ms = (uint64_t)o.granted * SYNC_TOA_MS;
    uint64_t window_ms  = 4ull * 3600000ull;
    TEST_ASSERT_TRUE_MESSAGE(airtime_ms * 100000u <= window_ms * PERCENT_X1000, "above the profile's duty cycle");
    TEST_ASSERT_TRUE_MESSAGE(airtime_ms * 100u < window_ms, "above 1 % of the time");
}

/* Even the longest sleep of a bucket: it only ever fills (never drops under
 * the 2500 ms a request needs). */
void test_credit_never_gets_close_to_the_request(void)
{
    TdmaTable_Init();
    ComplianceStatus_t st;
    s_now_ms = 0;
    ComplianceEngine_Init(&st, tick);
    uint32_t frame = frame_ms();
    for (uint32_t k = 0; k < 4u * 3600000u / frame; k++) {
        s_now_ms = k * frame;
        TEST_ASSERT_EQUAL(COMPLIANCE_GRANTED,
                          ComplianceEngine_RequestChannel(FREQ_HZ, TdmaTable_GetPhase(0)->slot_active_ms, TX_POWER_DBM));
        ComplianceEngine_ReportTxDone(FREQ_HZ, SYNC_TOA_MS);
        s_now_ms = k * frame + TdmaTable_PhaseDuration_ms(TdmaTable_GetPhase(0));
        TEST_ASSERT_EQUAL(COMPLIANCE_GRANTED,
                          ComplianceEngine_RequestChannel(FREQ_HZ, TdmaTable_GetPhase(1)->slot_active_ms, TX_POWER_DBM));
        ComplianceEngine_ReportTxDone(FREQ_HZ, SYNC_TOA_MS);
    }
}

#else   /* the bring-up table */

/* 5.95 s of airtime per 60 s frame is 9.9 %: the 36 s credit lasts 6.7 min
 * (the bench traces of 2026-09-29 and 2026-10-01 show the first TX_DENIED
 * 6.1 to 6.6 min after boot: the simulation gives 6.1 min). */
void test_default_schedule_is_ten_times_over_the_budget(void)
{
    TdmaTable_Init();
    TEST_ASSERT_EQUAL_UINT32(30000u, TdmaTable_PhaseDuration_ms(TdmaTable_GetPhase(0)));
    TEST_ASSERT_EQUAL_UINT32(3u, SYNC_TX_BUDGET);
    Outcome o = simulate(0, 1);
    TEST_ASSERT_TRUE_MESSAGE(o.denied > 0, "never denied");
    TEST_ASSERT_TRUE_MESSAGE(o.first_denied_ms > 6u * 60000u && o.first_denied_ms < 7u * 60000u,
                             "first denial not between 6 and 7 min");
}

void test_default_schedule_starves_c3_and_c2_alike(void)
{
    Outcome c3 = simulate(0, 4);
    Outcome c2 = simulate(1, 4);
    /* 1440 packets are scheduled in 4 h. The budget allows the 36 s bucket
     * plus 1 % of 4 h = 180 s of airtime: 181 packets of 991 ms. The rest is
     * denied: seven in eight. */
    TEST_ASSERT_EQUAL_UINT32(1440u, c3.granted + c3.denied);
    TEST_ASSERT_TRUE(c3.granted <= 181u && c3.granted >= 170u);
    TEST_ASSERT_TRUE(c3.denied > 7u * c3.granted);
    TEST_ASSERT_EQUAL_UINT32(c3.granted, c2.granted);          /* a relaying C2 is no better off */
    TEST_ASSERT_EQUAL_UINT32(c3.first_denied_ms, c2.first_denied_ms);
}

#endif

int main(void)
{
    UNITY_BEGIN();
#if SYNC_PROFILE != SYNC_PROFILE_BRINGUP
    RUN_TEST(test_the_schedule_has_the_profile_period_and_one_packet_per_phase);
    RUN_TEST(test_the_silence_timeout_outlasts_one_lost_packet);
    RUN_TEST(test_c3_is_never_denied_in_4_hours);
    RUN_TEST(test_a_relaying_c2_is_never_denied_in_4_hours);
    RUN_TEST(test_airtime_is_under_one_percent_with_margin);
    RUN_TEST(test_credit_never_gets_close_to_the_request);
#else
    RUN_TEST(test_default_schedule_is_ten_times_over_the_budget);
    RUN_TEST(test_default_schedule_starves_c3_and_c2_alike);
#endif
    return UNITY_END();
}
