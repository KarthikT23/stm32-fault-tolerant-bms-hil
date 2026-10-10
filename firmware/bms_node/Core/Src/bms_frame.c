/*
 * bms_frame.c
 *
 *  Created on: Oct 10, 2026
 *      Author: Karthik
 */
#include "bms_frame.h"
#include <string.h>

uint16_t bms_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++)
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
    }
    return crc;
}

void bms_parser_init(BmsParser_t *p)
{
    memset(p, 0, sizeof(*p));
}

/* Called after a bad LEN or bad CRC. The bytes already collected (buf[0..idx-1])
   may contain the start of the next frame, so scan them for a start byte and
   replay what follows it instead of discarding it all. A replay never holds
   enough bytes to complete a frame, so its result can be ignored. */
static void resync(BmsParser_t *p)
{
    uint8_t tmp[sizeof(p->buf)];
    uint8_t n = p->idx;
    uint8_t i, j;
    BmsSample_t unused;

    memcpy(tmp, p->buf, n);
    p->state = 0;
    p->idx   = 0;

    for (i = 0; i < n; i++) {
        if (tmp[i] == BMS_SOF) {
            p->state = 1;                       /* new candidate frame starts here */
            for (j = i + 1; j < n; j++) {
                bms_parser_feed(p, tmp[j], &unused);
            }
            return;
        }
    }
}

/* Feed one byte. Returns true when a complete, CRC-valid frame is in *out. */
bool bms_parser_feed(BmsParser_t *p, uint8_t b, BmsSample_t *out)
{
    if (p->state == 0) {
        if (b == BMS_SOF) { p->idx = 0; p->state = 1; }
        return false;
    }

    p->buf[p->idx++] = b;

    if (p->idx == 1 && b != BMS_PAYLOAD_LEN) {   /* bad LEN */
        p->len_err++;
        resync(p);
        return false;
    }
    if (p->idx < sizeof(p->buf)) return false;

    p->state = 0;
    uint16_t rx_crc = (uint16_t)(p->buf[p->idx - 2] | (p->buf[p->idx - 1] << 8));
    if (bms_crc16(p->buf, p->idx - 2) != rx_crc) {
        p->crc_err++;
        resync(p);
        return false;
    }

    out->seq = p->buf[1];
    memcpy(out->cell_mv,     &p->buf[2],  8);
    memcpy(&out->current_ma, &p->buf[10], 2);
    memcpy(out->temp_dc,     &p->buf[12], 4);
    p->frames_ok++;
    return true;
}

