#include "bms_protect.h"

static void set_first(BmsFault_t *dst, BmsFault_t f)
{
    if (*dst == BMS_FAULT_NONE) *dst = f;
}

/* Classify one frame: 0 = normal, 1 = warning, 2 = fault.
   warn_hold != 0 means the state is currently WARNING, so the warning
   thresholds are tightened by the hysteresis margin before it may clear. */
static int classify(const BmsSample_t *s, int warn_hold, BmsFault_t *cause)
{
    int level = 0;
    int i;
    int32_t cur;
    int hi = BMS_CELL_WARN_HIGH_MV - (warn_hold ? BMS_CELL_HYST_MV : 0);
    int lo = BMS_CELL_WARN_LOW_MV  + (warn_hold ? BMS_CELL_HYST_MV : 0);
    int wt = BMS_TEMP_WARN_DC      - (warn_hold ? BMS_TEMP_HYST_DC : 0);

    *cause = BMS_FAULT_NONE;

    for (i = 0; i < 4; i++) {
        if (s->cell_mv[i] >= BMS_CELL_FAULT_HIGH_MV) {
            level = 2; set_first(cause, BMS_FAULT_OVERVOLT);
        } else if (s->cell_mv[i] <= BMS_CELL_FAULT_LOW_MV) {
            level = 2; set_first(cause, BMS_FAULT_UNDERVOLT);
        } else if (s->cell_mv[i] >= hi || s->cell_mv[i] <= lo) {
            if (level < 1) level = 1;
        }
    }

    for (i = 0; i < 2; i++) {
        if (s->temp_dc[i] >= BMS_TEMP_FAULT_DC) {
            level = 2; set_first(cause, BMS_FAULT_OVERTEMP);
        } else if (s->temp_dc[i] >= wt) {
            if (level < 1) level = 1;
        }
    }

    cur = s->current_ma;
    if (cur < 0) cur = -cur;
    if (cur > BMS_CURRENT_FAULT_MA) { level = 2; set_first(cause, BMS_FAULT_OVERCURRENT); }

    return level;
}

void bms_protect_init(BmsProtect_t *p)
{
    p->state = BMS_ST_INIT;
    p->cause = BMS_FAULT_NONE;
    p->pending_state = BMS_ST_NORMAL;
    p->pending_count = 0;
}

/* v2: a state change needs BMS_DEBOUNCE_FRAMES consecutive frames asking for it. */
void bms_protect_update(BmsProtect_t *p, const BmsSample_t *s)
{
    BmsFault_t cause;
    BmsState_t want;
    int level;

    if (p->state == BMS_ST_FAULT) return;            /* FAULT is latched */
    if (p->state == BMS_ST_INIT) p->state = BMS_ST_NORMAL;   /* INIT lasts one frame */

    level = classify(s, p->state == BMS_ST_WARNING, &cause);
    want  = (level == 2) ? BMS_ST_FAULT : (level == 1) ? BMS_ST_WARNING : BMS_ST_NORMAL;

    if (want == p->state) {                          /* nothing asks for a change */
        p->pending_state = want;
        p->pending_count = 0;
        return;
    }

    if (want == p->pending_state) {
        if (p->pending_count < 255) p->pending_count++;
    } else {
        p->pending_state = want;
        p->pending_count = 1;
    }

    if (p->pending_count >= BMS_DEBOUNCE_FRAMES) {
        p->state = want;
        p->cause = (want == BMS_ST_FAULT) ? cause : BMS_FAULT_NONE;
        p->pending_count = 0;
    }
}
