#include "app_vnc_frame.h"

#include "main.h"

#include <stddef.h>
#include <string.h>

#define APP_VNC_CAPTURE_BUFFER_COUNT  3U
#define APP_VNC_FRAME_PIXELS          (APP_VNC_FRAME_WIDTH * APP_VNC_FRAME_HEIGHT)

#if defined(__GNUC__)
#define APP_VNC_SDRAM __attribute__((section(".sdram_data"), aligned(32)))
#else
#define APP_VNC_SDRAM
#endif

/* Three snapshots are intentional. With one client reading an old snapshot,
   the GUI can still publish every later completed frame without ever waiting
   for TCP or writing into the snapshot currently being encoded. */
static uint16_t capture_buffer[APP_VNC_CAPTURE_BUFFER_COUNT]
                              [APP_VNC_FRAME_PIXELS] APP_VNC_SDRAM;
static volatile uint8_t capture_readers[APP_VNC_CAPTURE_BUFFER_COUNT];
static volatile uint8_t capture_published_index;
static volatile uint8_t capture_ready;
static volatile uint32_t capture_generation;

static volatile uint8_t capture_dirty_valid;
static volatile uint16_t capture_dirty_left;
static volatile uint16_t capture_dirty_top;
static volatile uint16_t capture_dirty_right;
static volatile uint16_t capture_dirty_bottom;

static uint32_t App_VNC_Frame_Lock(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static void App_VNC_Frame_Unlock(uint32_t primask)
{
  if (primask == 0U)
  {
    __enable_irq();
  }
}

static void App_VNC_Frame_Dirty_Clamp(int32_t *left,
                                      int32_t *top,
                                      int32_t *right,
                                      int32_t *bottom)
{
  if (*left < 0)
  {
    *left = 0;
  }
  if (*top < 0)
  {
    *top = 0;
  }
  if (*right >= (int32_t)APP_VNC_FRAME_WIDTH)
  {
    *right = (int32_t)APP_VNC_FRAME_WIDTH - 1;
  }
  if (*bottom >= (int32_t)APP_VNC_FRAME_HEIGHT)
  {
    *bottom = (int32_t)APP_VNC_FRAME_HEIGHT - 1;
  }
}

void App_VNC_Frame_Publish(const uint16_t *source,
                           int32_t dirty_left,
                           int32_t dirty_top,
                           int32_t dirty_right,
                           int32_t dirty_bottom)
{
  uint8_t write_index = APP_VNC_CAPTURE_BUFFER_COUNT;
  uint32_t primask;

  if (source == NULL)
  {
    return;
  }

  App_VNC_Frame_Dirty_Clamp(&dirty_left, &dirty_top,
                            &dirty_right, &dirty_bottom);
  if ((dirty_left > dirty_right) || (dirty_top > dirty_bottom))
  {
    dirty_left = 0;
    dirty_top = 0;
    dirty_right = (int32_t)APP_VNC_FRAME_WIDTH - 1;
    dirty_bottom = (int32_t)APP_VNC_FRAME_HEIGHT - 1;
  }

  primask = App_VNC_Frame_Lock();
  for (uint8_t index = 0U; index < APP_VNC_CAPTURE_BUFFER_COUNT; index++)
  {
    if ((index != capture_published_index) &&
        (capture_readers[index] == 0U))
    {
      write_index = index;
      break;
    }
  }
  App_VNC_Frame_Unlock(primask);

  if (write_index >= APP_VNC_CAPTURE_BUFFER_COUNT)
  {
    /* This can only happen if an unsupported second reader holds a snapshot.
       Dropping a capture is preferable to stalling the GUI/LTDC pipeline. */
    return;
  }

  (void)memcpy(capture_buffer[write_index], source,
               sizeof(capture_buffer[write_index]));
  __DMB();

  primask = App_VNC_Frame_Lock();
  capture_published_index = write_index;
  capture_generation++;
  if (capture_generation == 0U)
  {
    capture_generation = 1U;
  }
  capture_ready = 1U;

  if (capture_dirty_valid == 0U)
  {
    capture_dirty_left = (uint16_t)dirty_left;
    capture_dirty_top = (uint16_t)dirty_top;
    capture_dirty_right = (uint16_t)dirty_right;
    capture_dirty_bottom = (uint16_t)dirty_bottom;
    capture_dirty_valid = 1U;
  }
  else
  {
    if ((uint16_t)dirty_left < capture_dirty_left)
    {
      capture_dirty_left = (uint16_t)dirty_left;
    }
    if ((uint16_t)dirty_top < capture_dirty_top)
    {
      capture_dirty_top = (uint16_t)dirty_top;
    }
    if ((uint16_t)dirty_right > capture_dirty_right)
    {
      capture_dirty_right = (uint16_t)dirty_right;
    }
    if ((uint16_t)dirty_bottom > capture_dirty_bottom)
    {
      capture_dirty_bottom = (uint16_t)dirty_bottom;
    }
  }
  App_VNC_Frame_Unlock(primask);
}

uint8_t App_VNC_Frame_Acquire(APP_VNC_FRAME_VIEW *view)
{
  uint8_t index;
  uint32_t primask;

  if (view == NULL)
  {
    return 0U;
  }

  primask = App_VNC_Frame_Lock();
  if (capture_ready == 0U)
  {
    App_VNC_Frame_Unlock(primask);
    return 0U;
  }

  index = capture_published_index;
  if (capture_readers[index] == UINT8_MAX)
  {
    App_VNC_Frame_Unlock(primask);
    return 0U;
  }

  capture_readers[index]++;
  view->pixels = capture_buffer[index];
  view->generation = capture_generation;
  view->dirty_valid = capture_dirty_valid;
  view->dirty_x = capture_dirty_left;
  view->dirty_y = capture_dirty_top;
  if (capture_dirty_valid != 0U)
  {
    view->dirty_width = (uint16_t)(capture_dirty_right -
                                   capture_dirty_left + 1U);
    view->dirty_height = (uint16_t)(capture_dirty_bottom -
                                    capture_dirty_top + 1U);
  }
  else
  {
    view->dirty_width = 0U;
    view->dirty_height = 0U;
  }
  view->buffer_index = index;
  App_VNC_Frame_Unlock(primask);
  return 1U;
}

void App_VNC_Frame_Release(APP_VNC_FRAME_VIEW *view)
{
  uint32_t primask;

  if ((view == NULL) ||
      (view->buffer_index >= APP_VNC_CAPTURE_BUFFER_COUNT))
  {
    return;
  }

  primask = App_VNC_Frame_Lock();
  if (capture_readers[view->buffer_index] != 0U)
  {
    capture_readers[view->buffer_index]--;
  }
  App_VNC_Frame_Unlock(primask);

  view->pixels = NULL;
  view->buffer_index = APP_VNC_CAPTURE_BUFFER_COUNT;
}

void App_VNC_Frame_Acknowledge(uint32_t generation)
{
  uint32_t primask = App_VNC_Frame_Lock();

  if ((capture_ready != 0U) && (capture_generation == generation))
  {
    capture_dirty_valid = 0U;
  }

  App_VNC_Frame_Unlock(primask);
}
