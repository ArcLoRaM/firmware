#include "unity.h"
#include "tdma_machine.h"
#include "mac_state_machine.h"
#include "freq_resolver.h"
#include "compliance_engine.h"
#include "shared_mem.h"
#include "tdma_table.h"
#include <string.h>
#include <stdint.h>

/*
 * C1 does not appear in the stub TDMA table's participant_mask
 * (Sync phase: PARTICIPANT_C2 | PARTICIPANT_C3).
 * These tests verify that the machine skips all radio calls and still
 * programs the alarm when the local node class is excluded.
 */

/* =========================================================================
 * Platform stubs
 * ========================================================================= */

static uint32_t s_rtc_ms;
static uint32_t s_alarm_programmed;
static int      s_alarm_calls;
static int      s_channel_calls;
static int      s_radio_send_calls;
static int      s_radio_set_rx_calls;
static int      s_radio_sleep_calls;

static uint32_t stub_GetRtcMs(void)                          { return s_rtc_ms;               }
static void     stub_ProgramAlarmA(uint32_t t)               { s_alarm_programmed = t; s_alarm_calls++; }
static void     stub_RadioSetChannel(uint32_t f)             { (void)f; s_channel_calls++;    }
static void     stub_RadioSend(const uint8_t *b, uint8_t l)  { (void)b; (void)l; s_radio_send_calls++;   }
static void     stub_RadioSetRx(uint32_t ms)                 { (void)ms; s_radio_set_rx_calls++; }
static void     stub_RadioSleep(void)                        { s_radio_sleep_calls++;          }
static uint32_t stub_RadioTimeOnAir(void)                    { return 2500u;                   }

static const TdmaPlatform_t k_platform = {
    .GetRtcMs       = stub_GetRtcMs,
    .ProgramAlarmA  = stub_ProgramAlarmA,
    .RadioSetChannel= stub_RadioSetChannel,
    .RadioSend      = stub_RadioSend,
    .RadioSetRx     = stub_RadioSetRx,
    .RadioSleep     = stub_RadioSleep,
    .RadioTimeOnAir = stub_RadioTimeOnAir,
};

/* =========================================================================
 * Fixtures
 * ========================================================================= */

static FrequencyResolverState_t s_freq_state;
static ComplianceStatus_t       s_comp_status;
static uint32_t comp_get_tick(void) { return s_rtc_ms; }
static const MAC_Hooks_t k_mac_hooks = { NULL, NULL, NULL, NULL };

void setUp(void)
{
    memset(&s_freq_state,  0, sizeof(s_freq_state));
    memset(&s_comp_status, 0, sizeof(s_comp_status));
    s_rtc_ms           = 0u;
    s_alarm_programmed = 0u;
    s_alarm_calls      = 0;
    s_channel_calls    = 0;
    s_radio_send_calls = 0;
    s_radio_set_rx_calls = 0;
    s_radio_sleep_calls  = 0;

    s_freq_state.phases[0].cell_mode           = CELL_FREQ_STATIC;
    s_freq_state.phases[0].cell.static_freq_hz = 868100000u;

    FrequencyResolver_Init(&s_freq_state);
    ComplianceEngine_Init(&s_comp_status, comp_get_tick);
    MAC_Init(&k_mac_hooks);
    TdmaMachine_Init(&k_platform);
}

void tearDown(void) {}

/* =========================================================================
 * participant_mask skip tests (NODE_CLASS=C1, excluded from Sync phase)
 * ========================================================================= */

void test_participant_mask_skip_no_radio_calls(void)
{
    TdmaMachine_SlotTask();

    /*
     * Sync phase participant_mask = C2|C3.  C1 is excluded.
     * No radio function should be called.
     */
    TEST_ASSERT_EQUAL(0, s_channel_calls);
    TEST_ASSERT_EQUAL(0, s_radio_send_calls);
    TEST_ASSERT_EQUAL(0, s_radio_set_rx_calls);
    TEST_ASSERT_EQUAL(0, s_radio_sleep_calls);
}

void test_participant_mask_skip_programs_alarm(void)
{
    TdmaMachine_SlotTask();

    /* Alarm must still be programmed even when the entire phase is skipped */
    TEST_ASSERT_EQUAL(1, s_alarm_calls);
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_participant_mask_skip_no_radio_calls);
    RUN_TEST(test_participant_mask_skip_programs_alarm);
    return UNITY_END();
}
