/*
 * app_wdg.c
 *
 *  Created on: Oct 20, 2026
 *      Author: Karthik
 */
#include "app_wdg.h"
#include "app_log.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

#define WD_ALL             (WD_PARSER | WD_LOG | WD_HEARTBEAT)
#define SUPERVISE_MS       100   /* how often the supervisor looks */
#define STALL_WARN_ROUNDS  5     /* 500 ms without every task checking in: report who is missing */

static volatile uint32_t wd_bits;

void wd_checkin(uint32_t mask)
{
    taskENTER_CRITICAL();
    wd_bits |= mask;
    taskEXIT_CRITICAL();
}

/* RCC_CSR reset flags (RM0090): 29 IWDG, 30 WWDG, 28 software, 27 power-on, 26 pin, 25 brown-out */
static const char *reset_cause(void)
{
    uint32_t csr = RCC->CSR;
    RCC->CSR |= (1u << 24);                 /* RMVF: clear the flags for the next boot */
    if (csr & (1u << 29)) return "IWDG";
    if (csr & (1u << 30)) return "WWDG";
    if (csr & (1u << 28)) return "SOFTWARE";
    if (csr & (1u << 27)) return "POWER_ON";
    if (csr & (1u << 25)) return "BROWN_OUT";
    if (csr & (1u << 26)) return "PIN";
    return "UNKNOWN";
}

void App_WdgInit(void)
{
    app_log("BOOT reset_cause=%s\r\n", reset_cause());
}

static void iwdg_start(void)
{
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;   /* do not reset while a debugger halts the core */
    IWDG->KR  = 0xCCCC;                /* start (this also starts the LSI oscillator) */
    IWDG->KR  = 0x5555;                /* unlock PR and RLR */
    IWDG->PR  = 3;                     /* prescaler /32: LSI (about 32 kHz) -> about 1 kHz */
    IWDG->RLR = 1000;                  /* about 1 s nominal; the LSI tolerance gives 0.7 to 1.9 s */
    while (IWDG->SR != 0u) { }         /* wait until the registers are updated */
    IWDG->KR  = 0xAAAA;                /* load the counter */
}

void App_WdgTask(void *argument)
{
    uint32_t stalled = 0;
    (void)argument;

    vTaskDelay(pdMS_TO_TICKS(500));    /* let every supervised task start and check in first */
    iwdg_start();
    app_log("WDG: armed, timeout about 1 s, supervising mask 0x%02x\r\n", (unsigned)WD_ALL);

    for (;;) {
        uint32_t bits;
        vTaskDelay(pdMS_TO_TICKS(SUPERVISE_MS));

        taskENTER_CRITICAL();
        bits = wd_bits;
        if ((bits & WD_ALL) == WD_ALL) wd_bits = 0;
        taskEXIT_CRITICAL();

        if ((bits & WD_ALL) == WD_ALL) {
            IWDG->KR = 0xAAAA;         /* every supervised task is alive: feed the watchdog */
            stalled = 0;
        } else if (++stalled == STALL_WARN_ROUNDS) {
            app_log("WDG: missing=0x%02lx (1=parser 2=log 4=heartbeat), reset in about 1 s\r\n",
                    (unsigned long)(WD_ALL & ~bits));
        }
    }
}

