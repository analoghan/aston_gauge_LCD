#pragma once

#include <lvgl.h>
#include <esp_heap_caps.h>
#include "Display_ST7701.h"

#define LCD_WIDTH                     ESP_PANEL_LCD_WIDTH
#define LCD_HEIGHT                    ESP_PANEL_LCD_HEIGHT
#define BUFFER_FACTOR                 5                        // Each draw buffer = 1/BUFFER_FACTOR of the 240x960 screen
                                                                // 5 = ~92KB per buffer, 8 = ~58KB, 10 = ~46KB (DMA RAM)

void lvgl_flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *color_p);
void lvgl_init(void);