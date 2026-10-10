/*
 * bms_frame.h
 *
 *  Created on: Oct 10, 2026
 *      Author: Karthik
 */

#ifndef INC_BMS_FRAME_H_
#define INC_BMS_FRAME_H_

#pragma once
#include <stdint.h>
#include <stdbool.h>

#define BMS_SOF          0xA5u
#define BMS_PAYLOAD_LEN  14u

typedef struct {
    uint16_t cell_mv[4];
    int16_t  current_ma;
    int16_t  temp_dc[2];
    uint8_t  seq;
} BmsSample_t;

typedef struct {
    uint8_t  state;                        /* 0 = wait for SOF, 1 = collecting */
    uint8_t  idx;
    uint8_t  buf[2 + BMS_PAYLOAD_LEN + 2]; /* LEN, SEQ, payload, CRC */
    uint32_t frames_ok;
    uint32_t crc_err;
    uint32_t len_err;
} BmsParser_t;

uint16_t bms_crc16(const uint8_t *data, uint16_t len);
void     bms_parser_init(BmsParser_t *p);
bool     bms_parser_feed(BmsParser_t *p, uint8_t byte, BmsSample_t *out);

#endif /* INC_BMS_FRAME_H_ */
