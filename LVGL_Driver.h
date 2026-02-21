#pragma once

#include <lvgl.h>
#include <esp_heap_caps.h>
#include "Display_ST7701.h"

#define LCD_WIDTH                     ESP_PANEL_LCD_WIDTH
#define LCD_HEIGHT                    ESP_PANEL_LCD_HEIGHT
#define BUFFER_FACTOR                 5                        // Larger buffer = smoother rendering (was 10)
                                                                // 5 = ~46KB per buffer, 8 = ~29KB, 10 = ~23KB

void lvgl_flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *color_p);
void lvgl_init(void);