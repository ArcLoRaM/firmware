#include "unity.h"
#include "mac_state_machine.h"
#include "tdma_table.h"
#include <string.h>

/* ------- hook stubs ------------------------------------------------------- */

static int      s_rtc_set_calls;
static int      s_sync_locked_calls;
static int      s_sync_lost_calls;
static uint32_t s_snapshot_ms;

static void stub_rtc_set(uint32_t target_ms,
                          uint8_t day, uint8_t month, uint8_t year)
{
    (void)day; (void)month; (void)year;
    s_snapshot_ms = target_ms;
    s_rtc_set_calls++;
}

static void stub_get_rtc_snapshot(uint32_t *ms,
                                   uint8_t *day, uint8_t *month, uint8_t *year)
{
    *ms = s_snapshot_ms; *day = 0x01u; *month = 0x01u; *year = 0x24u;
}

static void stub_sync_locked(void) { s_sync_locked_calls++; }
static void stub_sync_lost(void)   { s_sync_lost_calls++;   }

static const MAC_Hooks_t k_hooks = {
    .rtc_set           = stub_rtc_set,
    .get_rtc_snapshot  = stub_get_rtc_snapshot,
    .sync_bootstrapped = NULL,
    .sync_locked       = stub_sync_locked,
    .sync_lost         = stub_sync_lost,
};

/* ------- helpers ---------------------------------------------------------- */

/*
 * Stub TDMA table: PHASE_TYPE_SYNC, slot_active_ms=2500, gap_slots_ms[0]=500
 * Per-cell step = 3000 ms. Drive to CLOCK_WARM.
 */
static void three_sync_packets(void)
{
    SyncPayload_t p;
    memset(&p, 0, sizeof(p));
    p.sync_phase_index             = 0u;
    p.ms_since_midnight_sync_phase = 0u;
    s_snapshot_ms                  = 0u;

    p.sync_cell_index = 0u;  MAC_OnSyncPacketReceived(&p, 0u);
    p.sync_cell_index = 1u;  MAC_OnSyncPacketReceived(&p, 3000u);
    p.sync_cell_index = 2u;  MAC_OnSyncPacketReceived(&p, 6000u);
}

void setUp(void)
{
    s_rtc_set_calls    = 0;
    s_sync_locked_calls = 0;
    s_sync_lost_calls  = 0;
    s_snapshot_ms      = 0u;
    MAC_Init(&k_hooks);
}

void tearDown(void) {}

/* ------- Boot state ------------------------------------------------------- */

void test_c1_boot_state_is_scanning(void)
{
    TEST_ASSERT_EQUAL(MAC_STATE_SCANNING, MAC_GetState());
}

void test_c1_boot_clock_state_is_cold(void)
{
    TEST_ASSERT_EQUAL(CLOCK_COLD, MAC_GetClockState());
}

/* ------- Scanning — always RX --------------------------------------------- */

void test_c1_scanning_returns_rx_for_sync_phase(void)
{
    FrameCursor_t c = {.phase_index = 0u, .cell_index = 0u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&c, TdmaTable_GetPhase(0u)));
}

void test_c1_scanning_returns_rx_for_any_phase(void)
{
    static const Phase_t s_cluster = {
        .type           = PHASE_TYPE_CLUSTER_EXCHANGE,
        .direction_mode = DIRECTION_CELL_SKIP,
        .cell_count     = 4u,
        .slot_active_ms = 1000u,
    };
    FrameCursor_t c = {.phase_index = 1u, .cell_index = 0u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&c, &s_cluster));
}

/* ------- Sync acquisition ------------------------------------------------- */

void test_c1_sync_pkt1_transitions_acquiring(void)
{
    SyncPayload_t p;
    memset(&p, 0, sizeof(p));
    p.sync_cell_index = 0u;
    MAC_OnSyncPacketReceived(&p, 0u);
    TEST_ASSERT_EQUAL(CLOCK_ACQUIRING, MAC_GetClockState());
}

void test_c1_sync_pkt1_calls_rtc_set_hook(void)
{
    SyncPayload_t p;
    memset(&p, 0, sizeof(p));
    p.sync_cell_index = 0u;
    MAC_OnSyncPacketReceived(&p, 0u);
    TEST_ASSERT_EQUAL(1, s_rtc_set_calls);
}

void test_c1_sync_pkt3_valid_transitions_warm(void)
{
    three_sync_packets();
    TEST_ASSERT_EQUAL(CLOCK_WARM, MAC_GetClockState());
}

void test_c1_sync_pkt3_valid_transitions_synchronized(void)
{
    three_sync_packets();
    TEST_ASSERT_EQUAL(MAC_STATE_SYNCHRONIZED, MAC_GetState());
}

void test_c1_sync_locked_called_once(void)
{
    three_sync_packets();
    TEST_ASSERT_EQUAL(1, s_sync_locked_calls);
}

void test_c1_sync_pkt3_large_error_stays_acquiring(void)
{
    SyncPayload_t p;
    memset(&p, 0, sizeof(p));
    p.sync_phase_index             = 0u;
    p.ms_since_midnight_sync_phase = 0u;
    s_snapshot_ms                  = 0u;
    p.sync_cell_index = 0u;  MAC_OnSyncPacketReceived(&p, 0u);
    p.sync_cell_index = 1u;  MAC_OnSyncPacketReceived(&p, 3000u);
    /* cell=2, expected=6000, preamble=100 → error=5900 >> 8ms → consecutive reset */
    p.sync_cell_index = 2u;  MAC_OnSyncPacketReceived(&p, 100u);
    TEST_ASSERT_EQUAL(CLOCK_ACQUIRING, MAC_GetClockState());
    TEST_ASSERT_EQUAL(0, s_sync_locked_calls);
}

/* ------- SYNC_LOST (Tier 3: error ≥ 300 ms) ------------------------------- */

void test_c1_sync_lost_resets_to_cold(void)
{
    three_sync_packets();
    /* cell=0, ms_midnight=0, expected_arrival=0, preamble=400 → error=400 ≥ 300ms → Tier 3 */
    SyncPayload_t p;
    memset(&p, 0, sizeof(p));
    p.sync_cell_index              = 0u;
    p.ms_since_midnight_sync_phase = 0u;
    MAC_OnSyncPacketReceived(&p, 400u);
    TEST_ASSERT_EQUAL(CLOCK_COLD, MAC_GetClockState());
}

void test_c1_sync_lost_returns_to_scanning(void)
{
    three_sync_packets();
    SyncPayload_t p;
    memset(&p, 0, sizeof(p));
    p.sync_cell_index              = 0u;
    p.ms_since_midnight_sync_phase = 0u;
    MAC_OnSyncPacketReceived(&p, 400u);
    TEST_ASSERT_EQUAL(MAC_STATE_SCANNING, MAC_GetState());
}

void test_c1_sync_lost_calls_hook(void)
{
    three_sync_packets();
    SyncPayload_t p;
    memset(&p, 0, sizeof(p));
    p.sync_cell_index              = 0u;
    p.ms_since_midnight_sync_phase = 0u;
    MAC_OnSyncPacketReceived(&p, 400u);
    TEST_ASSERT_EQUAL(1, s_sync_lost_calls);
}

/* ------- phase_tx_flag = 0 for C1 ---------------------------------------- */

void test_c1_phase_tx_flag_always_zero(void)
{
    three_sync_packets();
    FrameCursor_t c = {.phase_index = 0u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c, TdmaTable_GetPhase(0u));
    TEST_ASSERT_EQUAL(0u, MAC_GetPhaseTxFlag());

    FrameCursor_t c2 = {.phase_index = 1u, .cell_index = 0u, .slot_index = 0u};
    static const Phase_t s_other = {
        .type = PHASE_TYPE_MESH_BEACON, .direction_mode = DIRECTION_MAC_CELL, .cell_count = 3u
    };
    MAC_OnSlotOpportunity(&c2, &s_other);
    MAC_OnSlotOpportunity(&c, TdmaTable_GetPhase(0u));
    TEST_ASSERT_EQUAL(0u, MAC_GetPhaseTxFlag());
}

/* ------- Beacon → Paired -------------------------------------------------- */

void test_c1_beacon_transitions_to_paired(void)
{
    three_sync_packets();
    BeaconPayload_t beacon = {.hop_count = 1u, .node_id = 10u, .route_cost = 100u};
    MAC_OnBeaconReceived(&beacon);
    TEST_ASSERT_EQUAL(MAC_STATE_PAIRED, MAC_GetState());
}

void test_c1_cell_eligibility_written_on_beacon(void)
{
    three_sync_packets();
    BeaconPayload_t beacon = {.hop_count = 1u, .node_id = 10u, .route_cost = 100u};
    MAC_OnBeaconReceived(&beacon);
    /* our hop=2: uplink = (1<<(2%3)) | (1<<(3%3)) = 0x04 | 0x01 = 0x05 */
    TEST_ASSERT_EQUAL_HEX8(0x05u, MAC_GetCellEligibilityMask_Uplink());
}

/* ------- main ------------------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_c1_boot_state_is_scanning);
    RUN_TEST(test_c1_boot_clock_state_is_cold);
    RUN_TEST(test_c1_scanning_returns_rx_for_sync_phase);
    RUN_TEST(test_c1_scanning_returns_rx_for_any_phase);
    RUN_TEST(test_c1_sync_pkt1_transitions_acquiring);
    RUN_TEST(test_c1_sync_pkt1_calls_rtc_set_hook);
    RUN_TEST(test_c1_sync_pkt3_valid_transitions_warm);
    RUN_TEST(test_c1_sync_pkt3_valid_transitions_synchronized);
    RUN_TEST(test_c1_sync_locked_called_once);
    RUN_TEST(test_c1_sync_pkt3_large_error_stays_acquiring);
    RUN_TEST(test_c1_sync_lost_resets_to_cold);
    RUN_TEST(test_c1_sync_lost_returns_to_scanning);
    RUN_TEST(test_c1_sync_lost_calls_hook);
    RUN_TEST(test_c1_phase_tx_flag_always_zero);
    RUN_TEST(test_c1_beacon_transitions_to_paired);
    RUN_TEST(test_c1_cell_eligibility_written_on_beacon);
    return UNITY_END();
}
