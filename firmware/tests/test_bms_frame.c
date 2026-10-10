/* Host-side tests for the frame parser (no HAL, no board needed). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "bms_frame.h"

static int failures = 0;

static void check(const char *name, int ok)
{
    printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) failures++;
}

static void check_eq(const char *name, long got, long want)
{
    int ok = (got == want);
    printf("%s  %s", ok ? "PASS" : "FAIL", name);
    if (!ok) printf("  (got %ld, expected %ld)", got, want);
    printf("\n");
    if (!ok) failures++;
}

/* Feed a byte array to the parser; returns how many valid frames came out. */
static int feed_all(BmsParser_t *p, const uint8_t *data, size_t n, BmsSample_t *last)
{
    int frames = 0;
    size_t i;
    BmsSample_t s;
    for (i = 0; i < n; i++) {
        if (bms_parser_feed(p, data[i], &s)) {
            frames++;
            if (last) *last = s;
        }
    }
    return frames;
}

/* Frame produced by the Python emulator (hil/emulator.py --selftest). */
static const uint8_t golden[19] = {
    0xa5, 0x0e, 0x00, 0xd8, 0x0e, 0xd8, 0x0e, 0xd8, 0x0e, 0xd8, 0x0e,
    0x24, 0xfa, 0x18, 0x01, 0x19, 0x01, 0x8a, 0x92
};

/* Build a valid 19-byte frame with the given sequence number. */
static void build_frame(uint8_t out[19], uint8_t seq)
{
    int i;
    uint16_t crc;
    out[0] = BMS_SOF;
    out[1] = BMS_PAYLOAD_LEN;
    out[2] = seq;
    for (i = 0; i < 4; i++) {                    /* cells 3800..3803 mV */
        uint16_t mv = (uint16_t)(3800 + i);
        out[3 + 2 * i] = (uint8_t)(mv & 0xFF);
        out[4 + 2 * i] = (uint8_t)(mv >> 8);
    }
    out[11] = 0x24; out[12] = 0xFA;              /* -1500 mA */
    out[13] = 0x18; out[14] = 0x01;              /* 28.0 C   */
    out[15] = 0x19; out[16] = 0x01;              /* 28.1 C   */
    crc = bms_crc16(&out[1], 16);                /* LEN + SEQ + 14 payload bytes */
    out[17] = (uint8_t)(crc & 0xFF);
    out[18] = (uint8_t)(crc >> 8);
}

int main(void)
{
    BmsParser_t p;
    BmsSample_t last;
    uint8_t f[19], a[19], b[19], c[19], stream[64];
    uint8_t junk[5] = { 0x00, 0xFF, 0x12, 0x34, 0x56 };
    int n;

    memset(&last, 0, sizeof(last));

    /* T-01: CRC-16/MODBUS standard check value */
    check_eq("T-01 CRC-16/MODBUS check value", bms_crc16((const uint8_t *)"123456789", 9), 0x4B37);

    /* T-02: the emulator's golden frame is accepted and decoded */
    bms_parser_init(&p);
    n = feed_all(&p, golden, sizeof(golden), &last);
    check_eq("T-02 golden frame accepted", n, 1);
    check("T-02 golden frame fields decoded",
          last.seq == 0 &&
          last.cell_mv[0] == 3800 && last.cell_mv[1] == 3800 &&
          last.cell_mv[2] == 3800 && last.cell_mv[3] == 3800 &&
          last.current_ma == -1500 &&
          last.temp_dc[0] == 280 && last.temp_dc[1] == 281);

    /* T-03: corrupted CRC is rejected, the next good frame still works */
    memcpy(f, golden, 19);
    f[18] ^= 0xFF;
    bms_parser_init(&p);
    n = feed_all(&p, f, 19, &last);
    check_eq("T-03 corrupted CRC rejected", n, 0);
    check_eq("T-03 crc_err counter", (long)p.crc_err, 1);
    n = feed_all(&p, golden, 19, &last);
    check_eq("T-03 next good frame accepted", n, 1);

    /* T-04: wrong LEN byte is rejected and the parser resyncs */
    memcpy(f, golden, 19);
    f[1] = 0x0F;
    bms_parser_init(&p);
    n = feed_all(&p, f, 19, &last);
    check_eq("T-04 bad LEN rejected", n, 0);
    check_eq("T-04 len_err counter", (long)p.len_err, 1);
    n = feed_all(&p, golden, 19, &last);
    check_eq("T-04 next good frame accepted", n, 1);

    /* T-05: garbage before the start byte is skipped */
    bms_parser_init(&p);
    feed_all(&p, junk, sizeof(junk), &last);
    n = feed_all(&p, golden, 19, &last);
    check_eq("T-05 frame after garbage prefix accepted", n, 1);

    /* T-06: three valid frames back to back */
    build_frame(a, 1); build_frame(b, 2); build_frame(c, 3);
    memcpy(stream, a, 19); memcpy(stream + 19, b, 19); memcpy(stream + 38, c, 19);
    bms_parser_init(&p);
    n = feed_all(&p, stream, 57, &last);
    check_eq("T-06 three back-to-back frames", n, 3);
    check_eq("T-06 last sequence number", last.seq, 3);

    /* T-07: one byte lost inside frame A; frames B and C should still be recovered */
    memcpy(stream, a, 7);                 /* A bytes 0..6 */
    memcpy(stream + 7, a + 8, 11);        /* A bytes 8..18 (byte 7 dropped) */
    memcpy(stream + 18, b, 19);
    memcpy(stream + 37, c, 19);
    bms_parser_init(&p);
    n = feed_all(&p, stream, 56, &last);
    check_eq("T-07 resync after a dropped byte (B and C recovered)", n, 2);

    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
