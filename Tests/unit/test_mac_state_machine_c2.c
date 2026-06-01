#include "unity.h"
#include "mac_state_machine.h"
#include "tdma_table.h"
#include <string.h>

/* ------- hook stubs ------------------------------------------------------- */

static int s_rtc_set_calls;
static int s_rtc_align_calls;
static int s_sync_locked_calls;
static int s_sync_lost_calls;

static void stub_rtc_set(uint8_t h, uint8_t m, uint8_t s,
                          uint8_t d, uint8_t mo, uint8_t y, uint32_t ss)
{
    (void)h; (void)m; (void)s; (void)d; (void)mo; (void)y; (void)ss;
    s_rtc_set_calls++;
}
static void stub_rtc_align(uint32_t preamble_ms, uint32_t expected_ms)
{
    (void)preamble_ms; (void)expected_ms;
    s_rtc_align_calls++;
}
static void stub_sync_locked(void) { s_sync_locked_calls++; }
static void stub_sync_lost(void)   { s_sync_lost_calls++;   }

static const MAC_Hooks_t k_hooks = {
    .rtc_set             = stub_rtc_set,
    .rtc_align_subsecond = stub_rtc_align,
    .sync_locked         = stub_sync_locked,
    .sync_lost           = stub_sync_lost,
};

/* ------- helpers ---------------------------------------------------------- */

/*
 * Stub TDMA table: PHASE_TYPE_SYNC, slot_active_ms=2500, gap_slots_ms[0]=500
 * Per-slot step = 3000 ms.
 */
static void three_sync_packets(void)
{
    SyncPayload_t p;
    memset(&p, 0, sizeof(p));
    p.sync_slot_index = 0u;  MAC_OnSyncPacketReceived(&p, 0u);
    p.sync_slot_index = 1u;  MAC_OnSyncPacketReceived(&p, 3000u);
    p.sync_slot_index = 2u;  MAC_OnSyncPacketReceived(&p, 6000u);
}

static void receive_first_beacon(uint8_t hop, uint16_t cost)
{
    BeaconPayload_t b = {.hop_count = hop, .node_id = 42u, .route_cost = cost};
    MAC_OnBeaconReceived(&b);
}

/* Beacon phase passed directly to MAC_OnSlotOpportunity for budget tests */
static const Phase_t s_beacon_phase = {
    .type           = PHASE_TYPE_MESH_BEACON,
    .direction_mode = DIRECTION_MAC_CELL,
    .cell_count     = 10u,
    .slot_count     = 1u,
    .slot_active_ms = 2000u,
};

/* Another phase used to force a phase-entry transition */
static const Phase_t s_other_phase = {
    .type           = PHASE_TYPE_MESH_UPLINK,
    .direction_mode = DIRECTION_CELL_SKIP,
    .cell_count     = 3u,
    .slot_active_ms = 2500u,
};

void setUp(void)
{
    s_rtc_set_calls     = 0;
    s_rtc_align_calls   = 0;
    s_sync_locked_calls = 0;
    s_sync_lost_calls   = 0;
    MAC_Init(&k_hooks);
}

void tearDown(void) {}

/* ------- Boot state ------------------------------------------------------- */

void test_c2_boot_state_is_scanning(void)
{
    TEST_ASSERT_EQUAL(MAC_STATE_SCANNING, MAC_GetState());
}

void test_c2_boot_clock_state_is_cold(void)
{
    TEST_ASSERT_EQUAL(CLOCK_COLD, MAC_GetClockState());
}

/* ------- Scanning — always RX --------------------------------------------- */

void test_c2_scanning_returns_rx_for_sync_phase(void)
{
    FrameCursor_t c = {.phase_index = 0u, .cell_index = 0u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&c, TdmaTable_GetPhase(0u)));
}

void test_c2_scanning_returns_rx_for_beacon_phase(void)
{
    FrameCursor_t c = {.phase_index = 5u, .cell_index = 0u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&c, &s_beacon_phase));
}

/* ------- Three-packet Sync acquisition ------------------------------------ */

void test_c2_sync_pkt1_transitions_acquiring(void)
{
    SyncPayload_t p;
    memset(&p, 0, sizeof(p));
    p.sync_slot_index = 0u;
    MAC_OnSyncPacketReceived(&p, 0u);
    TEST_ASSERT_EQUAL(CLOCK_ACQUIRING, MAC_GetClockState());
}

void test_c2_sync_pkt1_calls_rtc_set_hook(void)
{
    SyncPayload_t p;
    memset(&p, 0, sizeof(p));
    p.sync_slot_index = 0u;
    MAC_OnSyncPacketReceived(&p, 0u);
    TEST_ASSERT_EQUAL(1, s_rtc_set_calls);
}

void test_c2_sync_pkt2_calls_rtc_align_hook(void)
{
    SyncPayload_t p;
    memset(&p, 0, sizeof(p));
    p.sync_slot_index = 0u;  MAC_OnSyncPacketReceived(&p, 0u);
    p.sync_slot_index = 1u;  MAC_OnSyncPacketReceived(&p, 3000u);
    TEST_ASSERT_EQUAL(1, s_rtc_align_calls);
}

void test_c2_sync_pkt3_valid_transitions_warm(void)
{
    three_sync_packets();
    TEST_ASSERT_EQUAL(CLOCK_WARM, MAC_GetClockState());
}

void test_c2_sync_pkt3_valid_transitions_synchronized(void)
{
    three_sync_packets();
    TEST_ASSERT_EQUAL(MAC_STATE_SYNCHRONIZED, MAC_GetState());
}

void test_c2_sync_locked_called_once(void)
{
    three_sync_packets();
    TEST_ASSERT_EQUAL(1, s_sync_locked_calls);
}

void test_c2_sync_pkt3_large_error_stays_acquiring(void)
{
    SyncPayload_t p;
    memset(&p, 0, sizeof(p));
    p.sync_slot_index = 0u;  MAC_OnSyncPacketReceived(&p, 0u);
    p.sync_slot_index = 1u;  MAC_OnSyncPacketReceived(&p, 3000u);
    /* Packet 3: expected=6000, preamble=100 → error=5900 >> SYNC_LOCK_THRESHOLD_MS */
    p.sync_slot_index = 2u;  MAC_OnSyncPacketReceived(&p, 100u);
    TEST_ASSERT_EQUAL(CLOCK_ACQUIRING, MAC_GetClockState());
    TEST_ASSERT_EQUAL(0, s_sync_locked_calls);
}

/* ------- SYNC_LOST -------------------------------------------------------- */

void test_c2_sync_lost_resets_to_scanning(void)
{
    three_sync_packets();
    /* sync_slot_index=0 expected=0, preamble=100 → error=100 >> 16 */
    SyncPayload_t p;
    memset(&p, 0, sizeof(p));
    p.sync_slot_index = 0u;
    MAC_OnSyncPacketReceived(&p, 100u);
    TEST_ASSERT_EQUAL(MAC_STATE_SCANNING, MAC_GetState());
    TEST_ASSERT_EQUAL(CLOCK_COLD, MAC_GetClockState());
}

void test_c2_sync_lost_calls_hook(void)
{
    three_sync_packets();
    SyncPayload_t p;
    memset(&p, 0, sizeof(p));
    p.sync_slot_index = 0u;
    MAC_OnSyncPacketReceived(&p, 100u);
    TEST_ASSERT_EQUAL(1, s_sync_lost_calls);
}

/* ------- CellEligibilityMask for hop 0–5 ---------------------------------- */

void test_c2_cell_eligibility_all_hops(void)
{
    three_sync_packets();
    for (uint8_t hop = 0u; hop <= 5u; hop++) {
        /*
         * beacon.hop_count=hop means the PEER is at depth hop.
         * Our own hop_count = peer.hop_count + 1.
         */
        BeaconPayload_t b = {.hop_count = hop, .node_id = 1u, .route_cost = 100u};
        MAC_OnBeaconReceived(&b);

        uint8_t our_hop     = (uint8_t)(hop + 1u);
        uint8_t ul_expected = (uint8_t)((1u << (our_hop % 3u)) | (1u << ((our_hop + 1u) % 3u)));
        uint8_t dl_expected = (uint8_t)((1u << ((our_hop + 2u) % 3u)) | (1u << ((our_hop + 1u) % 3u)));

        TEST_ASSERT_EQUAL_HEX8(ul_expected, MAC_GetCellEligibilityMask_Uplink());
        TEST_ASSERT_EQUAL_HEX8(dl_expected, MAC_GetCellEligibilityMask_Downlink());
    }
}

/* ------- Beacon → Paired -------------------------------------------------- */

void test_c2_beacon_transitions_to_paired(void)
{
    three_sync_packets();
    receive_first_beacon(1u, 100u);
    TEST_ASSERT_EQUAL(MAC_STATE_PAIRED, MAC_GetState());
}

/* ------- BeaconTxBudget --------------------------------------------------- */

void test_c2_beacon_tx_budget_set_on_first_beacon(void)
{
    three_sync_packets();
    receive_first_beacon(1u, 100u);

    /* First K cells of a beacon phase must return TX */
    FrameCursor_t c = {.phase_index = 5u, .cell_index = 0u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&c, &s_beacon_phase));
}

void test_c2_beacon_tx_budget_decrements_on_tx(void)
{
    three_sync_packets();
    receive_first_beacon(1u, 100u);

    /* Consume both TX slots (cells 0 and 1 are the K-range) */
    FrameCursor_t c0 = {.phase_index = 5u, .cell_index = 0u, .slot_index = 0u};
    FrameCursor_t c1 = {.phase_index = 5u, .cell_index = 1u, .slot_index = 0u};
    FrameCursor_t c2 = {.phase_index = 5u, .cell_index = 2u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c0, &s_beacon_phase);   /* TX, budget→1 */
    MAC_OnSlotOpportunity(&c1, &s_beacon_phase);   /* TX, budget→0 */

    /* Next beacon phase: budget=0 → RX even for K-range cells */
    FrameCursor_t nc0 = {.phase_index = 6u, .cell_index = 0u, .slot_index = 0u};
    FrameCursor_t nc1 = {.phase_index = 6u, .cell_index = 1u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&nc0, &s_beacon_phase));
    TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&nc1, &s_beacon_phase));
    /* Silence unused-variable warning for c2 */
    (void)c2;
}

void test_c2_beacon_tx_budget_resets_on_hop_change(void)
{
    three_sync_packets();
    receive_first_beacon(1u, 100u);   /* budget = 2 */

    /* Exhaust budget */
    FrameCursor_t c0 = {.phase_index = 5u, .cell_index = 0u};
    FrameCursor_t c1 = {.phase_index = 5u, .cell_index = 1u};
    MAC_OnSlotOpportunity(&c0, &s_beacon_phase);
    MAC_OnSlotOpportunity(&c1, &s_beacon_phase);

    /* New beacon with different hop_count → structural change → budget = 2 */
    BeaconPayload_t b2 = {.hop_count = 2u, .node_id = 42u, .route_cost = 100u};
    MAC_OnBeaconReceived(&b2);

    FrameCursor_t nc0 = {.phase_index = 7u, .cell_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&nc0, &s_beacon_phase));
}

/* ------- phase_tx_flag C2 alternates -------------------------------------- */

void test_c2_phase_tx_flag_alternates(void)
{
    three_sync_packets();

    /* First Sync phase entry (audit_cycle=0 even → TX, flag=1) */
    FrameCursor_t c0 = {.phase_index = 0u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c0, TdmaTable_GetPhase(0u));
    PhaseTxFlag_t flag1 = MAC_GetPhaseTxFlag();

    /* Force phase-index transition so next entry to phase 0 is detected */
    FrameCursor_t co = {.phase_index = 1u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&co, &s_other_phase);

    /* Second Sync phase entry (audit_cycle=1 odd → RX, flag=0) */
    MAC_OnSlotOpportunity(&c0, TdmaTable_GetPhase(0u));
    PhaseTxFlag_t flag2 = MAC_GetPhaseTxFlag();

    TEST_ASSERT_NOT_EQUAL(flag1, flag2);
}

/* ------- main ------------------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_c2_boot_state_is_scanning);
    RUN_TEST(test_c2_boot_clock_state_is_cold);
    RUN_TEST(test_c2_scanning_returns_rx_for_sync_phase);
    RUN_TEST(test_c2_scanning_returns_rx_for_beacon_phase);
    RUN_TEST(test_c2_sync_pkt1_transitions_acquiring);
    RUN_TEST(test_c2_sync_pkt1_calls_rtc_set_hook);
    RUN_TEST(test_c2_sync_pkt2_calls_rtc_align_hook);
    RUN_TEST(test_c2_sync_pkt3_valid_transitions_warm);
    RUN_TEST(test_c2_sync_pkt3_valid_transitions_synchronized);
    RUN_TEST(test_c2_sync_locked_called_once);
    RUN_TEST(test_c2_sync_pkt3_large_error_stays_acquiring);
    RUN_TEST(test_c2_sync_lost_resets_to_scanning);
    RUN_TEST(test_c2_sync_lost_calls_hook);
    RUN_TEST(test_c2_cell_eligibility_all_hops);
    RUN_TEST(test_c2_beacon_transitions_to_paired);
    RUN_TEST(test_c2_beacon_tx_budget_set_on_first_beacon);
    RUN_TEST(test_c2_beacon_tx_budget_decrements_on_tx);
    RUN_TEST(test_c2_beacon_tx_budget_resets_on_hop_change);
    RUN_TEST(test_c2_phase_tx_flag_alternates);
    return UNITY_END();
}
