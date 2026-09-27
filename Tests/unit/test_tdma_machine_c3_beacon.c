#include "unity.h"
#include "tdma_machine.h"
#include "mac_state_machine.h"
#include "freq_resolver.h"
#include "compliance_engine.h"
#include "shared_mem.h"
#include "tdma_table.h"
#include "arclog_capture.h"
#include <string.h>
#include <stdint.h>

/*
 * Deterministic Tx start on a non-Sync phase (ADR-0016). Frame: Mesh_Beacon
 * (phase 0, cells at 0 / 3000 / 6000) then Sync (phase 1). C3 transmits in
 * every beacon cell. Every scheduled packet starts on air at its slot's
 * nominal start, whatever the phase; only the handling of a late fire
 * instant differs: a packet that carries no time is still sent.
 */

/* =========================================================================
 * Platform stubs
 * ========================================================================= */

static uint32_t s_rtc_ms;
static uint32_t s_alarm_programmed;
static int      s_radio_send_calls;
static int      s_wait_calls;
static uint32_t s_wait_target;

static uint32_t stub_GetRtcMs(void)                          { return s_rtc_ms;                        }
static void     stub_ProgramAlarmA(uint32_t t)               { s_alarm_programmed = t;                 }
static void     stub_CancelAlarmA(void)                      {                                         }
static void     stub_RadioSetChannel(uint32_t f)             { (void)f;                                }
static void     stub_RadioSend(const uint8_t *b, uint8_t l)  { (void)b; (void)l; s_radio_send_calls++; }
static void     stub_RadioSetRx(uint32_t w, uint32_t c)      { (void)w; (void)c;                       }
static void     stub_RadioScan(void)                         {                                         }
static void     stub_RadioSleep(void)                        {                                         }
static uint32_t stub_RadioTimeOnAir(uint8_t len)             { (void)len; return 991u;                 }
static void     stub_WaitUntilMs(uint32_t t)                 { s_wait_calls++; s_wait_target = t; s_rtc_ms = t; }

static const TdmaPlatform_t k_platform = {
    .GetRtcMs        = stub_GetRtcMs,
    .ProgramAlarmA   = stub_ProgramAlarmA,
    .CancelAlarmA    = stub_CancelAlarmA,
    .RadioSetChannel = stub_RadioSetChannel,
    .RadioSend       = stub_RadioSend,
    .RadioSetRx      = stub_RadioSetRx,
    .RadioScan       = stub_RadioScan,
    .RadioSleep      = stub_RadioSleep,
    .RadioTimeOnAir  = stub_RadioTimeOnAir,
    .WaitUntilMs     = stub_WaitUntilMs,
    .tx_ramp_ms      = 0u,
};

/* =========================================================================
 * Fixtures
 * ========================================================================= */

#define SLOT_STEP_MS  3000u   /* slot_active_ms 2500 + gap 500 */

static FrequencyResolverState_t s_freq_state;
static ComplianceStatus_t       s_comp_status;
static uint32_t comp_get_tick(void) { return s_rtc_ms; }

void setUp(void)
{
    memset(&s_freq_state,  0, sizeof(s_freq_state));
    memset(&s_comp_status, 0, sizeof(s_comp_status));
    s_rtc_ms                 = 0u;
    s_alarm_programmed       = 0u;
    s_radio_send_calls       = 0;
    s_wait_calls             = 0;
    s_wait_target            = 0u;
    ArcLog_CaptureReset();

    s_freq_state.phases[0].cell_mode         = CELL_FREQ_STATIC;
    s_freq_state.phases[0].cell_freq_or_seed = 868300000u;
    s_freq_state.phases[1].cell_mode         = CELL_FREQ_STATIC;
    s_freq_state.phases[1].cell_freq_or_seed = 868300000u;

    FrequencyResolver_Init(&s_freq_state);
    ComplianceEngine_Init(&s_comp_status, comp_get_tick);
    MAC_Init(NULL);
    TdmaMachine_Init(&k_platform);
}

void tearDown(void) {}

/* =========================================================================
 * Tests
 * ========================================================================= */

void test_beacon_tx_woken_one_lead_early_and_fired_at_nominal(void)
{
    TEST_ASSERT_TRUE(TdmaMachine_Start());       /* beacon cell 0 at 20 */
    TdmaMachine_SlotTask();
    TEST_ASSERT_EQUAL(TX_LEAD_MS + SLOT_STEP_MS - TX_LEAD_MS, s_alarm_programmed);

    s_rtc_ms = s_alarm_programmed;               /* beacon cell 1 */
    TdmaMachine_SlotTask();
    TEST_ASSERT_EQUAL(2, s_radio_send_calls);
    TEST_ASSERT_EQUAL(TX_LEAD_MS + SLOT_STEP_MS, s_wait_target);
    TEST_ASSERT_NO_ARCLOG("TX_LATE");
}

void test_late_beacon_tx_is_still_sent(void)
{
    TEST_ASSERT_TRUE(TdmaMachine_Start());
    TdmaMachine_SlotTask();
    int waits = s_wait_calls;
    s_rtc_ms = s_alarm_programmed + TX_LEAD_MS + 5u;   /* 3025, 5 ms late */
    ArcLog_CaptureReset();
    TdmaMachine_SlotTask();

    TEST_ASSERT_ARCLOG("TX_LATE plan=3020 fire=3020 now=3025");
    TEST_ASSERT_EQUAL(2, s_radio_send_calls);
    TEST_ASSERT_EQUAL(waits, s_wait_calls);           /* sent at once */
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_beacon_tx_woken_one_lead_early_and_fired_at_nominal);
    RUN_TEST(test_late_beacon_tx_is_still_sent);
    return UNITY_END();
}
