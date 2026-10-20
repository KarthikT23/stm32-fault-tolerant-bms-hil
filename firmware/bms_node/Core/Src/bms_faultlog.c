/*
 * bms_faultlog.c
 *
 *  Created on: Oct 20, 2026
 *      Author: Karthik
 */
#include "bms_faultlog.h"
#include <string.h>

static uint16_t rec_crc(const uint32_t w[FL_WORDS_PER_REC])
{
    uint8_t b[24];
    uint32_t i;
    for (i = 0; i < 6; i++) {                       /* little-endian, independent of the CPU */
        b[4 * i + 0] = (uint8_t)(w[i]);
        b[4 * i + 1] = (uint8_t)(w[i] >> 8);
        b[4 * i + 2] = (uint8_t)(w[i] >> 16);
        b[4 * i + 3] = (uint8_t)(w[i] >> 24);
    }
    return bms_crc16(b, sizeof(b));
}

static void build(uint32_t w[FL_WORDS_PER_REC], uint32_t seq, uint32_t type,
                  BmsFault_t cause, const BmsSample_t *s)
{
    uint32_t i;
    for (i = 0; i < FL_WORDS_PER_REC; i++) w[i] = 0;
    w[0] = seq;
    w[1] = type | ((uint32_t)cause << 8);
    if (s) {
        w[2] = (uint32_t)s->cell_mv[0] | ((uint32_t)s->cell_mv[1] << 16);
        w[3] = (uint32_t)s->cell_mv[2] | ((uint32_t)s->cell_mv[3] << 16);
        w[4] = (uint32_t)(uint16_t)s->current_ma | ((uint32_t)(uint16_t)s->temp_dc[0] << 16);
        w[5] = (uint32_t)(uint16_t)s->temp_dc[1] | ((uint32_t)s->seq << 16);
    }
    w[6] = rec_crc(w);
    w[7] = FL_COMMIT;
}

static int is_empty(const uint32_t w[FL_WORDS_PER_REC])
{
    uint32_t i;
    for (i = 0; i < FL_WORDS_PER_REC; i++) if (w[i] != 0xFFFFFFFFu) return 0;
    return 1;
}

static int is_valid(const uint32_t w[FL_WORDS_PER_REC])
{
    return w[7] == FL_COMMIT && w[6] == (uint32_t)rec_crc(w);
}

void fl_scan(const FlOps_t *ops, FlState_t *st)
{
    uint32_t cap = ops->size / FL_REC_BYTES;
    uint32_t i, k, w[FL_WORDS_PER_REC];
    uint32_t last_type = 0, bad_since_valid = 0;

    memset(st, 0, sizeof(*st));
    for (i = 0; i < cap; i++) {
        for (k = 0; k < FL_WORDS_PER_REC; k++)
            w[k] = ops->read32(ops->base + i * FL_REC_BYTES + 4u * k);
        if (is_empty(w)) break;                          /* end of the used area */
        if (!is_valid(w)) { st->damaged++; bad_since_valid++; continue; }

        st->valid++;
        bad_since_valid = 0;
        st->last_seq = w[0];
        last_type = w[1] & 0xFFu;
        if (last_type == FL_TYPE_FAULT) {
            st->cause = (BmsFault_t)((w[1] >> 8) & 0xFFu);
            st->snap.cell_mv[0]  = (uint16_t)(w[2]);
            st->snap.cell_mv[1]  = (uint16_t)(w[2] >> 16);
            st->snap.cell_mv[2]  = (uint16_t)(w[3]);
            st->snap.cell_mv[3]  = (uint16_t)(w[3] >> 16);
            st->snap.current_ma  = (int16_t)(uint16_t)(w[4]);
            st->snap.temp_dc[0]  = (int16_t)(uint16_t)(w[4] >> 16);
            st->snap.temp_dc[1]  = (int16_t)(uint16_t)(w[5]);
            st->snap.seq         = (uint8_t)(w[5] >> 16);
        } else {
            st->cause = BMS_FAULT_NONE;
            memset(&st->snap, 0, sizeof(st->snap));
        }
    }
    st->next_slot = i;
    st->suspect   = (bad_since_valid > 0) ? 1u : 0u;
    st->latched   = (last_type == FL_TYPE_FAULT || st->suspect) ? 1u : 0u;
}

/* Programs the first n words of w into the next free slot. The slot is used up
   even if programming fails, because a half-written slot cannot be rewritten. */
static int write_rec(const FlOps_t *ops, FlState_t *st, const uint32_t w[FL_WORDS_PER_REC], uint32_t n)
{
    uint32_t cap = ops->size / FL_REC_BYTES;
    uint32_t addr, k;

    if (st->next_slot >= cap) return -2;
    addr = ops->base + st->next_slot * FL_REC_BYTES;
    st->next_slot++;
    for (k = 0; k < n; k++) {
        if (ops->prog32(addr + 4u * k, w[k]) != 0) {
            st->damaged++; st->suspect = 1; st->latched = 1;
            return -1;
        }
    }
    return 0;
}

int fl_append_fault(const FlOps_t *ops, FlState_t *st, BmsFault_t cause, const BmsSample_t *s)
{
    uint32_t w[FL_WORDS_PER_REC];
    int rc;
    build(w, st->last_seq + 1u, FL_TYPE_FAULT, cause, s);
    rc = write_rec(ops, st, w, FL_WORDS_PER_REC);
    if (rc == 0) {
        st->last_seq++; st->valid++;
        st->latched = 1; st->suspect = 0; st->cause = cause; st->snap = *s;
    }
    return rc;
}

int fl_append_clear(const FlOps_t *ops, FlState_t *st)
{
    uint32_t w[FL_WORDS_PER_REC];
    int rc;
    build(w, st->last_seq + 1u, FL_TYPE_CLEAR, BMS_FAULT_NONE, NULL);
    rc = write_rec(ops, st, w, FL_WORDS_PER_REC);
    if (rc == 0) {
        st->last_seq++; st->valid++;
        st->latched = 0; st->suspect = 0; st->cause = BMS_FAULT_NONE;
        memset(&st->snap, 0, sizeof(st->snap));
    }
    return rc;
}

int fl_append_partial(const FlOps_t *ops, FlState_t *st, uint32_t type, uint32_t n_words)
{
    uint32_t w[FL_WORDS_PER_REC];
    int rc;
    if (n_words > FL_WORDS_PER_REC) n_words = FL_WORDS_PER_REC;
    build(w, st->last_seq + 1u, type, BMS_FAULT_NONE, NULL);
    rc = write_rec(ops, st, w, n_words);
    if (rc == 0 && n_words > 0 && n_words < FL_WORDS_PER_REC) {
        st->damaged++; st->suspect = 1; st->latched = 1;   /* what a boot scan would conclude */
    }
    return rc;
}

