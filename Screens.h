#pragma once
#include <lvgl.h>

// Screen objects
extern lv_obj_t *main_scr;
extern lv_obj_t *boot_scr1;

// Screen initialization functions
void boot_scr1_init(void);
void main_scr_init(void);
void screens_init(void);
void init_styles(void);

// Screen mode management
void update_screen_labels(uint8_t mode);
lv_obj_t* get_left_value_label(void);
lv_obj_t* get_right_value_label(void);
lv_obj_t* get_test_label(void);
uint8_t get_current_screen_mode(void);

// Callback functions
void boot_scr1_loaded_cb(lv_event_t *e);
