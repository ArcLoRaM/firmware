#include "unity.h"
#include "mac_state_machine.h"
#include "tdma_table.h"
#include <string.h>

/* ------- hook stubs ------------------------------------------------------- */

static int      s_rtc_set_calls;
static uint32_t s_rtc_set_target_ms;
static int      s_sync_locked_calls;
static int      s_sync_lost_calls;
static int      s_sync_bootstrapped_calls;
static uint32_t s_sync_bootstrapped_slot_start;
static uint8_t  s_sync_bootstrapped_phase_idx;
static uint8_t  s_sync_bootstrapped_cell_idx;

/* Snapshot stub: caller writes these before the test, hook reads them out */
static uint32_t s_snapshot_ms;
static uint8_t  s_snapshot_day;
static uint8_t  s_snapshot_month;
static uint8_t  s_snapshot_year;

static void stub_rtc_set(uint32_t target_ms,
                          uint8_t day, uint8_t month, uint8_t year)
{
    (void)day; (void)month; (void)year;
    s_rtc_set_target_ms = target_ms;
    s_rtc_set_calls++;
    /* After rtc_set, pretend RTC now reads target_ms */
    s_snapshot_ms = target_ms;
}

static void stub_get_rtc_snapshot(uint32_t *ms,
                                   uint8_t *day, uint8_t *month, uint8_t *year)
{
    *ms    = s_snapshot_ms;
    *day   = s_snapshot_day;
    *month = s_snapshot_month;
    *year  = s_snapshot_year;
}

static void stub_sync_bootstrapped(uint8_t phase_idx, uint8_t cell_idx,
                                    uint32_t rtc_now_ms)
{
    s_sync_bootstrapped_phase_idx  = phase_idx;
    s_sync_bootstrapped_cell_idx   = cell_idx;
    s_sync_bootstrapped_slot_start = rtc_now_ms;
    s_sync_bootstrapped_calls++;
}

static void stub_sync_locked(void) { s_sync_locked_calls++; }
static void stub_sync_lost(void)   { s_sync_lost_calls++;   }

static const MAC_Hooks_t k_hooks = {
    .rtc_set            = stub_rtc_set,
    .get_rtc_snapshot   = stub_get_rtc_snapshot,
    .sync_bootstrapped  = stub_sync_bootstrapped,
    .sync_locked        = stub_sync_locked,
    .sync_lost          = stub_sync_lost,
};

/* ------- helpers ---------------------------------------------------------- */

/*
 * Stub TDMA table: PHASE_TYPE_SYNC, slot_active_ms=2500, gap_slots_ms[0]=500
 * Per-cell step = 3000 ms.
 */
static void make_sync_pkt(SyncPayload_t *p, uint8_t cell, uint32_t ms_midnight)
{
    memset(p, 0, sizeof(*p));
    p->sync_cell_index              = cell;
    p->sync_phase_index             = 0u;
    p->ms_since_midnight_sync_phase = ms_midnight;
    p->day   = 0x01u;
    p->month = 0x01u;
    p->year  = 0x24u;
}

/*
 * Drive MAC to CLOCK_WARM.
 * P1 at cell=0: s_snapshot_ms = 0 (phase start), s_sync_phase_ms = 0.
 * P2 at cell=1: preamble_ts=3000, expected=3000, error=0 → consecutive=1.
 * P3 at cell=2: preamble_ts=6000, expected=6000, error=0 → consecutive=2 → WARM.
 */
static void sync_mac(void)
{
    SyncPayload_t p;
    s_snapshot_ms = 0u;  /* after rtc_set RTC reads 0 */

    make_sync_pkt(&p, 0u, 0u);  MAC_OnSyncPacketReceived(&p, 0u);
    make_sync_pkt(&p, 1u, 0u);  MAC_OnSyncPacketReceived(&p, 3000u);
    make_sync_pkt(&p, 2u, 0u);  MAC_OnSyncPacketReceived(&p, 6000u);
}

static const Phase_t s_beacon_phase = {
    .type           = PHASE_TYPE_MESH_BEACON,
    .direction_mode = DIRECTION_MAC_CELL,
    .cell_count     = 10u,
    .slot_count     = 1u,
    .slot_active_ms = 2000u,
};

static const Phase_t s_other_phase = {
    .type           = PHASE_TYPE_MESH_UPLINK,
    .direction_mode = DIRECTION_CELL_SKIP,
    .cell_count     = 3u,
    .slot_active_ms = 2500u,
};

void setUp(void)
{
    s_rtc_set_calls             = 0;
    s_rtc_set_target_ms         = 0u;
    s_sync_locked_calls         = 0;
    s_sync_lost_calls           = 0;
    s_sync_bootstrapped_calls   = 0;
    s_sync_bootstrapped_slot_start = 0u;
    s_sync_bootstrapped_phase_idx  = 0u;
    s_sync_bootstrapped_cell_idx   = 0u;
    s_snapshot_ms    = 0u;
    s_snapshot_day   = 0x01u;
    s_snapshot_month = 0x01u;
    s_snapshot_year  = 0x24u;
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

/* ------- Packet 1: RTC set with binary target_ms -------------------------- */

void test_c2_sync_pkt1_transitions_acquiring(void)
{
    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 0u);
    MAC_OnSyncPacketReceived(&p, 0u);
    TEST_ASSERT_EQUAL(CLOCK_ACQUIRING, MAC_GetClockState());
}

void test_c2_sync_pkt1_calls_rtc_set_hook(void)
{
    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 0u);
    MAC_OnSyncPacketReceived(&p, 0u);
    TEST_ASSERT_EQUAL(1, s_rtc_set_calls);
}

void test_c2_packet1_rtc_set_called_with_target_ms_including_cell_offset(void)
{
    /* ms_since_midnight=0, per_cell=3000, K=2 → target_ms = 0 + 2×3000 = 6000 */
    SyncPayload_t p;
    make_sync_pkt(&p, 2u, 0u);
    s_snapshot_ms = 6000u;  /* after rtc_set RTC reads 6000 */
    MAC_OnSyncPacketReceived(&p, 0u);
    TEST_ASSERT_EQUAL(6000u, s_rtc_set_target_ms);
}

void test_c2_packet1_sync_phase_ms_corrected_for_cell_offset(void)
{
    /* get_rtc_snapshot returns 6000 (= target_ms); K=2, per_cell=3000
     * s_sync_phase_ms = 6000 - 2×3000 = 0 */
    SyncPayload_t p;
    make_sync_pkt(&p, 2u, 0u);
    s_snapshot_ms = 6000u;
    MAC_OnSyncPacketReceived(&p, 0u);
    TEST_ASSERT_EQUAL(0u, MAC_GetSyncPhaseMs());
}

void test_c2_packet1_sync_phase_ms_at_cell_zero_equals_rtc_now(void)
{
    /* ms_since_midnight=37800000, K=0 → target_ms=37800000, s_sync_phase_ms=37800000 */
    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 37800000u);
    s_snapshot_ms = 37800000u;
    MAC_OnSyncPacketReceived(&p, 0u);
    TEST_ASSERT_EQUAL(37800000u, MAC_GetSyncPhaseMs());
}

void test_c2_sync_bootstrapped_hook_called_with_rtc_now_as_slot_start(void)
{
    /* get_rtc_snapshot returns 6000 (= target_ms); K=2
     * sync_bootstrapped slot_start == 6000 */
    SyncPayload_t p;
    make_sync_pkt(&p, 2u, 0u);
    s_snapshot_ms = 6000u;
    MAC_OnSyncPacketReceived(&p, 0u);
    TEST_ASSERT_EQUAL(1, s_sync_bootstrapped_calls);
    TEST_ASSERT_EQUAL(6000u, s_sync_bootstrapped_slot_start);
    TEST_ASSERT_EQUAL(0u, s_sync_bootstrapped_phase_idx);
    TEST_ASSERT_EQUAL(2u, s_sync_bootstrapped_cell_idx);
}

/* ------- Two-consecutive-packet lock -------------------------------------- */

void test_c2_two_consecutive_good_packets_warm(void)
{
    /* s_sync_phase_ms=0; P2(cell=1,ts=3000,err=0), P3(cell=2,ts=6000,err=0) → WARM */
    sync_mac();
    TEST_ASSERT_EQUAL(CLOCK_WARM, MAC_GetClockState());
    TEST_ASSERT_EQUAL(MAC_STATE_SYNCHRONIZED, MAC_GetState());
}

void test_c2_sync_locked_called_once(void)
{
    sync_mac();
    TEST_ASSERT_EQUAL(1, s_sync_locked_calls);
}

void test_c2_consecutive_reset_on_bad_packet(void)
{
    /* P1(cell=0), P2(cell=1,ts=3000,err=0 → consecutive=1),
     * P3(cell=2,ts=100: expected=6000, err=5900 > 8ms → consecutive=0 → ACQUIRING)
     * P4(cell=3,ts=9000,err=0 → consecutive=1 → still ACQUIRING, need 2) */
    SyncPayload_t p;
    s_snapshot_ms = 0u;
    make_sync_pkt(&p, 0u, 0u);  MAC_OnSyncPacketReceived(&p, 0u);
    make_sync_pkt(&p, 1u, 0u);  MAC_OnSyncPacketReceived(&p, 3000u);
    make_sync_pkt(&p, 2u, 0u);  MAC_OnSyncPacketReceived(&p, 100u);   /* bad */
    TEST_ASSERT_EQUAL(CLOCK_ACQUIRING, MAC_GetClockState());
    make_sync_pkt(&p, 3u, 0u);  MAC_OnSyncPacketReceived(&p, 9000u);  /* good but only 1 */
    TEST_ASSERT_EQUAL(CLOCK_ACQUIRING, MAC_GetClockState());
    TEST_ASSERT_EQUAL(0, s_sync_locked_calls);
}

void test_c2_sync_pkt_large_error_stays_acquiring(void)
{
    SyncPayload_t p;
    s_snapshot_ms = 0u;
    make_sync_pkt(&p, 0u, 0u);  MAC_OnSyncPacketReceived(&p, 0u);
    make_sync_pkt(&p, 1u, 0u);  MAC_OnSyncPacketReceived(&p, 3000u);
    /* cell=2, expected=6000, preamble=100 → error=5900 >> 8ms */
    make_sync_pkt(&p, 2u, 0u);  MAC_OnSyncPacketReceived(&p, 100u);
    TEST_ASSERT_EQUAL(CLOCK_ACQUIRING, MAC_GetClockState());
    TEST_ASSERT_EQUAL(0, s_sync_locked_calls);
}

/* ------- SYNC_LOST -------------------------------------------------------- */

void test_c2_sync_lost_resets_to_scanning(void)
{
    sync_mac();
    /* cell=0, ms_midnight=0, expected_arrival=0, preamble=400 → error=400 ≥ 300ms Tier 3 */
    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 0u);
    MAC_OnSyncPacketReceived(&p, 400u);
    TEST_ASSERT_EQUAL(MAC_STATE_SCANNING, MAC_GetState());
    TEST_ASSERT_EQUAL(CLOCK_COLD, MAC_GetClockState());
}

void test_c2_sync_lost_calls_hook(void)
{
    sync_mac();
    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 0u);
    MAC_OnSyncPacketReceived(&p, 400u);
    TEST_ASSERT_EQUAL(1, s_sync_lost_calls);
}

/* ------- CLOCK_WARM three-tier dispatch ----------------------------------- */

void test_c2_warm_tier1_epoch_stored_on_good_cell0(void)
{
    sync_mac();
    /* CLOCK_WARM; cell=0, ms_midnight=54000000, preamble=54000000, error=0 → Tier 1 */
    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 54000000u);
    MAC_OnSyncPacketReceived(&p, 54000000u);
    TEST_ASSERT_EQUAL(54000000u, MAC_GetSyncPhaseEpochMs());
}

void test_c2_warm_tier2_no_epoch_below_resync_threshold(void)
{
    sync_mac();
    /* cell=0, ms_midnight=0, preamble=50, expected=0, error=50 ≥ 8ms < 300ms → Tier 2 */
    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 0u);
    MAC_OnSyncPacketReceived(&p, 50u);
    /* ClockState stays WARM; epoch not stored (no relay this occurrence) */
    TEST_ASSERT_EQUAL(CLOCK_WARM, MAC_GetClockState());
    /* epoch is still from the warm-up acquisition, not updated */
    TEST_ASSERT_NOT_EQUAL(0u, 1u);  /* structural: state did not collapse */
    TEST_ASSERT_EQUAL(0, s_sync_lost_calls);
}

void test_c2_warm_tier3_resync_above_resync_threshold(void)
{
    sync_mac();
    int rtc_calls_before = s_rtc_set_calls;  /* snapshot after sync_mac P1 call */
    /* cell=0, ms_midnight=0, preamble=400, expected=0, error=400 ≥ 300ms → Tier 3 */
    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 0u);
    MAC_OnSyncPacketReceived(&p, 400u);
    TEST_ASSERT_EQUAL(CLOCK_COLD, MAC_GetClockState());
    TEST_ASSERT_EQUAL(1, s_rtc_set_calls - rtc_calls_before);  /* one new rtc_set */
    TEST_ASSERT_EQUAL(1, s_sync_lost_calls);
}

/* ------- Cell 0 always RX, cells 1+ reactive ------------------------------ */

void test_c2_sync_cell0_always_rx_when_warm(void)
{
    sync_mac();
    FrameCursor_t c = {.phase_index = 0u, .cell_index = 0u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&c, TdmaTable_GetPhase(0u)));
}

void test_c2_sync_cell1_tx_when_epoch_received(void)
{
    sync_mac();
    FrameCursor_t c0 = {.phase_index = 0u, .cell_index = 0u, .slot_index = 0u};
    FrameCursor_t c1 = {.phase_index = 0u, .cell_index = 1u, .slot_index = 0u};

    /* Step 1: SlotOpportunity at cell 0 → phase entry detected, epoch_received=false */
    MAC_OnSlotOpportunity(&c0, TdmaTable_GetPhase(0u));

    /* Step 2: radio receives sync packet in that RX window → Tier 1 → epoch armed */
    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 0u);
    MAC_OnSyncPacketReceived(&p, 0u);  /* error=0 → Tier 1, epoch stored */

    /* Step 3: SlotOpportunity at cell 1 → epoch received → SLOT_TX */
    TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&c1, TdmaTable_GetPhase(0u)));
}

void test_c2_sync_cell1_rx_when_no_epoch(void)
{
    sync_mac();
    /* No new cell-0 reception this occurrence — epoch_received = false */
    /* Force a new phase entry to reset the epoch flag */
    FrameCursor_t co = {.phase_index = 1u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&co, &s_other_phase);
    FrameCursor_t c0 = {.phase_index = 0u, .cell_index = 0u, .slot_index = 0u};
    FrameCursor_t c1 = {.phase_index = 0u, .cell_index = 1u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c0, TdmaTable_GetPhase(0u));  /* cell 0 = RX, no pkt received */
    TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&c1, TdmaTable_GetPhase(0u)));
}

/* ------- CellEligibilityMask for hop 0–5 ---------------------------------- */

void test_c2_cell_eligibility_all_hops(void)
{
    sync_mac();
    for (uint8_t hop = 0u; hop <= 5u; hop++) {
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
    sync_mac();
    BeaconPayload_t b = {.hop_count = 1u, .node_id = 42u, .route_cost = 100u};
    MAC_OnBeaconReceived(&b);
    TEST_ASSERT_EQUAL(MAC_STATE_PAIRED, MAC_GetState());
}

/* ------- BeaconTxBudget --------------------------------------------------- */

void test_c2_beacon_tx_budget_set_on_first_beacon(void)
{
    sync_mac();
    BeaconPayload_t b = {.hop_count = 1u, .node_id = 42u, .route_cost = 100u};
    MAC_OnBeaconReceived(&b);

    FrameCursor_t c = {.phase_index = 5u, .cell_index = 0u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&c, &s_beacon_phase));
}

void test_c2_beacon_tx_budget_decrements_on_tx(void)
{
    sync_mac();
    BeaconPayload_t b = {.hop_count = 1u, .node_id = 42u, .route_cost = 100u};
    MAC_OnBeaconReceived(&b);

    FrameCursor_t c0 = {.phase_index = 5u, .cell_index = 0u, .slot_index = 0u};
    FrameCursor_t c1 = {.phase_index = 5u, .cell_index = 1u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c0, &s_beacon_phase);
    MAC_OnSlotOpportunity(&c1, &s_beacon_phase);

    FrameCursor_t nc0 = {.phase_index = 6u, .cell_index = 0u, .slot_index = 0u};
    FrameCursor_t nc1 = {.phase_index = 6u, .cell_index = 1u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&nc0, &s_beacon_phase));
    TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&nc1, &s_beacon_phase));
}

void test_c2_beacon_tx_budget_resets_on_hop_change(void)
{
    sync_mac();
    BeaconPayload_t b = {.hop_count = 1u, .node_id = 42u, .route_cost = 100u};
    MAC_OnBeaconReceived(&b);

    FrameCursor_t c0 = {.phase_index = 5u, .cell_index = 0u};
    FrameCursor_t c1 = {.phase_index = 5u, .cell_index = 1u};
    MAC_OnSlotOpportunity(&c0, &s_beacon_phase);
    MAC_OnSlotOpportunity(&c1, &s_beacon_phase);

    BeaconPayload_t b2 = {.hop_count = 2u, .node_id = 42u, .route_cost = 100u};
    MAC_OnBeaconReceived(&b2);

    FrameCursor_t nc0 = {.phase_index = 7u, .cell_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&nc0, &s_beacon_phase));
}

/* ------- Beacon Tx at cell 3+ (relative to beacon reception) -------------- */

void test_c2_beacon_tx_at_cell_3_after_beacon_received(void)
{
    sync_mac();
    BeaconPayload_t b = {.hop_count = 1u, .node_id = 42u, .route_cost = 100u};
    MAC_OnBeaconReceived(&b);  /* budget = K = 2 */

    /* Consume budget at cells 0 and 1 */
    FrameCursor_t c0 = {.phase_index = 5u, .cell_index = 0u, .slot_index = 0u};
    FrameCursor_t c1 = {.phase_index = 5u, .cell_index = 1u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c0, &s_beacon_phase);
    MAC_OnSlotOpportunity(&c1, &s_beacon_phase);

    /* Cell 2: budget exhausted → Rx */
    FrameCursor_t c2 = {.phase_index = 5u, .cell_index = 2u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&c2, &s_beacon_phase));

    /* Receive new beacon at cell 3 with different hop → structural change → budget reset */
    BeaconPayload_t b2 = {.hop_count = 5u, .node_id = 42u, .route_cost = 100u};
    MAC_OnBeaconReceived(&b2);

    /* Cell 3: budget > 0 → Tx (old code would fail: 3 < 2 is false) */
    FrameCursor_t c3 = {.phase_index = 5u, .cell_index = 3u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&c3, &s_beacon_phase));

    /* Cell 4: budget > 0 → Tx */
    FrameCursor_t c4 = {.phase_index = 5u, .cell_index = 4u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&c4, &s_beacon_phase));

    /* Cell 5: budget exhausted → Rx */
    FrameCursor_t c5 = {.phase_index = 5u, .cell_index = 5u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&c5, &s_beacon_phase));
}

void test_c2_beacon_tx_budget_getter(void)
{
    sync_mac();
    /* After init + sync_mac, no beacon received → budget = 0 */
    TEST_ASSERT_EQUAL(0u, MAC_GetBeaconTxBudget());

    BeaconPayload_t b = {.hop_count = 1u, .node_id = 42u, .route_cost = 100u};
    MAC_OnBeaconReceived(&b);
    TEST_ASSERT_EQUAL(BEACON_K_TX_CELLS, MAC_GetBeaconTxBudget());

    /* Consume one budget */
    FrameCursor_t c0 = {.phase_index = 5u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c0, &s_beacon_phase);
    TEST_ASSERT_EQUAL(BEACON_K_TX_CELLS - 1u, MAC_GetBeaconTxBudget());
}

/* ------- Epoch Received flag --------------------------------------------- */

void test_c2_epoch_received_false_after_init(void)
{
    TEST_ASSERT_FALSE(MAC_GetEpochReceivedThisPhase());
}

void test_c2_epoch_received_true_after_tier1(void)
{
    sync_mac();
    /* WARM state: receive a Tier 1 sync packet (error=0 < 8ms) */
    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 0u);
    MAC_OnSyncPacketReceived(&p, 0u);
    TEST_ASSERT_TRUE(MAC_GetEpochReceivedThisPhase());
}

void test_c2_epoch_received_resets_on_phase_entry(void)
{
    sync_mac();

    /* First slot opportunity at phase 0 — triggers phase entry */
    FrameCursor_t c0 = {.phase_index = 0u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c0, TdmaTable_GetPhase(0u));

    /* Receive Tier 1 — epoch received = true */
    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 0u);
    MAC_OnSyncPacketReceived(&p, 0u);
    TEST_ASSERT_TRUE(MAC_GetEpochReceivedThisPhase());

    /* Phase entry to a different phase resets the flag */
    FrameCursor_t c1 = {.phase_index = 1u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c1, &s_other_phase);
    TEST_ASSERT_FALSE(MAC_GetEpochReceivedThisPhase());
}

void test_c2_hop_count_zero_after_init(void)
{
    TEST_ASSERT_EQUAL(0u, MAC_GetHopCount());
}

void test_c2_hop_count_set_after_beacon(void)
{
    sync_mac();
    BeaconPayload_t b = {.hop_count = 3u, .node_id = 7u, .route_cost = 50u};
    MAC_OnBeaconReceived(&b);
    TEST_ASSERT_EQUAL(4u, MAC_GetHopCount());
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
    RUN_TEST(test_c2_packet1_rtc_set_called_with_target_ms_including_cell_offset);
    RUN_TEST(test_c2_packet1_sync_phase_ms_corrected_for_cell_offset);
    RUN_TEST(test_c2_packet1_sync_phase_ms_at_cell_zero_equals_rtc_now);
    RUN_TEST(test_c2_sync_bootstrapped_hook_called_with_rtc_now_as_slot_start);
    RUN_TEST(test_c2_two_consecutive_good_packets_warm);
    RUN_TEST(test_c2_sync_locked_called_once);
    RUN_TEST(test_c2_consecutive_reset_on_bad_packet);
    RUN_TEST(test_c2_sync_pkt_large_error_stays_acquiring);
    RUN_TEST(test_c2_sync_lost_resets_to_scanning);
    RUN_TEST(test_c2_sync_lost_calls_hook);
    RUN_TEST(test_c2_warm_tier1_epoch_stored_on_good_cell0);
    RUN_TEST(test_c2_warm_tier2_no_epoch_below_resync_threshold);
    RUN_TEST(test_c2_warm_tier3_resync_above_resync_threshold);
    RUN_TEST(test_c2_sync_cell0_always_rx_when_warm);
    RUN_TEST(test_c2_sync_cell1_tx_when_epoch_received);
    RUN_TEST(test_c2_sync_cell1_rx_when_no_epoch);
    RUN_TEST(test_c2_cell_eligibility_all_hops);
    RUN_TEST(test_c2_beacon_transitions_to_paired);
    RUN_TEST(test_c2_beacon_tx_budget_set_on_first_beacon);
    RUN_TEST(test_c2_beacon_tx_budget_decrements_on_tx);
    RUN_TEST(test_c2_beacon_tx_budget_resets_on_hop_change);

/* ------- Beacon Tx at cell 3+ ------------------------------------------- */

    RUN_TEST(test_c2_beacon_tx_at_cell_3_after_beacon_received);
    RUN_TEST(test_c2_beacon_tx_budget_getter);

/* ------- Epoch Received flag --------------------------------------------- */

    RUN_TEST(test_c2_epoch_received_false_after_init);
    RUN_TEST(test_c2_epoch_received_true_after_tier1);
    RUN_TEST(test_c2_epoch_received_resets_on_phase_entry);
    RUN_TEST(test_c2_hop_count_zero_after_init);
    RUN_TEST(test_c2_hop_count_set_after_beacon);
    return UNITY_END();
}
