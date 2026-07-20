#ifndef APP_STATE_H
#define APP_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define APP_PACKET_SIZE                 64U
#define APP_USB_ACTIVITY_TIMEOUT_MS   2000U
#define APP_IO_FLASH_TIME_MS           250U

typedef struct
{
  int32_t temperature;
  uint32_t pressure;
  uint32_t humidity;
  uint8_t controls[4];

  uint8_t usb_cable_connected;
  uint8_t usb_configured;
  uint8_t usb_app_active;
  uint8_t usb_rx_active;
  uint8_t usb_tx_active;

  uint8_t lan_link_up;
  uint8_t lan_address_ready;
  uint8_t lan_client_active;
  uint8_t lan_rx_active;
  uint8_t lan_tx_active;

  uint32_t usb_rx_packets;
  uint32_t usb_tx_packets;
  uint32_t lan_rx_packets;
  uint32_t lan_tx_packets;
  uint32_t ipv4_address;
} APP_STATE_SNAPSHOT;

void App_State_Init(void);
void App_State_Get(APP_STATE_SNAPSHOT *snapshot);
void App_State_ServiceTimeouts(void);
void App_State_SetSensor(int32_t temperature, uint32_t pressure, uint32_t humidity);
void App_State_SetControls(const uint8_t controls[4]);
void App_State_GetControls(uint8_t controls[4]);
void App_State_SetUsbCable(uint8_t connected);
void App_State_SetUsbConfigured(uint8_t configured);
void App_State_SetLan(uint8_t link_up, uint8_t address_ready, uint32_t ipv4_address);
void App_State_SetLanClient(uint8_t active);
void App_State_NoteUsbRx(void);
void App_State_NoteUsbTx(void);
void App_State_NoteLanRx(void);
void App_State_NoteLanTx(void);
void App_State_ResetCounters(void);
void App_State_BuildResponse(uint8_t packet[APP_PACKET_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* APP_STATE_H */
