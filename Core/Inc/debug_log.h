#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void Debug_Log_Init(void);
void Debug_Log_ThreadingInit(void);
void Debug_Log_Write(const char *text);
void Debug_Log_Line(const char *text);
void Debug_Log_U32(const char *label, uint32_t value);
void Debug_Log_I32(const char *label, int32_t value);
void Debug_Log_Hex(const char *label, uint32_t value);
void Debug_Log_IPv4(const char *label, uint32_t address);
void Debug_Log_SetStage(const char *stage);
const char *Debug_Log_GetStage(void);
void Debug_Log_Fault(const char *fault_name);

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_LOG_H */
