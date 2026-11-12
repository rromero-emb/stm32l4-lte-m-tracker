#include "payload.h"

#include <string.h>

// Field widths in transmission order (MSB first).
#define W_VER 2U
#define W_TYPE 3U
#define W_FIX 2U
#define W_MOVING 1U
#define W_SEQ 8U
#define W_TIME 32U
#define W_LAT 24U
#define W_LON 24U
#define W_ALT 11U
#define W_SPEED 8U
#define W_HEADING 6U
#define W_HACC 5U
#define W_NSAT 4U
#define W_TTFF 5U
#define W_VBAT 8U
#define W_TEMP 7U
#define W_RSRP 6U

#define ALT_OFFSET_M 500
#define ALT_STEP_M 4
#define VBAT_BASE_MV 2000U
#define VBAT_STEP_MV 10U
#define TEMP_OFFSET_C 40
#define RSRP_OFFSET_DBM 140

typedef struct {
    uint8_t *buf;
    uint32_t bit;
} bitw_t;

static void put_bits(bitw_t *w, uint32_t value, uint32_t width)
{
    for (uint32_t i = width; i > 0U; i--) {
        uint32_t b = (value >> (i - 1U)) & 1U;
        if (b != 0U) {
            w->buf[w->bit / 8U] |= (uint8_t)(0x80U >> (w->bit % 8U));
        }
        w->bit++;
    }
}

static uint32_t get_bits(const uint8_t *buf, uint32_t *bit, uint32_t width)
{
    uint32_t v = 0U;
    for (uint32_t i = 0; i < width; i++) {
        v = (v << 1U) | ((buf[*bit / 8U] >> (7U - (*bit % 8U))) & 1U);
        (*bit)++;
    }
    return v;
}

static uint32_t clamp_u(int64_t v, uint32_t width)
{
    int64_t max = ((int64_t)1 << width) - 1;
    if (v < 0) {
        return 0U;
    }
    return (uint32_t)((v > max) ? max : v);
}

// Map [lo, lo + span) onto 0 .. 2^width - 1 (floor), using 64-bit integer math (no FPU needed).
static uint32_t scale(int64_t v, int64_t lo, int64_t span, uint32_t width)
{
    int64_t q = ((v - lo) << width) / span;
    return clamp_u(q, width);
}

// Centre of step q.
static int32_t unscale(uint32_t q, int64_t lo, int64_t span, uint32_t width)
{
    return (int32_t)(lo + ((((int64_t)q << 1) + 1) * span >> (width + 1U)));
}

bool payload_encode(const tracker_report_t *r, uint8_t out[PAYLOAD_LEN])
{
    if ((r == NULL) || (r->unix_time < PAYLOAD_EPOCH_UNIX) || (r->lat_e7 < -900000000) || (r->lat_e7 > 900000000) ||
        (r->lon_e7 < -1800000000) || (r->lon_e7 > 1800000000)) {
        return false;
    }
    memset(out, 0, PAYLOAD_LEN);
    bitw_t w = {out, 0U};
    put_bits(&w, PAYLOAD_VERSION, W_VER);
    put_bits(&w, clamp_u(r->type, W_TYPE), W_TYPE);
    put_bits(&w, clamp_u(r->fix, W_FIX), W_FIX);
    put_bits(&w, r->moving ? 1U : 0U, W_MOVING);
    put_bits(&w, r->seq, W_SEQ);
    put_bits(&w, r->unix_time - PAYLOAD_EPOCH_UNIX, W_TIME);
    put_bits(&w, scale(r->lat_e7, -900000000LL, 1800000000LL, W_LAT), W_LAT);
    put_bits(&w, scale(r->lon_e7, -1800000000LL, 3600000000LL, W_LON), W_LON);
    put_bits(&w, clamp_u(((int64_t)r->alt_m + ALT_OFFSET_M) / ALT_STEP_M, W_ALT), W_ALT);
    put_bits(&w, clamp_u(r->speed_kmh, W_SPEED), W_SPEED);
    put_bits(&w, clamp_u(((int64_t)(r->heading_deg % 360U) << W_HEADING) / 360, W_HEADING), W_HEADING);
    put_bits(&w, clamp_u(r->hacc_m / 2U, W_HACC), W_HACC);
    put_bits(&w, clamp_u(r->nsat, W_NSAT), W_NSAT);
    put_bits(&w, clamp_u(r->ttff_s / 2U, W_TTFF), W_TTFF);
    put_bits(&w, clamp_u(((int64_t)r->vbat_mv - VBAT_BASE_MV) / VBAT_STEP_MV, W_VBAT), W_VBAT);
    put_bits(&w, clamp_u((int64_t)r->temp_c + TEMP_OFFSET_C, W_TEMP), W_TEMP);
    put_bits(&w, clamp_u((int64_t)r->rsrp_dbm + RSRP_OFFSET_DBM, W_RSRP), W_RSRP);
    return true;  // 156 bits used, the last 4 stay 0
}

bool payload_decode(const uint8_t in[PAYLOAD_LEN], tracker_report_t *r)
{
    uint32_t bit = 0U;
    if (get_bits(in, &bit, W_VER) != PAYLOAD_VERSION) {
        return false;
    }
    memset(r, 0, sizeof *r);
    r->type = (msg_type_t)get_bits(in, &bit, W_TYPE);
    r->fix = (fix_type_t)get_bits(in, &bit, W_FIX);
    r->moving = get_bits(in, &bit, W_MOVING) != 0U;
    r->seq = (uint8_t)get_bits(in, &bit, W_SEQ);
    r->unix_time = get_bits(in, &bit, W_TIME) + PAYLOAD_EPOCH_UNIX;
    r->lat_e7 = unscale(get_bits(in, &bit, W_LAT), -900000000LL, 1800000000LL, W_LAT);
    r->lon_e7 = unscale(get_bits(in, &bit, W_LON), -1800000000LL, 3600000000LL, W_LON);
    r->alt_m = ((int32_t)get_bits(in, &bit, W_ALT) * ALT_STEP_M) - ALT_OFFSET_M;
    r->speed_kmh = (uint16_t)get_bits(in, &bit, W_SPEED);
    r->heading_deg = (uint16_t)((get_bits(in, &bit, W_HEADING) * 360U) >> W_HEADING);
    r->hacc_m = (uint16_t)(get_bits(in, &bit, W_HACC) * 2U);
    r->nsat = (uint8_t)get_bits(in, &bit, W_NSAT);
    r->ttff_s = (uint16_t)(get_bits(in, &bit, W_TTFF) * 2U);
    r->vbat_mv = (uint16_t)((get_bits(in, &bit, W_VBAT) * VBAT_STEP_MV) + VBAT_BASE_MV);
    r->temp_c = (int16_t)((int32_t)get_bits(in, &bit, W_TEMP) - TEMP_OFFSET_C);
    r->rsrp_dbm = (int16_t)((int32_t)get_bits(in, &bit, W_RSRP) - RSRP_OFFSET_DBM);
    return true;
}
