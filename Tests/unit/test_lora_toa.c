#include "unity.h"
#include <stdbool.h>
#include <stdint.h>
#include "lora_toa.h"

/*
 * LoRa time on air in microseconds, from the formula of the SX126x datasheet /
 * Semtech AN1200.13 (issue #82). Not measured: the expected values are worked
 * by hand from the formula and agree with the Semtech LoRa calculator.
 *
 *   Tsym = 2^SF / BW
 *   n_payload = 8 + max(ceil((8 PL - 4 SF + 28 + 16 CRC - 20 IH) / (4 (SF - 2 DE))) x (CR + 4), 0)
 *   ToA = (n_preamble + 4.25 + n_payload) x Tsym
 */

void setUp(void) {}
void tearDown(void) {}

/* The Sync packet: SF12, BW125, CR4/5, 8 preamble symbols, explicit header,
 * CRC, 10 bytes. Tsym 32.768 ms (DE = 1); n_payload = 8 + ceil(76/40) x 5 = 18;
 * (12.25 + 18) x 32.768 = 991.232 ms. The driver's whole-ms answer was 991. */
void test_the_sync_packet_is_991232_us(void)
{
    TEST_ASSERT_EQUAL_UINT32(991232u, LoraToa_Us(10u, 12u, 125000u, 1u, 8u, true, false));
}

/* SF7, BW125: Tsym 1.024 ms, no low data rate optimisation;
 * n_payload = 8 + ceil(96/28) x 5 = 28; (12.25 + 28) x 1.024 = 41.216 ms. */
void test_sf7_ten_bytes_is_41216_us(void)
{
    TEST_ASSERT_EQUAL_UINT32(41216u, LoraToa_Us(10u, 7u, 125000u, 1u, 8u, true, false));
}

/* SF12, 51 bytes: n_payload = 8 + ceil(388 / 40) x 5 = 63; 75.25 x 32.768 = 2465.792 ms. */
void test_sf12_fifty_one_bytes_is_2465792_us(void)
{
    TEST_ASSERT_EQUAL_UINT32(2465792u, LoraToa_Us(51u, 12u, 125000u, 1u, 8u, true, false));
}

/* The payload term cannot go negative: a 1-byte SF12 packet has the 8 fixed symbols. */
void test_a_tiny_packet_has_the_fixed_payload_symbols(void)
{
    /* numerator 8 - 48 + 28 + 16 = 4 -> ceil(4/40) = 1 -> 5 -> n_payload 13; (12.25 + 13) x 32.768 */
    TEST_ASSERT_EQUAL_UINT32(827392u, LoraToa_Us(1u, 12u, 125000u, 1u, 8u, true, false));
}

/* An implicit header saves 20 bits, no CRC saves 16: SF7, 10 bytes:
 * numerator 80 - 28 + 28 = 80 -> ceil(80/28) = 3 -> 15 -> n_payload 23; 35.25 x 1.024 = 36.096 ms. */
void test_implicit_header_and_no_crc(void)
{
    TEST_ASSERT_EQUAL_UINT32(36096u, LoraToa_Us(10u, 7u, 125000u, 1u, 8u, false, true));
}

/* A longer coding rate costs symbols: CR4/8 (4) at SF7, 10 bytes: ceil(96/28) = 4 x 8 = 32,
 * n_payload 40; 52.25 x 1.024 = 53.504 ms. */
void test_coding_rate_4_8(void)
{
    TEST_ASSERT_EQUAL_UINT32(53504u, LoraToa_Us(10u, 7u, 125000u, 4u, 8u, true, false));
}

/* The driver's whole-ms value and ours agree to the ms (991.232 -> 991). */
void test_the_us_value_rounds_to_the_drivers_ms(void)
{
    TEST_ASSERT_EQUAL_UINT32(991u, (LoraToa_Us(10u, 12u, 125000u, 1u, 8u, true, false) + 500u) / 1000u);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_sync_packet_is_991232_us);
    RUN_TEST(test_sf7_ten_bytes_is_41216_us);
    RUN_TEST(test_sf12_fifty_one_bytes_is_2465792_us);
    RUN_TEST(test_a_tiny_packet_has_the_fixed_payload_symbols);
    RUN_TEST(test_implicit_header_and_no_crc);
    RUN_TEST(test_coding_rate_4_8);
    RUN_TEST(test_the_us_value_rounds_to_the_drivers_ms);
    return UNITY_END();
}
