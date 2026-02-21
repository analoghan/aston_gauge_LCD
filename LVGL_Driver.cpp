/*****************************************************************************
  | File        :   LVGL_Driver.c
  
  | help        : 
    The provided LVGL library file must be installed first
******************************************************************************/
#include "LVGL_Driver.h"

// Virtual display size (what LVGL uses - smaller to save memory)
#define LVGL_WIDTH  240
#define LVGL_HEIGHT 960

// Physical display offset (where to position the virtual display on the physical panel)
#define DISPLAY_OFFSET_X 632  // ((960 - 120 pixel offset) - (240 display size))
#define DISPLAY_OFFSET_Y 26

static lv_color_t* buf1 = (lv_color_t*)heap_caps_aligned_alloc(32, (LVGL_WIDTH * LVGL_HEIGHT * 2) / BUFFER_FACTOR, MALLOC_CAP_DMA);
static lv_color_t* buf2 = (lv_color_t*)heap_caps_aligned_alloc(32, (LVGL_WIDTH * LVGL_HEIGHT * 2) / BUFFER_FACTOR, MALLOC_CAP_DMA);

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
  lv_tick_set_cb(xTaskGetTickCount);
  
  if (!buf1) {
    printf("LVGL buffer allocation failed!\n");
    return;
  }

  lv_display_t *disp_drv = lv_display_create(LVGL_WIDTH, LVGL_HEIGHT);

  /* Initialize the draw buffer */
//  lv_display_set_buffers(disp_drv, buf1, NULL, (LVGL_WIDTH * LVGL_HEIGHT) / BUFFER_FACTOR, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_buffers(disp_drv, buf1, buf2, (LVGL_WIDTH * LVGL_HEIGHT) / BUFFER_FACTOR, LV_DISPLAY_RENDER_MODE_PARTIAL);

  /* Set the display resolution to virtual size */
  lv_display_set_resolution(disp_drv, LVGL_WIDTH, LVGL_HEIGHT);
  lv_display_set_physical_resolution(disp_drv, LCD_WIDTH, LCD_HEIGHT);

//  lv_display_set_rotation(disp_drv, LV_DISP_ROTATION_90);  // RGB displays don't support software rotation

  /* Set flush callback */
  lv_display_set_flush_cb(disp_drv, lvgl_flush_callback);
}