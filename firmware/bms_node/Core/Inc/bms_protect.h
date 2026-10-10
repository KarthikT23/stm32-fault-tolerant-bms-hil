#pragma once
#include <stdint.h>
#include "bms_frame.h"

typedef enum {
    BMS_ST_INIT = 0,
    BMS_ST_NORMAL,
    BMS_ST_WARNING,
    BMS_ST_FAULT
} BmsState_t;

typedef enum {
    BMS_FAULT_NONE = 0,
    BMS_FAULT_OVERVOLT,      /* 1 */
    BMS_FAULT_UNDERVOLT,     /* 2 */
    BMS_FAULT_OVERTEMP,      /* 3 */
    BMS_FAULT_OVERCURRENT    /* 4 */
} BmsFault_t;

/* Thresholds, see docs/requirements.md (temperatures in 0.1 deg C, current in mA) */
#define BMS_CELL_WARN_HIGH_MV    4200
#define BMS_CELL_FAULT_HIGH_MV   4250
#define BMS_CELL_WARN_LOW_MV     3100
#define BMS_CELL_FAULT_LOW_MV    2900
#define BMS_TEMP_WARN_DC          500
#define BMS_TEMP_FAULT_DC         600
#define BMS_CURRENT_FAULT_MA    10000

typedef struct {
    BmsState_t state;
    BmsFault_t cause;
} BmsProtect_t;

void bms_protect_init(BmsProtect_t *p);
void bms_protect_update(BmsProtect_t *p, const BmsSample_t *s);
