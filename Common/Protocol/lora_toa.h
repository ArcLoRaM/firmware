/*!
 * \file      lora_toa.h
 *
 * \brief     LoRa time on air in microseconds (issue #82).
 *
 * \details   The radio driver's Radio.TimeOnAir answers in whole ms (991 for the
 *            Sync packet, which is 991.232 ms): a constant 0.2 ms bias in a
 *            SyncStamp that is now in ticks. This is the formula of the SX126x
 *            datasheet (Semtech AN1200.13), in integers:
 *
 *              Tsym      = 2^SF / BW
 *              DE        = 1 when Tsym > 16 ms (low data rate optimisation)
 *              n_payload = 8 + max(ceil((8 PL - 4 SF + 28 + 16 CRC - 20 IH)
 *                                       / (4 (SF - 2 DE))) x (CR + 4), 0)
 *              ToA       = (n_preamble + 4.25 + n_payload) x Tsym
 *
 *            Tsym is a multiple of 4 us at every SF and BW of the SX126x, so
 *            the quarter symbol of the preamble is exact.
 *
 * \author    Simon R.C. Langlais ( Celium )
 *
 */
#ifndef LORA_TOA_H
#define LORA_TOA_H

#include <stdbool.h>
#include <stdint.h>

/*!
 * \param   payload_len  Payload bytes.
 * \param   sf           Spreading factor, 5 to 12.
 * \param   bw_hz        Bandwidth, Hz (125000, 250000, 500000...).
 * \param   cr           Coding rate index: 1 = 4/5 ... 4 = 4/8.
 * \param   preamble     Programmed preamble symbols.
 * \param   crc          CRC on.
 * \param   implicit     Implicit header (no PHY header).
 */
static inline uint32_t LoraToa_Us(uint8_t payload_len, uint8_t sf, uint32_t bw_hz,
                                  uint8_t cr, uint16_t preamble, bool crc, bool implicit)
{
    uint64_t tsym_us = ((1ull << sf) * 1000000ull) / bw_hz;
    uint32_t de      = (tsym_us > 16000ull) ? 1u : 0u;

    int32_t  num  = 8 * (int32_t)payload_len - 4 * (int32_t)sf + 28
                    + (crc ? 16 : 0) - (implicit ? 20 : 0);
    int32_t  den  = 4 * ((int32_t)sf - 2 * (int32_t)de);
    int32_t  syms = (num > 0) ? ((num + den - 1) / den) * ((int32_t)cr + 4) : 0;
    uint64_t n_payload = 8u + (uint32_t)syms;

    /* (n_preamble + 4.25) = (4 n_preamble + 17) / 4 symbols */
    uint64_t quarter = (4ull * preamble + 17ull) * tsym_us;
    return (uint32_t)((quarter + 2ull) / 4ull + n_payload * tsym_us);
}

#endif /* LORA_TOA_H */
