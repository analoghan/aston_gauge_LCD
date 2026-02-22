#include "Screens.h"
#include "images/AstonLogo.h"
#include "images/CruiseControl.h"
#include "images/tcs.h"
#include "images/flag.h"
#include "images/ExhaustBypass.h"
#include "images/TwoStep.h"
#include "images/PeakRecall.h"
#include "images/ClearPeakRecall.h"

LV_IMG_DECLARE(AstonLogo);
LV_IMG_DECLARE(CruiseControl);
LV_IMG_DECLARE(tcs);
LV_IMG_DECLARE(flag);
LV_IMG_DECLARE(ExhaustBypass);
LV_IMG_DECLARE(TwoStep);
LV_IMG_DECLARE(PeakRecall);
LV_IMG_DECLARE(ClearPeakRecall);

// Single main screen
lv_obj_t *main_scr = NULL;
lv_obj_t *boot_scr1 = NULL;

// Label objects
lv_obj_t *left_title_label = NULL;
lv_obj_t *right_title_label = NULL;
lv_obj_t *left_label_value = NULL;
lv_obj_t *right_label_value = NULL;
lv_obj_t *odometer_label = NULL;
lv_obj_t *odometer_value = NULL;
lv_obj_t *trip_label = NULL;
lv_obj_t *trip_value = NULL;

// Status icons
lv_obj_t *cruise_control_img = NULL;
lv_obj_t *tcs_img = NULL;
lv_obj_t *launch_img = NULL;
lv_obj_t *exhaust_bypass_img = NULL;
lv_obj_t *two_step_img = NULL;
lv_obj_t *peak_recall_img = NULL;
lv_obj_t *clear_peak_recall_img = NULL;

// Reusable style objects
static lv_style_t style_label_title;
static lv_style_t style_label_value;
static bool styles_initialized = false;

// Current screen mode
static uint8_t current_screen_mode = 0; // 0=temp/oil, 1=AFR, 2=pressures, 3=fuel, 4=ethanol/battery

void init_styles(void) {
  if (styles_initialized) return;
  
  // Style for title labels (28pt custom font, white text, rotated)
  lv_style_init(&style_label_title);
  lv_style_set_text_font(&style_label_title, &aston_28);
  lv_style_set_text_color(&style_label_title, lv_color_make(255, 255, 255));
  lv_style_set_transform_angle(&style_label_title, 900);
  lv_style_set_text_opa(&style_label_title, LV_OPA_COVER); // Full opacity for smoother rendering
  
  // Style for value labels (48pt custom font, white text, rotated)
  lv_style_init(&style_label_value);
  lv_style_set_text_font(&style_label_value, &aston_48);
  lv_style_set_text_color(&style_label_value, lv_color_make(255, 255, 255));
  lv_style_set_transform_angle(&style_label_value, 900);
  lv_style_set_text_opa(&style_label_value, LV_OPA_COVER); // Full opacity for smoother rendering
  
  styles_initialized = true;
}

void boot_scr1_loaded_cb(lv_event_t *e)
{
  /* load default screen after 4000ms */
  lv_screen_load_anim(main_scr, LV_SCR_LOAD_ANIM_NONE, 0, 4000, false);
}

// Callback when main screen is loaded (after boot screen)
void main_scr_loaded_cb(lv_event_t *e)
{
  // Main screen loaded - restore task will handle mode restoration
}

// Helper function to create gauge containers (adjusted for 240px width)
void create_gauge_containers(lv_obj_t *parent) {
  lv_obj_t *left_gauge = lv_obj_create(parent);
  lv_obj_set_size(left_gauge, 238, 348);
  lv_obj_set_pos(left_gauge, 0, 0);
  lv_obj_set_style_bg_color(left_gauge, lv_color_make(0,0,0), LV_PART_MAIN);
  lv_obj_clear_flag(left_gauge, LV_OBJ_FLAG_SCROLLABLE);
  
  lv_obj_t *right_gauge = lv_obj_create(parent);
  lv_obj_set_size(right_gauge, 238, 348);
  lv_obj_set_pos(right_gauge, 0, 582);
  lv_obj_set_style_bg_color(right_gauge, lv_color_make(0,0,0), LV_PART_MAIN);
  lv_obj_clear_flag(right_gauge, LV_OBJ_FLAG_SCROLLABLE);
}

// create the elements on the 1st splash screen
void boot_scr1_init(void)
{
  boot_scr1 = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(boot_scr1, lv_color_make(0,0,0), 0);

  create_gauge_containers(boot_scr1);

  lv_obj_t *aston_img = lv_image_create(boot_scr1);
  lv_image_set_src(aston_img, &AstonLogo);
  lv_obj_set_pos(aston_img, 70, 20);
  lv_obj_fade_in(aston_img, 1000, 0);

  lv_obj_t *power_label = lv_label_create(boot_scr1);
  lv_label_set_text_static(power_label, "Power");
  lv_obj_set_pos(power_label, 185, 640);
  lv_obj_add_style(power_label, &style_label_title, 0);
  lv_obj_fade_in(power_label, 1000, 0);

  lv_obj_t *beauty_label = lv_label_create(boot_scr1);
  lv_label_set_text_static(beauty_label, "Beauty");
  lv_obj_set_pos(beauty_label, 135, 700);
  lv_obj_add_style(beauty_label, &style_label_title, 0);
  lv_obj_fade_in(beauty_label, 1000, 800);

  lv_obj_t *soul_label = lv_label_create(boot_scr1);
  lv_label_set_text_static(soul_label, "Soulless");
  lv_obj_set_pos(soul_label, 85, 755);
  lv_obj_add_style(soul_label, &style_label_title, 0);
  lv_obj_fade_in(soul_label, 1000, 1600);  

  lv_obj_add_event_cb(boot_scr1, boot_scr1_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
}

// Create the single main screen with reusable labels (adjusted for 240x960)
void main_scr_init(void) {
  main_scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(main_scr, lv_color_make(0,0,0), 0);

  create_gauge_containers(main_scr);

  // Title labels for left and right gauges
  left_title_label = lv_label_create(main_scr);
  lv_label_set_text(left_title_label, " ");
  lv_obj_set_pos(left_title_label, 210, 30);
  lv_obj_add_style(left_title_label, &style_label_title, 0);

  right_title_label = lv_label_create(main_scr);
  lv_label_set_text(right_title_label, " ");
  lv_obj_set_pos(right_title_label, 210, 612);
  lv_obj_add_style(right_title_label, &style_label_title, 0);

  //Cruise Control Status Icon
  cruise_control_img = lv_image_create(main_scr);
  lv_image_set_src(cruise_control_img, &CruiseControl);
  lv_obj_set_pos(cruise_control_img, 175, 870);

  //Traction Control Status Icon
  tcs_img = lv_image_create(main_scr);
  lv_image_set_src(tcs_img, &tcs);
  lv_obj_set_pos(tcs_img, 175, 820);

  //Launch Control Status Icon
  launch_img = lv_image_create(main_scr);
  lv_image_set_src(launch_img, &flag);
  lv_obj_set_pos(launch_img, 175, 762);

  //Two Step Status Icon
  two_step_img = lv_image_create(main_scr);
  lv_image_set_src(two_step_img, &TwoStep);
  lv_obj_set_pos(two_step_img, 170, 282);

  //Exhaust Bypass Status Icon
  exhaust_bypass_img = lv_image_create(main_scr);
  lv_image_set_src(exhaust_bypass_img, &ExhaustBypass);
  lv_obj_set_pos(exhaust_bypass_img, 175, 234);

  //Peak Recall Status Icon
  peak_recall_img = lv_image_create(main_scr);
  lv_image_set_src(peak_recall_img, &PeakRecall);
  lv_obj_set_pos(peak_recall_img, 180, 182);

  // Value labels (adjusted for 240px width)
  left_label_value = lv_label_create(main_scr);
  lv_label_set_text_static(left_label_value, "   0");
  lv_obj_set_pos(left_label_value, 130, 125);
  lv_obj_add_style(left_label_value, &style_label_value, 0);

  right_label_value = lv_label_create(main_scr);
  lv_label_set_text_static(right_label_value, "   0");
  lv_obj_set_pos(right_label_value, 130, 715);
  lv_obj_add_style(right_label_value, &style_label_value, 0);

  odometer_label = lv_label_create(main_scr);
  lv_label_set_text_static(odometer_label, "Miles");
  lv_obj_set_pos(odometer_label, 35, 210);
  lv_obj_add_style(odometer_label, &style_label_title, 0);

  odometer_value = lv_label_create(main_scr);
  lv_label_set_text(odometer_value, "0.0");
  lv_obj_set_pos(odometer_value, 35, 80);
  lv_obj_add_style(odometer_value, &style_label_title, 0);

  trip_label = lv_label_create(main_scr);
  lv_label_set_text_static(trip_label, "Trip 1");
  lv_obj_set_pos(trip_label, 35, 682);
  lv_obj_add_style(trip_label, &style_label_title, 0);

  trip_value = lv_label_create(main_scr);
  lv_label_set_text(trip_value, "0.0");
  lv_obj_set_pos(trip_value, 35, 782);
  lv_obj_add_style(trip_value, &style_label_title, 0);
  
  // Add callback for when main screen is loaded
  lv_obj_add_event_cb(main_scr, main_scr_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
}

// Update screen mode labels and reset values
void update_screen_labels(uint8_t mode) {
  current_screen_mode = mode;
  
  switch (mode) {
    case 0:
      lv_label_set_text(left_title_label, "ECT °F");
      lv_label_set_text(right_title_label, "Oil PSI");
      break;
    case 1:
      lv_label_set_text(left_title_label, "Left AFR");
      lv_label_set_text(right_title_label, "Right AFR");
      break;
    case 2:
      lv_label_set_text(left_title_label, "MAP");
      lv_label_set_text(right_title_label, "Speed");
      break;
    case 3:
      lv_label_set_text(left_title_label, "LS Fuel PSI");
      lv_label_set_text(right_title_label, "DI Fuel PSI");
      break;
    case 4:
      lv_label_set_text(left_title_label, "Ethanol %");
      lv_label_set_text(right_title_label, "Battery V");
      break;
  }
  
  // Reset value labels to 0 when changing modes
  lv_label_set_text(left_label_value, "   0");
  lv_label_set_text(right_label_value, "   0");
  
  // Reset colors to white
  lv_obj_set_style_text_color(left_label_value, lv_color_make(255, 255, 255), LV_PART_MAIN);
  lv_obj_set_style_text_color(right_label_value, lv_color_make(255, 255, 255), LV_PART_MAIN);
}

// Get pointers to value labels for updating
lv_obj_t* get_left_value_label(void) {
  return left_label_value;
}

lv_obj_t* get_right_value_label(void) {
  return right_label_value;
}

lv_obj_t* get_left_title_label(void) {
  return left_title_label;
}

lv_obj_t* get_odometer_label(void) {
  return odometer_value;
}

lv_obj_t* get_trip_label(void) {
  return trip_value;
}

lv_obj_t* get_trip_text_label(void) {
  return trip_label;
}

lv_obj_t* get_cruise_icon(void) {
  return cruise_control_img;
}

lv_obj_t* get_tcs_icon(void) {
  return tcs_img;
}

lv_obj_t* get_launch_icon(void) {
  return launch_img;
}

lv_obj_t* get_two_step_icon(void) {
  return two_step_img;
}

lv_obj_t* get_exhaust_bypass_icon(void) {
  return exhaust_bypass_img;
}

lv_obj_t* get_peak_recall_icon(void) {
  return peak_recall_img;
}

uint8_t get_current_screen_mode(void) {
  return current_screen_mode;
}

// build the screens
void screens_init(void) {
  init_styles();  // Initialize reusable styles first
  main_scr_init();
  boot_scr1_init();
  lv_screen_load(boot_scr1);
}
