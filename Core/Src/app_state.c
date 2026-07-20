#include "app_state.h"

#include "main.h"
#include <string.h>

typedef struct
{
  APP_STATE_SNAPSHOT public_state;
  uint32_t usb_last_rx_ms;
  uint32_t usb_last_tx_ms;
  uint32_t lan_last_rx_ms;
  uint32_t lan_last_tx_ms;
} APP_STATE_STORAGE;

static APP_STATE_STORAGE state;

static uint32_t state_lock(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static void state_unlock(uint32_t primask)
{
  if (primask == 0U)
  {
    __enable_irq();
  }
}

static void increment_saturated(uint32_t *value)
{
  if (*value != UINT32_MAX)
  {
    (*value)++;
  }
}

static void put_u32_le(uint8_t *destination, uint32_t value)
{
  destination[0] = (uint8_t)value;
  destination[1] = (uint8_t)(value >> 8);
  destination[2] = (uint8_t)(value >> 16);
  destination[3] = (uint8_t)(value >> 24);
}

void App_State_Init(void)
{
  uint32_t primask = state_lock();
  (void)memset(&state, 0, sizeof(state));
  state_unlock(primask);
}

void App_State_Get(APP_STATE_SNAPSHOT *snapshot)
{
  uint32_t primask;

  if (snapshot == NULL)
  {
    return;
  }

  primask = state_lock();
  (void)memcpy(snapshot, &state.public_state, sizeof(*snapshot));
  state_unlock(primask);
}

void App_State_ServiceTimeouts(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t primask = state_lock();

  state.public_state.usb_cable_connected =
      ((HAL_GPIO_ReadPin(USB_FS_VBUS_MONITOR_GPIO_Port,
                        USB_FS_VBUS_MONITOR_Pin) == GPIO_PIN_SET) ||
       (state.public_state.usb_configured != 0U)) ? 1U : 0U;
  state.public_state.usb_app_active =
      ((now - state.usb_last_rx_ms) <= APP_USB_ACTIVITY_TIMEOUT_MS) &&
      (state.public_state.usb_rx_packets != 0U);
  state.public_state.usb_rx_active =
      ((now - state.usb_last_rx_ms) <= APP_IO_FLASH_TIME_MS) &&
      (state.public_state.usb_rx_packets != 0U);
  state.public_state.usb_tx_active =
      ((now - state.usb_last_tx_ms) <= APP_IO_FLASH_TIME_MS) &&
      (state.public_state.usb_tx_packets != 0U);
  state.public_state.lan_rx_active =
      ((now - state.lan_last_rx_ms) <= APP_IO_FLASH_TIME_MS) &&
      (state.public_state.lan_rx_packets != 0U);
  state.public_state.lan_tx_active =
      ((now - state.lan_last_tx_ms) <= APP_IO_FLASH_TIME_MS) &&
      (state.public_state.lan_tx_packets != 0U);

  state_unlock(primask);
}

void App_State_SetSensor(int32_t temperature, uint32_t pressure, uint32_t humidity)
{
  uint32_t primask = state_lock();
  state.public_state.temperature = temperature;
  state.public_state.pressure = pressure;
  state.public_state.humidity = humidity;
  state_unlock(primask);
}

void App_State_SetControls(const uint8_t controls[4])
{
  uint32_t primask;
  if (controls == NULL)
  {
    return;
  }
  primask = state_lock();
  (void)memcpy(state.public_state.controls, controls, 4U);
  state_unlock(primask);
}

void App_State_GetControls(uint8_t controls[4])
{
  uint32_t primask;
  if (controls == NULL)
  {
    return;
  }
  primask = state_lock();
  (void)memcpy(controls, state.public_state.controls, 4U);
  state_unlock(primask);
}

void App_State_SetUsbCable(uint8_t connected)
{
  uint32_t primask = state_lock();
  state.public_state.usb_cable_connected = (connected != 0U);
  if (connected == 0U)
  {
    state.public_state.usb_configured = 0U;
    state.public_state.usb_app_active = 0U;
    state.public_state.usb_rx_active = 0U;
    state.public_state.usb_tx_active = 0U;
  }
  state_unlock(primask);
}

void App_State_SetUsbConfigured(uint8_t configured)
{
  uint32_t primask = state_lock();
  state.public_state.usb_configured = (configured != 0U);
  if (configured == 0U)
  {
    state.public_state.usb_app_active = 0U;
    state.public_state.usb_rx_active = 0U;
    state.public_state.usb_tx_active = 0U;
  }
  state_unlock(primask);
}

void App_State_SetLan(uint8_t link_up, uint8_t address_ready, uint32_t ipv4_address)
{
  uint32_t primask = state_lock();
  state.public_state.lan_link_up = (link_up != 0U);
  state.public_state.lan_address_ready = (address_ready != 0U);
  state.public_state.ipv4_address = ipv4_address;
  if (link_up == 0U)
  {
    state.public_state.lan_client_active = 0U;
  }
  state_unlock(primask);
}

void App_State_SetLanClient(uint8_t active)
{
  uint32_t primask = state_lock();
  state.public_state.lan_client_active = (active != 0U);
  state_unlock(primask);
}

void App_State_NoteUsbRx(void)
{
  uint32_t primask = state_lock();
  increment_saturated(&state.public_state.usb_rx_packets);
  state.usb_last_rx_ms = HAL_GetTick();
  state.public_state.usb_app_active = 1U;
  state.public_state.usb_rx_active = 1U;
  state_unlock(primask);
}

void App_State_NoteUsbTx(void)
{
  uint32_t primask = state_lock();
  increment_saturated(&state.public_state.usb_tx_packets);
  state.usb_last_tx_ms = HAL_GetTick();
  state.public_state.usb_tx_active = 1U;
  state_unlock(primask);
}

void App_State_NoteLanRx(void)
{
  uint32_t primask = state_lock();
  increment_saturated(&state.public_state.lan_rx_packets);
  state.lan_last_rx_ms = HAL_GetTick();
  state.public_state.lan_rx_active = 1U;
  state_unlock(primask);
}

void App_State_NoteLanTx(void)
{
  uint32_t primask = state_lock();
  increment_saturated(&state.public_state.lan_tx_packets);
  state.lan_last_tx_ms = HAL_GetTick();
  state.public_state.lan_tx_active = 1U;
  state_unlock(primask);
}

void App_State_ResetCounters(void)
{
  uint32_t primask = state_lock();
  state.public_state.usb_rx_packets = 0U;
  state.public_state.usb_tx_packets = 0U;
  state.public_state.lan_rx_packets = 0U;
  state.public_state.lan_tx_packets = 0U;
  state.public_state.usb_rx_active = 0U;
  state.public_state.usb_tx_active = 0U;
  state.public_state.lan_rx_active = 0U;
  state.public_state.lan_tx_active = 0U;
  state_unlock(primask);
}

void App_State_BuildResponse(uint8_t packet[APP_PACKET_SIZE])
{
  APP_STATE_SNAPSHOT snapshot;
  uint8_t flags = 0U;

  if (packet == NULL)
  {
    return;
  }

  App_State_Get(&snapshot);
  (void)memset(packet, 0, APP_PACKET_SIZE);

  /* Bytes 5..16 retain the working F4/WinUSB BME280 wire format. */
  put_u32_le(&packet[5], (uint32_t)snapshot.temperature);
  put_u32_le(&packet[9], snapshot.pressure);
  put_u32_le(&packet[13], snapshot.humidity);

  packet[20] = 'F';
  packet[21] = '7';
  packet[22] = '4';
  packet[23] = '6';
  packet[24] = 1U;
  flags |= snapshot.usb_configured ? 0x01U : 0U;
  flags |= snapshot.usb_app_active ? 0x02U : 0U;
  flags |= snapshot.lan_link_up ? 0x04U : 0U;
  flags |= snapshot.lan_address_ready ? 0x08U : 0U;
  flags |= snapshot.lan_client_active ? 0x10U : 0U;
  flags |= snapshot.usb_cable_connected ? 0x20U : 0U;
  flags |= (snapshot.usb_rx_active || snapshot.usb_tx_active) ? 0x40U : 0U;
  flags |= (snapshot.lan_rx_active || snapshot.lan_tx_active) ? 0x80U : 0U;
  packet[25] = flags;
  put_u32_le(&packet[28], snapshot.usb_rx_packets);
  put_u32_le(&packet[32], snapshot.usb_tx_packets);
  put_u32_le(&packet[36], snapshot.lan_rx_packets);
  put_u32_le(&packet[40], snapshot.lan_tx_packets);
  put_u32_le(&packet[44], snapshot.ipv4_address);
  (void)memcpy(&packet[48], snapshot.controls, 4U);
}
