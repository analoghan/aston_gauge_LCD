#include "Screens.h"
#include "images/AstonLogo.h"
#include "images/MotecLogo.h"
#include "images/jake.h"

LV_IMG_DECLARE(AstonLogo);
LV_IMG_DECLARE(MotecLogo);
LV_IMG_DECLARE(jake);

// Screen objects
lv_obj_t *main_scr = NULL;
lv_obj_t *main_scr2 = NULL;
lv_obj_t *main_scr3 = NULL;
lv_obj_t *main_scr4 = NULL;
lv_obj_t *boot_scr1 = NULL;

// UI element objects - Screen 1
lv_obj_t *coolant_temp_scr1 = NULL;
lv_obj_t *oil_press_scr1 = NULL;

// UI element objects - Screen 2
lv_obj_t *left_afr_scr2 = NULL;
lv_obj_t *right_afr_scr2 = NULL;

// UI element objects - Screen 3
lv_obj_t *map_press_scr3 = NULL;
lv_obj_t *coolant_press_scr3 = NULL;

// UI element objects - Screen 4
lv_obj_t *ls_fuel_press_scr4 = NULL;
lv_obj_t *hs_fuel_press_scr4 = NULL;

// Reusable style objects
static lv_style_t style_label_title;    // For "Coolant Temp", "Oil Pressure" etc
static lv_style_t style_label_value;    // For the actual values
static bool styles_initialized = false;

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
  lv_screen_load_anim(main_scr, LV_SCR_LOAD_ANIM_FADE_OUT, 200, 4000, true);
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

// create the elements on main screen 1
void main_scr_init(void) {
  main_scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(main_scr, lv_color_make(0,0,0), 0);

  create_gauge_containers(main_scr);

  lv_obj_t *coolant_label = lv_label_create(main_scr);
  lv_label_set_text_static(coolant_label, "Coolant Temp");
  lv_obj_set_pos(coolant_label, 850, 100);
  lv_obj_add_style(coolant_label, &style_label_title, 0);

  coolant_temp_scr1 = lv_label_create(main_scr);
  lv_label_set_text_static(coolant_temp_scr1, "Off");
  lv_obj_set_pos(coolant_temp_scr1, 770, 158);
  lv_obj_add_style(coolant_temp_scr1, &style_label_value, 0);

  lv_obj_t *oil_label = lv_label_create(main_scr);
  lv_label_set_text_static(oil_label, "Oil Pressure");
  lv_obj_set_pos(oil_label, 850, 706);
  lv_obj_add_style(oil_label, &style_label_title, 0);

  oil_press_scr1 = lv_label_create(main_scr);
  lv_label_set_text_static(oil_press_scr1, "Off");
  lv_obj_set_pos(oil_press_scr1, 770, 750);
  lv_obj_add_style(oil_press_scr1, &style_label_value, 0);
}

// create the elements on main screen 2
void main_scr2_init(void) {
  main_scr2 = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(main_scr2, lv_color_make(0,0,0), 0);

  create_gauge_containers(main_scr2);

  lv_obj_t *afr1_label = lv_label_create(main_scr2);
  lv_label_set_text_static(afr1_label, "Left Bank AFR");
  lv_obj_set_pos(afr1_label, 850, 100);
  lv_obj_add_style(afr1_label, &style_label_title, 0);

  left_afr_scr2 = lv_label_create(main_scr2);
  lv_label_set_text_static(left_afr_scr2, "Off");
  lv_obj_set_pos(left_afr_scr2, 770, 158);
  lv_obj_add_style(left_afr_scr2, &style_label_value, 0);

  lv_obj_t *afr2_label = lv_label_create(main_scr2);
  lv_label_set_text_static(afr2_label, "Right Bank AFR");
  lv_obj_set_pos(afr2_label, 850, 706);
  lv_obj_add_style(afr2_label, &style_label_title, 0);

  right_afr_scr2 = lv_label_create(main_scr2);
  lv_label_set_text_static(right_afr_scr2, "Off");
  lv_obj_set_pos(right_afr_scr2, 770, 750);
  lv_obj_add_style(right_afr_scr2, &style_label_value, 0);
}

// build the screens
void screens_init(void) {
  init_styles();  // Initialize reusable styles first
  main_scr_init();
  main_scr2_init();
  main_scr3_init();
  main_scr4_init();
  boot_scr1_init();
  lv_screen_load(boot_scr1);
}

// create the elements on main screen 3 (placeholder)
void main_scr3_init(void) {
  main_scr3 = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(main_scr3, lv_color_make(0,0,0), 0);

  create_gauge_containers(main_scr3);

  lv_obj_t *map_press_label = lv_label_create(main_scr3);
  lv_label_set_text_static(map_press_label, "MAP Pressure");
  lv_obj_set_pos(map_press_label, 850, 100);
  lv_obj_add_style(map_press_label, &style_label_title, 0);

  lv_obj_t *coolant_press_label = lv_label_create(main_scr3);
  lv_label_set_text_static(coolant_press_label, "Coolant Pressure");
  lv_obj_set_pos(coolant_press_label, 850, 706);
  lv_obj_add_style(coolant_press_label, &style_label_title, 0);

  map_press_scr3 = lv_label_create(main_scr3);
  lv_label_set_text_static(map_press_scr3, "Off");
  lv_obj_set_pos(map_press_scr3, 770, 158);
  lv_obj_add_style(map_press_scr3, &style_label_value, 0);

  coolant_press_scr3 = lv_label_create(main_scr3);
  lv_label_set_text_static(coolant_press_scr3, "Off");
  lv_obj_set_pos(coolant_press_scr3, 770, 750);
  lv_obj_add_style(coolant_press_scr3, &style_label_value, 0);
}

// create the elements on main screen 4 (placeholder)
void main_scr4_init(void) {
  main_scr4 = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(main_scr4, lv_color_make(0,0,0), 0);

  create_gauge_containers(main_scr4);

  lv_obj_t *ls_fuel_press_label = lv_label_create(main_scr4);
  lv_label_set_text_static(ls_fuel_press_label, "Low Side Fuel Pressure");
  lv_obj_set_pos(ls_fuel_press_label, 850, 36);
  lv_obj_add_style(ls_fuel_press_label, &style_label_title, 0);

  lv_obj_t *hs_fuel_press_label = lv_label_create(main_scr4);
  lv_label_set_text_static(hs_fuel_press_label, "DI Fuel Pressure");
  lv_obj_set_pos(hs_fuel_press_label, 850, 706);
  lv_obj_add_style(hs_fuel_press_label, &style_label_title, 0);

  ls_fuel_press_scr4 = lv_label_create(main_scr4);
  lv_label_set_text_static(ls_fuel_press_scr4, "Off");
  lv_obj_set_pos(ls_fuel_press_scr4, 770, 158);
  lv_obj_add_style(ls_fuel_press_scr4, &style_label_value, 0);

  hs_fuel_press_scr4 = lv_label_create(main_scr4);
  lv_label_set_text_static(hs_fuel_press_scr4, "Off");
  lv_obj_set_pos(hs_fuel_press_scr4, 770, 750);
  lv_obj_add_style(hs_fuel_press_scr4, &style_label_value, 0);
}
