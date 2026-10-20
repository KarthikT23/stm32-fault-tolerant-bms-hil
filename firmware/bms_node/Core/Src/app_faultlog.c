/*
 * app_faultlog.c
 *
 *  Created on: Oct 20, 2026
 *      Author: Karthik
 */
#include "app_faultlog.h"
#include "bms_faultlog.h"
#include "app_log.h"
#include "main.h"

#define FL_BASE_ADDR  0x080E0000u            /* sector 11, last 128 KB of the 1 MB flash */
#define FL_AREA_SIZE  (128u * 1024u)

/* Only the parser task touches these after init, so no locking is needed. */
static FlState_t st;

static uint32_t hw_read32(uint32_t addr)
{
    return *(volatile const uint32_t *)addr;
}

/* One word at a time, unlocked only for the duration of the write. */
static int hw_prog32(uint32_t addr, uint32_t val)
{
    int rc;
    HAL_FLASH_Unlock();
    rc = (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, val) == HAL_OK) ? 0 : -1;
    HAL_FLASH_Lock();
    return rc;
}

static const FlOps_t ops = { hw_read32, hw_prog32, FL_BASE_ADDR, FL_AREA_SIZE };

void App_FaultLogInit(void)
{
    fl_scan(&ops, &st);
    app_log("FLASH: log valid=%lu damaged=%lu next=%lu latched=%u suspect=%u\r\n",
            (unsigned long)st.valid, (unsigned long)st.damaged, (unsigned long)st.next_slot,
            (unsigned)st.latched, (unsigned)st.suspect);
}

uint8_t    App_FaultLogLatched(void)  { return st.latched; }
uint8_t    App_FaultLogSuspect(void)  { return st.suspect; }
BmsFault_t App_FaultLogCause(void)    { return st.cause; }
uint8_t    App_FaultLogSnapSeq(void)  { return st.snap.seq; }

void App_FaultLogRecordFault(BmsFault_t cause, const BmsSample_t *s)
{
    int rc = fl_append_fault(&ops, &st, cause, s);
    if (rc == 0) app_log("FLASH: fault record #%lu stored\r\n", (unsigned long)st.last_seq);
    else         app_log("FLASH: store failed rc=%d\r\n", rc);
}

int App_FaultLogClear(void)
{
    int rc = fl_append_clear(&ops, &st);
    if (rc == 0) app_log("CLEAR: record #%lu stored, FAULT released\r\n", (unsigned long)st.last_seq);
    else         app_log("CLEAR: flash write failed rc=%d, FAULT kept\r\n", rc);
    return rc;
}

#if SAFECELL_FAULT_INJECT
void App_FaultLogTorn(void)
{
    (void)fl_append_partial(&ops, &st, FL_TYPE_CLEAR, 5u);   /* 5 of 8 words, no commit marker */
}
#endif

