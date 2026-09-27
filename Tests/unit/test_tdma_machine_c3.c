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
 * and never stops for scanning. It transmits in the first SYNC_TX_BUDGET
 * cells of every Sync phase, which makes it the reference for Tx timing
 * (ADR-0016): every packet starts on air exactly at its slot's nominal
 * start, the node waking one Tx lead early and firing at the nominal start
 * minus the radio's ramp.
 */

/* =========================================================================
 * Platform stubs
 * ========================================================================= */

static uint32_t s_rtc_ms;
static uint32_t s_alarm_programmed;
static int      s_alarm_calls;
static int      s_cancel_alarm_calls;
static int      s_radio_send_calls;
static int      s_radio_prepare_tx_calls;
static int      s_radio_scan_calls;
static int      s_radio_sleep_calls;
static int      s_wait_calls;
static uint32_t s_wait_target;
/* Call order: each stub records the sequence number of its latest call. */
static int      s_seq;
static int      s_prepare_seq;
static int      s_wait_seq;
static int      s_send_seq;

static uint32_t stub_GetRtcMs(void)                          { return s_rtc_ms;                         }
static void     stub_ProgramAlarmA(uint32_t t)               { s_alarm_programmed = t; s_alarm_calls++; }
static void     stub_CancelAlarmA(void)                      { s_cancel_alarm_calls++;                  }
static void     stub_RadioSetChannel(uint32_t f)             { (void)f;                                 }
static void     stub_RadioPrepareTx(void)                    { s_radio_prepare_tx_calls++; s_prepare_seq = ++s_seq; }
static void     stub_RadioSend(const uint8_t *b, uint8_t l)  { (void)b; (void)l; s_radio_send_calls++; s_send_seq = ++s_seq; }
static void     stub_RadioSetRx(uint32_t w, uint32_t c)      { (void)w; (void)c;                        }
static void     stub_RadioScan(void)                         { s_radio_scan_calls++;                    }
static void     stub_RadioSleep(void)                        { s_radio_sleep_calls++;                   }
static uint32_t stub_RadioTimeOnAir(uint8_t len)             { (void)len; return 991u;                  }
static void     stub_WaitUntilMs(uint32_t t)
{
    s_wait_calls++;
    s_wait_target = t;
    s_wait_seq    = ++s_seq;
    s_rtc_ms      = t;
}

static const TdmaPlatform_t k_platform = {
    .GetRtcMs        = stub_GetRtcMs,
    .ProgramAlarmA   = stub_ProgramAlarmA,
    .CancelAlarmA    = stub_CancelAlarmA,
    .RadioSetChannel = stub_RadioSetChannel,
    .RadioPrepareTx  = stub_RadioPrepareTx,
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
    s_alarm_calls            = 0;
    s_cancel_alarm_calls     = 0;
    s_radio_send_calls       = 0;
    s_radio_prepare_tx_calls = 0;
    s_radio_scan_calls       = 0;
    s_radio_sleep_calls      = 0;
    s_wait_calls             = 0;
    s_wait_target            = 0u;
    s_seq = s_prepare_seq = s_wait_seq = s_send_seq = 0;
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

/* Call SlotTask after advancing the simulated RTC to the programmed alarm. */
static void step_slot(void)
{
    s_rtc_ms = s_alarm_programmed;
    TdmaMachine_SlotTask();
}

/* =========================================================================
 * Chain start
 * ========================================================================= */

void test_c3_start_runs_chain_from_now(void)
{
    /* The boot wake is the first slot's Tx lead: that slot starts one lead
     * from now, so its packet is on air at a nominal start, not late. */
    s_rtc_ms = 45000000u;
    TEST_ASSERT_TRUE(TdmaMachine_Start());
    TEST_ASSERT_EQUAL(0, s_radio_scan_calls);

    TdmaMachine_SlotTask();                      /* cell 0: Sync TX */
    TEST_ASSERT_EQUAL(1, s_radio_send_calls);
    TEST_ASSERT_EQUAL(45000000u + TX_LEAD_MS, s_wait_target);
    TEST_ASSERT_FALSE(TdmaMachine_IsCursorSuspect());
    TEST_ASSERT_NO_ARCLOG("SLOT_SUSPECT");
    TEST_ASSERT_NO_ARCLOG("TX_LATE");
    TEST_ASSERT_ARCLOG("SYNC_TX ph=0 ce=0 ep=45000020 plan=45000020 send=45000020");
}

/* =========================================================================
 * Deterministic Tx start (ADR-0016)
 * ========================================================================= */

void test_c3_tx_slot_woken_one_lead_early(void)
{
    TEST_ASSERT_TRUE(TdmaMachine_Start());       /* cell 0 starts at 20 */
    TdmaMachine_SlotTask();
    /* Cell 1 (Tx) starts at 3020: woken one Tx lead early. */
    TEST_ASSERT_EQUAL(TX_LEAD_MS + SLOT_STEP_MS - TX_LEAD_MS, s_alarm_programmed);
}

void test_c3_tx_fires_at_nominal_slot_start(void)
{
    TEST_ASSERT_TRUE(TdmaMachine_Start());
    TdmaMachine_SlotTask();
    ArcLog_CaptureReset();
    step_slot();                                 /* cell 1, woken at 3000 */

    TEST_ASSERT_EQUAL(2, s_wait_calls);
    TEST_ASSERT_EQUAL(TX_LEAD_MS + SLOT_STEP_MS, s_wait_target);
    TEST_ASSERT_EQUAL(2, s_radio_send_calls);
    /* The epoch is the phase's nominal start (cell 0 on air), and every
     * cell's packet starts on air at its own nominal start. */
    TEST_ASSERT_ARCLOG("SYNC_TX ph=0 ce=1 ep=20 plan=3020 send=3020");
}

void test_c3_tx_fires_one_ramp_before_nominal(void)
{
    TdmaPlatform_t plat = k_platform;
    plat.tx_ramp_ms = 3u;
    TdmaMachine_Init(&plat);

    TEST_ASSERT_TRUE(TdmaMachine_Start());
    TdmaMachine_SlotTask();

    /* The radio takes 3 ms from Radio.Send to air: fire 3 ms early so the
     * packet still starts on air at the nominal start. */
    TEST_ASSERT_EQUAL(TX_LEAD_MS - 3u, s_wait_target);
    TEST_ASSERT_ARCLOG("SYNC_TX ph=0 ce=0 ep=20 plan=20 send=17");
}

void test_c3_tx_prepares_radio_before_waiting_and_sending(void)
{
    TEST_ASSERT_TRUE(TdmaMachine_Start());
    TdmaMachine_SlotTask();
    TEST_ASSERT_EQUAL(1, s_radio_prepare_tx_calls);
    TEST_ASSERT_TRUE(s_prepare_seq < s_wait_seq);
    TEST_ASSERT_TRUE(s_wait_seq < s_send_seq);
}

void test_c3_tx_denied_puts_prepared_radio_to_sleep(void)
{
    /* Exhaust all compliance credit before the slot runs. */
    ComplianceEngine_RequestChannel(868300000u, 36000u, 14);
    TEST_ASSERT_TRUE(TdmaMachine_Start());
    TdmaMachine_SlotTask();

    TEST_ASSERT_EQUAL(1, s_radio_prepare_tx_calls);
    TEST_ASSERT_EQUAL(0, s_radio_send_calls);
    TEST_ASSERT_EQUAL(0, s_wait_calls);
    TEST_ASSERT_EQUAL(1, s_radio_sleep_calls);
    TEST_ASSERT_ARCLOG("TX_DENIED");
    TEST_ASSERT_EQUAL(1, s_alarm_calls);         /* chain continues */
}

void test_c3_tx_woken_at_fire_instant_is_not_late(void)
{
    TEST_ASSERT_TRUE(TdmaMachine_Start());
    TdmaMachine_SlotTask();
    s_rtc_ms = s_alarm_programmed + TX_LEAD_MS;  /* wake exactly at 3020 */
    ArcLog_CaptureReset();
    TdmaMachine_SlotTask();

    TEST_ASSERT_EQUAL(2, s_radio_send_calls);
    TEST_ASSERT_NO_ARCLOG("TX_LATE");
}

void test_c3_late_sync_tx_is_dropped(void)
{
    /* The slot task reached the fire instant after it passed: the packet
     * would be on air late and receivers would read the lateness as clock
     * error. A Sync packet is dropped; the chain continues. */
    TEST_ASSERT_TRUE(TdmaMachine_Start());
    TdmaMachine_SlotTask();
    int sends = s_radio_send_calls;
    int waits = s_wait_calls;
    s_rtc_ms = s_alarm_programmed + TX_LEAD_MS + 5u;   /* 3025, 5 ms late */
    ArcLog_CaptureReset();
    TdmaMachine_SlotTask();

    TEST_ASSERT_ARCLOG("TX_LATE plan=3020 fire=3020 now=3025");
    TEST_ASSERT_EQUAL(sends, s_radio_send_calls);
    TEST_ASSERT_EQUAL(waits, s_wait_calls);
    TEST_ASSERT_EQUAL(1, s_radio_sleep_calls);
    TEST_ASSERT_NO_ARCLOG("SYNC_TX");
    TEST_ASSERT_EQUAL(TX_LEAD_MS + 2u * SLOT_STEP_MS - TX_LEAD_MS, s_alarm_programmed);
}

/* =========================================================================
 * Cursor integrity checkpoint
 * ========================================================================= */

void test_c3_suspect_wake_rearms_alarm_from_actual_wake(void)
{
    TEST_ASSERT_TRUE(TdmaMachine_Start());
    TdmaMachine_SlotTask();
    TEST_ASSERT_EQUAL(1u, TdmaMachine_GetCursor().cell_index);

    s_rtc_ms = s_alarm_programmed + 4000u;       /* 3000 + 4000 = 7000 */
    TdmaMachine_SlotTask();

    /* One advance only, and the next alarm is one step after the actual
     * wake (the slot is taken to start there), one Tx lead early, not in the
     * past. C3 never drops to scanning. */
    TEST_ASSERT_TRUE(TdmaMachine_IsCursorSuspect());
    TEST_ASSERT_EQUAL(2u, TdmaMachine_GetCursor().cell_index);
    TEST_ASSERT_EQUAL(7000u + SLOT_STEP_MS - TX_LEAD_MS, s_alarm_programmed);
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
    RUN_TEST(test_c3_tx_slot_woken_one_lead_early);
    RUN_TEST(test_c3_tx_fires_at_nominal_slot_start);
    RUN_TEST(test_c3_tx_fires_one_ramp_before_nominal);
    RUN_TEST(test_c3_tx_prepares_radio_before_waiting_and_sending);
    RUN_TEST(test_c3_tx_denied_puts_prepared_radio_to_sleep);
    RUN_TEST(test_c3_tx_woken_at_fire_instant_is_not_late);
    RUN_TEST(test_c3_late_sync_tx_is_dropped);
    RUN_TEST(test_c3_suspect_wake_rearms_alarm_from_actual_wake);
    return UNITY_END();
}
