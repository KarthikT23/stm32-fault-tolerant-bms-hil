/* Host-side scenario tests for the protection logic (no HAL, no board needed). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "bms_protect.h"

static int failures = 0;

static const char *state_name(int st)
{
    switch (st) {
    case BMS_ST_INIT:    return "INIT";
    case BMS_ST_NORMAL:  return "NORMAL";
    case BMS_ST_WARNING: return "WARNING";
    case BMS_ST_FAULT:   return "FAULT";
    default:             return "?";
    }
}

static void check_eq(const char *name, long got, long want)
{
    int ok = (got == want);
    printf("%s  %s", ok ? "PASS" : "FAIL", name);
    if (!ok) printf("  (got %ld, expected %ld)", got, want);
    printf("\n");
    if (!ok) failures++;
}

static void check_state(const char *name, int got, int want)
{
    int ok = (got == want);
    printf("%s  %s", ok ? "PASS" : "FAIL", name);
    if (!ok) printf("  (got %s, expected %s)", state_name(got), state_name(want));
    printf("\n");
    if (!ok) failures++;
}

static void check_range(const char *name, long got, long lo, long hi)
{
    int ok = (got >= lo && got <= hi);
    printf("%s  %s  (%ld, allowed %ld..%ld)\n", ok ? "PASS" : "FAIL", name, got, lo, hi);
    if (!ok) failures++;
}

/* Deterministic pseudo-noise so every run gives the same result. */
static uint32_t rng_state = 12345u;
static int noise(int amp)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return (int)((rng_state >> 16) % (uint32_t)(2 * amp + 1)) - amp;
}

static BmsSample_t normal_sample(void)
{
    BmsSample_t s;
    int i;
    memset(&s, 0, sizeof(s));
    for (i = 0; i < 4; i++) s.cell_mv[i] = (uint16_t)(3800 + noise(5));
    s.current_ma = (int16_t)(-1500 + noise(50));
    s.temp_dc[0] = (int16_t)(280 + noise(3));
    s.temp_dc[1] = (int16_t)(281 + noise(3));
    return s;
}

static void start_normal(BmsProtect_t *p)
{
    BmsSample_t s = normal_sample();
    bms_protect_init(p);
    bms_protect_update(p, &s);
}

static void feed_normal(BmsProtect_t *p, int count)
{
    int i;
    for (i = 0; i < count; i++) {
        BmsSample_t s = normal_sample();
        bms_protect_update(p, &s);
    }
}

/* Feed the same sample repeatedly; returns frames until FAULT, 0 if never within max. */
static int frames_to_fault(BmsProtect_t *p, const BmsSample_t *s, int max)
{
    int i;
    for (i = 1; i <= max; i++) {
        bms_protect_update(p, s);
        if (p->state == BMS_ST_FAULT) return i;
    }
    return 0;
}

int main(void)
{
    BmsProtect_t p;
    BmsSample_t s;
    BmsState_t prev;
    int i, n, changes;

    /* P-01: start in INIT, first good frame gives NORMAL */
    bms_protect_init(&p);
    check_state("P-01 initial state is INIT", p.state, BMS_ST_INIT);
    s = normal_sample();
    bms_protect_update(&p, &s);
    check_state("P-01 first normal frame -> NORMAL", p.state, BMS_ST_NORMAL);

    /* P-02: noisy but healthy pack never leaves NORMAL */
    bms_protect_init(&p);
    n = 0;
    for (i = 0; i < 100; i++) {
        s = normal_sample();
        bms_protect_update(&p, &s);
        if (p.state != BMS_ST_NORMAL) n++;
    }
    check_eq("P-02 100 noisy healthy frames stay NORMAL (deviations)", n, 0);

    /* P-03..P-06: real, sustained faults are detected within 4 frames (400 ms at 10 Hz) */
    start_normal(&p);
    s = normal_sample(); s.temp_dc[0] = 650;
    n = frames_to_fault(&p, &s, 10);
    check_range("P-03 sustained over-temperature -> FAULT, frames", n, 1, 4);
    check_eq("P-03 cause is OVERTEMP", p.cause, BMS_FAULT_OVERTEMP);

    start_normal(&p);
    s = normal_sample(); s.cell_mv[2] = 4260;
    n = frames_to_fault(&p, &s, 10);
    check_range("P-04 sustained over-voltage -> FAULT, frames", n, 1, 4);
    check_eq("P-04 cause is OVERVOLT", p.cause, BMS_FAULT_OVERVOLT);

    start_normal(&p);
    s = normal_sample(); s.cell_mv[1] = 2850;
    n = frames_to_fault(&p, &s, 10);
    check_range("P-05 sustained under-voltage -> FAULT, frames", n, 1, 4);
    check_eq("P-05 cause is UNDERVOLT", p.cause, BMS_FAULT_UNDERVOLT);

    start_normal(&p);
    s = normal_sample(); s.current_ma = 12000;
    n = frames_to_fault(&p, &s, 10);
    check_range("P-06 sustained over-current -> FAULT, frames", n, 1, 4);
    check_eq("P-06 cause is OVERCURRENT", p.cause, BMS_FAULT_OVERCURRENT);

    /* P-07: FAULT is latched even after the values recover */
    start_normal(&p);
    s = normal_sample(); s.temp_dc[0] = 650;
    (void)frames_to_fault(&p, &s, 10);
    feed_normal(&p, 20);
    check_state("P-07 FAULT stays latched after values recover", p.state, BMS_ST_FAULT);

    /* P-08: one noisy temperature sample (65.0 C for a single frame) must not latch FAULT */
    start_normal(&p);
    s = normal_sample(); s.temp_dc[0] = 650;
    bms_protect_update(&p, &s);
    feed_normal(&p, 5);
    check_state("P-08 single-frame temperature spike is ignored", p.state, BMS_ST_NORMAL);

    /* P-09: one noisy voltage sample (4300 mV for a single frame) must not latch FAULT */
    start_normal(&p);
    s = normal_sample(); s.cell_mv[0] = 4300;
    bms_protect_update(&p, &s);
    feed_normal(&p, 5);
    check_state("P-09 single-frame voltage spike is ignored", p.state, BMS_ST_NORMAL);

    /* P-10: a value hovering around the warning threshold must not make the state chatter */
    start_normal(&p);
    prev = p.state;
    changes = 0;
    for (i = 0; i < 20; i++) {
        s = normal_sample();
        s.cell_mv[0] = (uint16_t)((i % 2) ? 4195 : 4205);
        bms_protect_update(&p, &s);
        if (p.state != prev) { changes++; prev = p.state; }
    }
    check_range("P-10 state changes while hovering at the warning threshold", changes, 0, 2);

    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
