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

/* Feed one byte. Returns true when a complete, CRC-valid frame is in *out. */
bool bms_parser_feed(BmsParser_t *p, uint8_t b, BmsSample_t *out)
{
    if (p->state == 0) {
        if (b == BMS_SOF) { p->idx = 0; p->state = 1; }
        return false;
    }

    p->buf[p->idx++] = b;

    if (p->idx == 1 && b != BMS_PAYLOAD_LEN) {   /* bad LEN: resync */
        p->len_err++;
        p->state = 0;
        return false;
    }
    if (p->idx < sizeof(p->buf)) return false;

    p->state = 0;
    uint16_t rx_crc = (uint16_t)(p->buf[p->idx - 2] | (p->buf[p->idx - 1] << 8));
    if (bms_crc16(p->buf, p->idx - 2) != rx_crc) { p->crc_err++; return false; }

    out->seq = p->buf[1];
    memcpy(out->cell_mv,     &p->buf[2],  8);
    memcpy(&out->current_ma, &p->buf[10], 2);
    memcpy(out->temp_dc,     &p->buf[12], 4);
    p->frames_ok++;
    return true;
}

