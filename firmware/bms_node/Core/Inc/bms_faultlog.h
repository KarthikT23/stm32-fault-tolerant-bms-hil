/*
 * bms_faultlog.h
 *
 *  Created on: Oct 20, 2026
 *      Author: Karthik
 */

#ifndef INC_BMS_FAULTLOG_H_
#define INC_BMS_FAULTLOG_H_

#pragma once
#include <stdint.h>
#include "bms_frame.h"
#include "bms_protect.h"

/* Append-only fault log. One record is 8 words (32 bytes):
   w0 sequence number, w1 type | cause << 8, w2..w5 frame snapshot,
   w6 CRC-16 of w0..w5, w7 commit marker. The commit marker is written LAST, so a
   record cut short by a power failure never looks valid. Erased flash reads 0xFFFFFFFF. */
#define FL_WORDS_PER_REC   8u
#define FL_REC_BYTES       (FL_WORDS_PER_REC * 4u)
#define FL_COMMIT          0xC0FFEE01u

#define FL_TYPE_FAULT      1u
#define FL_TYPE_CLEAR      2u

typedef struct {
    uint32_t (*read32)(uint32_t addr);
    int      (*prog32)(uint32_t addr, uint32_t val);   /* returns 0 on success */
    uint32_t base;                                     /* first byte of the log area */
    uint32_t size;                                     /* bytes, a multiple of FL_REC_BYTES */
} FlOps_t;

typedef struct {
    uint8_t     latched;     /* 1 = boot in FAULT */
    uint8_t     suspect;     /* 1 = latched only because a damaged record follows the last valid one */
    BmsFault_t  cause;       /* cause of the last valid FAULT record, NONE if unknown */
    uint32_t    last_seq;    /* number of the last valid record, 0 if none */
    uint32_t    valid;       /* valid records found */
    uint32_t    damaged;     /* non-empty records that failed the check */
    uint32_t    next_slot;   /* index of the next free record, capacity when full */
    BmsSample_t snap;        /* frame stored with the last valid FAULT record */
} FlState_t;

void fl_scan(const FlOps_t *ops, FlState_t *st);          /* read the log; call at boot */
int  fl_append_fault(const FlOps_t *ops, FlState_t *st, BmsFault_t cause, const BmsSample_t *s);
int  fl_append_clear(const FlOps_t *ops, FlState_t *st);
/* test hook: write only the first n words of a record, as if power failed part way */
int  fl_append_partial(const FlOps_t *ops, FlState_t *st, uint32_t type, uint32_t n_words);
/* return codes: 0 ok, -1 flash programming error, -2 log full (needs a sector erase) */

#endif /* INC_BMS_FAULTLOG_H_ */
