/*
 * app_faultlog.h
 *
 *  Created on: Oct 20, 2026
 *      Author: Karthik
 */

#ifndef INC_APP_FAULTLOG_H_
#define INC_APP_FAULTLOG_H_

#include <stdint.h>
#include "bms_frame.h"
#include "bms_protect.h"
#include "app_wdg.h"

void       App_FaultLogInit(void);          /* scan the log; call after App_LogInit(), before the scheduler */
uint8_t    App_FaultLogLatched(void);       /* 1 = boot in FAULT */
uint8_t    App_FaultLogSuspect(void);       /* 1 = latched because of a damaged record */
BmsFault_t App_FaultLogCause(void);
uint8_t    App_FaultLogSnapSeq(void);       /* frame seq stored with the last fault */
void       App_FaultLogRecordFault(BmsFault_t cause, const BmsSample_t *s);
int        App_FaultLogClear(void);         /* 0 = clear record stored */
#if SAFECELL_FAULT_INJECT
void       App_FaultLogTorn(void);          /* test hook: half-written CLEAR */
#endif

#endif /* INC_APP_FAULTLOG_H_ */

