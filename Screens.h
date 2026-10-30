#pragma once
#include <lvgl.h>

// Image declarations
LV_IMG_DECLARE(PeakRecall);
LV_IMG_DECLARE(ClearPeakRecall);

// Screen objects
extern lv_obj_t *main_scr;
extern lv_obj_t *boot_scr1;

// Objects the main sketch updates (created by main_scr_init)
extern lv_obj_t *left_label_value;
extern lv_obj_t *right_label_value;
extern lv_obj_t *odometer_value;
extern lv_obj_t *trip_label;   // "Trip 1" / "Trip 2"
extern lv_obj_t *trip_value;
extern lv_obj_t *cruise_control_img;
extern lv_obj_t *tcs_img;
extern lv_obj_t *launch_img;
extern lv_obj_t *exhaust_bypass_img;
extern lv_obj_t *two_step_img;
extern lv_obj_t *peak_recall_img;
extern lv_obj_t *warning_label_left;
extern lv_obj_t *warning_label_right;
extern lv_obj_t *left_gauge_container;
extern lv_obj_t *right_gauge_container;

// Screen initialization functions
void boot_scr1_init(void);
void main_scr_init(void);
void screens_init(void);
void init_styles(void);

// Set gauge titles and reset values when the screen mode changes
void update_screen_labels(const char *left_title, const char *right_title);

// Callback functions
void on_main_screen_loaded(void);  // Implemented in the main sketch
void boot_scr1_loaded_cb(lv_event_t *e);
void main_scr_loaded_cb(lv_event_t *e);
