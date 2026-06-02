#include "unity.h"
#include "cm4_diag.h"
#include <string.h>

void setUp(void)   { CM4Diag_Init(); }
void tearDown(void) {}

/* ------- helpers ---------------------------------------------------------- */

static ComplianceStatus_t make_status(uint8_t band_unknown_count,
                                       uint16_t skip_mesh,
                                       uint16_t skip_cluster)
{
    ComplianceStatus_t s;
    memset(&s, 0, sizeof(s));
    s.band_unknown_count  = band_unknown_count;
    s.skip_count_mesh     = skip_mesh;
    s.skip_count_cluster  = skip_cluster;
    return s;
}

/* ------- alarm flag ------------------------------------------------------- */

void test_alarm_clear_when_band_count_zero(void)
{
    ComplianceStatus_t s = make_status(0u, 0u, 0u);
    CM4Diag_PollCompliance(&s);
    TEST_ASSERT_FALSE(CM4Diag_GetState()->diag_alarm_pending);
}

void test_alarm_set_when_band_count_nonzero(void)
{
    ComplianceStatus_t s = make_status(1u, 0u, 0u);
    CM4Diag_PollCompliance(&s);
    TEST_ASSERT_TRUE(CM4Diag_GetState()->diag_alarm_pending);
}

void test_alarm_persists_across_zero_poll(void)
{
    /* First poll triggers the alarm. */
    ComplianceStatus_t s1 = make_status(1u, 0u, 0u);
    CM4Diag_PollCompliance(&s1);

    /* Second poll with a clean status must not clear the latching flag. */
    ComplianceStatus_t s2 = make_status(0u, 0u, 0u);
    CM4Diag_PollCompliance(&s2);

    TEST_ASSERT_TRUE(CM4Diag_GetState()->diag_alarm_pending);
}

/* ------- skip count mirroring --------------------------------------------- */

void test_skip_counts_mirrored(void)
{
    ComplianceStatus_t s = make_status(0u, 5u, 3u);
    CM4Diag_PollCompliance(&s);
    TEST_ASSERT_EQUAL(5u, CM4Diag_GetState()->skip_count_mesh);
    TEST_ASSERT_EQUAL(3u, CM4Diag_GetState()->skip_count_cluster);
}

void test_skip_counts_updated_on_subsequent_poll(void)
{
    ComplianceStatus_t s1 = make_status(0u, 2u, 1u);
    CM4Diag_PollCompliance(&s1);
    ComplianceStatus_t s2 = make_status(0u, 7u, 4u);
    CM4Diag_PollCompliance(&s2);
    TEST_ASSERT_EQUAL(7u, CM4Diag_GetState()->skip_count_mesh);
    TEST_ASSERT_EQUAL(4u, CM4Diag_GetState()->skip_count_cluster);
}

/* ------- main ------------------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_alarm_clear_when_band_count_zero);
    RUN_TEST(test_alarm_set_when_band_count_nonzero);
    RUN_TEST(test_alarm_persists_across_zero_poll);
    RUN_TEST(test_skip_counts_mirrored);
    RUN_TEST(test_skip_counts_updated_on_subsequent_poll);
    return UNITY_END();
}
