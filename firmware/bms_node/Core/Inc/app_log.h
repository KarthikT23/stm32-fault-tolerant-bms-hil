/*
 * app_log.h
 *
 *  Created on: Oct 20, 2026
 *      Author: Karthik
 */

#ifndef INC_APP_LOG_H_
#define INC_APP_LOG_H_

#pragma once
#include <stdint.h>

void     App_LogInit(void);               /* call before the scheduler starts */
void     App_LogTask(void *argument);     /* logger thread body, never returns */
void     app_log(const char *fmt, ...);   /* callable from any task, never blocks */
uint32_t App_LogDropped(void);

#endif /* INC_APP_LOG_H_ */
