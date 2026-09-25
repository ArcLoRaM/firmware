/*!
 * \file      arclog.c
 *
 * \brief     ArcLog helpers: sequence counter and enum-to-name tables.
 *
 * \author    Simon R.C. Langlais ( Celium )
 *
 */
#include "arclog.h"
#include "protocol_types.h"

/* Tables are indexed by enum value; the asserts keep them in step with
 * protocol_types.h. */
_Static_assert(CLOCK_WARM == 2, "update s_clock_names");
_Static_assert(MAC_STATE_PAIRED == 3, "update s_mac_state_names");
_Static_assert(SLOT_SKIP == 2, "update s_decision_names");
_Static_assert(PHASE_TYPE_SYNC == 4, "update s_phase_type_names");
_Static_assert(SLOT_POS_FOOTER == 2, "update s_slot_pos_names");

static const char *const s_clock_names[]      = { "COLD", "ACQ", "WARM" };
static const char *const s_mac_state_names[]  = { "SCAN", "SYNC", "ACTIVE", "PAIRED" };
static const char *const s_decision_names[]   = { "TX", "RX", "SKIP" };
static const char *const s_phase_type_names[] = { "BCN", "UL", "DL", "CLU", "SYNC" };
static const char *const s_slot_pos_names[]   = { "HDR", "CELL", "FTR" };

#define NAME_OF(table, v) \
    (((v) < (sizeof(table) / sizeof((table)[0]))) ? (table)[(v)] : "?")

static uint8_t s_seq;

uint8_t ArcLog_NextSeq(void)
{
    return s_seq++;
}

char ArcLog_LevelChar(uint32_t vlevel)
{
    static const char letters[] = { 'A', 'L', 'M', 'H' };
    return (vlevel < sizeof(letters)) ? letters[vlevel] : '?';
}

static uint8_t *put2(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)('0' + (v / 10u) % 10u);
    p[1] = (uint8_t)('0' + v % 10u);
    return p + 2;
}

uint16_t ArcLog_FormatTimestamp(uint8_t *buf,
                                uint8_t year, uint8_t month, uint8_t day,
                                uint8_t hours, uint8_t minutes, uint8_t seconds,
                                uint32_t ssr, uint32_t prediv_s)
{
    uint32_t frac = (ssr <= prediv_s)
                  ? ((prediv_s - ssr) * 10000u) / (prediv_s + 1u)
                  : 0u;
    uint8_t *p = buf;

    p = put2(p, year);
    p = put2(p, month);
    p = put2(p, day);
    *p++ = 'T';
    p = put2(p, hours);
    p = put2(p, minutes);
    p = put2(p, seconds);
    *p++ = '.';
    p = put2(p, frac / 100u);
    p = put2(p, frac % 100u);
    *p++ = ' ';
    return (uint16_t)(p - buf);
}

const char *ArcLog_ClockName(uint32_t v)     { return NAME_OF(s_clock_names, v);      }
const char *ArcLog_MacStateName(uint32_t v)  { return NAME_OF(s_mac_state_names, v);  }
const char *ArcLog_DecisionName(uint32_t v)  { return NAME_OF(s_decision_names, v);   }
const char *ArcLog_PhaseTypeName(uint32_t v) { return NAME_OF(s_phase_type_names, v); }
const char *ArcLog_SlotPosName(uint32_t v)   { return NAME_OF(s_slot_pos_names, v);   }
