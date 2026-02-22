#include <Arduino.h>
#include <Preferences.h>
#include "CANBus_Driver.h"
#include "LVGL_Driver.h"
#include "I2C_Driver.h"
#include "Screens.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/queue.h"

// ============================================================================
// GLOBAL VARIABLES AND CONFIGURATION
// ============================================================================

// Preferences object for persistent storage
Preferences preferences;

// CAN Bus Configuration
QueueHandle_t canMsgQueue;
#define CAN_QUEUE_LENGTH 64
#define CAN_QUEUE_ITEM_SIZE sizeof(twai_message_t)
#define TAG "TWAI"

bool can_initiated = false;

// Watchdog tracking
unsigned long last_can_message_time = 0;
unsigned long last_loop_time = 0;
#define CAN_TIMEOUT_MS 5000
#define LOOP_TIMEOUT_MS 1000

// ============================================================================
// CAN DATA STRUCTURES
// ============================================================================

// Thread-safe data structure for passing CAN data to UI
typedef struct {
  uint8_t water_temp;
  uint8_t oil_press;
  uint8_t left_afr;
  uint8_t right_afr;
  uint8_t map_press;
  uint8_t speed;
  uint8_t ls_fuel_press;
  uint8_t hs_fuel_press;
  bool updated_0x551;
  bool updated_0x553;
  bool updated_0x554;
  bool updated_0x555;
  bool screen_change_requested;
  unsigned long last_update_0x551;
  unsigned long last_update_0x553;
  unsigned long last_update_0x554;
  unsigned long last_update_0x555;
} DisplayData;

volatile DisplayData display_data = {0, 0, 0, 0, 0, 0, 0, 0, false, false, false, false, false, 0, 0, 0, 0};
portMUX_TYPE display_data_mutex = portMUX_INITIALIZER_UNLOCKED;

#define CAN_DATA_TIMEOUT_MS 5000 // Reset values after 5 seconds of no updates

// Max value tracking
typedef struct {
  uint8_t water_temp_max;
  uint8_t oil_press_max;
  uint8_t left_afr_max;
  uint8_t right_afr_max;
  uint8_t map_press_max;
  uint8_t speed_max;
  uint8_t ls_fuel_press_max;
  uint8_t hs_fuel_press_max;
} MaxValues;

volatile MaxValues max_values = {0, 0, 0, 0, 0, 0, 0, 0};
portMUX_TYPE max_values_mutex = portMUX_INITIALIZER_UNLOCKED;

// Max recall state
volatile bool max_recall_active = false;
volatile unsigned long max_recall_start_time = 0;
#define MAX_RECALL_DURATION_MS 2000 // Show max values for 2 seconds

// ============================================================================
// PERSISTENT STORAGE
// ============================================================================

// Persistent storage variables
uint32_t odometer_miles = 0;
uint32_t trip_miles = 0;
uint8_t last_screen_mode = 0;
bool boot_complete = false; // Flag to track when boot screen is done
bool restore_mode_pending = false; // Flag to trigger mode restore from main loop

// ============================================================================
// ODOMETER TRACKING
// ============================================================================

// Odometer tracking variables
volatile uint8_t current_speed = 0; // Current speed in MPH
portMUX_TYPE speed_mutex = portMUX_INITIALIZER_UNLOCKED;
volatile bool trip_reset_pending = false; // Flag to trigger trip reset from main loop
volatile bool odometer_display_pending = false; // Flag to trigger odometer display update

// Load values from NVS flash
void load_persistent_data() {
  preferences.begin("gauge", false); // Open in read-only mode
  
  // Load odometer/trip as hundredths of miles (default: 5862500 = 58,625.00 miles)
  odometer_miles = preferences.getUInt("odometer", 5862500);
  trip_miles = preferences.getUInt("trip", 51000); // Default: 510.00 miles
  last_screen_mode = preferences.getUChar("screen_mode", 0); // Default: mode 0
  
  // Check if values need migration (old format was whole miles, new is hundredths)
  // If odometer is less than 100000 (1000 miles), it's likely old format
  if (odometer_miles < 100000 && odometer_miles > 0) {
    Serial.println("Migrating old odometer format to hundredths");
    odometer_miles = odometer_miles * 100; // Convert to hundredths
    trip_miles = trip_miles * 100;
    preferences.end();
    
    // Save migrated values
    preferences.begin("gauge", false);
    preferences.putUInt("odometer", odometer_miles);
    preferences.putUInt("trip", trip_miles);
  }
  
  preferences.end();
  
  Serial.printf("Loaded from NVS: Odometer=%lu, Trip=%lu, Mode=%d\n", 
                odometer_miles, trip_miles, last_screen_mode);
}

// Save values to NVS flash
void save_persistent_data() {
  preferences.begin("gauge", false); // Open in read-write mode
  
  preferences.putUInt("odometer", odometer_miles);
  preferences.putUInt("trip", trip_miles);
  preferences.putUChar("screen_mode", last_screen_mode);
  
  preferences.end();
  
  Serial.printf("Saved to NVS: Odometer=%lu, Trip=%lu, Mode=%d\n", 
                odometer_miles, trip_miles, last_screen_mode);
}

// Periodic save task - saves every 10 seconds if values changed
void periodic_save_task(void *arg) {
  static uint32_t last_odometer = 0;
  static uint32_t last_trip = 0;
  static uint8_t last_mode = 0;
  
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(10000)); // Check every 10 seconds
    
    // Only save if values have changed
    if (odometer_miles != last_odometer || 
        trip_miles != last_trip || 
        last_screen_mode != last_mode) {
      save_persistent_data();
      last_odometer = odometer_miles;
      last_trip = last_trip;
      last_mode = last_screen_mode;
    }
  }
}

// Update odometer/trip display labels
void update_odometer_display() {
  static uint32_t last_displayed_odometer = 0;
  static uint32_t last_displayed_trip = 0;
  
  // Check if labels exist before updating
  lv_obj_t* odo_label = get_odometer_label();
  lv_obj_t* trip_label = get_trip_label();
  
  if (odo_label == NULL || trip_label == NULL) {
    Serial.println("Warning: Odometer/trip labels not ready yet");
    return;
  }
  
  if (odometer_miles != last_displayed_odometer) {
    char text[16];
    // Display as miles with 1 decimal place (stored as hundredths)
    float miles = odometer_miles / 100.0;
    sprintf(text, "%.1f", miles);
    lv_label_set_text(odo_label, text);
    last_displayed_odometer = odometer_miles;
  }
  
  if (trip_miles != last_displayed_trip) {
    char text[16];
    // Display as miles with 1 decimal place (stored as hundredths)
    float miles = trip_miles / 100.0;
    sprintf(text, "%.1f", miles);
    lv_label_set_text(trip_label, text);
    last_displayed_trip = trip_miles;
  }
}

// Process trip reset flag (called from main loop for LVGL safety)
void process_trip_reset() {
  if (trip_reset_pending) {
    trip_reset_pending = false;
    trip_miles = 0;
    update_odometer_display();
    save_persistent_data(); // Save immediately
    Serial.println("Trip meter reset");
  }
}

// Task to restore screen mode after boot screen completes
void restore_screen_mode_task(void *arg) {
  // Wait for boot screen to complete (4 seconds + small buffer)
  vTaskDelay(pdMS_TO_TICKS(4100));
  
  // Set flag to trigger mode restore from main loop (LVGL-safe)
  restore_mode_pending = true;
  boot_complete = true;
  
  Serial.printf("Boot complete, will restore mode to %d\n", last_screen_mode);
  
  // Wait a bit more for main screen to fully load, then trigger odometer display update
  vTaskDelay(pdMS_TO_TICKS(200));
  odometer_display_pending = true;
  
  // Task is done, delete itself
  vTaskDelete(NULL);
}

// Task to calculate distance traveled and update odometer/trip
void odometer_update_task(void *arg) {
  const int SAMPLES_PER_SECOND = 10; // Sample every 100ms
  uint32_t speed_samples[SAMPLES_PER_SECOND];
  int sample_index = 0;
  float accumulated_distance = 0.0; // Track fractional miles
  
  while (1) {
    // Sample speed every 100ms
    for (int i = 0; i < SAMPLES_PER_SECOND; i++) {
      portENTER_CRITICAL(&speed_mutex);
      speed_samples[i] = current_speed;
      portEXIT_CRITICAL(&speed_mutex);
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Calculate average speed over the last second
    uint32_t speed_sum = 0;
    for (int i = 0; i < SAMPLES_PER_SECOND; i++) {
      speed_sum += speed_samples[i];
    }
    float avg_speed_mph = speed_sum / (float)SAMPLES_PER_SECOND;
    
    // Calculate distance traveled in this second
    // Distance = Speed * Time
    // Speed is in MPH, time is 1 second = 1/3600 hour
    float distance_miles = avg_speed_mph / 3600.0;
    
    // Accumulate distance
    accumulated_distance += distance_miles;
    
    // Update odometer and trip when we've accumulated at least 0.01 miles
    if (accumulated_distance >= 0.01) {
      uint32_t miles_to_add = (uint32_t)(accumulated_distance * 100); // Convert to hundredths
      odometer_miles += miles_to_add;
      trip_miles += miles_to_add;
      accumulated_distance -= (miles_to_add / 100.0);
      
      // Trigger display update from main loop (LVGL-safe)
      // Only set flag if not already pending to avoid overwhelming the system
      if (!odometer_display_pending) {
        odometer_display_pending = true;
      }
    }
  }
}

void drivers_init(void) {
  i2c_init();

  Serial.println("Scanning for TCA9554...");
  bool found = false;
  for (int attempt = 0; attempt < 10; attempt++) {
    if (i2c_scan_address(0x20)) {
      found = true;
      break;
    }
    delay(50);
  }

  if (!found) {
    Serial.println("TCA9554 not detected! Skipping expander init.");
  } else {
    tca9554pwr_init(0x00);
  }
  lcd_init();
  lvgl_init();
}

void delayed_can_init_task(void *arg)
{
  Serial.println("Delayed CAN init task started, waiting 5 seconds...");
  vTaskDelay(pdMS_TO_TICKS(5000));
  Serial.println("5 seconds passed, initializing CANbus");
  canbus_init();
  can_initiated = true;
  Serial.println("CANbus initialized, flag set");
  vTaskDelete(NULL);
}

void watchdog_task(void *arg) {
  vTaskDelay(pdMS_TO_TICKS(10000));
  
  Serial.println("Watchdog task started");
  
  while (1) {
    unsigned long now = millis();
    
    if (now - last_loop_time > LOOP_TIMEOUT_MS) {
      Serial.printf("WARNING: Main loop appears frozen! Last run: %lu ms ago\n", now - last_loop_time);
    }
    
    if (can_initiated && last_can_message_time > 0) {
      if (now - last_can_message_time > CAN_TIMEOUT_MS) {
        Serial.println("WARNING: No CAN messages received recently.");
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void receive_can_task(void *arg) {
  while (!can_initiated) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  
  Serial.println("CAN receive task starting...");
  
  uint32_t alerts;
  uint32_t msg_count = 0;
  uint32_t overflow_count = 0;
  uint32_t last_stats_time = millis();
  
  while (1) {
    twai_read_alerts(&alerts, 0);
    if (alerts & TWAI_ALERT_RX_QUEUE_FULL) {
      overflow_count++;
      twai_message_t dummy;
      while (twai_receive(&dummy, 0) == ESP_OK) {}
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    
    if (alerts & TWAI_ALERT_BUS_ERROR) {
      Serial.println("CAN bus error, recovering...");
      twai_initiate_recovery();
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    
    twai_message_t message;
    esp_err_t err = twai_receive(&message, pdMS_TO_TICKS(10));
    
    if (err == ESP_OK) {
      msg_count++;
      last_can_message_time = millis();
      
      portENTER_CRITICAL(&display_data_mutex);
      switch (message.identifier) {
        case 0x550:
          if (message.data[0] == 1) {
            // Trip meter reset requested - set flag for main loop
            trip_reset_pending = true;
          }
          break;
        case 0x551:
          display_data.water_temp = message.data[0];
          display_data.oil_press = message.data[1];
          display_data.updated_0x551 = true;
          display_data.last_update_0x551 = millis();
          
          // Update max values
          portENTER_CRITICAL(&max_values_mutex);
          if (message.data[0] > max_values.water_temp_max) max_values.water_temp_max = message.data[0];
          if (message.data[1] > max_values.oil_press_max) max_values.oil_press_max = message.data[1];
          portEXIT_CRITICAL(&max_values_mutex);
          break;
        case 0x552:
          if (message.data[0] == 1) {
            display_data.screen_change_requested = true;
          }
          break;
        case 0x553:
          display_data.left_afr = message.data[0];
          display_data.right_afr = message.data[1];
          display_data.updated_0x553 = true;
          display_data.last_update_0x553 = millis();
          
          // Update max values
          portENTER_CRITICAL(&max_values_mutex);
          if (message.data[0] > max_values.left_afr_max) max_values.left_afr_max = message.data[0];
          if (message.data[1] > max_values.right_afr_max) max_values.right_afr_max = message.data[1];
          portEXIT_CRITICAL(&max_values_mutex);
          break;
        case 0x554:
          display_data.map_press = message.data[0];
          display_data.speed = message.data[1];
          display_data.updated_0x554 = true;
          display_data.last_update_0x554 = millis();
          
          // Update max values
          portENTER_CRITICAL(&max_values_mutex);
          if (message.data[0] > max_values.map_press_max) max_values.map_press_max = message.data[0];
          if (message.data[1] > max_values.speed_max) max_values.speed_max = message.data[1];
          portEXIT_CRITICAL(&max_values_mutex);
          
          // Update current speed for odometer tracking
          portENTER_CRITICAL(&speed_mutex);
          current_speed = message.data[1];
          portEXIT_CRITICAL(&speed_mutex);
          break;
        case 0x555:
          display_data.ls_fuel_press = message.data[0];
          display_data.hs_fuel_press = message.data[1];
          display_data.updated_0x555 = true;
          display_data.last_update_0x555 = millis();
          
          // Update max values
          portENTER_CRITICAL(&max_values_mutex);
          if (message.data[0] > max_values.ls_fuel_press_max) max_values.ls_fuel_press_max = message.data[0];
          if (message.data[1] > max_values.hs_fuel_press_max) max_values.hs_fuel_press_max = message.data[1];
          portEXIT_CRITICAL(&max_values_mutex);
          break;
        case 0x556:
          // Max recall requested
          max_recall_active = true;
          max_recall_start_time = millis();
          Serial.println("Max recall activated");
          break;
        case 0x557:
          // Reset max values
          portENTER_CRITICAL(&max_values_mutex);
          max_values.water_temp_max = 0;
          max_values.oil_press_max = 0;
          max_values.left_afr_max = 0;
          max_values.right_afr_max = 0;
          max_values.map_press_max = 0;
          max_values.speed_max = 0;
          max_values.ls_fuel_press_max = 0;
          max_values.hs_fuel_press_max = 0;
          portEXIT_CRITICAL(&max_values_mutex);
          Serial.println("Max values reset");
          break;
      }
      portEXIT_CRITICAL(&display_data_mutex);
      
      if (msg_count % 10 == 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
      }
      
      if (millis() - last_stats_time > 10000) {
        Serial.printf("CAN: %lu msgs, %lu overflows\n", msg_count, overflow_count);
        last_stats_time = millis();
        msg_count = 0;
      }
      
    } else if (err == ESP_ERR_TIMEOUT) {
      vTaskDelay(pdMS_TO_TICKS(5));
    } else {
      Serial.printf("CAN RX error: %s\n", esp_err_to_name(err));
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

// Helper function to format value with right padding for 4 total characters
// Examples: "5   ", "42  ", "123 ", "-5  ", "-42 ", "123 "
void format_value_with_padding(char* buffer, int value) {
  char temp[8];
  sprintf(temp, "%d", value);
  int len = strlen(temp);
  
  // Copy the number
  strcpy(buffer, temp);
  
  // Add padding spaces to reach 4 characters total
  for (int i = len; i < 4; i++) {
    buffer[i] = ' ';
  }
  buffer[4] = '\0';
}

void update_display_values(uint8_t mode, uint8_t left_val, uint8_t right_val) {
  static int last_left = -999;
  static int last_right = -999;
  static uint8_t last_left_r = 255, last_left_g = 255, last_left_b = 255;
  static uint8_t last_right_r = 255, last_right_g = 255, last_right_b = 255;
  static uint8_t last_mode = 255;
  
  // Reset cached values when mode changes
  if (mode != last_mode) {
    last_left = -999;
    last_right = -999;
    last_left_r = 255; last_left_g = 255; last_left_b = 255;
    last_right_r = 255; last_right_g = 255; last_right_b = 255;
    last_mode = mode;
  }
  
  lv_obj_t *left_label = get_left_value_label();
  lv_obj_t *right_label = get_right_value_label();
  
  int left_processed, right_processed;
  uint8_t left_r = 255, left_g = 255, left_b = 255;
  uint8_t right_r = 255, right_g = 255, right_b = 255;
  
  switch (mode) {
    case 0: // Coolant temp and oil pressure
      left_processed = left_val - 40;
      right_processed = right_val;
      
      // Coolant temp colors
      if (left_processed < 100) {
        left_r = 0; left_g = 0; left_b = 255;
      } else if (left_processed >= 210) {
        left_r = 255; left_g = 0; left_b = 0;
      }
      
      // Oil pressure colors
      if (right_processed < 20) {
        right_r = 255; right_g = 0; right_b = 0;
      }
      break;
      
    case 1: // AFR
      left_processed = left_val;
      right_processed = right_val;
      
      // AFR colors (green for 12-16 range)
      if (left_processed < 12 || left_processed > 16) {
        left_r = 255; left_g = 0; left_b = 0;
      } else {
        left_r = 0; left_g = 255; left_b = 0;
      }
      
      if (right_processed < 12 || right_processed > 16) {
        right_r = 255; right_g = 0; right_b = 0;
      } else {
        right_r = 0; right_g = 255; right_b = 0;
      }
      break;
      
    case 2: // MAP and Speed
    case 3: // Fuel
      left_processed = left_val;
      right_processed = right_val;
      break;
  }
  
  // Update left value
  if (left_processed != last_left) {
    char text[8];
    format_value_with_padding(text, left_processed);
    lv_label_set_text(left_label, text);
    last_left = left_processed;
  }
  
  // Update left color
  if (left_r != last_left_r || left_g != last_left_g || left_b != last_left_b) {
    lv_obj_set_style_text_color(left_label, lv_color_make(left_r, left_g, left_b), LV_PART_MAIN);
    last_left_r = left_r;
    last_left_g = left_g;
    last_left_b = left_b;
  }
  
  // Update right value
  if (right_processed != last_right) {
    char text[8];
    format_value_with_padding(text, right_processed);
    lv_label_set_text(right_label, text);
    last_right = right_processed;
  }
  
  // Update right color
  if (right_r != last_right_r || right_g != last_right_g || right_b != last_right_b) {
    lv_obj_set_style_text_color(right_label, lv_color_make(right_r, right_g, right_b), LV_PART_MAIN);
    last_right_r = right_r;
    last_right_g = right_g;
    last_right_b = right_b;
  }
}

void update_display_from_can_data(void) {
  static unsigned long last_update_time = 0;
  const unsigned long UPDATE_INTERVAL_MS = 100;
  
  unsigned long now = millis();
  
  // Rate limit data updates
  if (now - last_update_time < UPDATE_INTERVAL_MS) {
    return;
  }
  
  // Check for mode change request
  bool mode_change_requested = false;
  portENTER_CRITICAL(&display_data_mutex);
  if (display_data.screen_change_requested) {
    display_data.screen_change_requested = false;
    mode_change_requested = true;
  }
  portEXIT_CRITICAL(&display_data_mutex);
  
  // Cycle through modes - EXACTLY like the real implementation
  if (mode_change_requested) {
    uint8_t new_mode = (get_current_screen_mode() + 1) % 4;
    update_screen_labels(new_mode);
    last_screen_mode = new_mode; // Save for persistence
    Serial.printf("Changed to mode %d\n", new_mode);
  }
  
  // Check if max recall is active and if it should expire
  if (max_recall_active && (now - max_recall_start_time >= MAX_RECALL_DURATION_MS)) {
    max_recall_active = false;
    Serial.println("Max recall deactivated");
  }
  
  // Get data based on current mode
  uint8_t left_val = 0, right_val = 0;
  bool has_update = false;
  uint8_t mode = get_current_screen_mode();
  unsigned long now_time = millis();
  
  portENTER_CRITICAL(&display_data_mutex);
  
  // Check for timeouts and reset values if no updates received
  if (now_time - display_data.last_update_0x551 > CAN_DATA_TIMEOUT_MS && display_data.last_update_0x551 > 0) {
    display_data.water_temp = 40; // Set to 40 so it displays as 0 after -40 offset
    display_data.oil_press = 0;
    display_data.updated_0x551 = true; // Force update to show 0
  }
  if (now_time - display_data.last_update_0x553 > CAN_DATA_TIMEOUT_MS && display_data.last_update_0x553 > 0) {
    display_data.left_afr = 0;
    display_data.right_afr = 0;
    display_data.updated_0x553 = true;
  }
  if (now_time - display_data.last_update_0x554 > CAN_DATA_TIMEOUT_MS && display_data.last_update_0x554 > 0) {
    display_data.map_press = 0;
    display_data.speed = 0;
    display_data.updated_0x554 = true;
    
    // Also reset current_speed for odometer tracking
    portENTER_CRITICAL(&speed_mutex);
    current_speed = 0;
    portEXIT_CRITICAL(&speed_mutex);
  }
  if (now_time - display_data.last_update_0x555 > CAN_DATA_TIMEOUT_MS && display_data.last_update_0x555 > 0) {
    display_data.ls_fuel_press = 0;
    display_data.hs_fuel_press = 0;
    display_data.updated_0x555 = true;
  }
  
  switch (mode) {
    case 0:
      if (display_data.updated_0x551 || max_recall_active) {
        if (max_recall_active) {
          portENTER_CRITICAL(&max_values_mutex);
          left_val = max_values.water_temp_max;
          right_val = max_values.oil_press_max;
          portEXIT_CRITICAL(&max_values_mutex);
        } else {
          left_val = display_data.water_temp;
          right_val = display_data.oil_press;
        }
        display_data.updated_0x551 = false;
        has_update = true;
      }
      break;
    case 1:
      if (display_data.updated_0x553 || max_recall_active) {
        if (max_recall_active) {
          portENTER_CRITICAL(&max_values_mutex);
          left_val = max_values.left_afr_max;
          right_val = max_values.right_afr_max;
          portEXIT_CRITICAL(&max_values_mutex);
        } else {
          left_val = display_data.left_afr;
          right_val = display_data.right_afr;
        }
        display_data.updated_0x553 = false;
        has_update = true;
      }
      break;
    case 2:
      if (display_data.updated_0x554 || max_recall_active) {
        if (max_recall_active) {
          portENTER_CRITICAL(&max_values_mutex);
          left_val = max_values.map_press_max;
          right_val = max_values.speed_max;
          portEXIT_CRITICAL(&max_values_mutex);
        } else {
          left_val = display_data.map_press;
          right_val = display_data.speed;
        }
        display_data.updated_0x554 = false;
        has_update = true;
      }
      break;
    case 3:
      if (display_data.updated_0x555 || max_recall_active) {
        if (max_recall_active) {
          portENTER_CRITICAL(&max_values_mutex);
          left_val = max_values.ls_fuel_press_max;
          right_val = max_values.hs_fuel_press_max;
          portEXIT_CRITICAL(&max_values_mutex);
        } else {
          left_val = display_data.ls_fuel_press;
          right_val = display_data.hs_fuel_press;
        }
        display_data.updated_0x555 = false;
        has_update = true;
      }
      break;
  }
  portEXIT_CRITICAL(&display_data_mutex);
  
  if (has_update) {
    update_display_values(mode, left_val, right_val);
    last_update_time = now;
  }
}

void setup(void) {
  Serial.begin(115200);
  delay(100);
  Serial.println("Setup starting...");
  
  // Load persistent data from NVS
  load_persistent_data();
  
  drivers_init();  
  set_backlight(40);  
  screens_init();
  
  // Update odometer/trip display with loaded values before boot screen shows
  delay(50); // Small delay to ensure LVGL is ready
  update_odometer_display();
  
  set_exio(EXIO_PIN4, Low);
  
  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("Reset reason: %d\n", reason);

  canMsgQueue = xQueueCreate(CAN_QUEUE_LENGTH, CAN_QUEUE_ITEM_SIZE);
  if (canMsgQueue == NULL) {
    ESP_LOGE(TAG, "Failed to create CAN message queue");
    while (1) vTaskDelay(1000);
  }

  xTaskCreatePinnedToCore(receive_can_task, "RX_CAN", 4096, NULL, 2, NULL, 1);  
  xTaskCreatePinnedToCore(delayed_can_init_task, "Init_CAN", 2048, NULL, 1, NULL, 1);  
  xTaskCreatePinnedToCore(watchdog_task, "Watchdog", 2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(periodic_save_task, "Save_NVS", 2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(restore_screen_mode_task, "Restore_Mode", 2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(odometer_update_task, "Odometer", 4096, NULL, 1, NULL, 0);
  
  // Don't update display here - will be done after boot screen in restore task
  
  Serial.println("Setup complete");
}

void loop(void) {
  static unsigned long last_lvgl_time = 0;
  const unsigned long LVGL_INTERVAL_MS = 16; // Run LVGL at ~60Hz
  
  last_loop_time = millis();
  unsigned long now = millis();
  
  // Check if we need to restore screen mode after boot
  if (restore_mode_pending) {
    restore_mode_pending = false;
    Serial.printf("Restoring screen mode to %d\n", last_screen_mode);
    update_screen_labels(last_screen_mode);
  }
  
  // Check if odometer display update is pending
  if (odometer_display_pending) {
    odometer_display_pending = false;
    update_odometer_display();
  }
  
  // Check if trip reset was requested
  process_trip_reset();
  
  // Run LVGL timer handler at consistent intervals
  if (now - last_lvgl_time >= LVGL_INTERVAL_MS) {
    lv_timer_handler();
    last_lvgl_time = now;
  }
  
  // Update data values
  update_display_from_can_data();
  
  // Small delay to prevent CPU hogging, but don't block LVGL timing
  vTaskDelay(pdMS_TO_TICKS(1));
}