/*
 * app_log.c
 *
 *  Created on: Oct 20, 2026
 *      Author: Karthik
 */
#include "app_log.h"
#include "main.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "app_wdg.h"
#include <stdarg.h>
#include <stdio.h>

extern UART_HandleTypeDef huart2;

#define LOG_MSG_MAX    96
#define LOG_QUEUE_LEN  32

typedef struct {
    uint8_t len;
    char    text[LOG_MSG_MAX];
} LogMsg_t;

static QueueHandle_t   log_q;
static volatile uint32_t dropped;

void App_LogInit(void)
{
    log_q = xQueueCreate(LOG_QUEUE_LEN, sizeof(LogMsg_t));
}

uint32_t App_LogDropped(void) { return dropped; }

/* Formats into a local buffer and copies it into the queue. If the queue is full
   the line is dropped and counted, so the caller is never delayed. */
void app_log(const char *fmt, ...)
{
    LogMsg_t m;
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(m.text, sizeof(m.text), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof(m.text)) n = (int)sizeof(m.text) - 1;
    m.len = (uint8_t)n;

    if (xQueueSend(log_q, &m, 0) != pdPASS) dropped++;
}

void App_LogTask(void *argument)
{
    LogMsg_t m;
    uint32_t reported = 0;
    char note[32];
    (void)argument;

    for (;;) {
        wd_checkin(WD_LOG);
        if (xQueueReceive(log_q, &m, pdMS_TO_TICKS(100)) != pdPASS) continue;
        HAL_UART_Transmit(&huart2, (uint8_t *)m.text, m.len, 100);

        uint32_t d = dropped;
        if (d != reported) {                     /* tell the reader that lines were lost */
            int n = snprintf(note, sizeof(note), "LOG dropped=%lu\r\n", (unsigned long)d);
            HAL_UART_Transmit(&huart2, (uint8_t *)note, (uint16_t)n, 100);
            reported = d;
        }
    }
}

