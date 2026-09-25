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
 * C3 is the SyncAnchor: never CLOCK_COLD, so its alarm chain starts at boot
 * and never stops for scanning.
 */

/* =========================================================================
 * Platform stubs
 * ========================================================================= */

static uint32_t s_rtc_ms;
static uint32_t s_alarm_programmed;
static int      s_alarm_calls;
static int      s_cancel_alarm_calls;
static int      s_radio_send_calls;
static int      s_radio_scan_calls;

static uint32_t stub_GetRtcMs(void)                          { return s_rtc_ms;                         }
static void     stub_ProgramAlarmA(uint32_t t)               { s_alarm_programmed = t; s_alarm_calls++; }
static void     stub_CancelAlarmA(void)                      { s_cancel_alarm_calls++;                  }
static void     stub_RadioSetChannel(uint32_t f)             { (void)f;                                 }
static void     stub_RadioSend(const uint8_t *b, uint8_t l)  { (void)b; (void)l; s_radio_send_calls++;  }
static void     stub_RadioSetRx(uint32_t w, uint32_t c)      { (void)w; (void)c;                        }
static void     stub_RadioScan(void)                         { s_radio_scan_calls++;                    }
static void     stub_RadioSleep(void)                        {                                          }
static uint32_t stub_RadioTimeOnAir(uint8_t len)             { (void)len; return 991u;                  }
static void     stub_WaitUntilMs(uint32_t t)                 { s_rtc_ms = t;                            }

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
    s_rtc_ms             = 0u;
    s_alarm_programmed   = 0u;
    s_alarm_calls        = 0;
    s_cancel_alarm_calls = 0;
    s_radio_send_calls   = 0;
    s_radio_scan_calls   = 0;
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

void test_c3_start_runs_chain_from_now(void)
{
    s_rtc_ms = 45000000u;
    TEST_ASSERT_TRUE(TdmaMachine_Start());
    TEST_ASSERT_EQUAL(0, s_radio_scan_calls);

    TdmaMachine_SlotTask();                      /* cell 0: Sync TX */
    TEST_ASSERT_EQUAL(1, s_radio_send_calls);
    TEST_ASSERT_FALSE(TdmaMachine_IsCursorSuspect());
    TEST_ASSERT_NO_ARCLOG("SLOT_SUSPECT");
    TEST_ASSERT_EQUAL(45000000u + SLOT_STEP_MS, s_alarm_programmed);
}

void test_c3_suspect_wake_rearms_alarm_from_actual_wake(void)
{
    TEST_ASSERT_TRUE(TdmaMachine_Start());
    TdmaMachine_SlotTask();
    TEST_ASSERT_EQUAL(1u, TdmaMachine_GetCursor().cell_index);

    s_rtc_ms = s_alarm_programmed + 4000u;       /* 3000 + 4000 = 7000 */
    TdmaMachine_SlotTask();

    /* One advance only, and the next alarm is one step after the actual
     * wake, not in the past. C3 never drops to scanning. */
    TEST_ASSERT_TRUE(TdmaMachine_IsCursorSuspect());
    TEST_ASSERT_EQUAL(2u, TdmaMachine_GetCursor().cell_index);
    TEST_ASSERT_EQUAL(7000u + SLOT_STEP_MS, s_alarm_programmed);
    TEST_ASSERT_EQUAL(0, s_cancel_alarm_calls);
    TEST_ASSERT_EQUAL(0, s_radio_scan_calls);
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_c3_start_runs_chain_from_now);
    RUN_TEST(test_c3_suspect_wake_rearms_alarm_from_actual_wake);
    return UNITY_END();
}
