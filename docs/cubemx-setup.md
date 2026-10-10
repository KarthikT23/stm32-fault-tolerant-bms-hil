# CubeMX configuration and rebuild steps

The generated vendor code (`Drivers/`, `Middlewares/`) is not stored in this repo. It is
regenerated from `firmware/bms_node/bms_node.ioc`. This page records every setting used.

## 1. Rebuild the project from a fresh clone

1. Install STM32CubeIDE.
2. File > Import > General > Existing Projects into Workspace, select `firmware/bms_node`,
   untick "Copy projects into workspace".
3. Open `bms_node.ioc` and press Ctrl+S (Generate Code). This recreates `Drivers/` and
   `Middlewares/` and downloads the STM32Cube F4 package if it is missing. Code between the
   USER CODE markers is preserved.
4. Build (hammer icon). Expect 0 errors.

## 2. Settings

Board: STM32F407G-DISC1 (STM32F407VGT6), board default pin set, peripherals not initialised
by default.

| Area | Setting |
|---|---|
| SYS | Debug = Serial Wire, Timebase Source = TIM6 (FreeRTOS owns SysTick) |
| RCC | HSE = crystal (8 MHz) |
| Clock tree | PLL source HSE, M = 8, N = 336, P = 2, Q = 7, so SYSCLK = HCLK = 168 MHz; APB1 /4 = 42 MHz, APB2 /2 = 84 MHz |
| USART2 | Asynchronous, 115200 8N1, PA2 = TX, PA3 = RX |
| DMA | DMA1 Stream 5 = USART2_RX, peripheral to memory, Circular, byte/byte, increment on memory side only |
| LEDs | LD4 green PD12, LD3 orange PD13, LD5 red PD14, LD6 blue PD15 (board labels kept) |
| FreeRTOS | CMSIS_V2, heap_4, TOTAL_HEAP_SIZE = 32768, tick 1 kHz, USE_NEWLIB_REENTRANT = 1 |
| Tasks | defaultTask: Normal, 128 words. ParserTask: AboveNormal, 1024 words, entry StartParserTask |
| Code generation | Keep User Code when re-generating = on. Peripheral init stays in main.c |

## 3. Interrupt priorities

| Interrupt | Preemption priority | Why |
|---|---|---|
| DMA1 Stream 5 | 5 | calls FreeRTOS `...FromISR` functions, so it must be 5 or numerically higher |
| USART2 global | 5 | same reason |
| TIM6 (HAL tick) | 0 | calls no FreeRTOS function |
| PendSV, SysTick | 15 | RTOS context switch and tick must be the least urgent |

`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` = 5 and `configLIBRARY_LOWEST_INTERRUPT_PRIORITY` = 15
in `Core/Inc/FreeRTOSConfig.h`. CubeMX locks the PendSV and SysTick rows, so those two
are set in code (see below).

## 4. Edits made outside the CubeMX GUI

All of these sit inside USER CODE blocks of `Core/Src/main.c` so regeneration keeps them.

| Place | Edit | Purpose |
|---|---|---|
| Includes | `#include "app_uart_rx.h"` | access the receive task |
| USER CODE 2 | `HAL_NVIC_SetPriority` for PendSV and SysTick = 15 | explicit lowest priority |
| StartDefaultTask | toggle LD4, `osDelay(500)` | scheduler proof-of-life (temporary) |
| StartParserTask | `App_ParserTaskRun();` | start UART DMA receive and frame parser |

Own source files (not generated): `Core/Src/bms_frame.c`, `Core/Inc/bms_frame.h`
(HAL-free frame parser and CRC, unit-testable on a PC), `Core/Src/app_uart_rx.c`,
`Core/Inc/app_uart_rx.h` (UART DMA to stream buffer to parser task).
