/*
 * app_wdg.h
 *
 *  Created on: Oct 20, 2026
 *      Author: Karthik
 */

#ifndef INC_APP_WDG_H_
#define INC_APP_WDG_H_

#pragma once
#include <stdint.h>

/* 1 = test hooks compiled in (the "!HANG" command); set to 0 for a release build */
#define SAFECELL_FAULT_INJECT  1

/* One bit per supervised task */
#define WD_PARSER     (1u << 0)
#define WD_LOG        (1u << 1)
#define WD_HEARTBEAT  (1u << 2)

void App_WdgInit(void);               /* logs the reset cause; call after App_LogInit() */
void App_WdgTask(void *argument);     /* supervisor thread body, never returns */
void wd_checkin(uint32_t mask);       /* each supervised task calls this regularly */


#endif /* INC_APP_WDG_H_ */
