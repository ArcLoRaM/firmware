#include "unity.h"
#include "date_bcd.h"

/*
 * Calendar day step on the BCD date of a SyncPayload (issue #28).
 * Dates are written day, month, year in BCD, year 00-99 = 2000-2099.
 */

void setUp(void)    {}
void tearDown(void) {}

void test_next_day_within_a_month(void)
{
    uint8_t d = 0x14, m = 0x03, y = 0x26;
    DateBcd_AddDays(&d, &m, &y, 1);
    TEST_ASSERT_EQUAL_HEX8(0x15, d);
    TEST_ASSERT_EQUAL_HEX8(0x03, m);
    TEST_ASSERT_EQUAL_HEX8(0x26, y);
}

void test_next_day_at_the_end_of_a_30_day_month(void)
{
    uint8_t d = 0x30, m = 0x04, y = 0x26;
    DateBcd_AddDays(&d, &m, &y, 1);
    TEST_ASSERT_EQUAL_HEX8(0x01, d);
    TEST_ASSERT_EQUAL_HEX8(0x05, m);
    TEST_ASSERT_EQUAL_HEX8(0x26, y);
}

void test_next_day_at_the_end_of_a_31_day_month(void)
{
    uint8_t d = 0x31, m = 0x01, y = 0x26;
    DateBcd_AddDays(&d, &m, &y, 1);
    TEST_ASSERT_EQUAL_HEX8(0x01, d);
    TEST_ASSERT_EQUAL_HEX8(0x02, m);
    TEST_ASSERT_EQUAL_HEX8(0x26, y);
}

void test_next_day_after_28_february_in_a_common_year(void)
{
    uint8_t d = 0x28, m = 0x02, y = 0x25;
    DateBcd_AddDays(&d, &m, &y, 1);
    TEST_ASSERT_EQUAL_HEX8(0x01, d);
    TEST_ASSERT_EQUAL_HEX8(0x03, m);
    TEST_ASSERT_EQUAL_HEX8(0x25, y);
}

void test_next_day_after_28_february_in_a_leap_year(void)
{
    uint8_t d = 0x28, m = 0x02, y = 0x24;
    DateBcd_AddDays(&d, &m, &y, 1);
    TEST_ASSERT_EQUAL_HEX8(0x29, d);
    TEST_ASSERT_EQUAL_HEX8(0x02, m);
    TEST_ASSERT_EQUAL_HEX8(0x24, y);
}

void test_next_day_after_29_february_in_a_leap_year(void)
{
    uint8_t d = 0x29, m = 0x02, y = 0x24;
    DateBcd_AddDays(&d, &m, &y, 1);
    TEST_ASSERT_EQUAL_HEX8(0x01, d);
    TEST_ASSERT_EQUAL_HEX8(0x03, m);
    TEST_ASSERT_EQUAL_HEX8(0x24, y);
}

void test_the_year_2000_is_a_leap_year(void)
{
    uint8_t d = 0x28, m = 0x02, y = 0x00;
    DateBcd_AddDays(&d, &m, &y, 1);
    TEST_ASSERT_EQUAL_HEX8(0x29, d);
    TEST_ASSERT_EQUAL_HEX8(0x02, m);
}

void test_next_day_at_the_end_of_the_year(void)
{
    uint8_t d = 0x31, m = 0x12, y = 0x25;
    DateBcd_AddDays(&d, &m, &y, 1);
    TEST_ASSERT_EQUAL_HEX8(0x01, d);
    TEST_ASSERT_EQUAL_HEX8(0x01, m);
    TEST_ASSERT_EQUAL_HEX8(0x26, y);
}

void test_next_day_at_the_end_of_2099_wraps_to_2000(void)
{
    uint8_t d = 0x31, m = 0x12, y = 0x99;
    DateBcd_AddDays(&d, &m, &y, 1);
    TEST_ASSERT_EQUAL_HEX8(0x01, d);
    TEST_ASSERT_EQUAL_HEX8(0x01, m);
    TEST_ASSERT_EQUAL_HEX8(0x00, y);
}

void test_previous_day_within_a_month(void)
{
    uint8_t d = 0x15, m = 0x03, y = 0x26;
    DateBcd_AddDays(&d, &m, &y, -1);
    TEST_ASSERT_EQUAL_HEX8(0x14, d);
    TEST_ASSERT_EQUAL_HEX8(0x03, m);
    TEST_ASSERT_EQUAL_HEX8(0x26, y);
}

void test_previous_day_at_the_start_of_a_month(void)
{
    uint8_t d = 0x01, m = 0x05, y = 0x26;
    DateBcd_AddDays(&d, &m, &y, -1);
    TEST_ASSERT_EQUAL_HEX8(0x30, d);
    TEST_ASSERT_EQUAL_HEX8(0x04, m);
    TEST_ASSERT_EQUAL_HEX8(0x26, y);
}

void test_previous_day_of_1_march_in_a_common_year(void)
{
    uint8_t d = 0x01, m = 0x03, y = 0x25;
    DateBcd_AddDays(&d, &m, &y, -1);
    TEST_ASSERT_EQUAL_HEX8(0x28, d);
    TEST_ASSERT_EQUAL_HEX8(0x02, m);
}

void test_previous_day_of_1_march_in_a_leap_year(void)
{
    uint8_t d = 0x01, m = 0x03, y = 0x24;
    DateBcd_AddDays(&d, &m, &y, -1);
    TEST_ASSERT_EQUAL_HEX8(0x29, d);
    TEST_ASSERT_EQUAL_HEX8(0x02, m);
}

void test_previous_day_at_the_start_of_the_year(void)
{
    uint8_t d = 0x01, m = 0x01, y = 0x26;
    DateBcd_AddDays(&d, &m, &y, -1);
    TEST_ASSERT_EQUAL_HEX8(0x31, d);
    TEST_ASSERT_EQUAL_HEX8(0x12, m);
    TEST_ASSERT_EQUAL_HEX8(0x25, y);
}

void test_previous_day_at_the_start_of_2000_wraps_to_2099(void)
{
    uint8_t d = 0x01, m = 0x01, y = 0x00;
    DateBcd_AddDays(&d, &m, &y, -1);
    TEST_ASSERT_EQUAL_HEX8(0x31, d);
    TEST_ASSERT_EQUAL_HEX8(0x12, m);
    TEST_ASSERT_EQUAL_HEX8(0x99, y);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_previous_day_within_a_month);
    RUN_TEST(test_previous_day_at_the_start_of_a_month);
    RUN_TEST(test_previous_day_of_1_march_in_a_common_year);
    RUN_TEST(test_previous_day_of_1_march_in_a_leap_year);
    RUN_TEST(test_previous_day_at_the_start_of_the_year);
    RUN_TEST(test_previous_day_at_the_start_of_2000_wraps_to_2099);
    RUN_TEST(test_next_day_at_the_end_of_the_year);
    RUN_TEST(test_next_day_at_the_end_of_2099_wraps_to_2000);
    RUN_TEST(test_next_day_within_a_month);
    RUN_TEST(test_next_day_at_the_end_of_a_30_day_month);
    RUN_TEST(test_next_day_at_the_end_of_a_31_day_month);
    RUN_TEST(test_next_day_after_28_february_in_a_common_year);
    RUN_TEST(test_next_day_after_28_february_in_a_leap_year);
    RUN_TEST(test_next_day_after_29_february_in_a_leap_year);
    RUN_TEST(test_the_year_2000_is_a_leap_year);
    return UNITY_END();
}
