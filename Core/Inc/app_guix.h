#ifndef APP_GUIX_H
#define APP_GUIX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "tx_api.h"
#include <stdint.h>

UINT App_GUIX_Init(TX_BYTE_POOL *byte_pool);
void App_GUIX_RemotePointerEvent(uint16_t x, uint16_t y, uint8_t buttons);
void App_GUIX_RemotePointerRelease(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_GUIX_H */
