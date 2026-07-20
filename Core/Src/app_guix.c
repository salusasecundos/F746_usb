#include "app_guix.h"

#include "app_state.h"
#include "app_threadx.h"
#include "app_vnc_frame.h"
#include "app_netxduo.h"
#include "debug_log.h"
#include "gx_api.h"
#include "gx_display.h"
#include "i2c.h"
#include "ltdc.h"
#include "main.h"

#include <string.h>

#define LCD_WIDTH                         480U
#define LCD_HEIGHT                        272U
#define FRAMEBUFFER_PIXELS                (LCD_WIDTH * LCD_HEIGHT)
#define FRAMEBUFFER_COUNT                 2U
#define FRAMEBUFFER_VBLANK_TIMEOUT        5000000U
#define TOUCH_I2C_ADDRESS                 0x70U
#define TOUCH_STATUS_REGISTER             0x02U
#define GUI_TIMER_ID                      1U
#define GUI_REFRESH_TICKS                 ((TX_TIMER_TICKS_PER_SECOND >= 10U) ? \
                                           (TX_TIMER_TICKS_PER_SECOND / 10U) : 1U)
#define SCREEN_SAVER_TIMER_ID             2U
#define SCREEN_SAVER_TIMEOUT_TICKS        (60U * TX_TIMER_TICKS_PER_SECOND)
#define SCREEN_SAVER_ANIMATION_TICKS      ((TX_TIMER_TICKS_PER_SECOND >= 8U) ? \
                                           (TX_TIMER_TICKS_PER_SECOND / 8U) : 1U)
#define SCREEN_SAVER_POINT_COUNT          4U
#define SCREEN_SAVER_TRAIL_FRAMES         1U
#define SCREEN_SAVER_BEZIER_SEGMENTS      16U

#define ID_TAB_STATUS                    10U
#define ID_TAB_CONTROLS                  11U
#define ID_TAB_NETWORK                   12U
#define ID_TAB_DIAGNOSTICS               13U
#define ID_RESET_COUNTERS                20U
#define ID_DEFAULTS                      21U
#define ID_APPLY                         22U
#define ID_DHCP_RENEW                    23U
#define ID_NET_CLOSE                     24U
#define ID_STATUS_SLIDER_1               40U
#define ID_STATUS_SLIDER_2               41U
#define ID_STATUS_SLIDER_3               42U
#define ID_CONFIG_SLIDER_1               50U
#define ID_CONFIG_SLIDER_2               51U
#define ID_CONFIG_SLIDER_3               52U
#define ID_NET_SLIDER_1                  60U
#define ID_NET_SLIDER_2                  61U

#if (APP_DIAGNOSTICS_GUI_ENABLE != 0U)
#define GUI_PAGE_COUNT                    4U
#define GUI_TAB_COUNT                     4U
#else
#define GUI_PAGE_COUNT                    3U
#define GUI_TAB_COUNT                     3U
#endif

#define RGB565(r, g, b) ((GX_COLOR)((((uint32_t)(r) & 0xF8U) << 8) | \
                                     (((uint32_t)(g) & 0xFCU) << 3) | \
                                     ((uint32_t)(b) >> 3)))

extern GX_FONT _gx_system_font_8bpp;

__attribute__((section(".framebuffer"), aligned(32)))
static uint16_t framebuffer[FRAMEBUFFER_COUNT][FRAMEBUFFER_PIXELS];

static GX_DISPLAY display;
static GX_CANVAS canvas;
static GX_WINDOW_ROOT root;
static GX_WINDOW pages[GUI_PAGE_COUNT];
static GX_TEXT_BUTTON tab_buttons[GUI_TAB_COUNT];
static GX_TEXT_BUTTON page_buttons[5];
static GX_SLIDER status_sliders[3];
static GX_SLIDER config_sliders[3];
static GX_SLIDER network_sliders[2];
static GX_PROMPT status_prompts[8];
static GX_PROMPT slider_labels[8];
static GX_WINDOW screen_saver;
static char status_text[8][48];
static TX_THREAD touch_thread;
static UCHAR *touch_stack;
static volatile ULONG last_user_activity_ticks;
static uint8_t remote_pointer_pressed;
static GX_POINT remote_pointer_last;
static UINT screen_saver_active;
static uint32_t screen_saver_random_state;
static UINT display_vblank_timeout_reported;
#if (APP_DIAGNOSTICS_GUI_ENABLE != 0U)
static APP_DIAGNOSTICS_SNAPSHOT diagnostics_gui_snapshot;
static ULONG diagnostics_gui_next_tick;
#endif

typedef struct
{
  int32_t x;
  int32_t y;
  int32_t velocity_x;
  int32_t velocity_y;
} SCREEN_SAVER_POINT;

typedef struct
{
  int16_t x;
  int16_t y;
} SCREEN_SAVER_POSITION;

typedef struct
{
  SCREEN_SAVER_POSITION points[SCREEN_SAVER_POINT_COUNT];
  GX_COLOR color;
} SCREEN_SAVER_TRAIL_FRAME;

static SCREEN_SAVER_POINT screen_saver_points[SCREEN_SAVER_POINT_COUNT];
static SCREEN_SAVER_TRAIL_FRAME
    screen_saver_trail[SCREEN_SAVER_TRAIL_FRAMES];
static UINT screen_saver_trail_head;
static UINT screen_saver_trail_count;

static GX_COLOR color_table[GX_MAX_DEFAULT_COLORS] =
{
  RGB565(12, 20, 32), RGB565(35, 49, 64), RGB565(18, 29, 43),
  RGB565(80, 100, 120), RGB565(55, 72, 91), RGB565(232, 239, 246),
  RGB565(255, 255, 255), RGB565(0, 145, 210), RGB565(5, 10, 15),
  RGB565(120, 150, 175), RGB565(80, 110, 135), RGB565(52, 78, 103),
  RGB565(28, 44, 60), RGB565(244, 248, 252), RGB565(25, 40, 55),
  RGB565(0, 145, 210), RGB565(235, 240, 245), RGB565(25, 40, 55),
  RGB565(150, 170, 185), RGB565(12, 20, 32), RGB565(95, 115, 130),
  RGB565(0, 0, 0), RGB565(0, 190, 135), RGB565(190, 220, 230),
  RGB565(70, 95, 115), RGB565(125, 140, 150), RGB565(45, 55, 65),
  RGB565(160, 170, 180), RGB565(35, 45, 55)
};

static GX_FONT *font_table[GX_DEFAULT_FONT_COUNT] =
{
  &_gx_system_font_8bpp, &_gx_system_font_8bpp,
  &_gx_system_font_8bpp, &_gx_system_font_8bpp
};

static GX_PIXELMAP *pixelmap_table[GX_DEFAULT_PIXELMAP_COUNT] =
{
  GX_NULL, GX_NULL, GX_NULL, GX_NULL, GX_NULL
};

#if (APP_DIAGNOSTICS_GUI_ENABLE != 0U)
/* Compact 5x7 diagnostic font: digits and uppercase ASCII. Lowercase names
   are converted to uppercase while drawing. */
static const uint8_t diagnostics_digit_font[10][5] =
{
  {0x3EU, 0x51U, 0x49U, 0x45U, 0x3EU},
  {0x00U, 0x42U, 0x7FU, 0x40U, 0x00U},
  {0x42U, 0x61U, 0x51U, 0x49U, 0x46U},
  {0x21U, 0x41U, 0x45U, 0x4BU, 0x31U},
  {0x18U, 0x14U, 0x12U, 0x7FU, 0x10U},
  {0x27U, 0x45U, 0x45U, 0x45U, 0x39U},
  {0x3CU, 0x4AU, 0x49U, 0x49U, 0x30U},
  {0x01U, 0x71U, 0x09U, 0x05U, 0x03U},
  {0x36U, 0x49U, 0x49U, 0x49U, 0x36U},
  {0x06U, 0x49U, 0x49U, 0x29U, 0x1EU}
};

static const uint8_t diagnostics_upper_font[26][5] =
{
  {0x7EU, 0x11U, 0x11U, 0x11U, 0x7EU},
  {0x7FU, 0x49U, 0x49U, 0x49U, 0x36U},
  {0x3EU, 0x41U, 0x41U, 0x41U, 0x22U},
  {0x7FU, 0x41U, 0x41U, 0x22U, 0x1CU},
  {0x7FU, 0x49U, 0x49U, 0x49U, 0x41U},
  {0x7FU, 0x09U, 0x09U, 0x09U, 0x01U},
  {0x3EU, 0x41U, 0x49U, 0x49U, 0x7AU},
  {0x7FU, 0x08U, 0x08U, 0x08U, 0x7FU},
  {0x00U, 0x41U, 0x7FU, 0x41U, 0x00U},
  {0x20U, 0x40U, 0x41U, 0x3FU, 0x01U},
  {0x7FU, 0x08U, 0x14U, 0x22U, 0x41U},
  {0x7FU, 0x40U, 0x40U, 0x40U, 0x40U},
  {0x7FU, 0x02U, 0x0CU, 0x02U, 0x7FU},
  {0x7FU, 0x04U, 0x08U, 0x10U, 0x7FU},
  {0x3EU, 0x41U, 0x41U, 0x41U, 0x3EU},
  {0x7FU, 0x09U, 0x09U, 0x09U, 0x06U},
  {0x3EU, 0x41U, 0x51U, 0x21U, 0x5EU},
  {0x7FU, 0x09U, 0x19U, 0x29U, 0x46U},
  {0x46U, 0x49U, 0x49U, 0x49U, 0x31U},
  {0x01U, 0x01U, 0x7FU, 0x01U, 0x01U},
  {0x3FU, 0x40U, 0x40U, 0x40U, 0x3FU},
  {0x1FU, 0x20U, 0x40U, 0x20U, 0x1FU},
  {0x3FU, 0x40U, 0x38U, 0x40U, 0x3FU},
  {0x63U, 0x14U, 0x08U, 0x14U, 0x63U},
  {0x07U, 0x08U, 0x70U, 0x08U, 0x07U},
  {0x61U, 0x51U, 0x49U, 0x45U, 0x43U}
};
#endif

static GX_RECTANGLE make_rect(GX_VALUE left, GX_VALUE top,
                              GX_VALUE right, GX_VALUE bottom)
{
  GX_RECTANGLE rectangle;
  rectangle.gx_rectangle_left = left;
  rectangle.gx_rectangle_top = top;
  rectangle.gx_rectangle_right = right;
  rectangle.gx_rectangle_bottom = bottom;
  return rectangle;
}

static UINT string_length(const char *text)
{
  UINT length = 0U;
  while ((text != GX_NULL) && (text[length] != '\0'))
  {
    length++;
  }
  return length;
}

static void set_prompt_text(GX_PROMPT *prompt, char *text)
{
  GX_STRING string;
  string.gx_string_ptr = text;
  string.gx_string_length = string_length(text);
  (void)gx_prompt_text_set_ext(prompt, &string);
}

static void set_button_text(GX_TEXT_BUTTON *button, const char *text)
{
  GX_STRING string;
  string.gx_string_ptr = text;
  string.gx_string_length = string_length(text);
  (void)gx_text_button_text_set_ext(button, &string);
}

static UINT append_text(char *buffer, UINT position, UINT capacity, const char *text)
{
  while ((*text != '\0') && ((position + 1U) < capacity))
  {
    buffer[position++] = *text++;
  }
  buffer[position] = '\0';
  return position;
}

static UINT text_is_equal(const char *first, const char *second)
{
  while ((*first != '\0') && (*first == *second))
  {
    first++;
    second++;
  }
  return (*first == *second) ? 1U : 0U;
}

static void update_status_prompt(UINT index, const char *new_text)
{
  UINT position;

  if ((index >= 8U) || (text_is_equal(status_text[index], new_text) != 0U))
  {
    return;
  }

  status_text[index][0] = '\0';
  position = append_text(status_text[index], 0U, sizeof(status_text[index]),
                         new_text);
  status_text[index][position] = '\0';
  set_prompt_text(&status_prompts[index], status_text[index]);
}

static UINT append_u32(char *buffer, UINT position, UINT capacity, uint32_t value)
{
  char digits[10];
  UINT count = 0U;

  do
  {
    digits[count++] = (char)('0' + (value % 10U));
    value /= 10U;
  } while ((value != 0U) && (count < sizeof(digits)));

  while ((count != 0U) && ((position + 1U) < capacity))
  {
    buffer[position++] = digits[--count];
  }
  buffer[position] = '\0';
  return position;
}

static UINT append_fixed_i32(char *buffer, UINT position, UINT capacity,
                             int32_t value, uint32_t divisor)
{
  uint32_t magnitude;
  uint32_t fraction;
  uint32_t fraction_width = 0U;
  uint32_t width_divisor = divisor;

  if (value < 0)
  {
    if ((position + 1U) < capacity)
    {
      buffer[position++] = '-';
    }
    magnitude = (uint32_t)(-(value + 1)) + 1U;
  }
  else
  {
    magnitude = (uint32_t)value;
  }

  position = append_u32(buffer, position, capacity, magnitude / divisor);
  if ((position + 1U) < capacity)
  {
    buffer[position++] = '.';
    buffer[position] = '\0';
  }

  fraction = magnitude % divisor;
  while (width_divisor > 1U)
  {
    width_divisor /= 10U;
    fraction_width++;
  }
  while ((fraction_width > 1U) && (fraction < (divisor / 10U)))
  {
    if ((position + 1U) < capacity)
    {
      buffer[position++] = '0';
      buffer[position] = '\0';
    }
    divisor /= 10U;
    fraction_width--;
  }
  return append_u32(buffer, position, capacity, fraction);
}

static void build_status_line(char *buffer, UINT capacity, const char *prefix,
                              uint32_t first, const char *middle, uint32_t second)
{
  UINT position = 0U;
  buffer[0] = '\0';
  position = append_text(buffer, position, capacity, prefix);
  position = append_u32(buffer, position, capacity, first);
  position = append_text(buffer, position, capacity, middle);
  (void)append_u32(buffer, position, capacity, second);
}

static void update_status_text(void)
{
  APP_STATE_SNAPSHOT state;
  char new_text[sizeof(status_text[0])];
  UINT position;
  uint32_t ip;
  uint32_t humidity_milli_percent;

  App_State_ServiceTimeouts();
  App_State_Get(&state);

  new_text[0] = '\0';
  position = append_text(new_text, 0U, sizeof(new_text), "Temp: ");
  (void)append_fixed_i32(new_text, position, sizeof(new_text),
                         state.temperature, 100U);
  update_status_prompt(0U, new_text);

  new_text[0] = '\0';
  position = append_text(new_text, 0U, sizeof(new_text), "Pressure: ");
  (void)append_fixed_i32(new_text, position, sizeof(new_text),
                         (int32_t)state.pressure, 10000U);
  update_status_prompt(1U, new_text);

  new_text[0] = '\0';
  position = append_text(new_text, 0U, sizeof(new_text), "Humidity: ");
  /* Bosch integer compensation returns %RH x1024.  Convert it to a decimal
     milli-percent value before using the power-of-ten text formatter. */
  humidity_milli_percent =
      (uint32_t)((((uint64_t)state.humidity * 1000U) + 512U) / 1024U);
  (void)append_fixed_i32(new_text, position, sizeof(new_text),
                         (int32_t)humidity_milli_percent, 1000U);
  update_status_prompt(2U, new_text);

  build_status_line(new_text, sizeof(new_text), "USB C/Cfg/App: ",
                    state.usb_cable_connected, "/", state.usb_configured);
  position = string_length(new_text);
  position = append_text(new_text, position, sizeof(new_text), "/");
  (void)append_u32(new_text, position, sizeof(new_text), state.usb_app_active);
  update_status_prompt(3U, new_text);

  build_status_line(new_text, sizeof(new_text),
                    state.usb_rx_active ? "USB RX* " : "USB RX  ",
                    state.usb_rx_packets,
                    state.usb_tx_active ? "  TX* " : "  TX  ",
                    state.usb_tx_packets);
  update_status_prompt(4U, new_text);

  new_text[0] = '\0';
  position = append_text(new_text, 0U, sizeof(new_text), "LAN: ");
  position = append_text(new_text, position, sizeof(new_text),
                         state.lan_link_up ? "LINK " : "DOWN ");
  position = append_text(new_text, position, sizeof(new_text),
                         state.lan_address_ready ? "IP " : "NO-IP ");
  (void)append_text(new_text, position, sizeof(new_text),
                    state.lan_client_active ? "CLIENT" : "IDLE");
  update_status_prompt(5U, new_text);

  build_status_line(new_text, sizeof(new_text),
                    state.lan_rx_active ? "LAN RX* " : "LAN RX  ",
                    state.lan_rx_packets,
                    state.lan_tx_active ? "  TX* " : "  TX  ",
                    state.lan_tx_packets);
  update_status_prompt(6U, new_text);

  new_text[0] = '\0';
  position = append_text(new_text, 0U, sizeof(new_text), "IP: ");
  ip = state.ipv4_address;
  position = append_u32(new_text, position, sizeof(new_text), ip >> 24);
  position = append_text(new_text, position, sizeof(new_text), ".");
  position = append_u32(new_text, position, sizeof(new_text), (ip >> 16) & 0xFFU);
  position = append_text(new_text, position, sizeof(new_text), ".");
  position = append_u32(new_text, position, sizeof(new_text), (ip >> 8) & 0xFFU);
  position = append_text(new_text, position, sizeof(new_text), ".");
  (void)append_u32(new_text, position, sizeof(new_text), ip & 0xFFU);
  update_status_prompt(7U, new_text);

  /* Commands received from USB/LAN must be visible on both slider pages. */
  for (UINT index = 0U; index < 3U; index++)
  {
    if (status_sliders[index].gx_slider_info.gx_slider_info_current_val !=
        (INT)state.controls[index + 1U])
    {
      (void)gx_slider_value_set(&status_sliders[index],
                                &status_sliders[index].gx_slider_info,
                                state.controls[index + 1U]);
    }
    if (config_sliders[index].gx_slider_info.gx_slider_info_current_val !=
        (INT)state.controls[index + 1U])
    {
      (void)gx_slider_value_set(&config_sliders[index],
                                &config_sliders[index].gx_slider_info,
                                state.controls[index + 1U]);
    }
  }
  if (network_sliders[0].gx_slider_info.gx_slider_info_current_val !=
      (INT)state.controls[0])
  {
    (void)gx_slider_value_set(&network_sliders[0],
                              &network_sliders[0].gx_slider_info,
                              state.controls[0]);
  }
}

#if (APP_DIAGNOSTICS_GUI_ENABLE != 0U)

static const uint8_t *diagnostics_glyph_get(char character)
{
  static const uint8_t blank[5] = {0U, 0U, 0U, 0U, 0U};
  static const uint8_t dash[5] = {0x08U, 0x08U, 0x08U, 0x08U, 0x08U};
  static const uint8_t dot[5] = {0U, 0x60U, 0x60U, 0U, 0U};
  static const uint8_t colon[5] = {0U, 0x36U, 0x36U, 0U, 0U};
  static const uint8_t slash[5] = {0x20U, 0x10U, 0x08U, 0x04U, 0x02U};
  static const uint8_t underscore[5] = {0x40U, 0x40U, 0x40U, 0x40U, 0x40U};

  if ((character >= 'a') && (character <= 'z'))
  {
    character = (char)(character - ('a' - 'A'));
  }
  if ((character >= '0') && (character <= '9'))
  {
    return diagnostics_digit_font[(UINT)(character - '0')];
  }
  if ((character >= 'A') && (character <= 'Z'))
  {
    return diagnostics_upper_font[(UINT)(character - 'A')];
  }

  switch (character)
  {
    case '-': return dash;
    case '.': return dot;
    case ':': return colon;
    case '/': return slash;
    case '_': return underscore;
    default:  return blank;
  }
}

static void diagnostics_small_text_draw(GX_VALUE x, GX_VALUE y,
                                        const char *text, GX_COLOR color)
{
  (void)gx_context_raw_brush_define(color, color, GX_BRUSH_SOLID_FILL);

  while ((*text != '\0') && (x <= (GX_VALUE)(LCD_WIDTH - 6U)))
  {
    const uint8_t *glyph = diagnostics_glyph_get(*text++);
    for (UINT column = 0U; column < 5U; column++)
    {
      for (UINT row = 0U; row < 7U; row++)
      {
        if ((glyph[column] & (1U << row)) != 0U)
        {
          GX_POINT point = {(GX_VALUE)(x + column),
                            (GX_VALUE)(y + row)};
          (void)gx_canvas_pixel_draw(point);
        }
      }
    }
    x = (GX_VALUE)(x + 6);
  }
}

static UINT diagnostics_append_kib(char *line, UINT position, UINT capacity,
                                   ULONG bytes)
{
  position = append_u32(line, position, capacity, bytes / 1024U);
  return append_text(line, position, capacity, "K");
}

static const char *diagnostics_state_name(UINT state)
{
  switch (state)
  {
    case TX_READY:          return "RDY";
    case TX_COMPLETED:      return "DONE";
    case TX_TERMINATED:     return "TERM";
    case TX_SLEEP:          return "SLP";
    case TX_SUSPENDED:      return "SUSP";
    case TX_QUEUE_SUSP:     return "QUE";
    case TX_SEMAPHORE_SUSP: return "SEM";
    case TX_EVENT_FLAG:     return "EVT";
    case TX_TCP_IP:         return "TCP";
    case TX_MUTEX_SUSP:     return "MTX";
    default:                return "WAIT";
  }
}

static void diagnostics_memory_line_draw(GX_VALUE y, const char *name,
                                         ULONG used, ULONG free)
{
  char line[80];
  UINT position = 0U;

  line[0] = '\0';
  position = append_text(line, position, sizeof(line), name);
  position = append_text(line, position, sizeof(line), " USED/FREE ");
  position = diagnostics_append_kib(line, position, sizeof(line), used);
  position = append_text(line, position, sizeof(line), "/");
  (void)diagnostics_append_kib(line, position, sizeof(line), free);
  diagnostics_small_text_draw(4, y, line, color_table[23]);
}

static VOID diagnostics_page_draw(GX_WINDOW *window)
{
  char line[80];
  GX_VALUE y = 45;
  UINT position = 0U;

  gx_window_draw(window);

  line[0] = '\0';
  position = append_text(line, position, sizeof(line), "UP ");
  position = append_u32(line, position, sizeof(line),
                        diagnostics_gui_snapshot.uptime_seconds);
  position = append_text(line, position, sizeof(line),
                         "S  THREADS ");
  position = append_u32(line, position, sizeof(line),
                        diagnostics_gui_snapshot.thread_count);
  position = append_text(line, position, sizeof(line), "  POOLS ");
  (void)append_u32(line, position, sizeof(line),
                   diagnostics_gui_snapshot.pool_count);
  diagnostics_small_text_draw(4, y, line, color_table[22]);
  y = (GX_VALUE)(y + 10);

  diagnostics_memory_line_draw(y, "FLASH",
                               diagnostics_gui_snapshot.flash_used,
                               diagnostics_gui_snapshot.flash_free);
  y = (GX_VALUE)(y + 9);
  diagnostics_memory_line_draw(y, "AXI RAM",
                               diagnostics_gui_snapshot.axi_ram_used,
                               diagnostics_gui_snapshot.axi_ram_free);
  y = (GX_VALUE)(y + 9);
  diagnostics_memory_line_draw(y, "SDRAM",
                               diagnostics_gui_snapshot.sdram_reserved,
                               diagnostics_gui_snapshot.sdram_unassigned);
  y = (GX_VALUE)(y + 9);
  diagnostics_memory_line_draw(y, "HEAP",
                               diagnostics_gui_snapshot.heap_total -
                                   diagnostics_gui_snapshot.heap_free,
                               diagnostics_gui_snapshot.heap_free);
  y = (GX_VALUE)(y + 9);
  diagnostics_memory_line_draw(y, "STACK",
                               diagnostics_gui_snapshot.stack_total -
                                   diagnostics_gui_snapshot.stack_free,
                               diagnostics_gui_snapshot.stack_free);
  y = (GX_VALUE)(y + 11);

  for (UINT index = 0U; index < diagnostics_gui_snapshot.pool_count; index++)
  {
    APP_DIAGNOSTICS_POOL *pool = &diagnostics_gui_snapshot.pools[index];
    position = 0U;
    line[0] = '\0';
    position = append_text(line, position, sizeof(line), "P ");
    position = append_text(line, position, sizeof(line), pool->name);
    position = append_text(line, position, sizeof(line), " F/T ");
    position = append_u32(line, position, sizeof(line), pool->free_bytes);
    position = append_text(line, position, sizeof(line), "/");
    position = append_u32(line, position, sizeof(line), pool->total_bytes);
    position = append_text(line, position, sizeof(line), " FR ");
    (void)append_u32(line, position, sizeof(line), pool->fragments);
    diagnostics_small_text_draw(4, y, line, color_table[23]);
    y = (GX_VALUE)(y + 9);
  }

  y = (GX_VALUE)(y + 2);
  diagnostics_small_text_draw(4, y,
                              "TASK STATE PR RUN STACK-FREE/TOTAL",
                              color_table[22]);
  y = (GX_VALUE)(y + 9);

  for (UINT index = 0U;
       (index < diagnostics_gui_snapshot.thread_count) && (y <= 263);
       index++)
  {
    APP_DIAGNOSTICS_THREAD *thread =
        &diagnostics_gui_snapshot.threads[index];
    position = 0U;
    line[0] = '\0';
    position = append_text(line, position, sizeof(line), thread->name);
    position = append_text(line, position, sizeof(line), " ");
    position = append_text(line, position, sizeof(line),
                           diagnostics_state_name(thread->state));
    position = append_text(line, position, sizeof(line), " ");
    position = append_u32(line, position, sizeof(line), thread->priority);
    position = append_text(line, position, sizeof(line), " ");
    position = append_u32(line, position, sizeof(line), thread->run_count);
    position = append_text(line, position, sizeof(line), " ");
    position = append_u32(line, position, sizeof(line), thread->stack_free);
    position = append_text(line, position, sizeof(line), "/");
    (void)append_u32(line, position, sizeof(line), thread->stack_total);
    diagnostics_small_text_draw(4, y, line, color_table[23]);
    y = (GX_VALUE)(y + 9);
  }
}

static void diagnostics_gui_service(void)
{
  ULONG now = tx_time_get();

  if ((diagnostics_gui_next_tick != 0U) &&
      ((LONG)(now - diagnostics_gui_next_tick) < 0))
  {
    return;
  }

  diagnostics_gui_next_tick =
      now + (APP_DIAGNOSTICS_GUI_PERIOD_SECONDS * TX_TIMER_TICKS_PER_SECOND);
  if (App_Diagnostics_Snapshot_Get(&diagnostics_gui_snapshot) == TX_SUCCESS)
  {
    if ((pages[3].gx_widget_status & GX_STATUS_VISIBLE) != 0U)
    {
      gx_system_dirty_mark((GX_WIDGET *)&pages[3]);
    }
  }
}

#endif /* APP_DIAGNOSTICS_GUI_ENABLE */

static void framebuffer_area_copy(uint16_t *destination,
                                  const uint16_t *source,
                                  const GX_RECTANGLE *dirty_area)
{
  int32_t left = 0;
  int32_t top = 0;
  int32_t right = (int32_t)LCD_WIDTH - 1;
  int32_t bottom = (int32_t)LCD_HEIGHT - 1;

  if (dirty_area != GX_NULL)
  {
    left = dirty_area->gx_rectangle_left;
    top = dirty_area->gx_rectangle_top;
    right = dirty_area->gx_rectangle_right;
    bottom = dirty_area->gx_rectangle_bottom;
  }

  if (left < 0)
  {
    left = 0;
  }
  if (top < 0)
  {
    top = 0;
  }
  if (right >= (int32_t)LCD_WIDTH)
  {
    right = (int32_t)LCD_WIDTH - 1;
  }
  if (bottom >= (int32_t)LCD_HEIGHT)
  {
    bottom = (int32_t)LCD_HEIGHT - 1;
  }
  if ((left > right) || (top > bottom))
  {
    return;
  }

  size_t row_size = (size_t)(right - left + 1) * sizeof(uint16_t);
  for (int32_t y = top; y <= bottom; y++)
  {
    size_t offset = ((size_t)y * LCD_WIDTH) + (size_t)left;
    (void)memcpy(&destination[offset], &source[offset], row_size);
  }
}

static VOID display_toggle(GX_CANVAS *canvas_ptr, GX_RECTANGLE *dirty_area)
{
  uint16_t *rendered_buffer = (uint16_t *)canvas_ptr->gx_canvas_memory;
  uint16_t *next_draw_buffer;
  uint32_t timeout = FRAMEBUFFER_VBLANK_TIMEOUT;

  if (rendered_buffer == framebuffer[0])
  {
    next_draw_buffer = framebuffer[1];
  }
  else
  {
    next_draw_buffer = framebuffer[0];
  }

  /* Publish the completely rendered buffer only at vertical blanking. */
  __DSB();
  if (HAL_LTDC_SetAddress_NoReload(&hltdc, (uint32_t)rendered_buffer, 0U) == HAL_OK)
  {
    LTDC->SRCR = LTDC_SRCR_VBR;
    while (((LTDC->SRCR & LTDC_SRCR_VBR) != 0U) && (timeout != 0U))
    {
      timeout--;
    }
  }
  else
  {
    timeout = 0U;
  }

  if (timeout == 0U)
  {
    /* Keep the GUI alive if a VBlank event is ever missed. */
    LTDC_Layer1->CFBAR = (uint32_t)rendered_buffer;
    LTDC->SRCR = LTDC_SRCR_IMR;
    if (display_vblank_timeout_reported == 0U)
    {
      display_vblank_timeout_reported = 1U;
      Debug_Log_Line("[GUIX] LTDC VBlank timeout; immediate swap used");
    }
  }

  /* RFB must snapshot the completed front frame here, not the next GUIX draw
     buffer. The capture module owns immutable SDRAM snapshots, so TCP can be
     slower than the display without racing GUIX or LTDC. */
  if (dirty_area != GX_NULL)
  {
    App_VNC_Frame_Publish(rendered_buffer,
                          dirty_area->gx_rectangle_left,
                          dirty_area->gx_rectangle_top,
                          dirty_area->gx_rectangle_right,
                          dirty_area->gx_rectangle_bottom);
  }
  else
  {
    App_VNC_Frame_Publish(rendered_buffer, 0, 0,
                          (int32_t)LCD_WIDTH - 1,
                          (int32_t)LCD_HEIGHT - 1);
  }

  /* Both buffers must contain the same completed frame before GUIX applies
     the next partial update to the hidden one. */
  framebuffer_area_copy(next_draw_buffer, rendered_buffer, dirty_area);
  canvas_ptr->gx_canvas_memory = (GX_COLOR *)next_draw_buffer;
}

static UINT display_driver_setup(GX_DISPLAY *display_ptr)
{
  _gx_display_driver_565rgb_setup(display_ptr, GX_NULL, display_toggle);
  return GX_SUCCESS;
}

static void show_page(UINT page)
{
  for (UINT index = 0U; index < GUI_PAGE_COUNT; index++)
  {
    if (index == page)
    {
      (void)gx_widget_show((GX_WIDGET *)&pages[index]);
    }
    else
    {
      (void)gx_widget_hide((GX_WIDGET *)&pages[index]);
    }
  }
}

static uint32_t screen_saver_random(void)
{
  uint32_t value = screen_saver_random_state;

  /* Xorshift32 is sufficient here: it is small, fast and only selects visual
     starting positions/directions, not security-related values. */
  value ^= value << 13;
  value ^= value >> 17;
  value ^= value << 5;
  screen_saver_random_state = value;
  return value;
}

static int32_t screen_saver_random_velocity(void)
{
  uint32_t value = screen_saver_random();
  int32_t velocity = (int32_t)(1U + (value % 4U));
  return ((value & 0x10U) != 0U) ? velocity : -velocity;
}

static void screen_saver_randomize_motion(void)
{
  screen_saver_random_state ^=
      HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2() ^
      (uint32_t)tx_time_get() ^ HAL_GetTick();
  if (screen_saver_random_state == 0U)
  {
    screen_saver_random_state = 0x746A5C31U;
  }

  /* Points 0 and 3 are the curve endpoints. Points 1 and 2 are the two
     independently moving Bezier control poles. */
  screen_saver_points[0].x =
      (int32_t)(screen_saver_random() % (LCD_WIDTH / 2U));
  screen_saver_points[3].x =
      (int32_t)(LCD_WIDTH / 2U +
                (screen_saver_random() % (LCD_WIDTH / 2U)));
  screen_saver_points[1].x =
      (int32_t)(screen_saver_random() % LCD_WIDTH);
  screen_saver_points[2].x =
      (int32_t)(screen_saver_random() % LCD_WIDTH);

  for (UINT index = 0U; index < SCREEN_SAVER_POINT_COUNT; index++)
  {
    screen_saver_points[index].y =
        (int32_t)(screen_saver_random() % LCD_HEIGHT);
    screen_saver_points[index].velocity_x =
        screen_saver_random_velocity();
    screen_saver_points[index].velocity_y =
        screen_saver_random_velocity();
  }
}

static void screen_saver_move_point(SCREEN_SAVER_POINT *point)
{
  point->x += point->velocity_x;
  point->y += point->velocity_y;

  if (point->x <= 0)
  {
    point->x = 0;
    point->velocity_x = -point->velocity_x;
  }
  else if (point->x >= (int32_t)(LCD_WIDTH - 1U))
  {
    point->x = (int32_t)(LCD_WIDTH - 1U);
    point->velocity_x = -point->velocity_x;
  }

  if (point->y <= 0)
  {
    point->y = 0;
    point->velocity_y = -point->velocity_y;
  }
  else if (point->y >= (int32_t)(LCD_HEIGHT - 1U))
  {
    point->y = (int32_t)(LCD_HEIGHT - 1U);
    point->velocity_y = -point->velocity_y;
  }
}

static GX_COLOR screen_saver_color_get(void)
{
  return RGB565(0U, 190U, 150U);
}

static GX_COLOR screen_saver_color_fade(GX_COLOR color,
                                        UINT numerator,
                                        UINT denominator)
{
  uint32_t red = (((uint32_t)color >> 11) & 0x1FU) * numerator / denominator;
  uint32_t green = (((uint32_t)color >> 5) & 0x3FU) * numerator / denominator;
  uint32_t blue = ((uint32_t)color & 0x1FU) * numerator / denominator;

  return (GX_COLOR)((red << 11) | (green << 5) | blue);
}

static void screen_saver_trail_push(void)
{
  SCREEN_SAVER_TRAIL_FRAME *frame;

  if (screen_saver_trail_count == 0U)
  {
    screen_saver_trail_head = 0U;
  }
  else
  {
    screen_saver_trail_head =
        (screen_saver_trail_head + 1U) % SCREEN_SAVER_TRAIL_FRAMES;
  }

  frame = &screen_saver_trail[screen_saver_trail_head];
  for (UINT index = 0U; index < SCREEN_SAVER_POINT_COUNT; index++)
  {
    frame->points[index].x = (int16_t)screen_saver_points[index].x;
    frame->points[index].y = (int16_t)screen_saver_points[index].y;
  }
  frame->color = screen_saver_color_get();

  if (screen_saver_trail_count < SCREEN_SAVER_TRAIL_FRAMES)
  {
    screen_saver_trail_count++;
  }
}

static void screen_saver_bezier_draw(const SCREEN_SAVER_TRAIL_FRAME *frame,
                                     GX_COLOR color, UINT width)
{
  const int32_t denominator =
      (int32_t)(SCREEN_SAVER_BEZIER_SEGMENTS *
                SCREEN_SAVER_BEZIER_SEGMENTS *
                SCREEN_SAVER_BEZIER_SEGMENTS);
  GX_VALUE previous_x = (GX_VALUE)frame->points[0].x;
  GX_VALUE previous_y = (GX_VALUE)frame->points[0].y;

  /* Keep this drawing path deliberately simple.  GUIX anti-aliased rounded
     zero-length segments can be very expensive on this target. */
  (void)gx_context_raw_brush_define(color, color, GX_BRUSH_OUTLINE);
  (void)gx_context_brush_width_set(width);

  for (int32_t step = 1;
       step <= (int32_t)SCREEN_SAVER_BEZIER_SEGMENTS; step++)
  {
    int32_t inverse = (int32_t)SCREEN_SAVER_BEZIER_SEGMENTS - step;
    int32_t weight0 = inverse * inverse * inverse;
    int32_t weight1 = 3 * inverse * inverse * step;
    int32_t weight2 = 3 * inverse * step * step;
    int32_t weight3 = step * step * step;
    GX_VALUE x = (GX_VALUE)((weight0 * frame->points[0].x +
                             weight1 * frame->points[1].x +
                             weight2 * frame->points[2].x +
                             weight3 * frame->points[3].x +
                             denominator / 2) / denominator);
    GX_VALUE y = (GX_VALUE)((weight0 * frame->points[0].y +
                             weight1 * frame->points[1].y +
                             weight2 * frame->points[2].y +
                             weight3 * frame->points[3].y +
                             denominator / 2) / denominator);

    if ((x != previous_x) || (y != previous_y))
    {
      (void)gx_canvas_line_draw(previous_x, previous_y, x, y);
    }
    previous_x = x;
    previous_y = y;
  }
}

static VOID screen_saver_draw(GX_WINDOW *window)
{

  /* Rebuild the frame from a black background, then draw stored curves from
     oldest/dimmest to newest/brightest to form a short fading trail. */
  gx_window_background_draw(window);
  for (UINT draw_order = screen_saver_trail_count;
       draw_order > 0U; draw_order--)
  {
    UINT age = draw_order - 1U;
    UINT history_index =
        (screen_saver_trail_head + SCREEN_SAVER_TRAIL_FRAMES - age) %
        SCREEN_SAVER_TRAIL_FRAMES;
    GX_COLOR color = screen_saver_color_fade(
        screen_saver_trail[history_index].color,
        SCREEN_SAVER_TRAIL_FRAMES - age,
        SCREEN_SAVER_TRAIL_FRAMES);

    screen_saver_bezier_draw(&screen_saver_trail[history_index], color,
                             (age == 0U) ? 2U : 1U);
  }
}

static void screen_saver_start(void)
{
  GX_BOOL moved;

  if (screen_saver_active != 0U)
  {
    return;
  }

  screen_saver_active = 1U;
  screen_saver_randomize_motion();
  screen_saver_trail_count = 0U;
  screen_saver_trail_push();
  (void)gx_widget_front_move((GX_WIDGET *)&screen_saver, &moved);
  (void)gx_widget_show((GX_WIDGET *)&screen_saver);
  (void)gx_system_timer_start((GX_WIDGET *)&screen_saver,
                              SCREEN_SAVER_TIMER_ID, 1U,
                              SCREEN_SAVER_ANIMATION_TICKS);
  Debug_Log_Line("[GUIX] screen saver active");
}

static void screen_saver_stop(void)
{
  if (screen_saver_active == 0U)
  {
    return;
  }

  (void)gx_system_timer_stop((GX_WIDGET *)&screen_saver,
                             SCREEN_SAVER_TIMER_ID);
  screen_saver_active = 0U;
  last_user_activity_ticks = tx_time_get();
  (void)gx_widget_hide((GX_WIDGET *)&screen_saver);
  update_status_text();
  Debug_Log_Line("[GUIX] screen saver dismissed");
}

static UINT screen_saver_event_process(GX_WINDOW *window, GX_EVENT *event_ptr)
{
  switch (event_ptr->gx_event_type)
  {
    case GX_EVENT_TIMER:
      if (event_ptr->gx_event_payload.gx_event_timer_id ==
          SCREEN_SAVER_TIMER_ID)
      {
        for (UINT index = 0U; index < SCREEN_SAVER_POINT_COUNT; index++)
        {
          screen_saver_move_point(&screen_saver_points[index]);
        }
        screen_saver_trail_push();
        gx_system_dirty_mark((GX_WIDGET *)window);
        return GX_SUCCESS;
      }
      break;

    case GX_EVENT_PEN_DOWN:
    case GX_EVENT_PEN_DRAG:
      /* Consume the complete wake-up gesture. It must not operate a control
         hidden behind the screen saver. */
      return GX_SUCCESS;

    case GX_EVENT_PEN_UP:
      screen_saver_stop();
      return GX_SUCCESS;

    default:
      break;
  }

  return gx_window_event_process(window, event_ptr);
}

static void apply_slider_event(USHORT sender, INT value)
{
  uint8_t controls[4];
  if (value < 0)
  {
    value = 0;
  }
  if (value > 255)
  {
    value = 255;
  }

  App_State_GetControls(controls);
  if ((sender == ID_STATUS_SLIDER_1) || (sender == ID_CONFIG_SLIDER_1))
  {
    controls[1] = (uint8_t)value;
  }
  else if ((sender == ID_STATUS_SLIDER_2) || (sender == ID_CONFIG_SLIDER_2))
  {
    controls[2] = (uint8_t)value;
  }
  else if ((sender == ID_STATUS_SLIDER_3) || (sender == ID_CONFIG_SLIDER_3))
  {
    controls[3] = (uint8_t)value;
  }
  else if (sender == ID_NET_SLIDER_1)
  {
    controls[0] = (uint8_t)value;
  }
  App_State_SetControls(controls);
}

static UINT page_event_process(GX_WINDOW *window, GX_EVENT *event_ptr)
{
  ULONG event_type = event_ptr->gx_event_type;

  if ((event_type & GX_SIGNAL_EVENT_MASK) == GX_EVENT_SLIDER_VALUE)
  {
    apply_slider_event(event_ptr->gx_event_sender,
                       (INT)event_ptr->gx_event_payload.gx_event_longdata);
    return GX_SUCCESS;
  }

  switch (event_type)
  {
    case GX_SIGNAL(ID_RESET_COUNTERS, GX_EVENT_CLICKED):
      App_State_ResetCounters();
      return GX_SUCCESS;

    case GX_SIGNAL(ID_DEFAULTS, GX_EVENT_CLICKED):
    {
      uint8_t controls[4] = {0U, 64U, 128U, 192U};
      App_State_SetControls(controls);
      for (UINT index = 0U; index < 3U; index++)
      {
        (void)gx_slider_value_set(&config_sliders[index],
                                  &config_sliders[index].gx_slider_info,
                                  controls[index + 1U]);
      }
      return GX_SUCCESS;
    }

    case GX_SIGNAL(ID_APPLY, GX_EVENT_CLICKED):
      /* Slider changes are live; APPLY provides immediate visual sync. */
      update_status_text();
      return GX_SUCCESS;

    case GX_SIGNAL(ID_NET_CLOSE, GX_EVENT_CLICKED):
      App_NetXDuo_RequestDisconnect();
      return GX_SUCCESS;

    case GX_SIGNAL(ID_DHCP_RENEW, GX_EVENT_CLICKED):
      App_NetXDuo_RequestDhcpRenew();
      return GX_SUCCESS;

    default:
      break;
  }

  return gx_window_event_process(window, event_ptr);
}

static UINT root_event_process(GX_WINDOW *window, GX_EVENT *event_ptr)
{
  switch (event_ptr->gx_event_type)
  {
    case GX_SIGNAL(ID_TAB_STATUS, GX_EVENT_CLICKED):
      show_page(0U);
      return GX_SUCCESS;

    case GX_SIGNAL(ID_TAB_CONTROLS, GX_EVENT_CLICKED):
      show_page(1U);
      return GX_SUCCESS;

    case GX_SIGNAL(ID_TAB_NETWORK, GX_EVENT_CLICKED):
      show_page(2U);
      return GX_SUCCESS;

#if (APP_DIAGNOSTICS_GUI_ENABLE != 0U)
    case GX_SIGNAL(ID_TAB_DIAGNOSTICS, GX_EVENT_CLICKED):
      diagnostics_gui_service();
      show_page(3U);
      return GX_SUCCESS;
#endif

    case GX_EVENT_TIMER:
      if (event_ptr->gx_event_payload.gx_event_timer_id == GUI_TIMER_ID)
      {
        if (screen_saver_active == 0U)
        {
          update_status_text();
#if (APP_DIAGNOSTICS_GUI_ENABLE != 0U)
          diagnostics_gui_service();
#endif
          if ((ULONG)(tx_time_get() - last_user_activity_ticks) >=
              SCREEN_SAVER_TIMEOUT_TICKS)
          {
            screen_saver_start();
          }
        }
        return GX_SUCCESS;
      }
      break;

    default:
      break;
  }

  return gx_window_root_event_process((GX_WINDOW_ROOT *)window, event_ptr);
}

static void create_prompt(GX_PROMPT *prompt, GX_WIDGET *parent,
                          GX_VALUE top, GX_VALUE bottom)
{
  GX_RECTANGLE rectangle = make_rect(8, top, 248, bottom);
  (void)gx_prompt_create(prompt, GX_NULL, parent, 0U,
                         GX_STYLE_TEXT_LEFT | GX_STYLE_BORDER_NONE,
                         0U, &rectangle);
}

static void create_slider(GX_SLIDER *slider, GX_WIDGET *parent, USHORT id,
                          GX_VALUE left, GX_VALUE top, GX_VALUE right,
                          INT initial)
{
  GX_RECTANGLE rectangle = make_rect(left, top, right, (GX_VALUE)(top + 28));
  GX_SLIDER_INFO info = {0, 255, initial, 1, 8, 8, 12, 24, 6, 0};
  (void)gx_slider_create(slider, GX_NULL, parent, 5, &info,
                         GX_STYLE_ENABLED | GX_STYLE_SHOW_NEEDLE |
                         GX_STYLE_SHOW_TICKMARKS | GX_STYLE_BORDER_THIN,
                         id, &rectangle);
}

static void create_button(GX_TEXT_BUTTON *button, GX_WIDGET *parent, USHORT id,
                          const char *text, GX_VALUE left, GX_VALUE top,
                          GX_VALUE right, GX_VALUE bottom)
{
  GX_RECTANGLE rectangle = make_rect(left, top, right, bottom);
  (void)gx_text_button_create(button, GX_NULL, parent, 0U,
                              GX_STYLE_ENABLED | GX_STYLE_BORDER_RAISED |
                              GX_STYLE_TEXT_CENTER,
                              id, &rectangle);
  set_button_text(button, text);
}

static void create_interface(void)
{
  GX_RECTANGLE rectangle;
  uint8_t controls[4];
  static const char *labels[8] =
  {
    "Control 1", "Control 2", "Control 3", "Channel A",
    "Channel B", "Channel C", "TCP mode", "Refresh"
  };

  App_State_GetControls(controls);

  rectangle = make_rect(0, 0, 479, 271);
  (void)gx_window_root_create(&root, "F746", &canvas,
                              GX_STYLE_BORDER_NONE, 1U, &rectangle);
  root.gx_widget_event_process_function = (UINT (*)(GX_WIDGET *, GX_EVENT *))root_event_process;

#if (APP_DIAGNOSTICS_GUI_ENABLE != 0U)
  create_button(&tab_buttons[0], (GX_WIDGET *)&root, ID_TAB_STATUS,
                "STATUS", 0, 0, 118, 39);
  create_button(&tab_buttons[1], (GX_WIDGET *)&root, ID_TAB_CONTROLS,
                "CONTROLS", 120, 0, 238, 39);
  create_button(&tab_buttons[2], (GX_WIDGET *)&root, ID_TAB_NETWORK,
                "NETWORK", 240, 0, 358, 39);
  create_button(&tab_buttons[3], (GX_WIDGET *)&root, ID_TAB_DIAGNOSTICS,
                "DIAG", 360, 0, 479, 39);
#else
  create_button(&tab_buttons[0], (GX_WIDGET *)&root, ID_TAB_STATUS,
                "STATUS", 0, 0, 158, 39);
  create_button(&tab_buttons[1], (GX_WIDGET *)&root, ID_TAB_CONTROLS,
                "CONTROLS", 160, 0, 318, 39);
  create_button(&tab_buttons[2], (GX_WIDGET *)&root, ID_TAB_NETWORK,
                "NETWORK", 320, 0, 479, 39);
#endif

  for (UINT index = 0U; index < GUI_PAGE_COUNT; index++)
  {
    rectangle = make_rect(0, 41, 479, 271);
    (void)gx_window_create(&pages[index], GX_NULL, (GX_WIDGET *)&root,
                           GX_STYLE_BORDER_NONE | GX_STYLE_ENABLED,
                           (USHORT)(100U + index), &rectangle);
    pages[index].gx_widget_event_process_function =
        (UINT (*)(GX_WIDGET *, GX_EVENT *))page_event_process;
  }

#if (APP_DIAGNOSTICS_GUI_ENABLE != 0U)
  (void)gx_widget_draw_set((GX_WIDGET *)&pages[3],
                           (VOID (*)(GX_WIDGET *))diagnostics_page_draw);
#endif

  for (UINT index = 0U; index < 8U; index++)
  {
    create_prompt(&status_prompts[index], (GX_WIDGET *)&pages[0],
                  (GX_VALUE)(46 + (index * 27U)),
                  (GX_VALUE)(70 + (index * 27U)));
  }
  create_slider(&status_sliders[0], (GX_WIDGET *)&pages[0], ID_STATUS_SLIDER_1,
                270, 60, 462, controls[1]);
  create_slider(&status_sliders[1], (GX_WIDGET *)&pages[0], ID_STATUS_SLIDER_2,
                270, 120, 462, controls[2]);
  create_slider(&status_sliders[2], (GX_WIDGET *)&pages[0], ID_STATUS_SLIDER_3,
                270, 180, 462, controls[3]);
  create_button(&page_buttons[0], (GX_WIDGET *)&pages[0], ID_RESET_COUNTERS,
                "RESET COUNTERS", 305, 226, 462, 264);

  for (UINT index = 0U; index < 3U; index++)
  {
    rectangle = make_rect(35, (GX_VALUE)(55 + index * 62U),
                          150, (GX_VALUE)(80 + index * 62U));
    (void)gx_prompt_create(&slider_labels[index], GX_NULL,
                           (GX_WIDGET *)&pages[1], 0U,
                           GX_STYLE_TEXT_LEFT | GX_STYLE_BORDER_NONE,
                           0U, &rectangle);
    set_prompt_text(&slider_labels[index], (char *)labels[index]);
    create_slider(&config_sliders[index], (GX_WIDGET *)&pages[1],
                  (USHORT)(ID_CONFIG_SLIDER_1 + index),
                  155, (GX_VALUE)(54 + index * 62U), 445, controls[index + 1U]);
  }
  create_button(&page_buttons[1], (GX_WIDGET *)&pages[1], ID_DEFAULTS,
                "DEFAULTS", 55, 232, 205, 268);
  create_button(&page_buttons[2], (GX_WIDGET *)&pages[1], ID_APPLY,
                "APPLY", 275, 232, 425, 268);

  for (UINT index = 0U; index < 2U; index++)
  {
    rectangle = make_rect(35, (GX_VALUE)(85 + index * 72U),
                          150, (GX_VALUE)(110 + index * 72U));
    (void)gx_prompt_create(&slider_labels[6U + index], GX_NULL,
                           (GX_WIDGET *)&pages[2], 0U,
                           GX_STYLE_TEXT_LEFT | GX_STYLE_BORDER_NONE,
                           0U, &rectangle);
    set_prompt_text(&slider_labels[6U + index], (char *)labels[6U + index]);
    create_slider(&network_sliders[index], (GX_WIDGET *)&pages[2],
                  (USHORT)(ID_NET_SLIDER_1 + index),
                  155, (GX_VALUE)(82 + index * 72U), 445,
                  (index == 0U) ? controls[0] : 100);
  }
  create_button(&page_buttons[3], (GX_WIDGET *)&pages[2], ID_DHCP_RENEW,
                "DHCP RENEW", 55, 232, 205, 268);
  create_button(&page_buttons[4], (GX_WIDGET *)&pages[2], ID_NET_CLOSE,
                "CLOSE CLIENT", 275, 232, 425, 268);

  /* This last root child overlays every tab without changing which page is
     selected. Hiding it therefore restores the exact previous screen. */
  rectangle = make_rect(0, 0, 479, 271);
  (void)gx_window_create(&screen_saver, "Screen saver",
                         (GX_WIDGET *)&root,
                         GX_STYLE_BORDER_NONE | GX_STYLE_ENABLED,
                         200U, &rectangle);
  (void)gx_widget_fill_color_set((GX_WIDGET *)&screen_saver, 21U, 21U, 21U);
  (void)gx_widget_draw_set((GX_WIDGET *)&screen_saver, screen_saver_draw);
  screen_saver.gx_widget_event_process_function =
      (UINT (*)(GX_WIDGET *, GX_EVENT *))screen_saver_event_process;
  (void)gx_widget_hide((GX_WIDGET *)&screen_saver);

  update_status_text();
#if (APP_DIAGNOSTICS_GUI_ENABLE != 0U)
  diagnostics_gui_service();
#endif
  show_page(0U);
  last_user_activity_ticks = tx_time_get();
}

static VOID touch_thread_entry(ULONG argument)
{
  GX_EVENT event = {0};
  uint8_t data[5];
  uint8_t pressed = 0U;
  GX_POINT last = {0, 0};

  (void)argument;
  event.gx_event_display_handle = 0U;
  event.gx_event_target = GX_NULL;

  for (;;)
  {
    if ((HAL_I2C_Mem_Read(&hi2c3, TOUCH_I2C_ADDRESS, TOUCH_STATUS_REGISTER,
                          I2C_MEMADD_SIZE_8BIT, data, sizeof(data), 20U) == HAL_OK) &&
        ((data[0] & 0x0FU) != 0U))
    {
      GX_POINT point;
      uint16_t raw_x = (uint16_t)(((uint16_t)(data[1] & 0x0FU) << 8) | data[2]);
      uint16_t raw_y = (uint16_t)(((uint16_t)(data[3] & 0x0FU) << 8) | data[4]);

      /* The F746 Discovery panel is mounted with X/Y swapped. */
      point.gx_point_x = (GX_VALUE)raw_y;
      point.gx_point_y = (GX_VALUE)raw_x;
      if (point.gx_point_x >= (GX_VALUE)LCD_WIDTH)
      {
        point.gx_point_x = (GX_VALUE)(LCD_WIDTH - 1U);
      }
      if (point.gx_point_y >= (GX_VALUE)LCD_HEIGHT)
      {
        point.gx_point_y = (GX_VALUE)(LCD_HEIGHT - 1U);
      }

      event.gx_event_payload.gx_event_pointdata = point;
      event.gx_event_type = (pressed == 0U) ? GX_EVENT_PEN_DOWN : GX_EVENT_PEN_DRAG;
      if ((pressed == 0U) || (point.gx_point_x != last.gx_point_x) ||
          (point.gx_point_y != last.gx_point_y))
      {
        last_user_activity_ticks = tx_time_get();
        if (event.gx_event_type == GX_EVENT_PEN_DRAG)
        {
          (void)gx_system_event_fold(&event);
        }
        else
        {
          (void)gx_system_event_send(&event);
        }
      }
      pressed = 1U;
      last = point;
    }
    else if (pressed != 0U)
    {
      event.gx_event_type = GX_EVENT_PEN_UP;
      event.gx_event_payload.gx_event_pointdata = last;
      (void)gx_system_event_send(&event);
      pressed = 0U;
    }

    tx_thread_sleep(2U);
  }
}

static UINT App_GUIX_PointerEventPost(GX_EVENT *event)
{
  UINT status;

  if (event->gx_event_type == GX_EVENT_PEN_DRAG)
  {
    /* Keep only the newest drag coordinate if GUIX has not consumed the
       previous one yet.  A mouse can otherwise fill the GUIX event queue. */
    return gx_system_event_fold(event);
  }

  /* PEN_DOWN and especially PEN_UP must not be lost when the queue is
     temporarily full.  The GUIX thread has a higher priority, so a short
     yield lets it consume an event without stalling the VNC connection. */
  for (uint32_t attempt = 0U; attempt < 3U; attempt++)
  {
    status = gx_system_event_send(event);
    if (status == GX_SUCCESS)
    {
      return status;
    }
    tx_thread_sleep(1U);
  }

  return status;
}

void App_GUIX_RemotePointerEvent(uint16_t x, uint16_t y, uint8_t buttons)
{
  GX_EVENT event = {0};
  GX_POINT point;
  uint8_t pressed = ((buttons & 0x01U) != 0U) ? 1U : 0U;
  uint8_t event_posted = 0U;
  UINT status = GX_SUCCESS;

  if (x >= LCD_WIDTH)
  {
    x = LCD_WIDTH - 1U;
  }
  if (y >= LCD_HEIGHT)
  {
    y = LCD_HEIGHT - 1U;
  }

  point.gx_point_x = (GX_VALUE)x;
  point.gx_point_y = (GX_VALUE)y;
  event.gx_event_display_handle = 0U;
  event.gx_event_target = GX_NULL;
  event.gx_event_payload.gx_event_pointdata = point;

  if (pressed != 0U)
  {
    event.gx_event_type = (remote_pointer_pressed == 0U) ?
                          GX_EVENT_PEN_DOWN : GX_EVENT_PEN_DRAG;
    if ((remote_pointer_pressed == 0U) ||
        (point.gx_point_x != remote_pointer_last.gx_point_x) ||
        (point.gx_point_y != remote_pointer_last.gx_point_y))
    {
      status = App_GUIX_PointerEventPost(&event);
      event_posted = 1U;
    }
  }
  else if (remote_pointer_pressed != 0U)
  {
    event.gx_event_type = GX_EVENT_PEN_UP;
    status = App_GUIX_PointerEventPost(&event);
    event_posted = 1U;
  }

  if (status == GX_SUCCESS)
  {
    if (event_posted != 0U)
    {
      last_user_activity_ticks = tx_time_get();
    }
    remote_pointer_pressed = pressed;
    remote_pointer_last = point;
  }
}

void App_GUIX_RemotePointerRelease(void)
{
  if (remote_pointer_pressed != 0U)
  {
    App_GUIX_RemotePointerEvent((uint16_t)remote_pointer_last.gx_point_x,
                                (uint16_t)remote_pointer_last.gx_point_y, 0U);
  }
}

UINT App_GUIX_Init(TX_BYTE_POOL *byte_pool)
{
  UINT status;

  Debug_Log_Line("[GUIX] initialization started");
  if (byte_pool == TX_NULL)
  {
    Debug_Log_Line("[GUIX] error: null byte pool");
    return TX_PTR_ERROR;
  }

  status = gx_system_initialize();
  if (status != GX_SUCCESS)
  {
    Debug_Log_U32("[GUIX] gx_system_initialize failed: ", status);
    return status;
  }
  Debug_Log_Line("[GUIX] system initialized");

  status = gx_display_create(&display, "LTDC RGB565", display_driver_setup,
                             LCD_WIDTH, LCD_HEIGHT);
  if (status != GX_SUCCESS)
  {
    Debug_Log_U32("[GUIX] gx_display_create failed: ", status);
    return status;
  }
  Debug_Log_Line("[GUIX] display created");
  (void)gx_display_color_table_set(&display, color_table, GX_MAX_DEFAULT_COLORS);
  (void)gx_display_font_table_set(&display, font_table, GX_DEFAULT_FONT_COUNT);
  (void)gx_display_pixelmap_table_set(&display, pixelmap_table, GX_DEFAULT_PIXELMAP_COUNT);

  status = gx_canvas_create(&canvas, "Main Canvas", &display, GX_CANVAS_MANAGED,
                            LCD_WIDTH, LCD_HEIGHT, (GX_COLOR *)framebuffer[1],
                            sizeof(framebuffer[1]));
  if (status != GX_SUCCESS)
  {
    Debug_Log_U32("[GUIX] gx_canvas_create failed: ", status);
    return status;
  }
  Debug_Log_Line("[GUIX] canvas created");

  create_interface();
  Debug_Log_Line("[GUIX] widgets created");
  (void)gx_widget_show((GX_WIDGET *)&root);
  (void)gx_canvas_show(&canvas);
  (void)gx_system_timer_start((GX_WIDGET *)&root, GUI_TIMER_ID, 1U,
                              GUI_REFRESH_TICKS);

  if (tx_byte_allocate(byte_pool, (VOID **)&touch_stack, 2048U, TX_NO_WAIT) != TX_SUCCESS)
  {
    Debug_Log_Line("[GUIX] touch stack allocation failed");
    return TX_NO_MEMORY;
  }
  if (tx_thread_create(&touch_thread, "FT5336 touch", touch_thread_entry, 0U,
                       touch_stack, 2048U, 13U, 13U, 2U,
                       TX_AUTO_START) != TX_SUCCESS)
  {
    Debug_Log_Line("[GUIX] touch thread creation failed");
    return TX_THREAD_ERROR;
  }
  Debug_Log_Line("[GUIX] touch thread created");

  HAL_GPIO_WritePin(LCD_DISP_GPIO_Port, LCD_DISP_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_SET);

  status = gx_system_start();
  Debug_Log_U32("[GUIX] gx_system_start returned: ", status);
  return status;
}
