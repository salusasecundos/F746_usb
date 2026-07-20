#include "app_vnc_server.h"

#include "app_guix.h"
#include "app_vnc_frame.h"
#include "debug_log.h"

#include <stddef.h>
#include <string.h>

#define APP_VNC_THREAD_STACK_SIZE       4096U
#define APP_VNC_THREAD_PRIORITY         15U
#define APP_VNC_ACCEPT_WAIT             (TX_TIMER_TICKS_PER_SECOND / 5U)
#define APP_VNC_IO_WAIT                 (2U * TX_TIMER_TICKS_PER_SECOND)
#define APP_VNC_POLL_WAIT               ((TX_TIMER_TICKS_PER_SECOND >= 20U) ? \
                                         (TX_TIMER_TICKS_PER_SECOND / 20U) : 1U)
#define APP_VNC_TX_CHUNK_SIZE           1400U
#define APP_VNC_INPUT_DRAIN_LIMIT       16U
#define APP_VNC_TX_QUEUE_DEPTH          6U
#define APP_VNC_TCP_RETRY_COUNT         3U
#define APP_VNC_LISTEN_BACKLOG          2U
#define APP_VNC_DESKTOP_NAME            "STM32F746 GUIX"

#define RFB_C2S_SET_PIXEL_FORMAT        0U
#define RFB_C2S_FIX_COLOUR_MAP          1U
#define RFB_C2S_SET_ENCODINGS           2U
#define RFB_C2S_FRAMEBUFFER_REQUEST     3U
#define RFB_C2S_KEY_EVENT               4U
#define RFB_C2S_POINTER_EVENT           5U
#define RFB_C2S_CLIENT_CUT_TEXT         6U

#define RFB_ENCODING_RAW                0L

typedef struct
{
  uint8_t bits_per_pixel;
  uint8_t depth;
  uint8_t big_endian;
  uint8_t true_colour;
  uint16_t red_max;
  uint16_t green_max;
  uint16_t blue_max;
  uint8_t red_shift;
  uint8_t green_shift;
  uint8_t blue_shift;
} RFB_PIXEL_FORMAT;

typedef struct
{
  NX_TCP_SOCKET *socket;
  NX_PACKET *packet;
  ULONG packet_offset;
  ULONG packet_length;
} RFB_STREAM;

typedef struct
{
  uint16_t x;
  uint16_t y;
  uint16_t width;
  uint16_t height;
} RFB_RECTANGLE;

typedef struct
{
  RFB_STREAM stream;
  RFB_PIXEL_FORMAT pixel_format;
  uint8_t sent_first_update;
  uint8_t update_pending;
  uint8_t update_incremental;
  RFB_RECTANGLE update_request;
} RFB_SESSION;

static NX_IP *vnc_ip;
static NX_PACKET_POOL *vnc_packet_pool;
static NX_TCP_SOCKET vnc_socket;
static TX_THREAD vnc_thread;
static UCHAR *vnc_thread_stack;
static UCHAR vnc_tx_chunk[APP_VNC_TX_CHUNK_SIZE];
static UINT vnc_started;
static UINT vnc_socket_created;

static VOID App_VNC_Thread(ULONG argument);
static UINT RFB_Session_Run(NX_TCP_SOCKET *socket);
static UINT RFB_Process_Client_Message(RFB_SESSION *session);
static UINT RFB_Drain_Client_Messages(RFB_SESSION *session);

static uint16_t RFB_Get_U16(const UCHAR *data)
{
  return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t RFB_Get_U32(const UCHAR *data)
{
  return ((uint32_t)data[0] << 24) |
         ((uint32_t)data[1] << 16) |
         ((uint32_t)data[2] << 8) |
         (uint32_t)data[3];
}

static void RFB_Put_U16(UCHAR *data, uint16_t value)
{
  data[0] = (UCHAR)(value >> 8);
  data[1] = (UCHAR)value;
}

static void RFB_Put_U32(UCHAR *data, uint32_t value)
{
  data[0] = (UCHAR)(value >> 24);
  data[1] = (UCHAR)(value >> 16);
  data[2] = (UCHAR)(value >> 8);
  data[3] = (UCHAR)value;
}

static UINT RFB_Send(NX_TCP_SOCKET *socket, const UCHAR *data, ULONG length)
{
  NX_PACKET *packet;
  ULONG chunk;
  UINT status;

  while (length != 0U)
  {
    chunk = length;
    if (chunk > APP_VNC_TX_CHUNK_SIZE)
    {
      chunk = APP_VNC_TX_CHUNK_SIZE;
    }

    status = nx_packet_allocate(vnc_packet_pool, &packet, NX_TCP_PACKET,
                                APP_VNC_IO_WAIT);
    if (status != NX_SUCCESS)
    {
      return status;
    }

    status = nx_packet_data_append(packet, (VOID *)data, chunk,
                                   vnc_packet_pool, APP_VNC_IO_WAIT);
    if (status != NX_SUCCESS)
    {
      (void)nx_packet_release(packet);
      return status;
    }

    status = nx_tcp_socket_send(socket, packet, APP_VNC_IO_WAIT);
    if (status != NX_SUCCESS)
    {
      (void)nx_packet_release(packet);
      return status;
    }

    data += chunk;
    length -= chunk;
  }

  return NX_SUCCESS;
}

static void RFB_Stream_Reset(RFB_STREAM *stream, NX_TCP_SOCKET *socket)
{
  stream->socket = socket;
  stream->packet = NX_NULL;
  stream->packet_offset = 0U;
  stream->packet_length = 0U;
}

static void RFB_Stream_Close(RFB_STREAM *stream)
{
  if (stream->packet != NX_NULL)
  {
    (void)nx_packet_release(stream->packet);
    stream->packet = NX_NULL;
  }
}

static UINT RFB_Stream_Fill(RFB_STREAM *stream, ULONG wait_option)
{
  UINT status;

  if (stream->packet != NX_NULL)
  {
    return NX_SUCCESS;
  }

  status = nx_tcp_socket_receive(stream->socket, &stream->packet, wait_option);
  if (status != NX_SUCCESS)
  {
    stream->packet = NX_NULL;
    return status;
  }

  stream->packet_offset = 0U;
  status = nx_packet_length_get(stream->packet, &stream->packet_length);
  if (status != NX_SUCCESS)
  {
    RFB_Stream_Close(stream);
  }
  return status;
}

static UINT RFB_Stream_Receive(RFB_STREAM *stream, UCHAR *data, ULONG length)
{
  ULONG available;
  ULONG copied;
  ULONG chunk;
  ULONG actual_status;
  UINT status;

  while (length != 0U)
  {
    if (stream->packet == NX_NULL)
    {
      status = RFB_Stream_Fill(stream, APP_VNC_IO_WAIT);
      if (status == NX_NO_PACKET)
      {
        if (nx_ip_status_check(vnc_ip, NX_IP_ADDRESS_RESOLVED,
                               &actual_status, NX_NO_WAIT) == NX_SUCCESS)
        {
          continue;
        }
        return status;
      }
      if (status != NX_SUCCESS)
      {
        return status;
      }
    }

    available = stream->packet_length - stream->packet_offset;
    chunk = (length < available) ? length : available;
    copied = 0U;
    status = nx_packet_data_extract_offset(stream->packet,
                                           stream->packet_offset,
                                           data, chunk, &copied);
    if ((status != NX_SUCCESS) || (copied != chunk))
    {
      RFB_Stream_Close(stream);
      return (status == NX_SUCCESS) ? NX_NOT_SUCCESSFUL : status;
    }

    stream->packet_offset += copied;
    data += copied;
    length -= copied;

    if (stream->packet_offset >= stream->packet_length)
    {
      RFB_Stream_Close(stream);
      stream->packet_offset = 0U;
      stream->packet_length = 0U;
    }
  }

  return NX_SUCCESS;
}

static UINT RFB_Stream_Skip(RFB_STREAM *stream, uint32_t length)
{
  ULONG chunk;
  UINT status;

  while (length != 0U)
  {
    chunk = length;
    if (chunk > sizeof(vnc_tx_chunk))
    {
      chunk = sizeof(vnc_tx_chunk);
    }
    status = RFB_Stream_Receive(stream, vnc_tx_chunk, chunk);
    if (status != NX_SUCCESS)
    {
      return status;
    }
    length -= chunk;
  }

  return NX_SUCCESS;
}

static void RFB_Default_Pixel_Format(RFB_PIXEL_FORMAT *format)
{
  format->bits_per_pixel = 16U;
  format->depth = 16U;
  format->big_endian = 0U;
  format->true_colour = 1U;
  format->red_max = 31U;
  format->green_max = 63U;
  format->blue_max = 31U;
  format->red_shift = 11U;
  format->green_shift = 5U;
  format->blue_shift = 0U;
}

static uint8_t RFB_Pixel_Format_Valid(const RFB_PIXEL_FORMAT *format)
{
  uint8_t bits = format->bits_per_pixel;

  if (((bits != 8U) && (bits != 16U) && (bits != 32U)) ||
      (format->depth == 0U) || (format->depth > bits) ||
      (format->true_colour == 0U) ||
      (format->red_max == 0U) || (format->green_max == 0U) ||
      (format->blue_max == 0U) ||
      (format->red_shift >= bits) ||
      (format->green_shift >= bits) ||
      (format->blue_shift >= bits))
  {
    return 0U;
  }

  if (((uint32_t)format->red_max > (UINT32_MAX >> format->red_shift)) ||
      ((uint32_t)format->green_max > (UINT32_MAX >> format->green_shift)) ||
      ((uint32_t)format->blue_max > (UINT32_MAX >> format->blue_shift)))
  {
    return 0U;
  }

  if (bits < 32U)
  {
    uint32_t limit = (1UL << bits) - 1UL;
    uint32_t mask = ((uint32_t)format->red_max << format->red_shift) |
                    ((uint32_t)format->green_max << format->green_shift) |
                    ((uint32_t)format->blue_max << format->blue_shift);
    if (mask > limit)
    {
      return 0U;
    }
  }

  return 1U;
}

static UINT RFB_Send_Server_Init(RFB_SESSION *session)
{
  static const char desktop_name[] = APP_VNC_DESKTOP_NAME;
  UCHAR message[24U + sizeof(desktop_name) - 1U] = {0};
  RFB_PIXEL_FORMAT *format = &session->pixel_format;

  RFB_Put_U16(&message[0], APP_VNC_FRAME_WIDTH);
  RFB_Put_U16(&message[2], APP_VNC_FRAME_HEIGHT);
  message[4] = format->bits_per_pixel;
  message[5] = format->depth;
  message[6] = format->big_endian;
  message[7] = format->true_colour;
  RFB_Put_U16(&message[8], format->red_max);
  RFB_Put_U16(&message[10], format->green_max);
  RFB_Put_U16(&message[12], format->blue_max);
  message[14] = format->red_shift;
  message[15] = format->green_shift;
  message[16] = format->blue_shift;
  RFB_Put_U32(&message[20], sizeof(desktop_name) - 1U);
  (void)memcpy(&message[24], desktop_name, sizeof(desktop_name) - 1U);

  return RFB_Send(session->stream.socket, message, sizeof(message));
}

static UINT RFB_Handshake(RFB_SESSION *session)
{
  static const UCHAR server_version[12] = "RFB 003.008\n";
  UCHAR client_version[12];
  const UCHAR security_type_none[4] = {0U, 0U, 0U, 1U};
  const UCHAR security_result_ok[4] = {0U, 0U, 0U, 0U};
  UCHAR selection;
  UCHAR shared_flag;
  uint16_t major;
  uint16_t minor;
  UINT status;

  status = RFB_Send(session->stream.socket, server_version,
                    sizeof(server_version));
  if (status != NX_SUCCESS)
  {
    return status;
  }

  status = RFB_Stream_Receive(&session->stream, client_version,
                              sizeof(client_version));
  if (status != NX_SUCCESS)
  {
    return status;
  }

  if ((memcmp(client_version, "RFB ", 4U) != 0) ||
      (client_version[7] != '.') || (client_version[11] != '\n'))
  {
    return NX_INVALID_PARAMETERS;
  }

  major = (uint16_t)((client_version[4] - '0') * 100U +
                     (client_version[5] - '0') * 10U +
                     (client_version[6] - '0'));
  minor = (uint16_t)((client_version[8] - '0') * 100U +
                     (client_version[9] - '0') * 10U +
                     (client_version[10] - '0'));
  if ((major != 3U) || (minor < 3U))
  {
    return NX_INVALID_PARAMETERS;
  }

  if (minor >= 7U)
  {
    const UCHAR security_types[2] = {1U, 1U};
    status = RFB_Send(session->stream.socket, security_types,
                      sizeof(security_types));
    if (status != NX_SUCCESS)
    {
      return status;
    }
    status = RFB_Stream_Receive(&session->stream, &selection, 1U);
    if ((status != NX_SUCCESS) || (selection != 1U))
    {
      return (status == NX_SUCCESS) ? NX_NOT_SUPPORTED : status;
    }

    /* RFB 3.8 added SecurityResult for the None security type. */
    if (minor >= 8U)
    {
      status = RFB_Send(session->stream.socket, security_result_ok,
                        sizeof(security_result_ok));
      if (status != NX_SUCCESS)
      {
        return status;
      }
    }
  }
  else
  {
    status = RFB_Send(session->stream.socket, security_type_none,
                      sizeof(security_type_none));
    if (status != NX_SUCCESS)
    {
      return status;
    }
  }

  status = RFB_Stream_Receive(&session->stream, &shared_flag, 1U);
  if (status != NX_SUCCESS)
  {
    return status;
  }
  (void)shared_flag;

  return RFB_Send_Server_Init(session);
}

static UINT RFB_Set_Pixel_Format(RFB_SESSION *session)
{
  UCHAR message[19];
  RFB_PIXEL_FORMAT requested;
  UINT status = RFB_Stream_Receive(&session->stream, message, sizeof(message));

  if (status != NX_SUCCESS)
  {
    return status;
  }

  requested.bits_per_pixel = message[3];
  requested.depth = message[4];
  requested.big_endian = (message[5] != 0U) ? 1U : 0U;
  requested.true_colour = message[6];
  requested.red_max = RFB_Get_U16(&message[7]);
  requested.green_max = RFB_Get_U16(&message[9]);
  requested.blue_max = RFB_Get_U16(&message[11]);
  requested.red_shift = message[13];
  requested.green_shift = message[14];
  requested.blue_shift = message[15];

  if (RFB_Pixel_Format_Valid(&requested) == 0U)
  {
    Debug_Log_Line("[VNC] unsupported client pixel format");
    return NX_NOT_SUPPORTED;
  }

  session->pixel_format = requested;
  return NX_SUCCESS;
}

static UINT RFB_Set_Encodings(RFB_SESSION *session)
{
  UCHAR header[3];
  uint16_t count;
  UINT status = RFB_Stream_Receive(&session->stream, header, sizeof(header));

  if (status != NX_SUCCESS)
  {
    return status;
  }
  count = RFB_Get_U16(&header[1]);
  return RFB_Stream_Skip(&session->stream, (uint32_t)count * 4U);
}

static uint8_t RFB_Rectangle_Clip(RFB_RECTANGLE *rectangle)
{
  uint32_t right;
  uint32_t bottom;

  if ((rectangle->width == 0U) || (rectangle->height == 0U) ||
      (rectangle->x >= APP_VNC_FRAME_WIDTH) ||
      (rectangle->y >= APP_VNC_FRAME_HEIGHT))
  {
    return 0U;
  }

  right = (uint32_t)rectangle->x + rectangle->width;
  bottom = (uint32_t)rectangle->y + rectangle->height;
  if (right > APP_VNC_FRAME_WIDTH)
  {
    rectangle->width = (uint16_t)(APP_VNC_FRAME_WIDTH - rectangle->x);
  }
  if (bottom > APP_VNC_FRAME_HEIGHT)
  {
    rectangle->height = (uint16_t)(APP_VNC_FRAME_HEIGHT - rectangle->y);
  }
  return 1U;
}

static uint8_t RFB_Rectangle_Intersect_Dirty(RFB_RECTANGLE *rectangle,
                                             const APP_VNC_FRAME_VIEW *view)
{
  uint32_t left = rectangle->x;
  uint32_t top = rectangle->y;
  uint32_t right = left + rectangle->width;
  uint32_t bottom = top + rectangle->height;
  uint32_t dirty_right;
  uint32_t dirty_bottom;

  if (view->dirty_valid == 0U)
  {
    return 0U;
  }

  dirty_right = (uint32_t)view->dirty_x + view->dirty_width;
  dirty_bottom = (uint32_t)view->dirty_y + view->dirty_height;
  if (left < view->dirty_x)
  {
    left = view->dirty_x;
  }
  if (top < view->dirty_y)
  {
    top = view->dirty_y;
  }
  if (right > dirty_right)
  {
    right = dirty_right;
  }
  if (bottom > dirty_bottom)
  {
    bottom = dirty_bottom;
  }
  if ((left >= right) || (top >= bottom))
  {
    return 0U;
  }

  rectangle->x = (uint16_t)left;
  rectangle->y = (uint16_t)top;
  rectangle->width = (uint16_t)(right - left);
  rectangle->height = (uint16_t)(bottom - top);
  return 1U;
}

static uint8_t RFB_Request_Covers_Dirty(const RFB_RECTANGLE *request,
                                        const APP_VNC_FRAME_VIEW *view)
{
  uint32_t request_right = (uint32_t)request->x + request->width;
  uint32_t request_bottom = (uint32_t)request->y + request->height;
  uint32_t dirty_right = (uint32_t)view->dirty_x + view->dirty_width;
  uint32_t dirty_bottom = (uint32_t)view->dirty_y + view->dirty_height;

  return ((view->dirty_valid != 0U) &&
          (request->x <= view->dirty_x) &&
          (request->y <= view->dirty_y) &&
          (request_right >= dirty_right) &&
          (request_bottom >= dirty_bottom)) ? 1U : 0U;
}

static uint32_t RFB_Convert_Pixel(uint16_t rgb565,
                                  const RFB_PIXEL_FORMAT *format)
{
  uint32_t red = (rgb565 >> 11) & 0x1FU;
  uint32_t green = (rgb565 >> 5) & 0x3FU;
  uint32_t blue = rgb565 & 0x1FU;

  red = ((red * format->red_max) + 15U) / 31U;
  green = ((green * format->green_max) + 31U) / 63U;
  blue = ((blue * format->blue_max) + 15U) / 31U;

  return (red << format->red_shift) |
         (green << format->green_shift) |
         (blue << format->blue_shift);
}

static ULONG RFB_Append_Pixel(UCHAR *destination,
                              uint32_t pixel,
                              const RFB_PIXEL_FORMAT *format)
{
  ULONG bytes = format->bits_per_pixel / 8U;

  if (format->big_endian != 0U)
  {
    for (ULONG index = 0U; index < bytes; index++)
    {
      destination[index] = (UCHAR)(pixel >> (8U * (bytes - index - 1U)));
    }
  }
  else
  {
    for (ULONG index = 0U; index < bytes; index++)
    {
      destination[index] = (UCHAR)(pixel >> (8U * index));
    }
  }

  return bytes;
}

static UINT RFB_Send_Raw_Pixels(RFB_SESSION *session,
                                const APP_VNC_FRAME_VIEW *view,
                                const RFB_RECTANGLE *rectangle,
                                const RFB_PIXEL_FORMAT *format)
{
  ULONG used = 0U;
  ULONG bytes_per_pixel = format->bits_per_pixel / 8U;
  UINT status;

  for (uint32_t y = rectangle->y;
       y < ((uint32_t)rectangle->y + rectangle->height); y++)
  {
    const uint16_t *source = &view->pixels[(y * APP_VNC_FRAME_WIDTH) +
                                           rectangle->x];
    for (uint32_t x = 0U; x < rectangle->width; x++)
    {
      if ((used + bytes_per_pixel) > sizeof(vnc_tx_chunk))
      {
        status = RFB_Send(session->stream.socket, vnc_tx_chunk, used);
        if (status != NX_SUCCESS)
        {
          return status;
        }
        used = 0U;

        /* RFB input and output share one worker.  Service queued mouse
           packets between network-sized image chunks so a full-screen Raw
           update cannot starve pointer input until the frame is complete. */
        status = RFB_Drain_Client_Messages(session);
        if (status != NX_SUCCESS)
        {
          return status;
        }
      }

      used += RFB_Append_Pixel(&vnc_tx_chunk[used],
                               RFB_Convert_Pixel(source[x], format),
                               format);
    }
  }

  if (used != 0U)
  {
    status = RFB_Send(session->stream.socket, vnc_tx_chunk, used);
    if (status != NX_SUCCESS)
    {
      return status;
    }
  }

  return RFB_Drain_Client_Messages(session);
}

static UINT RFB_Send_Empty_Update(RFB_SESSION *session)
{
  const UCHAR message[4] = {0U, 0U, 0U, 0U};
  return RFB_Send(session->stream.socket, message, sizeof(message));
}

static UINT RFB_Send_Framebuffer_Update(RFB_SESSION *session,
                                        uint8_t incremental,
                                        RFB_RECTANGLE request,
                                        uint8_t *sent)
{
  APP_VNC_FRAME_VIEW view = {0};
  RFB_RECTANGLE update;
  RFB_PIXEL_FORMAT pixel_format;
  UCHAR header[16] = {0};
  uint8_t covers_dirty;
  UINT status;

  *sent = 0U;

  if (RFB_Rectangle_Clip(&request) == 0U)
  {
    status = RFB_Send_Empty_Update(session);
    *sent = (status == NX_SUCCESS) ? 1U : 0U;
    return status;
  }

  if (App_VNC_Frame_Acquire(&view) == 0U)
  {
    return NX_SUCCESS;
  }

  update = request;
  /* A SetPixelFormat received while pixels are being streamed applies to
     the next update; one rectangle must keep a single format throughout. */
  pixel_format = session->pixel_format;
  covers_dirty = RFB_Request_Covers_Dirty(&request, &view);
  if ((incremental != 0U) && (session->sent_first_update != 0U) &&
      (RFB_Rectangle_Intersect_Dirty(&update, &view) == 0U))
  {
    App_VNC_Frame_Release(&view);
    return NX_SUCCESS;
  }

  header[0] = 0U; /* FramebufferUpdate */
  RFB_Put_U16(&header[2], 1U);
  RFB_Put_U16(&header[4], update.x);
  RFB_Put_U16(&header[6], update.y);
  RFB_Put_U16(&header[8], update.width);
  RFB_Put_U16(&header[10], update.height);
  RFB_Put_U32(&header[12], (uint32_t)RFB_ENCODING_RAW);

  status = RFB_Send(session->stream.socket, header, sizeof(header));
  if (status == NX_SUCCESS)
  {
    status = RFB_Send_Raw_Pixels(session, &view, &update, &pixel_format);
  }

  if (status == NX_SUCCESS)
  {
    *sent = 1U;
    session->sent_first_update = 1U;
    if (covers_dirty != 0U)
    {
      App_VNC_Frame_Acknowledge(view.generation);
    }
  }

  App_VNC_Frame_Release(&view);
  return status;
}

static UINT RFB_Framebuffer_Request(RFB_SESSION *session)
{
  UCHAR message[9];
  RFB_RECTANGLE request;
  UINT status = RFB_Stream_Receive(&session->stream, message, sizeof(message));

  if (status != NX_SUCCESS)
  {
    return status;
  }

  request.x = RFB_Get_U16(&message[1]);
  request.y = RFB_Get_U16(&message[3]);
  request.width = RFB_Get_U16(&message[5]);
  request.height = RFB_Get_U16(&message[7]);
  session->update_incremental = message[0];
  session->update_request = request;
  session->update_pending = 1U;
  return NX_SUCCESS;
}

static UINT RFB_Service_Pending_Update(RFB_SESSION *session)
{
  RFB_RECTANGLE request;
  uint8_t incremental;
  uint8_t sent;
  UINT status;

  if (session->update_pending == 0U)
  {
    return NX_SUCCESS;
  }

  incremental = session->update_incremental;
  request = session->update_request;

  /* Clear this request before transmitting.  A new request received by the
     input drain then remains pending instead of being erased afterwards. */
  session->update_pending = 0U;
  status = RFB_Send_Framebuffer_Update(session, incremental, request, &sent);
  if ((status == NX_SUCCESS) && (sent == 0U) &&
      (session->update_pending == 0U))
  {
    session->update_incremental = incremental;
    session->update_request = request;
    session->update_pending = 1U;
  }
  return status;
}

static UINT RFB_Fix_Colour_Map(RFB_SESSION *session)
{
  UCHAR message[5];
  uint16_t count;
  UINT status = RFB_Stream_Receive(&session->stream, message, sizeof(message));

  if (status != NX_SUCCESS)
  {
    return status;
  }
  count = RFB_Get_U16(&message[3]);
  return RFB_Stream_Skip(&session->stream, (uint32_t)count * 6U);
}

static UINT RFB_Pointer_Event(RFB_SESSION *session)
{
  UCHAR message[5];
  UINT status = RFB_Stream_Receive(&session->stream, message, sizeof(message));

  if (status == NX_SUCCESS)
  {
    App_GUIX_RemotePointerEvent(RFB_Get_U16(&message[1]),
                                RFB_Get_U16(&message[3]), message[0]);
  }
  return status;
}

static UINT RFB_Client_Cut_Text(RFB_SESSION *session)
{
  UCHAR message[7];
  UINT status = RFB_Stream_Receive(&session->stream, message, sizeof(message));

  if (status != NX_SUCCESS)
  {
    return status;
  }
  return RFB_Stream_Skip(&session->stream, RFB_Get_U32(&message[3]));
}

static UINT RFB_Process_Client_Message(RFB_SESSION *session)
{
  UCHAR message_type;
  UINT status;

  status = RFB_Stream_Receive(&session->stream, &message_type, 1U);
  if (status != NX_SUCCESS)
  {
    return status;
  }

  switch (message_type)
  {
    case RFB_C2S_SET_PIXEL_FORMAT:
      return RFB_Set_Pixel_Format(session);

    case RFB_C2S_FIX_COLOUR_MAP:
      return RFB_Fix_Colour_Map(session);

    case RFB_C2S_SET_ENCODINGS:
      return RFB_Set_Encodings(session);

    case RFB_C2S_FRAMEBUFFER_REQUEST:
      return RFB_Framebuffer_Request(session);

    case RFB_C2S_KEY_EVENT:
      return RFB_Stream_Skip(&session->stream, 7U);

    case RFB_C2S_POINTER_EVENT:
      return RFB_Pointer_Event(session);

    case RFB_C2S_CLIENT_CUT_TEXT:
      return RFB_Client_Cut_Text(session);

    default:
      Debug_Log_U32("[VNC] unsupported client message: ", message_type);
      return NX_NOT_SUPPORTED;
  }
}

static UINT RFB_Drain_Client_Messages(RFB_SESSION *session)
{
  UINT status;

  for (uint32_t count = 0U; count < APP_VNC_INPUT_DRAIN_LIMIT; count++)
  {
    status = RFB_Stream_Fill(&session->stream, NX_NO_WAIT);
    if (status == NX_NO_PACKET)
    {
      return NX_SUCCESS;
    }
    if (status != NX_SUCCESS)
    {
      return status;
    }

    status = RFB_Process_Client_Message(session);
    if (status != NX_SUCCESS)
    {
      return status;
    }
  }

  return NX_SUCCESS;
}

static UINT RFB_Session_Run(NX_TCP_SOCKET *socket)
{
  RFB_SESSION session = {0};
  UINT status;

  RFB_Stream_Reset(&session.stream, socket);
  RFB_Default_Pixel_Format(&session.pixel_format);

  status = RFB_Handshake(&session);
  if (status != NX_SUCCESS)
  {
    RFB_Stream_Close(&session.stream);
    return status;
  }

  Debug_Log_Line("[VNC] RFB client connected");
  for (;;)
  {
    status = RFB_Service_Pending_Update(&session);
    if (status != NX_SUCCESS)
    {
      break;
    }

    status = RFB_Stream_Fill(&session.stream, APP_VNC_POLL_WAIT);
    if (status == NX_NO_PACKET)
    {
      continue;
    }
    if (status != NX_SUCCESS)
    {
      break;
    }

    status = RFB_Process_Client_Message(&session);

    if (status != NX_SUCCESS)
    {
      break;
    }
  }

  RFB_Stream_Close(&session.stream);
  App_GUIX_RemotePointerRelease();
  Debug_Log_Line("[VNC] RFB client disconnected");
  return status;
}

static UINT App_VNC_Socket_Open(void)
{
  UINT status;

  status = nx_tcp_socket_create(vnc_ip, &vnc_socket,
                                "STM32F746 RFB server", NX_IP_NORMAL,
                                NX_FRAGMENT_OKAY, NX_IP_TIME_TO_LIVE,
                                16384U, NX_NULL, NX_NULL);
  if (status != NX_SUCCESS)
  {
    return status;
  }
  vnc_socket_created = 1U;

  status = nx_tcp_socket_transmit_configure(&vnc_socket,
                                             APP_VNC_TX_QUEUE_DEPTH,
                                             NX_IP_PERIODIC_RATE,
                                             APP_VNC_TCP_RETRY_COUNT, 1U);
  if (status == NX_SUCCESS)
  {
    status = nx_tcp_server_socket_listen(vnc_ip, APP_VNC_SERVER_PORT,
                                         &vnc_socket,
                                         APP_VNC_LISTEN_BACKLOG, NX_NULL);
  }
  if (status != NX_SUCCESS)
  {
    (void)nx_tcp_socket_delete(&vnc_socket);
    vnc_socket_created = 0U;
  }
  return status;
}

static UINT App_VNC_Socket_Reset(void)
{
  UINT disconnect_status;
  UINT status;

  if (vnc_socket_created == 0U)
  {
    return App_VNC_Socket_Open();
  }

  /* Do not wait for all queued Raw pixels to be acknowledged.  Unaccept
     immediately flushes the old connection and returns this server socket
     to a known CLOSED state before it is attached to the listener again. */
  disconnect_status = nx_tcp_socket_disconnect(&vnc_socket, NX_NO_WAIT);
  if ((disconnect_status != NX_SUCCESS) &&
      (disconnect_status != NX_IN_PROGRESS) &&
      (disconnect_status != NX_NOT_CONNECTED))
  {
    Debug_Log_U32("[VNC] disconnect failed: ", disconnect_status);
  }

  status = nx_tcp_server_socket_unaccept(&vnc_socket);
  if (status != NX_SUCCESS)
  {
    Debug_Log_U32("[VNC] unaccept failed: ", status);
    return status;
  }

  status = nx_tcp_server_socket_relisten(vnc_ip, APP_VNC_SERVER_PORT,
                                         &vnc_socket);
  if (status == NX_CONNECTION_PENDING)
  {
    return NX_SUCCESS;
  }
  if (status == NX_INVALID_RELISTEN)
  {
    status = nx_tcp_server_socket_listen(vnc_ip, APP_VNC_SERVER_PORT,
                                         &vnc_socket,
                                         APP_VNC_LISTEN_BACKLOG, NX_NULL);
  }
  else if (status != NX_SUCCESS)
  {
    Debug_Log_U32("[VNC] relisten failed: ", status);
  }
  return status;
}

static VOID App_VNC_Thread(ULONG argument)
{
  ULONG actual_status;
  UINT status;

  (void)argument;
  status = App_VNC_Socket_Open();
  if (status != NX_SUCCESS)
  {
    Debug_Log_U32("[VNC] socket/listen failed: ", status);
    return;
  }

  Debug_Log_U32("[VNC] RFB server port: ", APP_VNC_SERVER_PORT);
  for (;;)
  {
    if (nx_ip_status_check(vnc_ip, NX_IP_ADDRESS_RESOLVED, &actual_status,
                           NX_NO_WAIT) != NX_SUCCESS)
    {
      tx_thread_sleep(APP_VNC_ACCEPT_WAIT);
      continue;
    }

    status = nx_tcp_server_socket_accept(&vnc_socket, APP_VNC_ACCEPT_WAIT);
    if (status != NX_SUCCESS)
    {
      continue;
    }

    (void)RFB_Session_Run(&vnc_socket);
    do
    {
      status = App_VNC_Socket_Reset();
      if (status != NX_SUCCESS)
      {
        tx_thread_sleep(APP_VNC_ACCEPT_WAIT);
      }
    } while (status != NX_SUCCESS);
  }
}

UINT App_VNC_Server_Init(TX_BYTE_POOL *byte_pool,
                         NX_IP *ip,
                         NX_PACKET_POOL *packet_pool)
{
  UINT status;

  if ((byte_pool == TX_NULL) || (ip == NX_NULL) || (packet_pool == NX_NULL))
  {
    return NX_PTR_ERROR;
  }
  if (vnc_started != 0U)
  {
    return NX_ALREADY_ENABLED;
  }

  status = tx_byte_allocate(byte_pool, (VOID **)&vnc_thread_stack,
                            APP_VNC_THREAD_STACK_SIZE, TX_NO_WAIT);
  if (status != TX_SUCCESS)
  {
    return NX_NOT_SUCCESSFUL;
  }

  vnc_ip = ip;
  vnc_packet_pool = packet_pool;
  status = tx_thread_create(&vnc_thread, "VNC RFB server", App_VNC_Thread, 0U,
                            vnc_thread_stack, APP_VNC_THREAD_STACK_SIZE,
                            APP_VNC_THREAD_PRIORITY, APP_VNC_THREAD_PRIORITY,
                            TX_NO_TIME_SLICE, TX_AUTO_START);
  if (status != TX_SUCCESS)
  {
    (void)tx_byte_release(vnc_thread_stack);
    vnc_thread_stack = TX_NULL;
    return NX_NOT_SUCCESSFUL;
  }

  vnc_started = 1U;
  return NX_SUCCESS;
}
