#pragma once
#include <lvgl.h>

// Screen objects
extern lv_obj_t *main_scr;
extern lv_obj_t *main_scr2;
extern lv_obj_t *main_scr3;
extern lv_obj_t *main_scr4;
extern lv_obj_t *boot_scr1;

// UI element objects - Screen 1
extern lv_obj_t *coolant_temp_scr1;
extern lv_obj_t *oil_press_scr1;

// UI element objects - Screen 2
extern lv_obj_t *left_afr_scr2;
extern lv_obj_t *right_afr_scr2;

// UI element objects - Screen 3
extern lv_obj_t *map_press_scr3;
extern lv_obj_t *coolant_press_scr3;

// UI element objects - Screen 4
extern lv_obj_t *ls_fuel_press_scr4;
extern lv_obj_t *hs_fuel_press_scr4;

// Screen initialization functions
void boot_scr1_init(void);
void main_scr_init(void);
void main_scr2_init(void);
void main_scr3_init(void);
void main_scr4_init(void);
void screens_init(void);
void init_styles(void);

// Callback functions
void boot_scr1_loaded_cb(lv_event_t *e);
