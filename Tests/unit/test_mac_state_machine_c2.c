#include "unity.h"
#include "mac_state_machine.h"
#include "tdma_table.h"
#include "arclog_capture.h"
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
 * Stub TDMA table: PHASE_TYPE_SYNC, slot_active_ms=2500, gap_after_slot_ms=500
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
 * P2 at cell=1: stamp=3000, expected=3000, error=0 → consecutive=1.
 * P3 at cell=2: stamp=6000, expected=6000, error=0 → consecutive=2 → WARM.
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

static const Phase_t s_sync_phase_6 = {
    .type              = PHASE_TYPE_SYNC,
    .direction_mode    = DIRECTION_MAC_CELL,
    .cell_count        = 6u,
    .slot_count        = 1u,
    .slot_active_ms    = 2500u,
    .gap_after_slot_ms = 500u,
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
    ArcLog_CaptureReset();
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
    TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&c, TdmaTable_GetPhase(0u), 0u));
}

void test_c2_scanning_returns_rx_for_beacon_phase(void)
{
    FrameCursor_t c = {.phase_index = 5u, .cell_index = 0u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&c, &s_beacon_phase, 0u));
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

void test_c2_acquiring_bad_packet_drops_to_cold(void)
{
    /* P1(cell=0), P2(cell=1,ts=3000,err=0 → consecutive=1),
     * P3(cell=2,ts=6100: expected=6000, err=100 ≥ 8ms → bad): the packet
     * disagrees with the RTC set from P1, so the node re-acquires. */
    SyncPayload_t p;
    s_snapshot_ms = 0u;
    make_sync_pkt(&p, 0u, 0u);  MAC_OnSyncPacketReceived(&p, 0u);
    make_sync_pkt(&p, 1u, 0u);  MAC_OnSyncPacketReceived(&p, 3000u);
    make_sync_pkt(&p, 2u, 0u);  MAC_OnSyncPacketReceived(&p, 6100u);   /* bad */
    TEST_ASSERT_EQUAL(CLOCK_COLD, MAC_GetClockState());
    TEST_ASSERT_EQUAL(MAC_STATE_SCANNING, MAC_GetState());
    TEST_ASSERT_EQUAL(0, s_sync_locked_calls);
    TEST_ASSERT_ARCLOG("SYNC_RX ph=0 ce=2 ep=0 st=6100 exp=6000 err=100 clk=ACQ act=bad");
    TEST_ASSERT_ARCLOG("CLK from=ACQ to=COLD why=acq_bad");
}

void test_c2_acquiring_bad_packet_then_next_packet_sets_rtc(void)
{
    /* After a bad packet the next one is a fresh Packet 1: RTC set again. */
    SyncPayload_t p;
    s_snapshot_ms = 0u;
    make_sync_pkt(&p, 0u, 0u);      MAC_OnSyncPacketReceived(&p, 0u);
    make_sync_pkt(&p, 1u, 0u);      MAC_OnSyncPacketReceived(&p, 3033u);  /* bad */
    make_sync_pkt(&p, 2u, 0u);      MAC_OnSyncPacketReceived(&p, 6033u);
    TEST_ASSERT_EQUAL(2, s_rtc_set_calls);
    TEST_ASSERT_EQUAL(CLOCK_ACQUIRING, MAC_GetClockState());
}

void test_c2_acquiring_checks_packet_against_its_own_epoch(void)
{
    /* Bench case: Packet 1 from phase occurrence ep=30000, the next packets
     * from occurrence ep=60000. Each is judged against its own epoch, not
     * against Packet 1's occurrence (which read err=30003). */
    SyncPayload_t p;
    s_snapshot_ms = 30000u;
    make_sync_pkt(&p, 0u, 30000u);  MAC_OnSyncPacketReceived(&p, 30000u);
    make_sync_pkt(&p, 0u, 60000u);  MAC_OnSyncPacketReceived(&p, 60003u);
    TEST_ASSERT_ARCLOG("SYNC_RX ph=0 ce=0 ep=60000 st=60003 exp=60000 err=3 clk=ACQ act=good");
    make_sync_pkt(&p, 1u, 60000u);  MAC_OnSyncPacketReceived(&p, 63003u);
    TEST_ASSERT_EQUAL(CLOCK_WARM, MAC_GetClockState());
}

void test_c2_acquiring_locks_when_packet1_is_the_last_tx_cell(void)
{
    /* Packet 1 in cell 2, the sender's last Tx cell of the occurrence: no
     * later packet of that occurrence exists, so the lock must come from
     * the next occurrence. */
    SyncPayload_t p;
    s_snapshot_ms = 6000u;
    make_sync_pkt(&p, 2u, 0u);      MAC_OnSyncPacketReceived(&p, 6000u);
    make_sync_pkt(&p, 0u, 30000u);  MAC_OnSyncPacketReceived(&p, 30000u);
    make_sync_pkt(&p, 1u, 30000u);  MAC_OnSyncPacketReceived(&p, 33000u);
    TEST_ASSERT_EQUAL(CLOCK_WARM, MAC_GetClockState());
    TEST_ASSERT_EQUAL(1, s_sync_locked_calls);
}

/* ------- SYNC_LOST -------------------------------------------------------- */

void test_c2_sync_lost_resets_to_scanning(void)
{
    sync_mac();
    /* cell=0, ms_midnight=0, expected_arrival=0, stamp=400 → error=400 ≥ SYNC_RESYNC_THRESHOLD_MS Tier 3 */
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
    /* CLOCK_WARM; cell=0, ms_midnight=54000000, stamp=54000000, error=0 → Tier 1 */
    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 54000000u);
    MAC_OnSyncPacketReceived(&p, 54000000u);
    TEST_ASSERT_EQUAL(54000000u, MAC_GetSyncPhaseEpochMs());
}

void test_c2_warm_tier2_no_epoch_below_resync_threshold(void)
{
    sync_mac();
    /* cell=0, ms_midnight=0, stamp=50, expected=0, error=50 ≥ 8ms < SYNC_RESYNC_THRESHOLD_MS → Tier 2 */
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
    /* cell=0, ms_midnight=0, stamp=400, expected=0, error=400 ≥ SYNC_RESYNC_THRESHOLD_MS → Tier 3 */
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
    TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&c, TdmaTable_GetPhase(0u), 0u));
}

void test_c2_sync_cell1_tx_when_epoch_received(void)
{
    sync_mac();
    FrameCursor_t c0 = {.phase_index = 0u, .cell_index = 0u, .slot_index = 0u};
    FrameCursor_t c1 = {.phase_index = 0u, .cell_index = 1u, .slot_index = 0u};

    /* Step 1: SlotOpportunity at cell 0 → phase entry detected, epoch_received=false */
    MAC_OnSlotOpportunity(&c0, TdmaTable_GetPhase(0u), 0u);

    /* Step 2: radio receives sync packet in that RX window → Tier 1 → epoch armed */
    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 0u);
    MAC_OnSyncPacketReceived(&p, 0u);  /* error=0 → Tier 1, epoch stored */

    /* Step 3: SlotOpportunity at cell 1 → epoch received → SLOT_TX */
    TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&c1, TdmaTable_GetPhase(0u), 0u));
}

void test_c2_sync_cell1_rx_when_no_epoch(void)
{
    sync_mac();
    /* No new cell-0 reception this occurrence — epoch_received = false */
    /* Force a new phase entry to reset the epoch flag */
    FrameCursor_t co = {.phase_index = 1u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&co, &s_other_phase, 0u);
    FrameCursor_t c0 = {.phase_index = 0u, .cell_index = 0u, .slot_index = 0u};
    FrameCursor_t c1 = {.phase_index = 0u, .cell_index = 1u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c0, TdmaTable_GetPhase(0u), 0u);  /* cell 0 = RX, no pkt received */
    TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&c1, TdmaTable_GetPhase(0u), 0u));
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
    TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&c, &s_beacon_phase, 0u));
}

void test_c2_beacon_tx_budget_decrements_on_tx(void)
{
    sync_mac();
    BeaconPayload_t b = {.hop_count = 1u, .node_id = 42u, .route_cost = 100u};
    MAC_OnBeaconReceived(&b);

    FrameCursor_t c0 = {.phase_index = 5u, .cell_index = 0u, .slot_index = 0u};
    FrameCursor_t c1 = {.phase_index = 5u, .cell_index = 1u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c0, &s_beacon_phase, 0u);
    MAC_OnSlotOpportunity(&c1, &s_beacon_phase, 0u);

    FrameCursor_t nc0 = {.phase_index = 6u, .cell_index = 0u, .slot_index = 0u};
    FrameCursor_t nc1 = {.phase_index = 6u, .cell_index = 1u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&nc0, &s_beacon_phase, 0u));
    TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&nc1, &s_beacon_phase, 0u));
}

void test_c2_beacon_tx_budget_resets_on_hop_change(void)
{
    sync_mac();
    BeaconPayload_t b = {.hop_count = 1u, .node_id = 42u, .route_cost = 100u};
    MAC_OnBeaconReceived(&b);

    FrameCursor_t c0 = {.phase_index = 5u, .cell_index = 0u};
    FrameCursor_t c1 = {.phase_index = 5u, .cell_index = 1u};
    MAC_OnSlotOpportunity(&c0, &s_beacon_phase, 0u);
    MAC_OnSlotOpportunity(&c1, &s_beacon_phase, 0u);

    BeaconPayload_t b2 = {.hop_count = 2u, .node_id = 42u, .route_cost = 100u};
    MAC_OnBeaconReceived(&b2);

    FrameCursor_t nc0 = {.phase_index = 7u, .cell_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&nc0, &s_beacon_phase, 0u));
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
    MAC_OnSlotOpportunity(&c0, &s_beacon_phase, 0u);
    MAC_OnSlotOpportunity(&c1, &s_beacon_phase, 0u);

    /* Cell 2: budget exhausted → Rx */
    FrameCursor_t c2 = {.phase_index = 5u, .cell_index = 2u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&c2, &s_beacon_phase, 0u));

    /* Receive new beacon at cell 3 with different hop → structural change → budget reset */
    BeaconPayload_t b2 = {.hop_count = 5u, .node_id = 42u, .route_cost = 100u};
    MAC_OnBeaconReceived(&b2);

    /* Cell 3: budget > 0 → Tx (old code would fail: 3 < 2 is false) */
    FrameCursor_t c3 = {.phase_index = 5u, .cell_index = 3u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&c3, &s_beacon_phase, 0u));

    /* Cell 4: budget > 0 → Tx */
    FrameCursor_t c4 = {.phase_index = 5u, .cell_index = 4u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&c4, &s_beacon_phase, 0u));

    /* Cell 5: budget exhausted → Rx */
    FrameCursor_t c5 = {.phase_index = 5u, .cell_index = 5u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&c5, &s_beacon_phase, 0u));
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
    MAC_OnSlotOpportunity(&c0, &s_beacon_phase, 0u);
    TEST_ASSERT_EQUAL(BEACON_K_TX_CELLS - 1u, MAC_GetBeaconTxBudget());
}

/* ------- Sync TX Budget (issue 29 - C2 only) ------------------------------ */

void test_c2_sync_tx_budget_limits_to_3_per_occurrence(void)
{
    sync_mac();
    /* Enter a 6-cell sync phase at cell 0 */
    FrameCursor_t c0 = {.phase_index = 10u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c0, &s_sync_phase_6, 0u);  /* cell 0 = RX, phase entry */

    /* Receive epoch at cell 0 - Tier 1 */
    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 0u);
    MAC_OnSyncPacketReceived(&p, 0u);

    /* Cells 1-3: SLOT_TX (budget = 3) */
    for (uint8_t cell = 1u; cell <= 3u; cell++) {
        FrameCursor_t c = {.phase_index = 10u, .cell_index = cell, .slot_index = 0u};
        TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&c, &s_sync_phase_6, 0u));
    }
    /* Cells 4-5: SLOT_RX (budget exhausted) */
    for (uint8_t cell = 4u; cell <= 5u; cell++) {
        FrameCursor_t c = {.phase_index = 10u, .cell_index = cell, .slot_index = 0u};
        TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&c, &s_sync_phase_6, 0u));
    }
}

void test_c2_sync_tx_budget_resets_on_next_occurrence(void)
{
    sync_mac();
    /* First occurrence: exhaust budget in 6-cell phase */
    FrameCursor_t c0 = {.phase_index = 10u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c0, &s_sync_phase_6, 0u);

    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 0u);
    MAC_OnSyncPacketReceived(&p, 0u);

    for (uint8_t cell = 1u; cell <= 5u; cell++) {
        FrameCursor_t c = {.phase_index = 10u, .cell_index = cell, .slot_index = 0u};
        MAC_OnSlotOpportunity(&c, &s_sync_phase_6, 0u);
    }

    /* Second occurrence: enter a different phase, then re-enter sync phase */
    FrameCursor_t co = {.phase_index = 11u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&co, &s_other_phase, 0u);

    FrameCursor_t c0b = {.phase_index = 10u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c0b, &s_sync_phase_6, 0u);  /* phase entry → budget reset */

    /* Receive epoch again */
    make_sync_pkt(&p, 0u, 0u);
    MAC_OnSyncPacketReceived(&p, 0u);

    /* Budget should be restored: cells 1-3 TX, cell 4 RX */
    for (uint8_t cell = 1u; cell <= 3u; cell++) {
        FrameCursor_t c = {.phase_index = 10u, .cell_index = cell, .slot_index = 0u};
        TEST_ASSERT_EQUAL(SLOT_TX, MAC_OnSlotOpportunity(&c, &s_sync_phase_6, 0u));
    }
    FrameCursor_t c4 = {.phase_index = 10u, .cell_index = 4u, .slot_index = 0u};
    TEST_ASSERT_EQUAL(SLOT_RX, MAC_OnSlotOpportunity(&c4, &s_sync_phase_6, 0u));
}

void test_c2_sync_tx_budget_getter_after_init(void)
{
    TEST_ASSERT_EQUAL(SYNC_TX_BUDGET, MAC_GetSyncTxBudget());
}

void test_c2_sync_tx_budget_getter_decrements_on_tx(void)
{
    sync_mac();
    FrameCursor_t c0 = {.phase_index = 10u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c0, &s_sync_phase_6, 0u);

    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 0u);
    MAC_OnSyncPacketReceived(&p, 0u);

    FrameCursor_t c1 = {.phase_index = 10u, .cell_index = 1u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c1, &s_sync_phase_6, 0u);
    TEST_ASSERT_EQUAL(SYNC_TX_BUDGET - 1u, MAC_GetSyncTxBudget());

    FrameCursor_t c2 = {.phase_index = 10u, .cell_index = 2u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c2, &s_sync_phase_6, 0u);
    TEST_ASSERT_EQUAL(SYNC_TX_BUDGET - 2u, MAC_GetSyncTxBudget());
}

void test_c2_sync_tx_budget_getter_zero_after_exhaustion(void)
{
    sync_mac();
    FrameCursor_t c0 = {.phase_index = 10u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c0, &s_sync_phase_6, 0u);

    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 0u);
    MAC_OnSyncPacketReceived(&p, 0u);

    for (uint8_t cell = 1u; cell <= 3u; cell++) {
        FrameCursor_t c = {.phase_index = 10u, .cell_index = cell, .slot_index = 0u};
        MAC_OnSlotOpportunity(&c, &s_sync_phase_6, 0u);
    }
    TEST_ASSERT_EQUAL(0u, MAC_GetSyncTxBudget());
}

void test_c2_sync_tx_budget_getter_resets_on_phase_entry(void)
{
    sync_mac();
    FrameCursor_t c0 = {.phase_index = 10u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c0, &s_sync_phase_6, 0u);

    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 0u);
    MAC_OnSyncPacketReceived(&p, 0u);

    for (uint8_t cell = 1u; cell <= 3u; cell++) {
        FrameCursor_t c = {.phase_index = 10u, .cell_index = cell, .slot_index = 0u};
        MAC_OnSlotOpportunity(&c, &s_sync_phase_6, 0u);
    }
    TEST_ASSERT_EQUAL(0u, MAC_GetSyncTxBudget());

    /* Phase entry to a different phase resets the budget */
    FrameCursor_t co = {.phase_index = 11u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&co, &s_other_phase, 0u);
    TEST_ASSERT_EQUAL(SYNC_TX_BUDGET, MAC_GetSyncTxBudget());
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
    MAC_OnSlotOpportunity(&c0, TdmaTable_GetPhase(0u), 0u);

    /* Receive Tier 1 — epoch received = true */
    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 0u);
    MAC_OnSyncPacketReceived(&p, 0u);
    TEST_ASSERT_TRUE(MAC_GetEpochReceivedThisPhase());

    /* Phase entry to a different phase resets the flag */
    FrameCursor_t c1 = {.phase_index = 1u, .cell_index = 0u, .slot_index = 0u};
    MAC_OnSlotOpportunity(&c1, &s_other_phase, 0u);
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

/* ------- Sync silence timeout (ADR-0013) -------------------------------- */

/*
 * sync_mac() drives to CLOCK_WARM with the last stamp at 6000 ms.
 * s_last_sync_received_ms = 6000 after acquisition.
 * 14 min = 840000 ms, 15 min = 900000 ms = SYNC_SILENCE_TIMEOUT_MS.
 */

void test_c2_warm_14min_silence_no_degradation(void)
{
    sync_mac();
    MAC_CheckSyncTimeout(6000u + 840000u);
    TEST_ASSERT_EQUAL(CLOCK_WARM, MAC_GetClockState());
    TEST_ASSERT_EQUAL(0, s_sync_lost_calls);
}

void test_c2_warm_15min_silence_degrades_to_cold(void)
{
    sync_mac();
    MAC_CheckSyncTimeout(6000u + 900000u);
    TEST_ASSERT_EQUAL(CLOCK_COLD, MAC_GetClockState());
    TEST_ASSERT_EQUAL(1, s_sync_lost_calls);
}

void test_c2_acquiring_15min_silence_degrades_to_cold(void)
{
    /* P1 only — CLOCK_ACQUIRING, last_sync = 0 */
    SyncPayload_t p;
    s_snapshot_ms = 0u;
    make_sync_pkt(&p, 0u, 0u);
    MAC_OnSyncPacketReceived(&p, 0u);
    TEST_ASSERT_EQUAL(CLOCK_ACQUIRING, MAC_GetClockState());

    MAC_CheckSyncTimeout(900000u);
    TEST_ASSERT_EQUAL(CLOCK_COLD, MAC_GetClockState());
    TEST_ASSERT_EQUAL(1, s_sync_lost_calls);
}

void test_c2_cold_timeout_is_noop(void)
{
    MAC_CheckSyncTimeout(900000u);
    TEST_ASSERT_EQUAL(CLOCK_COLD, MAC_GetClockState());
    TEST_ASSERT_EQUAL(0, s_sync_lost_calls);
}

void test_c2_sync_at_14min_resets_timer(void)
{
    sync_mac();  /* CLOCK_WARM, last_sync = 6000 */

    /* 14 min: no degradation */
    MAC_CheckSyncTimeout(6000u + 840000u);
    TEST_ASSERT_EQUAL(CLOCK_WARM, MAC_GetClockState());

    /* Receive Tier 1 sync at 14 min — resets timer to 846000 */
    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 846000u);
    MAC_OnSyncPacketReceived(&p, 846000u);  /* error=0 → Tier 1 */

    /* 15 min from original start (906000): only 1 min after reset → no degradation */
    MAC_CheckSyncTimeout(906000u);
    TEST_ASSERT_EQUAL(CLOCK_WARM, MAC_GetClockState());
    TEST_ASSERT_EQUAL(0, s_sync_lost_calls);
}

void test_c2_tier2_resets_silence_timer(void)
{
    sync_mac();  /* CLOCK_WARM, last_sync = 6000 */

    /* Tier 2 packet (error 50 ms) at 14 min — stays WARM, resets timer */
    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 846000u);
    MAC_OnSyncPacketReceived(&p, 846050u);  /* error=50 → Tier 2 */
    TEST_ASSERT_EQUAL(CLOCK_WARM, MAC_GetClockState());

    /* 14 min from Tier 2 reset: no degradation */
    MAC_CheckSyncTimeout(846050u + 840000u);
    TEST_ASSERT_EQUAL(CLOCK_WARM, MAC_GetClockState());
    TEST_ASSERT_EQUAL(0, s_sync_lost_calls);

    /* 15 min from Tier 2 reset: degradation */
    MAC_CheckSyncTimeout(846050u + 900000u);
    TEST_ASSERT_EQUAL(CLOCK_COLD, MAC_GetClockState());
    TEST_ASSERT_EQUAL(1, s_sync_lost_calls);
}

void test_c2_tier3_resets_silence_timer(void)
{
    sync_mac();  /* CLOCK_WARM, last_sync = 6000 */

    /* Tier 3 packet at 14 min — immediate degradation to COLD */
    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 846000u);
    MAC_OnSyncPacketReceived(&p, 846400u);  /* error=400 ≥ SYNC_RESYNC_THRESHOLD_MS → Tier 3 */
    TEST_ASSERT_EQUAL(CLOCK_COLD, MAC_GetClockState());
    TEST_ASSERT_EQUAL(1, s_sync_lost_calls);

    /* Re-acquire from the Tier 3 moment */
    s_snapshot_ms = 846400u;
    make_sync_pkt(&p, 0u, 846400u);
    MAC_OnSyncPacketReceived(&p, 846400u);  /* P1 → ACQUIRING */
    make_sync_pkt(&p, 1u, 846400u);  MAC_OnSyncPacketReceived(&p, 849400u);  /* P2 */
    make_sync_pkt(&p, 2u, 846400u);  MAC_OnSyncPacketReceived(&p, 852400u);  /* P3 → WARM */
    TEST_ASSERT_EQUAL(CLOCK_WARM, MAC_GetClockState());

    /* 14 min from re-acquisition: no degradation */
    MAC_CheckSyncTimeout(852400u + 840000u);
    TEST_ASSERT_EQUAL(CLOCK_WARM, MAC_GetClockState());

    /* 15 min from re-acquisition: degradation */
    MAC_CheckSyncTimeout(852400u + 900000u);
    TEST_ASSERT_EQUAL(CLOCK_COLD, MAC_GetClockState());
    TEST_ASSERT_EQUAL(2, s_sync_lost_calls);
}

void test_c2_tier3_immediate_degradation_unchanged(void)
{
    sync_mac();
    SyncPayload_t p;
    make_sync_pkt(&p, 0u, 0u);
    MAC_OnSyncPacketReceived(&p, 400u);  /* error=400 ≥ SYNC_RESYNC_THRESHOLD_MS → Tier 3 */
    TEST_ASSERT_EQUAL(CLOCK_COLD, MAC_GetClockState());
    TEST_ASSERT_EQUAL(MAC_STATE_SCANNING, MAC_GetState());
    TEST_ASSERT_EQUAL(1, s_sync_lost_calls);
}

/* ------- main ------------------------------------------------------------- */

/* ------- ArcLog events ----------------------------------------------------
 * The MAC's log lines are part of its contract: the host tool (tools/arclog)
 * classifies and correlates them, so their names and keys are asserted here.
 * ------------------------------------------------------------------------- */

void test_c2_arclog_acquisition_sequence(void)
{
    sync_mac();

    TEST_ASSERT_ARCLOG("SYNC_RX ph=0 ce=0 ep=0 st=0 exp=0 err=0 clk=COLD act=set");
    TEST_ASSERT_ARCLOG("CLK from=COLD to=ACQ why=rtc_set");
    TEST_ASSERT_ARCLOG("SYNC_RX ph=0 ce=1 ep=0 st=3000 exp=3000 err=0 clk=ACQ act=good");
    TEST_ASSERT_ARCLOG("CLK from=ACQ to=WARM why=lock");
    TEST_ASSERT_ARCLOG("MAC_ST from=SCAN to=SYNC why=lock");
    /* Header: core, module, verbosity letter, sequence number. */
    TEST_ASSERT_EQUAL_STRING_LEN("4Y L #", ArcLog_CaptureLine((uint32_t)ArcLog_CaptureFind("CLK from=ACQ")), 6);
}

void test_c2_arclog_warm_tiers(void)
{
    SyncPayload_t p;
    sync_mac();
    ArcLog_CaptureReset();

    make_sync_pkt(&p, 1u, 30000u);
    MAC_OnSyncPacketReceived(&p, 33004u);            /* err 4 ms  -> tier 1 */
    MAC_OnSyncPacketReceived(&p, 33050u);            /* err 50 ms -> tier 2 */
    MAC_OnSyncPacketReceived(&p, 33500u);            /* err 500   -> tier 3 */

    TEST_ASSERT_ARCLOG("exp=33000 err=4 clk=WARM act=t1");
    TEST_ASSERT_ARCLOG("exp=33000 err=50 clk=WARM act=t2");
    TEST_ASSERT_ARCLOG("exp=33000 err=500 clk=WARM act=t3");
    TEST_ASSERT_ARCLOG("CLK from=WARM to=COLD why=tier3");
    TEST_ASSERT_ARCLOG("MAC_ST from=SYNC to=SCAN why=tier3");
}

void test_c2_arclog_signed_error(void)
{
    SyncPayload_t p;
    sync_mac();
    ArcLog_CaptureReset();

    make_sync_pkt(&p, 1u, 30000u);
    MAC_OnSyncPacketReceived(&p, 32995u);            /* 5 ms early */
    TEST_ASSERT_ARCLOG("st=32995 exp=33000 err=-5 clk=WARM act=t1");
}

void test_c2_arclog_silence(void)
{
    sync_mac();
    ArcLog_CaptureReset();

    MAC_CheckSyncTimeout(6000u + SYNC_SILENCE_TIMEOUT_MS);
    TEST_ASSERT_ARCLOG("SYNC_SILENCE last=6000");
    TEST_ASSERT_ARCLOG("CLK from=WARM to=COLD why=silence");
}

/* ------- SyncStamp age carry ---------------------------------------------
 * The MAC runs at RxDone, ~one airtime after the SyncStamp. rtc_set must
 * add the time elapsed since the stamp, so that the new RTC domain reads the
 * sender's nominal cell start at the stamp instant.
 * ------------------------------------------------------------------------- */

void test_c2_cold_rtc_set_carries_time_since_stamp(void)
{
    SyncPayload_t p;
    make_sync_pkt(&p, 1u, 30000u);        /* nominal cell start 33000 */
    s_snapshot_ms = 50991u;               /* RxDone: 991 ms after the stamp */
    MAC_OnSyncPacketReceived(&p, 50000u);

    TEST_ASSERT_EQUAL(33991u, s_rtc_set_target_ms);
}

void test_c2_cold_stale_stamp_is_not_carried(void)
{
    SyncPayload_t p;
    make_sync_pkt(&p, 1u, 30000u);
    s_snapshot_ms = 50000u + SYNC_STAMP_MAX_AGE_MS + 1u;
    MAC_OnSyncPacketReceived(&p, 50000u);

    TEST_ASSERT_EQUAL(33000u, s_rtc_set_target_ms);
}

void test_c2_tier3_rtc_set_carries_time_since_stamp(void)
{
    SyncPayload_t p;
    sync_mac();
    make_sync_pkt(&p, 1u, 30000u);        /* expected arrival 33000 */
    s_snapshot_ms = 34491u;               /* RxDone 991 ms after the stamp */
    MAC_OnSyncPacketReceived(&p, 33500u); /* 500 ms late -> tier 3 */

    TEST_ASSERT_EQUAL(33991u, s_rtc_set_target_ms);
    TEST_ASSERT_EQUAL(CLOCK_COLD, MAC_GetClockState());
}

void test_c2_cold_rtc_set_carry_wraps_at_midnight(void)
{
    SyncPayload_t p;
    make_sync_pkt(&p, 0u, MS_PER_DAY - 500u);  /* nominal 23:59:59.500 */
    s_snapshot_ms = 400u;                      /* old domain wrapped */
    MAC_OnSyncPacketReceived(&p, MS_PER_DAY - 600u);

    TEST_ASSERT_EQUAL(500u, s_rtc_set_target_ms);  /* 1000 ms later, wrapped */
}

/* ------- Cursor suspect --------------------------------------------------- */

void test_c2_cursor_suspect_drops_to_cold_without_rtc_write(void)
{
    sync_mac();
    int rtc_sets = s_rtc_set_calls;

    MAC_OnCursorSuspect();

    TEST_ASSERT_EQUAL(CLOCK_COLD, MAC_GetClockState());
    TEST_ASSERT_EQUAL(MAC_STATE_SCANNING, MAC_GetState());
    TEST_ASSERT_EQUAL(rtc_sets, s_rtc_set_calls);
    TEST_ASSERT_EQUAL(1, s_sync_lost_calls);
    TEST_ASSERT_ARCLOG("CLK from=WARM to=COLD why=suspect");
}

void test_c2_cursor_suspect_in_cold_is_noop(void)
{
    MAC_OnCursorSuspect();

    TEST_ASSERT_EQUAL(CLOCK_COLD, MAC_GetClockState());
    TEST_ASSERT_EQUAL(0, s_rtc_set_calls);
    TEST_ASSERT_EQUAL(0, s_sync_lost_calls);
}

/* A phase index that is not a Sync phase cannot anchor the FrameCursor:
 * the packet is dropped before it touches the RTC or the ClockState, so a
 * cold node keeps scanning instead of reaching ACQUIRING with no chain. */
void test_c2_sync_pkt_with_non_sync_phase_is_rejected(void)
{
    SyncPayload_t p;
    memset(&p, 0, sizeof(p));
    p.sync_phase_index = 0xFFu;
    p.sync_cell_index  = 1u;
    ArcLog_CaptureReset();

    MAC_OnSyncPacketReceived(&p, 1000u);

    TEST_ASSERT_EQUAL(CLOCK_COLD, MAC_GetClockState());
    TEST_ASSERT_EQUAL(0, s_rtc_set_calls);
    TEST_ASSERT_ARCLOG("SYNC_REJ ph=255 ce=1");
}

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
    RUN_TEST(test_c2_sync_bootstrapped_hook_called_with_rtc_now_as_slot_start);
    RUN_TEST(test_c2_two_consecutive_good_packets_warm);
    RUN_TEST(test_c2_sync_locked_called_once);
    RUN_TEST(test_c2_acquiring_bad_packet_drops_to_cold);
    RUN_TEST(test_c2_acquiring_bad_packet_then_next_packet_sets_rtc);
    RUN_TEST(test_c2_acquiring_checks_packet_against_its_own_epoch);
    RUN_TEST(test_c2_acquiring_locks_when_packet1_is_the_last_tx_cell);
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

/* ------- Sync TX Budget (issue 29 - C2 only) ------------------------------ */

    RUN_TEST(test_c2_sync_tx_budget_limits_to_3_per_occurrence);
    RUN_TEST(test_c2_sync_tx_budget_resets_on_next_occurrence);
    RUN_TEST(test_c2_sync_tx_budget_getter_after_init);
    RUN_TEST(test_c2_sync_tx_budget_getter_decrements_on_tx);
    RUN_TEST(test_c2_sync_tx_budget_getter_zero_after_exhaustion);
    RUN_TEST(test_c2_sync_tx_budget_getter_resets_on_phase_entry);

/* ------- Epoch Received flag --------------------------------------------- */

    RUN_TEST(test_c2_epoch_received_false_after_init);
    RUN_TEST(test_c2_epoch_received_true_after_tier1);
    RUN_TEST(test_c2_epoch_received_resets_on_phase_entry);
    RUN_TEST(test_c2_hop_count_zero_after_init);
    RUN_TEST(test_c2_hop_count_set_after_beacon);

/* ------- Sync silence timeout (ADR-0013) -------------------------------- */

    RUN_TEST(test_c2_warm_14min_silence_no_degradation);
    RUN_TEST(test_c2_warm_15min_silence_degrades_to_cold);
    RUN_TEST(test_c2_acquiring_15min_silence_degrades_to_cold);
    RUN_TEST(test_c2_cold_timeout_is_noop);
    RUN_TEST(test_c2_sync_at_14min_resets_timer);
    RUN_TEST(test_c2_tier2_resets_silence_timer);
    RUN_TEST(test_c2_tier3_resets_silence_timer);
    RUN_TEST(test_c2_tier3_immediate_degradation_unchanged);
    RUN_TEST(test_c2_arclog_acquisition_sequence);
    RUN_TEST(test_c2_arclog_warm_tiers);
    RUN_TEST(test_c2_arclog_signed_error);
    RUN_TEST(test_c2_arclog_silence);
    RUN_TEST(test_c2_cursor_suspect_drops_to_cold_without_rtc_write);
    RUN_TEST(test_c2_cursor_suspect_in_cold_is_noop);
    RUN_TEST(test_c2_cold_rtc_set_carries_time_since_stamp);
    RUN_TEST(test_c2_cold_stale_stamp_is_not_carried);
    RUN_TEST(test_c2_tier3_rtc_set_carries_time_since_stamp);
    RUN_TEST(test_c2_cold_rtc_set_carry_wraps_at_midnight);
    RUN_TEST(test_c2_sync_pkt_with_non_sync_phase_is_rejected);
    return UNITY_END();
}
