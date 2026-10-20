/*
 * app_uart_rx.c
 *
 *  Created on: Oct 10, 2026
 *      Author: Karthik
 */
#include "app_uart_rx.h"
#include "bms_frame.h"
#include "bms_protect.h"
#include "main.h"
#include "FreeRTOS.h"
#include "stream_buffer.h"
#include <stdio.h>

extern UART_HandleTypeDef huart2;

#define DMA_BUF_SZ 64

static uint8_t rx_dma_buf[DMA_BUF_SZ];
static StreamBufferHandle_t rx_stream;
static uint16_t old_pos;

static const char *const st_name[]    = { "INIT", "NORMAL", "WARNING", "FAULT" };
static const char *const cause_name[] = { "NONE", "OVERVOLT", "UNDERVOLT", "OVERTEMP", "OVERCURRENT" };

/* LD4 green = NORMAL, LD3 orange = WARNING, LD5 red = FAULT */
static void leds_show(BmsState_t st)
{
    HAL_GPIO_WritePin(LD4_GPIO_Port, LD4_Pin, (st == BMS_ST_NORMAL)  ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, (st == BMS_ST_WARNING) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LD5_GPIO_Port, LD5_Pin, (st == BMS_ST_FAULT)   ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* ISR context: runs on UART idle line and on DMA half/full transfer.
   It only copies the new bytes into the stream buffer, so it stays short. */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t pos)
{
    (void)pos;                                   /* use the live DMA position instead */
    if (huart->Instance != USART2) return;

    uint16_t head = DMA_BUF_SZ - (uint16_t)__HAL_DMA_GET_COUNTER(huart->hdmarx);
    if (head >= DMA_BUF_SZ) head = 0;            /* counter just reloaded: wrapped to 0 */

    BaseType_t woken = pdFALSE;
    if (head > old_pos) {
        xStreamBufferSendFromISR(rx_stream, &rx_dma_buf[old_pos], head - old_pos, &woken);
    } else if (head < old_pos) {                 /* DMA wrapped around */
        xStreamBufferSendFromISR(rx_stream, &rx_dma_buf[old_pos], DMA_BUF_SZ - old_pos, &woken);
        if (head) xStreamBufferSendFromISR(rx_stream, &rx_dma_buf[0], head, &woken);
    }
    old_pos = head;                              /* head == old_pos: nothing new, send nothing */
    portYIELD_FROM_ISR(woken);
}

int _write(int fd, char *ptr, int len)              /* printf goes out USART2 */
{
    (void)fd;
    HAL_UART_Transmit(&huart2, (uint8_t *)ptr, len, 100);
    return len;
}

void App_ParserTaskRun(void)
{
    BmsParser_t parser;
    BmsSample_t s;
    BmsProtect_t prot;
    BmsState_t   last_state = BMS_ST_INIT;
    bms_parser_init(&parser);
    bms_protect_init(&prot);

    rx_stream = xStreamBufferCreate(256, 1);
    HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_dma_buf, DMA_BUF_SZ);

    for (;;) {
        uint8_t b;
        if (xStreamBufferReceive(rx_stream, &b, 1, portMAX_DELAY) != 1) continue;
        if (!bms_parser_feed(&parser, b, &s)) continue;

        bms_protect_update(&prot, &s);

        if (prot.state != last_state) {              /* log only state changes */
            last_state = prot.state;
            leds_show(prot.state);
            printf("STATE -> %s cause=%s seq=%u\r\n",
                   st_name[prot.state], cause_name[prot.cause], s.seq);
        }
#ifdef BMS_DEBOUNCE_FRAMES
        if (prot.pending_count > 0u) { /* a frame is asking for a state change but it is not confirmed yet */
            printf("PENDING -> %s count=%u/%u seq=%u\r\n",
                   st_name[prot.pending_state], prot.pending_count,
                   BMS_DEBOUNCE_FRAMES, s.seq);
        }
#endif

        if ((parser.frames_ok % 10u) == 0u) {        /* 1 line per second */
            printf("RX seq=%u V=%u,%u,%u,%u I=%d T=%d,%d ok=%lu crc_err=%lu\r\n",
                   s.seq, s.cell_mv[0], s.cell_mv[1], s.cell_mv[2], s.cell_mv[3],
                   s.current_ma, s.temp_dc[0], s.temp_dc[1],
                   (unsigned long)parser.frames_ok, (unsigned long)parser.crc_err);
        }
    }
}

