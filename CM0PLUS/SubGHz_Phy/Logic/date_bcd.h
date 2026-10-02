/*!
 * \file      date_bcd.h
 *
 * \brief     Calendar day step on the BCD date of a SyncPayload.
 *
 * \details   The date travels as three BCD bytes (day, month, year, as
 *            \c HAL_RTC_GetDate writes them), the year being 00-99 for
 *            2000-2099. A Sync packet's date is the date of the Sync phase
 *            epoch; a node whose target time falls on the next day moves it
 *            with \ref DateBcd_AddDays (issue #28).
 *
 * \author    Simon R.C. Langlais ( Celium )
 *
 */
#ifndef DATE_BCD_H
#define DATE_BCD_H

#include <stdint.h>

static inline uint8_t date_bcd_to_bin(uint8_t bcd)
{
    return (uint8_t)((bcd >> 4) * 10u + (bcd & 0x0Fu));
}

static inline uint8_t date_bin_to_bcd(uint8_t bin)
{
    return (uint8_t)(((bin / 10u) << 4) | (bin % 10u));
}

/* year is 0-99 (2000-2099): every fourth year is a leap year, 2000 included. */
static inline uint8_t date_days_in_month(uint8_t month, uint8_t year)
{
    static const uint8_t k_days[12] = { 31, 28, 31, 30, 31, 30,
                                        31, 31, 30, 31, 30, 31 };
    if (month == 2u && (year % 4u) == 0u) {
        return 29u;
    }
    return k_days[month - 1u];
}

/*!
 * \brief   Move a BCD date by one day, forward (\c +1) or back (\c -1).
 */
static inline void DateBcd_AddDays(uint8_t *day, uint8_t *month, uint8_t *year,
                                   int delta)
{
    uint8_t d = date_bcd_to_bin(*day);
    uint8_t m = date_bcd_to_bin(*month);
    uint8_t y = date_bcd_to_bin(*year);

    if (delta < 0) {
        if (d > 1u) {
            d--;
        } else {
            if (m == 1u) {
                m = 12u;
                y = (uint8_t)((y + 99u) % 100u);
            } else {
                m--;
            }
            d = date_days_in_month(m, y);
        }
    } else if (d >= date_days_in_month(m, y)) {
        d = 1u;
        m++;
        if (m > 12u) {
            m = 1u;
            y = (uint8_t)((y + 1u) % 100u);
        }
    } else {
        d++;
    }
    *day   = date_bin_to_bcd(d);
    *month = date_bin_to_bcd(m);
    *year  = date_bin_to_bcd(y);
}

#endif /* DATE_BCD_H */
