#ifndef UX_DEVICE_BULK_H
#define UX_DEVICE_BULK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ux_api.h"

#define USB_BULK_VENDOR_ID              0x0483U
#define USB_BULK_PRODUCT_ID             0x5752U
#define USB_BULK_OUT_ENDPOINT           0x01U
#define USB_BULK_IN_ENDPOINT            0x81U
#define USB_BULK_PACKET_SIZE            64U
#define USB_BULK_MS_VENDOR_CODE         0x20U

UINT App_USBX_Bulk_Class_Entry(UX_SLAVE_CLASS_COMMAND *command);
UINT App_USBX_Bulk_Microsoft_Request(ULONG request, ULONG value, ULONG index,
                                    ULONG length, UCHAR *data,
                                    ULONG *actual_length);
UINT App_USBX_Bulk_Thread_Create(TX_BYTE_POOL *byte_pool);
VOID App_USBX_Bulk_Force_Disconnect(VOID);

#ifdef __cplusplus
}
#endif

#endif /* UX_DEVICE_BULK_H */
