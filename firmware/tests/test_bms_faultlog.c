/* Host-side tests for the flash fault log, using a simulated flash (no board needed). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "bms_faultlog.h"

#define SIM_RECS 8u
static uint32_t flash[SIM_RECS * FL_WORDS_PER_REC];
static int fail_on_call = -1;          /* make the n-th program call fail, -1 = never */
static int prog_calls;

/* Simulated NOR flash: a word can only be programmed once after an erase (0xFFFFFFFF). */
static uint32_t sim_read(uint32_t addr) { return flash[addr / 4u]; }
static int sim_prog(uint32_t addr, uint32_t val)
{
    if (prog_calls++ == fail_on_call) return -1;
    if (flash[addr / 4u] != 0xFFFFFFFFu) return -1;     /* programming a non-erased word */
    flash[addr / 4u] = val;
    return 0;
}
static const FlOps_t ops = { sim_read, sim_prog, 0u, SIM_RECS * FL_REC_BYTES };

static int failures = 0;
static void check_eq(const char *name, long got, long want)
{
    int ok = (got == want);
    printf("%s  %s", ok ? "PASS" : "FAIL", name);
    if (!ok) printf("  (got %ld, expected %ld)", got, want);
    printf("\n");
    if (!ok) failures++;
}

static void erase(void) { memset(flash, 0xFF, sizeof(flash)); prog_calls = 0; fail_on_call = -1; }

static BmsSample_t sample(void)
{
    BmsSample_t s;
    memset(&s, 0, sizeof(s));
    s.cell_mv[0] = 3801; s.cell_mv[1] = 3799; s.cell_mv[2] = 4260; s.cell_mv[3] = 3800;
    s.current_ma = -1500;
    s.temp_dc[0] = 650;  s.temp_dc[1] = -45;          /* negative values must survive the round trip */
    s.seq = 32;
    return s;
}

int main(void)
{
    FlState_t st, boot;
    BmsSample_t s = sample();
    uint32_t n;
    int bad;

    /* FL-01: fresh flash is empty */
    erase();
    fl_scan(&ops, &st);
    check_eq("FL-01 empty flash: not latched", st.latched, 0);
    check_eq("FL-01 empty flash: next slot 0", (long)st.next_slot, 0);

    /* FL-02: a stored fault is restored after a reboot, with its snapshot */
    erase(); fl_scan(&ops, &st);
    check_eq("FL-02 append fault ok", fl_append_fault(&ops, &st, BMS_FAULT_OVERTEMP, &s), 0);
    fl_scan(&ops, &boot);                                 /* "reboot": rebuild the state from flash */
    check_eq("FL-02 latched after reboot", boot.latched, 1);
    check_eq("FL-02 cause restored", boot.cause, BMS_FAULT_OVERTEMP);
    check_eq("FL-02 not suspect", boot.suspect, 0);
    check_eq("FL-02 snapshot cell 2", boot.snap.cell_mv[2], 4260);
    check_eq("FL-02 snapshot current (negative)", boot.snap.current_ma, -1500);
    check_eq("FL-02 snapshot temp 1 (negative)", boot.snap.temp_dc[1], -45);
    check_eq("FL-02 snapshot frame seq", boot.snap.seq, 32);

    /* FL-03: a proper clear record releases the latch */
    check_eq("FL-03 append clear ok", fl_append_clear(&ops, &boot), 0);
    fl_scan(&ops, &boot);
    check_eq("FL-03 not latched after clear", boot.latched, 0);
    check_eq("FL-03 sequence numbers count up", (long)boot.last_seq, 2);

    /* FL-04: fault, clear, fault again: the newest record wins */
    check_eq("FL-04 second fault ok", fl_append_fault(&ops, &boot, BMS_FAULT_OVERVOLT, &s), 0);
    fl_scan(&ops, &boot);
    check_eq("FL-04 latched, new cause", boot.cause, BMS_FAULT_OVERVOLT);
    check_eq("FL-04 three valid records", (long)boot.valid, 3);

    /* FL-05: power lost while writing a FAULT: every cut point must come back latched (fail-safe) */
    bad = 0;
    for (n = 1; n < FL_WORDS_PER_REC; n++) {
        erase(); fl_scan(&ops, &st);
        fl_append_partial(&ops, &st, FL_TYPE_FAULT, n);
        fl_scan(&ops, &boot);
        if (!(boot.latched == 1 && boot.suspect == 1)) bad++;
    }
    check_eq("FL-05 torn FAULT at cut points 1..7 -> latched (cut points that failed)", bad, 0);

    /* FL-06: power lost before the first word: nothing was written, nothing changes */
    erase(); fl_scan(&ops, &st);
    fl_append_partial(&ops, &st, FL_TYPE_FAULT, 0);
    fl_scan(&ops, &boot);
    check_eq("FL-06 nothing written -> not latched", boot.latched, 0);

    /* FL-07: a FAULT is stored, then power is lost while writing the CLEAR: fault must stay latched */
    bad = 0;
    for (n = 0; n < FL_WORDS_PER_REC; n++) {
        erase(); fl_scan(&ops, &st);
        fl_append_fault(&ops, &st, BMS_FAULT_OVERTEMP, &s);
        fl_append_partial(&ops, &st, FL_TYPE_CLEAR, n);
        fl_scan(&ops, &boot);
        if (boot.latched != 1) bad++;
    }
    check_eq("FL-07 torn CLEAR at cut points 0..7 -> still latched (cut points that failed)", bad, 0);

    /* FL-08: after a torn CLEAR, a real CLEAR still works */
    erase(); fl_scan(&ops, &st);
    fl_append_fault(&ops, &st, BMS_FAULT_OVERTEMP, &s);
    fl_append_partial(&ops, &st, FL_TYPE_CLEAR, 5);
    fl_scan(&ops, &boot);
    check_eq("FL-08 latched with a damaged record after the fault", boot.latched, 1);
    check_eq("FL-08 clear after the torn one ok", fl_append_clear(&ops, &boot), 0);
    fl_scan(&ops, &boot);
    check_eq("FL-08 released by the valid clear", boot.latched, 0);
    check_eq("FL-08 damaged slot is skipped, not reused", (long)boot.next_slot, 3);

    /* FL-09: a flipped bit in the only stored fault is detected, and the board stays latched */
    erase(); fl_scan(&ops, &st);
    fl_append_fault(&ops, &st, BMS_FAULT_OVERTEMP, &s);
    flash[3] ^= 0x00000010u;                              /* corrupt a snapshot word */
    fl_scan(&ops, &boot);
    check_eq("FL-09 corrupted record rejected by CRC", (long)boot.valid, 0);
    check_eq("FL-09 corruption -> latched (fail-safe)", boot.latched, 1);

    /* FL-10: a flipped bit in an old record that a later valid CLEAR supersedes is harmless */
    erase(); fl_scan(&ops, &st);
    fl_append_fault(&ops, &st, BMS_FAULT_OVERTEMP, &s);
    fl_append_clear(&ops, &st);
    flash[3] ^= 0x00000010u;                              /* corrupt the old FAULT record */
    fl_scan(&ops, &boot);
    check_eq("FL-10 old damage before a valid clear -> not latched", boot.latched, 0);

    /* FL-11: a flash programming error is reported and leaves the board latched */
    erase(); fl_scan(&ops, &st);
    fail_on_call = 3;
    check_eq("FL-11 programming error reported", fl_append_fault(&ops, &st, BMS_FAULT_OVERTEMP, &s), -1);
    fail_on_call = -1;
    fl_scan(&ops, &boot);
    check_eq("FL-11 half-written fault -> latched", boot.latched, 1);

    /* FL-12: when the log is full, appending reports it and nothing is overwritten */
    erase(); fl_scan(&ops, &st);
    bad = 0;
    for (n = 0; n < SIM_RECS; n++) if (fl_append_clear(&ops, &st) != 0) bad++;
    check_eq("FL-12 filled all slots without error (errors)", bad, 0);
    check_eq("FL-12 append to a full log returns -2", fl_append_fault(&ops, &st, BMS_FAULT_OVERTEMP, &s), -2);
    fl_scan(&ops, &boot);
    check_eq("FL-12 full log: next slot equals capacity", (long)boot.next_slot, (long)SIM_RECS);
    check_eq("FL-12 full log: all records still valid", (long)boot.valid, (long)SIM_RECS);

    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}