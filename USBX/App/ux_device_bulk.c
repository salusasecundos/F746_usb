#include "ux_device_bulk.h"

#include "app_state.h"
#include "ux_device_stack.h"
#include <string.h>

#define BULK_EVENT_CONFIGURED          0x01UL
#define BULK_THREAD_STACK_SIZE         2048U

typedef struct
{
  volatile UX_SLAVE_INTERFACE *interface_ptr;
  volatile UX_SLAVE_ENDPOINT *endpoint_in;
  volatile UX_SLAVE_ENDPOINT *endpoint_out;
  volatile uint8_t configured;
} APP_USBX_BULK_INSTANCE;

static APP_USBX_BULK_INSTANCE bulk_instance;
static TX_EVENT_FLAGS_GROUP bulk_events;
static TX_THREAD bulk_thread;
static UCHAR *bulk_thread_stack;

static const UCHAR microsoft_compat_id[40] =
{
  0x28U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U, 0x04U, 0x00U,
  0x01U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
  0x00U, 0x01U, 'W', 'I', 'N', 'U', 'S', 'B', 0x00U, 0x00U,
  0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
  0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U
};

static const UCHAR microsoft_extended_properties[142] =
{
  0x8EU, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U, 0x05U, 0x00U,
  0x01U, 0x00U,
  0x84U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U,
  0x28U, 0x00U,
  'D', 0x00U, 'e', 0x00U, 'v', 0x00U, 'i', 0x00U,
  'c', 0x00U, 'e', 0x00U, 'I', 0x00U, 'n', 0x00U,
  't', 0x00U, 'e', 0x00U, 'r', 0x00U, 'f', 0x00U,
  'a', 0x00U, 'c', 0x00U, 'e', 0x00U, 'G', 0x00U,
  'U', 0x00U, 'I', 0x00U, 'D', 0x00U, 0x00U, 0x00U,
  0x4EU, 0x00U, 0x00U, 0x00U,
  '{', 0x00U, '8', 0x00U, 'B', 0x00U, '4', 0x00U,
  'B', 0x00U, '6', 0x00U, 'B', 0x00U, '6', 0x00U,
  'A', 0x00U, '-', 0x00U, '3', 0x00U, '2', 0x00U,
  '6', 0x00U, '6', 0x00U, '-', 0x00U, '4', 0x00U,
  'A', 0x00U, '1', 0x00U, '8', 0x00U, '-', 0x00U,
  'A', 0x00U, 'C', 0x00U, '3', 0x00U, 'B', 0x00U,
  '-', 0x00U, '7', 0x00U, '1', 0x00U, '1', 0x00U,
  '0', 0x00U, 'B', 0x00U, '6', 0x00U, '0', 0x00U,
  '2', 0x00U, 'D', 0x00U, '0', 0x00U, 'A', 0x00U,
  '3', 0x00U, '}', 0x00U, 0x00U, 0x00U
};

static VOID bulk_thread_entry(ULONG argument)
{
  ULONG actual_flags;
  uint8_t controls[4];
  UX_SLAVE_ENDPOINT *endpoint_in;
  UX_SLAVE_ENDPOINT *endpoint_out;
  UX_SLAVE_TRANSFER *transfer_in;
  UX_SLAVE_TRANSFER *transfer_out;

  (void)argument;
  for (;;)
  {
    (void)tx_event_flags_get(&bulk_events, BULK_EVENT_CONFIGURED,
                             TX_OR_CLEAR, &actual_flags, TX_WAIT_FOREVER);

    while (bulk_instance.configured != 0U)
    {
      endpoint_in = (UX_SLAVE_ENDPOINT *)bulk_instance.endpoint_in;
      endpoint_out = (UX_SLAVE_ENDPOINT *)bulk_instance.endpoint_out;
      if ((endpoint_in == UX_NULL) || (endpoint_out == UX_NULL))
      {
        tx_thread_sleep(1U);
        continue;
      }

      transfer_out = &endpoint_out->ux_slave_endpoint_transfer_request;
      if (ux_device_stack_transfer_request(transfer_out, USB_BULK_PACKET_SIZE,
                                           USB_BULK_PACKET_SIZE) != UX_SUCCESS)
      {
        break;
      }

      App_State_GetControls(controls);
      for (ULONG index = 0U;
           (index < transfer_out->ux_slave_transfer_request_actual_length) &&
           (index < sizeof(controls)); index++)
      {
        controls[index] = transfer_out->ux_slave_transfer_request_data_pointer[index];
      }
      App_State_SetControls(controls);
      App_State_NoteUsbRx();

      transfer_in = &endpoint_in->ux_slave_endpoint_transfer_request;
      App_State_BuildResponse(transfer_in->ux_slave_transfer_request_data_pointer);
      if (ux_device_stack_transfer_request(transfer_in, USB_BULK_PACKET_SIZE,
                                           USB_BULK_PACKET_SIZE) != UX_SUCCESS)
      {
        break;
      }
      App_State_NoteUsbTx();
    }
  }
}

UINT App_USBX_Bulk_Thread_Create(TX_BYTE_POOL *byte_pool)
{
  if (tx_event_flags_create(&bulk_events, "USB Bulk events") != TX_SUCCESS)
  {
    return UX_EVENT_ERROR;
  }
  if (tx_byte_allocate(byte_pool, (VOID **)&bulk_thread_stack,
                       BULK_THREAD_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS)
  {
    return UX_MEMORY_INSUFFICIENT;
  }
  if (tx_thread_create(&bulk_thread, "USBX Bulk", bulk_thread_entry, 0U,
                       bulk_thread_stack, BULK_THREAD_STACK_SIZE,
                       10U, 10U, 2U, TX_AUTO_START) != TX_SUCCESS)
  {
    return UX_THREAD_ERROR;
  }
  return UX_SUCCESS;
}

VOID App_USBX_Bulk_Force_Disconnect(VOID)
{
  UX_SLAVE_ENDPOINT *endpoint_in;
  UX_SLAVE_ENDPOINT *endpoint_out;

  /* Stop the application loop first, then wake it if it is blocked in a
     transfer. The stock USBX disconnect path destroys endpoints but does not
     abort the waiting transfer semaphore in this STM32 DCD version. */
  bulk_instance.configured = 0U;
  endpoint_in = (UX_SLAVE_ENDPOINT *)bulk_instance.endpoint_in;
  endpoint_out = (UX_SLAVE_ENDPOINT *)bulk_instance.endpoint_out;

  if (endpoint_out != UX_NULL)
  {
    (void)ux_device_stack_transfer_abort(
        &endpoint_out->ux_slave_endpoint_transfer_request,
        UX_TRANSFER_BUS_RESET);
  }
  if (endpoint_in != UX_NULL)
  {
    (void)ux_device_stack_transfer_abort(
        &endpoint_in->ux_slave_endpoint_transfer_request,
        UX_TRANSFER_BUS_RESET);
  }

  App_State_SetUsbConfigured(0U);
}

UINT App_USBX_Bulk_Class_Entry(UX_SLAVE_CLASS_COMMAND *command)
{
  UX_SLAVE_CLASS *class_ptr;
  UX_SLAVE_INTERFACE *interface_ptr;
  UX_SLAVE_ENDPOINT *endpoint;

  if (command == UX_NULL)
  {
    return UX_ERROR;
  }

  class_ptr = command->ux_slave_class_command_class_ptr;
  switch (command->ux_slave_class_command_request)
  {
    case UX_SLAVE_CLASS_COMMAND_INITIALIZE:
      (void)memset(&bulk_instance, 0, sizeof(bulk_instance));
      class_ptr->ux_slave_class_instance = &bulk_instance;
      return UX_SUCCESS;

    case UX_SLAVE_CLASS_COMMAND_QUERY:
      return (command->ux_slave_class_command_class == 0xFFU) ?
             UX_SUCCESS : UX_NO_CLASS_MATCH;

    case UX_SLAVE_CLASS_COMMAND_ACTIVATE:
      interface_ptr = (UX_SLAVE_INTERFACE *)command->ux_slave_class_command_interface;
      bulk_instance.interface_ptr = interface_ptr;
      bulk_instance.endpoint_in = UX_NULL;
      bulk_instance.endpoint_out = UX_NULL;
      interface_ptr->ux_slave_interface_class_instance = &bulk_instance;

      endpoint = interface_ptr->ux_slave_interface_first_endpoint;
      while (endpoint != UX_NULL)
      {
        if ((endpoint->ux_slave_endpoint_descriptor.bEndpointAddress &
             UX_ENDPOINT_DIRECTION) == UX_ENDPOINT_IN)
        {
          bulk_instance.endpoint_in = endpoint;
        }
        else
        {
          bulk_instance.endpoint_out = endpoint;
        }
        endpoint = endpoint->ux_slave_endpoint_next_endpoint;
      }

      if ((bulk_instance.endpoint_in == UX_NULL) ||
          (bulk_instance.endpoint_out == UX_NULL))
      {
        return UX_ENDPOINT_HANDLE_UNKNOWN;
      }
      bulk_instance.configured = 1U;
      App_State_SetUsbConfigured(1U);
      (void)tx_event_flags_set(&bulk_events, BULK_EVENT_CONFIGURED, TX_OR);
      return UX_SUCCESS;

    case UX_SLAVE_CLASS_COMMAND_DEACTIVATE:
      bulk_instance.configured = 0U;
      App_State_SetUsbConfigured(0U);
      bulk_instance.interface_ptr = UX_NULL;
      bulk_instance.endpoint_in = UX_NULL;
      bulk_instance.endpoint_out = UX_NULL;
      return UX_SUCCESS;

    case UX_SLAVE_CLASS_COMMAND_UNINITIALIZE:
      bulk_instance.configured = 0U;
      class_ptr->ux_slave_class_instance = UX_NULL;
      return UX_SUCCESS;

    case UX_SLAVE_CLASS_COMMAND_REQUEST:
    default:
      return UX_ERROR;
  }
}

UINT App_USBX_Bulk_Microsoft_Request(ULONG request, ULONG value, ULONG index,
                                    ULONG length, UCHAR *data,
                                    ULONG *actual_length)
{
  const UCHAR *descriptor;
  ULONG descriptor_length;

  (void)value;
  if ((request != USB_BULK_MS_VENDOR_CODE) || (data == UX_NULL) ||
      (actual_length == UX_NULL))
  {
    return UX_ERROR;
  }

  if (index == 0x0004U)
  {
    descriptor = microsoft_compat_id;
    descriptor_length = sizeof(microsoft_compat_id);
  }
  else if (index == 0x0005U)
  {
    descriptor = microsoft_extended_properties;
    descriptor_length = sizeof(microsoft_extended_properties);
  }
  else
  {
    return UX_ERROR;
  }

  if (descriptor_length > length)
  {
    descriptor_length = length;
  }
  if (descriptor_length > *actual_length)
  {
    descriptor_length = *actual_length;
  }
  (void)memcpy(data, descriptor, descriptor_length);
  *actual_length = descriptor_length;
  return UX_SUCCESS;
}
