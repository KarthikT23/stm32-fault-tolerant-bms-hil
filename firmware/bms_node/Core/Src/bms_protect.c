#include "bms_protect.h"

static void set_first(BmsFault_t *dst, BmsFault_t f)
{
    if (*dst == BMS_FAULT_NONE) *dst = f;
}

void bms_protect_init(BmsProtect_t *p)
{
    p->state = BMS_ST_INIT;
    p->cause = BMS_FAULT_NONE;
}

/* v1 baseline: every frame is judged on its own, with no filtering. */
void bms_protect_update(BmsProtect_t *p, const BmsSample_t *s)
{
    BmsFault_t fault = BMS_FAULT_NONE;
    int warn = 0;
    int i;
    int32_t cur;

    if (p->state == BMS_ST_FAULT) return;            /* FAULT is latched */

    for (i = 0; i < 4; i++) {
        if (s->cell_mv[i] >= BMS_CELL_FAULT_HIGH_MV) {
            set_first(&fault, BMS_FAULT_OVERVOLT);
        } else if (s->cell_mv[i] <= BMS_CELL_FAULT_LOW_MV) {
            set_first(&fault, BMS_FAULT_UNDERVOLT);
        } else if (s->cell_mv[i] >= BMS_CELL_WARN_HIGH_MV ||
                   s->cell_mv[i] <= BMS_CELL_WARN_LOW_MV) {
            warn = 1;
        }
    }

    for (i = 0; i < 2; i++) {
        if (s->temp_dc[i] >= BMS_TEMP_FAULT_DC) {
            set_first(&fault, BMS_FAULT_OVERTEMP);
        } else if (s->temp_dc[i] >= BMS_TEMP_WARN_DC) {
            warn = 1;
        }
    }

    cur = s->current_ma;
    if (cur < 0) cur = -cur;
    if (cur > BMS_CURRENT_FAULT_MA) set_first(&fault, BMS_FAULT_OVERCURRENT);

    if (fault != BMS_FAULT_NONE) {
        p->state = BMS_ST_FAULT;
        p->cause = fault;
    } else {
        p->state = warn ? BMS_ST_WARNING : BMS_ST_NORMAL;
        p->cause = BMS_FAULT_NONE;
    }
}
