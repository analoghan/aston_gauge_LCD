/*****************************************************************************
  | File        :   LVGL_Driver.c
  
  | help        : 
    The provided LVGL library file must be installed first
******************************************************************************/
#include "LVGL_Driver.h"
#include "lvgl_private.h"  // lv_draw_buf_handlers_t fields

// Virtual display size (what LVGL uses - smaller to save memory)
#define LVGL_WIDTH  240
#define LVGL_HEIGHT 960

// Physical display offset (where to position the virtual display on the physical panel)
#define DISPLAY_OFFSET_X 632  // ((960 - 120 pixel offset) - (240 display size))
#define DISPLAY_OFFSET_Y 26

// Size of each draw buffer in BYTES (RGB565 = 2 bytes/pixel). LVGL 9's lv_display_set_buffers takes bytes.
#define LVGL_BUF_BYTES ((LVGL_WIDTH * LVGL_HEIGHT * 2) / BUFFER_FACTOR)

static lv_color_t* buf1 = NULL;
static lv_color_t* buf2 = NULL;

// Rotated labels are drawn through ARGB8888 layer buffers (tens of KB each). LVGL allocates
// these from its fixed 64KB pool by default, where they can fail and leave text undrawn.
// Allocate them from PSRAM instead; extra bytes leave room for LVGL to align the pointer.
static void *psram_draw_buf_malloc(size_t size, lv_color_format_t color_format) {
  LV_UNUSED(color_format);
  return heap_caps_malloc(size + LV_DRAW_BUF_ALIGN - 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void psram_draw_buf_free(void *buf) {
  heap_caps_free(buf);
}

/* Flush callback: Transfers LVGL-rendered area to the actual LCD with offset */
void lvgl_flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *color_p) {
  // Add offset to position the virtual display on the physical display
  lcd_add_window(area->x1 + DISPLAY_OFFSET_X, area->x2 + DISPLAY_OFFSET_X, 
                 area->y1 + DISPLAY_OFFSET_Y, area->y2 + DISPLAY_OFFSET_Y, color_p);
  lv_display_flush_ready(disp);
}

/* Initialize LVGL with double buffering and display flushing */
void lvgl_init(void) {
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });  // LVGL needs milliseconds, not RTOS ticks

  lv_draw_buf_handlers_t *handlers = lv_draw_buf_get_handlers();
  handlers->buf_malloc_cb = psram_draw_buf_malloc;
  handlers->buf_free_cb = psram_draw_buf_free;
  
  buf1 = (lv_color_t*)heap_caps_aligned_alloc(32, LVGL_BUF_BYTES, MALLOC_CAP_DMA);
  buf2 = (lv_color_t*)heap_caps_aligned_alloc(32, LVGL_BUF_BYTES, MALLOC_CAP_DMA);
  if (!buf1) {
    printf("LVGL buffer allocation failed!\n");
    return;
  }
  if (!buf2) {
    printf("LVGL second buffer allocation failed, using single buffering\n");
  }

  lv_display_t *disp_drv = lv_display_create(LVGL_WIDTH, LVGL_HEIGHT);

  /* Initialize the draw buffer with double buffering */
  lv_display_set_buffers(disp_drv, buf1, buf2, LVGL_BUF_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);

  /* Set the display resolution to virtual size only - don't set physical resolution */
  lv_display_set_resolution(disp_drv, LVGL_WIDTH, LVGL_HEIGHT);
  // Physical resolution not set - we handle offset in flush callback

  /* Set flush callback */
  lv_display_set_flush_cb(disp_drv, lvgl_flush_callback);
}