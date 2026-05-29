#include "unity.h"
#include "protocol_types.h"
#include "shared_mem.h"
#include <stddef.h>  /* offsetof */

void setUp(void)    {}
void tearDown(void) {}

/* --- SyncPayload layout ------------------------------------------------- */

void test_sync_payload_is_11_bytes(void)
{
    /* On-wire format must be exactly 11 bytes (packed, no padding).
     * If this fails, __attribute__((packed)) was lost or a field was added. */
    TEST_ASSERT_EQUAL(11u, sizeof(SyncPayload_t));
}

/* --- AlarmBRequest layout ----------------------------------------------- */

void test_alarm_b_request_pending_is_at_offset_zero(void)
{
    /* pending must be the first byte: uint8_t is naturally atomic on ARM
     * Cortex-M. Any padding before it would break the atomicity guarantee. */
    TEST_ASSERT_EQUAL(0u, offsetof(AlarmBRequest_t, pending));
}

void test_alarm_b_request_is_12_bytes(void)
{
    /* 8 uint8_t fields + 1 uint32_t (naturally aligned) = 12 bytes. */
    TEST_ASSERT_EQUAL(12u, sizeof(AlarmBRequest_t));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sync_payload_is_11_bytes);
    RUN_TEST(test_alarm_b_request_pending_is_at_offset_zero);
    RUN_TEST(test_alarm_b_request_is_12_bytes);
    return UNITY_END();
}
