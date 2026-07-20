#ifndef APP_VNC_FRAME_H
#define APP_VNC_FRAME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define APP_VNC_FRAME_WIDTH          480U
#define APP_VNC_FRAME_HEIGHT         272U

typedef struct
{
  const uint16_t *pixels;
  uint32_t generation;
  uint16_t dirty_x;
  uint16_t dirty_y;
  uint16_t dirty_width;
  uint16_t dirty_height;
  uint8_t dirty_valid;
  uint8_t buffer_index;
} APP_VNC_FRAME_VIEW;

/* Called by the GUIX display driver after a completed RGB565 frame has become
   the LTDC front buffer. The source remains owned by GUIX. */
void App_VNC_Frame_Publish(const uint16_t *source,
                           int32_t dirty_left,
                           int32_t dirty_top,
                           int32_t dirty_right,
                           int32_t dirty_bottom);

/* A view stays immutable until it is released. Only one RFB server thread is
   expected to hold a view at a time. */
uint8_t App_VNC_Frame_Acquire(APP_VNC_FRAME_VIEW *view);
void App_VNC_Frame_Release(APP_VNC_FRAME_VIEW *view);

/* Clears accumulated damage only if no newer frame was published meanwhile. */
void App_VNC_Frame_Acknowledge(uint32_t generation);

#ifdef __cplusplus
}
#endif

#endif /* APP_VNC_FRAME_H */
