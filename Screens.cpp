#include "Screens.h"
#include "images/AstonLogo.h"
#include "images/MotecLogo.h"
#include "images/jake.h"

LV_IMG_DECLARE(AstonLogo);

// Single main screen
lv_obj_t *main_scr = NULL;
lv_obj_t *boot_scr1 = NULL;

// Test labels - simulating left and right titles
lv_obj_t *test_label_left = NULL;
lv_obj_t *test_label_right = NULL;
lv_obj_t *left_label_value = NULL;
lv_obj_t *right_label_value = NULL;
lv_obj_t *odometer_label = NULL;
lv_obj_t *odometer_value = NULL;
lv_obj_t *trip_meter_label = NULL;
lv_obj_t *trip_meter_value = NULL;

// Reusable style objects
static lv_style_t style_label_title;
static lv_style_t style_label_value;
static bool styles_initialized = false;

// Current screen mode
static uint8_t current_screen_mode = 0; // 0=temp/oil, 1=AFR, 2=pressures, 3=fuel

void init_styles(void) {
  if (styles_initialized) return;
  
  // Style for title labels (28pt white text, rotated)
  lv_style_init(&style_label_title);
  lv_style_set_text_font(&style_label_title, &lv_font_montserrat_28);
  lv_style_set_text_color(&style_label_title, lv_color_make(255, 255, 255));
  lv_style_set_transform_angle(&style_label_title, 900);
  
  // Style for value labels (48pt white text, rotated)
  lv_style_init(&style_label_value);
  lv_style_set_text_font(&style_label_value, &lv_font_montserrat_48);
  lv_style_set_text_color(&style_label_value, lv_color_make(255, 255, 255));
  lv_style_set_transform_angle(&style_label_value, 900);
  
  styles_initialized = true;
}

void boot_scr1_loaded_cb(lv_event_t *e)
{
  /* load default screen after 4000ms */
  lv_screen_load_anim(main_scr, LV_SCR_LOAD_ANIM_NONE, 0, 4000, false);
}

// Helper function to create gauge containers
void create_gauge_containers(lv_obj_t *parent) {
  lv_obj_t *left_gauge = lv_obj_create(parent);
  lv_obj_set_size(left_gauge, 240, 350);
  lv_obj_set_pos(left_gauge, 632, 26);
  lv_obj_set_style_bg_color(left_gauge, lv_color_make(0,0,0), LV_PART_MAIN);
  lv_obj_clear_flag(left_gauge, LV_OBJ_FLAG_SCROLLABLE);
  
  lv_obj_t *right_gauge = lv_obj_create(parent);
  lv_obj_set_size(right_gauge, 240, 348);
  lv_obj_set_pos(right_gauge, 632, 610);
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
  lv_obj_set_pos(aston_img, 700, 50);
  lv_obj_fade_in(aston_img, 1000, 0);

  lv_obj_t *power_label = lv_label_create(boot_scr1);
  lv_label_set_text_static(power_label, "Power");
  lv_obj_set_pos(power_label, 817, 690);
  lv_obj_add_style(power_label, &style_label_title, 0);
  lv_obj_fade_in(power_label, 1000, 0);

  lv_obj_t *beauty_label = lv_label_create(boot_scr1);
  lv_label_set_text_static(beauty_label, "Beauty");
  lv_obj_set_pos(beauty_label, 767, 750);
  lv_obj_add_style(beauty_label, &style_label_title, 0);
  lv_obj_fade_in(beauty_label, 1000, 800);

  lv_obj_t *soul_label = lv_label_create(boot_scr1);
  lv_label_set_text_static(soul_label, "Soul");
  lv_obj_set_pos(soul_label, 717, 810);
  lv_obj_add_style(soul_label, &style_label_title, 0);
  lv_obj_fade_in(soul_label, 1000, 1600);  

  lv_obj_add_event_cb(boot_scr1, boot_scr1_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
}

// create the single main screen with reusable labels
void main_scr_init(void) {
  main_scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(main_scr, lv_color_make(0,0,0), 0);

  create_gauge_containers(main_scr);

  // Two test labels - simulating left and right titles
  test_label_left = lv_label_create(main_scr);
  lv_label_set_text(test_label_left, "ECT °F");
  lv_obj_set_pos(test_label_left, 850, 60);
  lv_obj_add_style(test_label_left, &style_label_title, 0);

  test_label_right = lv_label_create(main_scr);
  lv_label_set_text(test_label_right, "Oil PSI");
  lv_obj_set_pos(test_label_right, 850, 660);
  lv_obj_add_style(test_label_right, &style_label_title, 0);

  // Value labels
  left_label_value = lv_label_create(main_scr);
  lv_label_set_text_static(left_label_value, "  0");
  lv_obj_set_pos(left_label_value, 770, 158);
  lv_obj_add_style(left_label_value, &style_label_value, 0);

  right_label_value = lv_label_create(main_scr);
  lv_label_set_text_static(right_label_value, "  0");
  lv_obj_set_pos(right_label_value, 770, 750);
  lv_obj_add_style(right_label_value, &style_label_value, 0);

  odometer_label = lv_label_create(main_scr);
  lv_label_set_text_static(odometer_label, "Miles");
  lv_obj_set_pos(odometer_label, 680, 60);
  lv_obj_add_style(odometer_label, &style_label_title, 0);

  odometer_value = lv_label_create(main_scr);
  lv_label_set_text_static(odometer_value, "58,625");
  lv_obj_set_pos(odometer_value, 680, 150);
  lv_obj_add_style(odometer_value, &style_label_title, 0);

/*
  trip_meter_label = lv_label_create(main_scr);
  lv_label_set_text_static(trip_meter_label, "Trip");
  lv_obj_set_pos(trip_meter_label, 680, 660);
  lv_obj_add_style(trip_meter_label, &style_label_title, 0);  

  trip_meter_value = lv_label_create(main_scr);
  lv_label_set_text_static(trip_meter_value, "0");
  lv_obj_set_pos(trip_meter_value, 680, 750);
  lv_obj_add_style(trip_meter_value, &style_label_title, 0);    
*/
}

// Test function - cycle through 4 modes with ABBREVIATED text
void update_screen_labels(uint8_t mode) {
  current_screen_mode = mode;
  
  switch (mode) {
    case 0:
      lv_label_set_text(test_label_left, "ECT °F");
      lv_label_set_text(test_label_right, "Oil PSI");
      break;
    case 1:
      lv_label_set_text(test_label_left, "L AFR");
      lv_label_set_text(test_label_right, "R AFR");
      break;
    case 2:
      lv_label_set_text(test_label_left, "MAP");
      lv_label_set_text(test_label_right, "Water PSI");
      break;
    case 3:
      lv_label_set_text(test_label_left, "LS PSI");
      lv_label_set_text(test_label_right, "DI PSI");
      break;
  }
  
  // Reset value labels to 0 when changing modes
  lv_label_set_text(left_label_value, "  0");
  lv_label_set_text(right_label_value, "  0");
  
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

lv_obj_t* get_test_label(void) {
  return test_label_left;
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
