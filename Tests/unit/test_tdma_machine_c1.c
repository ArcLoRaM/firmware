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
 * C1 participates in the Sync phase as a pure receiver: it boots cold and
 * scans, and once its clock leaves CLOCK_COLD it opens a bounded Rx window
 * in every Sync cell and never transmits.
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
static int      s_radio_scan_calls;
static int      s_radio_sleep_calls;

static uint32_t stub_GetRtcMs(void)                          { return s_rtc_ms;               }
static void     stub_ProgramAlarmA(uint32_t t)               { s_alarm_programmed = t; s_alarm_calls++; }
static void     stub_CancelAlarmA(void)                      {                                }
static void     stub_RadioSetChannel(uint32_t f)             { (void)f; s_channel_calls++;    }
static void     stub_RadioSend(const uint8_t *b, uint8_t l)  { (void)b; (void)l; s_radio_send_calls++;   }
static void     stub_RadioSetRx(uint32_t w, uint32_t c)      { (void)w; (void)c; s_radio_set_rx_calls++; }
static void     stub_RadioScan(void)                         { s_radio_scan_calls++;          }
static void     stub_RadioSleep(void)                        { s_radio_sleep_calls++;          }
static uint32_t stub_RadioTimeOnAir(uint8_t len)             { (void)len; return 991u;        }
static void     stub_WaitUntilMs(uint32_t t)                 { s_rtc_ms = t;                  }

static const TdmaPlatform_t k_platform = {
    .GetRtcMs       = stub_GetRtcMs,
    .ProgramAlarmA  = stub_ProgramAlarmA,
    .CancelAlarmA   = stub_CancelAlarmA,
    .RadioSetChannel= stub_RadioSetChannel,
    .RadioSend      = stub_RadioSend,
    .RadioSetRx     = stub_RadioSetRx,
    .RadioScan      = stub_RadioScan,
    .RadioSleep     = stub_RadioSleep,
    .RadioTimeOnAir = stub_RadioTimeOnAir,
    .WaitUntilMs    = stub_WaitUntilMs,
};

/* =========================================================================
 * Fixtures
 * ========================================================================= */

static FrequencyResolverState_t s_freq_state;
static ComplianceStatus_t       s_comp_status;
static uint32_t comp_get_tick(void) { return s_rtc_ms; }
static const MAC_Hooks_t k_mac_hooks = {0};

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
    s_radio_scan_calls   = 0;
    s_radio_sleep_calls  = 0;

    s_freq_state.phases[0].cell_mode           = CELL_FREQ_STATIC;
    s_freq_state.phases[0].cell_freq_or_seed = 868100000u;
    s_freq_state.phases[1].cell_mode           = CELL_FREQ_STATIC;
    s_freq_state.phases[1].cell_freq_or_seed = 868100000u;

    FrequencyResolver_Init(&s_freq_state);
    ComplianceEngine_Init(&s_comp_status, comp_get_tick);
    MAC_Init(&k_mac_hooks);
    TdmaMachine_Init(&k_platform);
}

void tearDown(void) {}

/* =========================================================================
 * Tests
 * ========================================================================= */

void test_c1_boots_scanning(void)
{
    TEST_ASSERT_FALSE(TdmaMachine_Start());
    TEST_ASSERT_EQUAL(1, s_radio_scan_calls);
    TEST_ASSERT_EQUAL(0, s_alarm_calls);
}

void test_c1_receives_every_sync_cell_once_out_of_cold(void)
{
    /* Packet 1 at cell 0 → CLOCK_ACQUIRING; the chain can then run. */
    SyncPayload_t p;
    memset(&p, 0, sizeof(p));
    MAC_OnSyncPacketReceived(&p, 0u);
    TEST_ASSERT_TRUE(TdmaMachine_Start());

    for (int i = 0; i < 6; i++) {           /* both 3-cell Sync phases */
        s_rtc_ms = s_alarm_programmed;
        TdmaMachine_SlotTask();
    }
    TEST_ASSERT_EQUAL(6, s_radio_set_rx_calls);
    TEST_ASSERT_EQUAL(0, s_radio_send_calls);
    TEST_ASSERT_EQUAL(6, s_alarm_calls);
}

/* =========================================================================
 * main
 * ========================================================================= */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_c1_boots_scanning);
    RUN_TEST(test_c1_receives_every_sync_cell_once_out_of_cold);
    return UNITY_END();
}
