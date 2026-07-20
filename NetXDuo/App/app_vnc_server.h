#ifndef APP_VNC_SERVER_H
#define APP_VNC_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "nx_api.h"
#include "tx_api.h"

#ifndef APP_VNC_SERVER_PORT
#define APP_VNC_SERVER_PORT             5900U
#endif

UINT App_VNC_Server_Init(TX_BYTE_POOL *byte_pool,
                         NX_IP *ip,
                         NX_PACKET_POOL *packet_pool);

#ifdef __cplusplus
}
#endif

#endif /* APP_VNC_SERVER_H */
