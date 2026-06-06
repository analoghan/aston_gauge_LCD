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
#define TAG "TWAI"

bool can_initiated = false;
unsigned long last_can_message_time = 0;
unsigned long last_loop_time = 0;
#define CAN_TIMEOUT_MS 5000
#define LOOP_TIMEOUT_MS 1000

// TCA9554 P5-P8 Input Monitoring
volatile uint8_t tca_inputs_last_state = 0xFF;  // Assume all pulled high initially
unsigned long last_tca_check = 0;
#define TCA_CHECK_INTERVAL_MS 50  // Check every 50ms

// Status Icon Control
volatile bool cruise_active = false;
volatile bool tcs_active = false;
volatile bool launch_active = false;
volatile bool two_step_active = false;
volatile bool exhaust_bypass_active = false;
volatile unsigned long last_cruise_time = 0;
volatile unsigned long last_tcs_time = 0;
volatile unsigned long last_launch_time = 0;
volatile unsigned long last_two_step_time = 0;
volatile unsigned long last_exhaust_bypass_time = 0;
volatile bool icons_startup_shown = false;
#define ICON_TIMEOUT_MS 500  // Hide icons after 500ms of no CAN data

// ECU Warning state (from 0x64C)
volatile uint8_t ecu_warning_flags = 0; // Bitmask of active warnings
volatile unsigned long last_warning_time = 0;
#define WARNING_TIMEOUT_MS 500 // Clear warning after 500ms of no updates

// Warning flag bit positions
#define WARN_FUEL_PRESSURE    (1 << 0)
#define WARN_CRANKCASE_PRESS  (1 << 1)
#define WARN_OIL_PRESSURE     (1 << 2)
#define WARN_OIL_TEMP         (1 << 3)
#define WARN_ENGINE_SPEED     (1 << 4)
#define WARN_COOLANT_PRESSURE (1 << 5)
#define WARN_COOLANT_TEMP     (1 << 6)
#define WARN_KNOCK            (1 << 7)

// ============================================================================
// CAN DATA STRUCTURES
// ============================================================================

// Thread-safe data structure for passing CAN data to UI
// Raw values stored as received from M1 ECU (pre-scaling)
typedef struct {
  // 0x649: Coolant temp (B0, x1 -40 offset, °C), Oil temp (B2), Battery volts (B5, x0.1 V)
  uint8_t  coolant_temp_raw;    // x1, -40 offset → °C
  uint8_t  battery_volts_raw;   // x0.1 → V
  bool     updated_0x649;
  unsigned long last_update_0x649;

  // 0x644: Engine oil pressure (B4-5, x0.1 kPa)
  uint16_t oil_press_raw;       // x0.1 kPa → PSI (/6.895)
  bool     updated_0x644;
  unsigned long last_update_0x644;

  // 0x651: Lambda Bank 1 (B2, x0.01 LA), Lambda Bank 2 (B3, x0.01 LA)
  uint8_t  lambda_bank1_raw;    // x0.01 → AFR (x14.7)
  uint8_t  lambda_bank2_raw;    // x0.01 → AFR (x14.7)
  bool     updated_0x651;
  unsigned long last_update_0x651;

  // 0x640: Engine speed (B0-1, x1 RPM), MAP (B2-3, x0.1 kPa)
  uint16_t map_raw;             // x0.1 kPa → PSI (/6.895)
  bool     updated_0x640;
  unsigned long last_update_0x640;

  // 0x659: Vehicle speed (B4-5, x0.1 km/h)
  uint16_t speed_raw;           // x0.1 km/h → MPH (/1.60934)
  bool     updated_0x659;
  unsigned long last_update_0x659;

  // 0x641: Low side fuel pressure (B4-5, x0.1 kPa)
  uint16_t ls_fuel_press_raw;   // x0.1 kPa → PSI (/6.895)
  bool     updated_0x641;
  unsigned long last_update_0x641;

  // 0x641: Fuel Injector Primary Duty Cycle (B6, x1 %)
  uint8_t  inj_duty_cycle_raw;  // x1 → %
  bool     updated_0x641_duty;
  unsigned long last_update_0x641_duty;

  // 0x670: Ethanol/fuel composition (B5, x1 %)
  uint8_t  ethanol_pct_raw;     // x1 → %
  bool     updated_0x670;
  unsigned long last_update_0x670;
} DisplayData;

volatile DisplayData display_data = {};
SemaphoreHandle_t display_data_mutex = NULL;

#define CAN_DATA_TIMEOUT_MS 500 // Reset values after 500ms of no updates

// Max value tracking (same types as DisplayData raw values)
typedef struct {
  uint8_t  coolant_temp_max;
  uint16_t oil_press_max;
  uint8_t  lambda_bank1_max;
  uint8_t  lambda_bank2_max;
  uint16_t map_max;
  uint16_t speed_max;
  uint16_t ls_fuel_press_max;
  uint8_t  inj_duty_cycle_max;
  uint8_t  ethanol_pct_max;
  uint8_t  battery_volts_max;
} MaxValues;

volatile MaxValues max_values = {};

// Max recall state
volatile bool max_recall_active = false;
volatile bool max_clear_active = false; // Track if we're clearing (vs recalling)
volatile unsigned long max_recall_start_time = 0;
volatile unsigned long max_recall_button_press_start = 0; // When button was first pressed
volatile bool max_recall_button_held = false; // Is the button currently held
volatile bool max_recall_cleared_this_press = false; // Already cleared during this hold
#define MAX_RECALL_DISPLAY_MS 2000 // Show max values for 2 seconds after release
#define MAX_CLEAR_HOLD_MS 3000 // Hold 3 seconds to clear

// ============================================================================
// PERSISTENT STORAGE
// ============================================================================

// Persistent storage variables
uint32_t odometer_miles = 0;
uint32_t trip_miles = 0;
uint32_t trip2_miles = 0;
uint8_t last_screen_mode = 0;
uint8_t current_trip_display = 1; // 1 or 2, always starts at 1
bool restore_mode_pending = false; // Flag to trigger mode restore from main loop

// ============================================================================
// ODOMETER TRACKING
// ============================================================================

// Odometer tracking variables
volatile uint8_t current_speed = 0; // Current speed in MPH
portMUX_TYPE speed_mutex = portMUX_INITIALIZER_UNLOCKED;
volatile bool trip_reset_pending = false; // Flag to trigger trip reset from main loop
volatile bool trip_switch_pending = false; // Flag to trigger trip display switch from main loop
volatile bool odometer_display_pending = false; // Flag to trigger odometer display update

// Load values from NVS flash
void load_persistent_data() {
  preferences.begin("gauge", false); // Open in read-only mode
  
  // Load odometer/trip as hundredths of miles (default: 5862500 = 58,625.00 miles)
  odometer_miles = preferences.getUInt("odometer", 5862500);
  trip_miles = preferences.getUInt("trip", 51000); // Default: 510.00 miles
  trip2_miles = preferences.getUInt("trip2", 0); // Default: 0.00 miles
  last_screen_mode = preferences.getUChar("screen_mode", 0); // Default: mode 0
  
  // Check if values need migration (old format was whole miles, new is hundredths)
  // If odometer is less than 100000 (1000 miles), it's likely old format
  if (odometer_miles < 100000 && odometer_miles > 0) {
    Serial.println("Migrating old odometer format to hundredths");
    odometer_miles = odometer_miles * 100; // Convert to hundredths
    trip_miles = trip_miles * 100;
    trip2_miles = trip2_miles * 100;
    preferences.end();
    
    // Save migrated values
    preferences.begin("gauge", false);
    preferences.putUInt("odometer", odometer_miles);
    preferences.putUInt("trip", trip_miles);
    preferences.putUInt("trip2", trip2_miles);
  }
  
  preferences.end();
  
  Serial.printf("Loaded from NVS: Odometer=%lu, Trip1=%lu, Trip2=%lu, Mode=%d\n", 
                odometer_miles, trip_miles, trip2_miles, last_screen_mode);
}

// Save values to NVS flash
void save_persistent_data() {
  preferences.begin("gauge", false); // Open in read-write mode
  
  preferences.putUInt("odometer", odometer_miles);
  preferences.putUInt("trip", trip_miles);
  preferences.putUInt("trip2", trip2_miles);
  preferences.putUChar("screen_mode", last_screen_mode);
  
  preferences.end();
  
  /*
  Serial.printf("Saved to NVS: Odometer=%lu, Trip1=%lu, Trip2=%lu, Mode=%d\n", 
                odometer_miles, trip_miles, trip2_miles, last_screen_mode);
  */
}

// Periodic save task - saves every 10 seconds if values changed
void periodic_save_task(void *arg) {
  static uint32_t last_odometer = 0;
  static uint32_t last_trip = 0;
  static uint32_t last_trip2 = 0;
  static uint8_t last_mode = 0;
  
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(10000)); // Check every 10 seconds
    
    // Only save if values have changed
    if (odometer_miles != last_odometer || 
        trip_miles != last_trip ||
        trip2_miles != last_trip2 ||
        last_screen_mode != last_mode) {
      save_persistent_data();
      last_odometer = odometer_miles;
      last_trip = trip_miles;
      last_trip2 = trip2_miles;
      last_mode = last_screen_mode;
    }
  }
}

// Update odometer/trip display labels
void update_odometer_display() {
  static uint32_t last_displayed_odometer = 0;
  static uint32_t last_displayed_trip1 = 0;
  static uint32_t last_displayed_trip2 = 0;
  static uint8_t last_displayed_trip_num = 0;
  
  // Check if labels exist before updating
  lv_obj_t* odo_label = get_odometer_label();
  lv_obj_t* trip_label = get_trip_label();
  lv_obj_t* trip_text_label = get_trip_text_label();
  
  if (odo_label == NULL || trip_label == NULL || trip_text_label == NULL) {
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
  
  // Check if trip display changed
  bool trip_changed = (current_trip_display != last_displayed_trip_num);
  
  // Update trip text label if trip number changed
  if (trip_changed) {
    if (current_trip_display == 1) {
      lv_label_set_text(trip_text_label, "Trip 1");
    } else {
      lv_label_set_text(trip_text_label, "Trip 2");
    }
    last_displayed_trip_num = current_trip_display;
  }
  
  // Always update trip value if trip changed, or if the value changed
  if (current_trip_display == 1) {
    if (trip_changed || trip_miles != last_displayed_trip1) {
      char text[16];
      float miles = trip_miles / 100.0;
      sprintf(text, "%.1f", miles);
      lv_label_set_text(trip_label, text);
      last_displayed_trip1 = trip_miles;
    }
  } else {
    if (trip_changed || trip2_miles != last_displayed_trip2) {
      char text[16];
      float miles = trip2_miles / 100.0;
      sprintf(text, "%.1f", miles);
      lv_label_set_text(trip_label, text);
      last_displayed_trip2 = trip2_miles;
    }
  }
}

// Process trip reset flag (called from main loop for LVGL safety)
void process_trip_reset() {
  if (trip_reset_pending) {
    trip_reset_pending = false;
    
    // Reset only the currently displayed trip
    if (current_trip_display == 1) {
      trip_miles = 0;
      Serial.println("Trip 1 meter reset");
    } else {
      trip2_miles = 0;
      Serial.println("Trip 2 meter reset");
    }
    
    update_odometer_display();
    save_persistent_data(); // Save immediately
  }
}

// Process trip switch flag (called from main loop for LVGL safety)
void process_trip_switch() {
  if (trip_switch_pending) {
    trip_switch_pending = false;
    
    // Toggle between trip 1 and trip 2
    current_trip_display = (current_trip_display == 1) ? 2 : 1;
    Serial.printf("Switched to Trip %d\n", current_trip_display);
    
    update_odometer_display();
  }
}

// Task to restore screen mode after boot screen completes
void restore_screen_mode_task(void *arg) {
  // Wait for boot screen to complete (4 seconds + small buffer)
  vTaskDelay(pdMS_TO_TICKS(4100));
  
  // Set flag to trigger mode restore from main loop (LVGL-safe)
  restore_mode_pending = true;
  
  Serial.printf("Boot complete, will restore mode to %d\n", last_screen_mode);
  
  // Wait a bit more for main screen to fully load, then trigger odometer display update
  vTaskDelay(pdMS_TO_TICKS(200));
  odometer_display_pending = true;
  
  // Show icons for 2 seconds on startup
  icons_startup_shown = true;
  vTaskDelay(pdMS_TO_TICKS(2000));
  icons_startup_shown = false;
  
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
      trip2_miles += miles_to_add;
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
    // Initialize TCA9554 with P5-P8 as inputs (bits 4-7 = 1), others as output (0)
    // Binary: 1111 0000 = 0xF0
    tca9554pwr_init(0xF0);
    Serial.println("TCA9554 initialized with P5-P8 as inputs");
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
      unsigned long now_msg = millis(); // Capture time once, outside any critical section
      
      xSemaphoreTake(display_data_mutex, portMAX_DELAY);
      switch (message.identifier) {
        // ---- M1 ECU native messages ----

        case 0x640: {
          // Inlet_Manifold_Pressure: bit 23|16@0+ = B2-B3
          uint16_t map_raw = ((uint16_t)message.data[2] << 8) | message.data[3];
          display_data.map_raw = map_raw;
          display_data.updated_0x640 = true;
          display_data.last_update_0x640 = now_msg;
          if (map_raw > max_values.map_max) max_values.map_max = map_raw;
          break;
        }

        case 0x641: {
          // Fuel_Pressure_Sensor: bit 39|16@0+ = B4-B5, x0.1 kPa
          uint16_t ls_raw = ((uint16_t)message.data[4] << 8) | message.data[5];
          display_data.ls_fuel_press_raw = ls_raw;
          display_data.updated_0x641 = true;
          display_data.last_update_0x641 = now_msg;
          if (ls_raw > max_values.ls_fuel_press_max) max_values.ls_fuel_press_max = ls_raw;
          // Fuel_Injector_Primary_Duty_Cycle: bit 55|8@0+ = B6, x1 %
          uint8_t duty_raw = message.data[6];
          display_data.inj_duty_cycle_raw = duty_raw;
          display_data.updated_0x641_duty = true;
          display_data.last_update_0x641_duty = now_msg;
          if (duty_raw > max_values.inj_duty_cycle_max) max_values.inj_duty_cycle_max = duty_raw;
          break;
        }

        case 0x644: {
          // Engine_Oil_Pressure: bit 55|16@0+ = B6-B7, x0.1 kPa
          uint16_t op_raw = ((uint16_t)message.data[6] << 8) | message.data[7];
          display_data.oil_press_raw = op_raw;
          display_data.updated_0x644 = true;
          display_data.last_update_0x644 = now_msg;
          if (op_raw > max_values.oil_press_max) max_values.oil_press_max = op_raw;
          break;
        }

        case 0x649: {
          display_data.coolant_temp_raw = message.data[0];
          display_data.battery_volts_raw = message.data[5];
          display_data.updated_0x649 = true;
          display_data.last_update_0x649 = now_msg;
          if (message.data[0] > max_values.coolant_temp_max) max_values.coolant_temp_max = message.data[0];
          if (message.data[5] > max_values.battery_volts_max) max_values.battery_volts_max = message.data[5];
          break;
        }

        case 0x651: {
          display_data.lambda_bank1_raw = message.data[2];
          display_data.lambda_bank2_raw = message.data[3];
          display_data.updated_0x651 = true;
          display_data.last_update_0x651 = now_msg;
          if (message.data[2] > max_values.lambda_bank1_max) max_values.lambda_bank1_max = message.data[2];
          if (message.data[3] > max_values.lambda_bank2_max) max_values.lambda_bank2_max = message.data[3];
          break;
        }

        case 0x659: {
          uint16_t spd_raw = ((uint16_t)message.data[4] << 8) | message.data[5];
          display_data.speed_raw = spd_raw;
          display_data.updated_0x659 = true;
          display_data.last_update_0x659 = now_msg;
          if (spd_raw > max_values.speed_max) max_values.speed_max = spd_raw;
          break;
        }

        case 0x670: {
          display_data.ethanol_pct_raw = message.data[5];
          display_data.updated_0x670 = true;
          display_data.last_update_0x670 = now_msg;
          if (message.data[5] > max_values.ethanol_pct_max) max_values.ethanol_pct_max = message.data[5];
          break;
        }

        case 0x178: {
          // Peak recall button: byte 1, bit 6 (1 = pressed, 0 = released)
          bool button_now = (message.data[1] >> 6) & 0x01;
          
          if (button_now && !max_recall_button_held) {
            // Button just pressed - start tracking
            max_recall_button_held = true;
            max_recall_button_press_start = now_msg;
            max_recall_cleared_this_press = false;
            // Immediately show max recall
            max_recall_active = true;
            max_clear_active = false;
            max_recall_start_time = now_msg;
          } else if (button_now && max_recall_button_held) {
            // Button still held - check for 3-second clear threshold
            if (!max_recall_cleared_this_press && 
                (now_msg - max_recall_button_press_start >= MAX_CLEAR_HOLD_MS)) {
              // 3 seconds held - clear max values for current screen
              uint8_t current_mode = get_current_screen_mode();
              switch (current_mode) {
                case 0:
                  max_values.coolant_temp_max = 0;
                  max_values.oil_press_max = 0;
                  break;
                case 1:
                  max_values.lambda_bank1_max = 0;
                  max_values.lambda_bank2_max = 0;
                  break;
                case 2:
                  max_values.map_max = 0;
                  max_values.speed_max = 0;
                  break;
                case 3:
                  max_values.ls_fuel_press_max = 0;
                  max_values.inj_duty_cycle_max = 0;
                  break;
                case 4:
                  max_values.ethanol_pct_max = 0;
                  max_values.battery_volts_max = 0;
                  break;
              }
              max_clear_active = true;
              max_recall_start_time = now_msg;
              max_recall_cleared_this_press = true;
            }
            // Keep max_recall_active while held
            max_recall_active = true;
            max_recall_start_time = now_msg;
          } else if (!button_now && max_recall_button_held) {
            // Button released - show max recall for 2 more seconds then fade
            max_recall_button_held = false;
            max_recall_start_time = now_msg;
            // max_recall_active stays true, will expire via MAX_RECALL_DISPLAY_MS
          }
          break;
        }

        case 0x64E: {
          if ((message.data[3] >> 5) & 0x01) {
            launch_active = true;
            last_launch_time = now_msg;
          }
          if ((message.data[3] >> 4) & 0x01) {
            tcs_active = true;
            last_tcs_time = now_msg;
          }
          break;
        }

        case 0x650: {
          if ((message.data[7] >> 6) & 0x01) {
            two_step_active = true;
            last_two_step_time = now_msg;
          }
          break;
        }

        case 0x64C: {
          // ECU Warning flags from M1
          // Byte 5 (data[5]): bit0=Fuel_Pressure, bit1=Crankcase_Pressure,
          //   bit3=Oil_Pressure, bit4=Oil_Temp, bit5=Engine_Speed,
          //   bit6=Coolant_Pressure, bit7=Coolant_Temp
          // Byte 6 (data[6]): bit7=Knock
          uint8_t flags = 0;
          uint8_t b5 = message.data[5];
          if (b5 & (1 << 0)) flags |= WARN_FUEL_PRESSURE;
          if (b5 & (1 << 1)) flags |= WARN_CRANKCASE_PRESS;
          if (b5 & (1 << 3)) flags |= WARN_OIL_PRESSURE;
          if (b5 & (1 << 4)) flags |= WARN_OIL_TEMP;
          if (b5 & (1 << 5)) flags |= WARN_ENGINE_SPEED;
          if (b5 & (1 << 6)) flags |= WARN_COOLANT_PRESSURE;
          if (b5 & (1 << 7)) flags |= WARN_COOLANT_TEMP;
          if (message.data[6] & (1 << 7)) flags |= WARN_KNOCK;
          ecu_warning_flags = flags;
          last_warning_time = now_msg;
          break;
        }

        case 0x6A8: {
          if (message.data[3]) {
            cruise_active = true;
            last_cruise_time = now_msg;
          }
          if (message.data[5]) {
            exhaust_bypass_active = true;
            last_exhaust_bypass_time = now_msg;
          }
          break;
        }
      }
      xSemaphoreGive(display_data_mutex);

      // Update current_speed outside the critical section (avoids nested lock with speed_mutex)
      if (message.identifier == 0x659) {
        uint16_t spd_raw = ((uint16_t)message.data[4] << 8) | message.data[5];
        portENTER_CRITICAL(&speed_mutex);
        current_speed = (uint8_t)(spd_raw * 0.1f / 1.60934f);
        portEXIT_CRITICAL(&speed_mutex);
      }

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

// Scale and format a float value for display (1 decimal place)
void format_float_value(char* buffer, float value) {
  // Right-align in 5 chars to accommodate decimals e.g. " 14.7"
  char temp[10];
  sprintf(temp, "%.1f", value);
  int len = strlen(temp);
  int padding = 5 - len;
  if (padding < 0) padding = 0;
  int i;
  for (i = 0; i < padding; i++) buffer[i] = ' ';
  strcpy(buffer + padding, temp);
}

// Helper function to format integer value with leading padding (4 chars, right-aligned)
void format_value_with_padding(char* buffer, int value) {
  char temp[8];
  sprintf(temp, "%d", value);
  int len = strlen(temp);
  int padding = 4 - len;
  if (padding < 0) padding = 0;
  int i;
  for (i = 0; i < padding; i++) buffer[i] = ' ';
  strcpy(buffer + padding, temp);
}

void update_display_values(uint8_t mode, float left_val, float right_val) {
  static float last_left = -9999;
  static float last_right = -9999;
  static uint8_t last_left_r = 255, last_left_g = 255, last_left_b = 255;
  static uint8_t last_right_r = 255, last_right_g = 255, last_right_b = 255;
  static uint8_t last_mode = 255;
  
  if (mode != last_mode) {
    last_left = -9999;
    last_right = -9999;
    last_left_r = 255; last_left_g = 255; last_left_b = 255;
    last_right_r = 255; last_right_g = 255; last_right_b = 255;
    last_mode = mode;
  }
  
  lv_obj_t *left_label = get_left_value_label();
  lv_obj_t *right_label = get_right_value_label();
  
  uint8_t left_r = 255, left_g = 255, left_b = 255;
  uint8_t right_r = 255, right_g = 255, right_b = 255;
  
  switch (mode) {
    case 0: // Coolant temp (°F) and oil pressure (PSI)
      // Coolant: blue if cold (<100°F), red if hot (≥210°F)
      if (left_val > 0 && left_val < 100)       { left_r = 0;   left_g = 0;   left_b = 255; }
      else if (left_val >= 210)                  { left_r = 255; left_g = 0;   left_b = 0;   }
      // Oil: red if low (<20 PSI)
      if (right_val > 0 && right_val < 20)       { right_r = 255; right_g = 0; right_b = 0;  }
      break;
      
    case 1: // AFR (Lambda × 14.7) - green 12-16, red otherwise
      if (left_val > 0) {
        if (left_val < 12 || left_val > 16)      { left_r = 255; left_g = 0;   left_b = 0;   }
        else                                      { left_r = 0;   left_g = 255; left_b = 0;   }
      }
      if (right_val > 0) {
        if (right_val < 12 || right_val > 16)    { right_r = 255; right_g = 0; right_b = 0;  }
        else                                      { right_r = 0;   right_g = 255; right_b = 0;}
      }
      break;
      
    case 2: // MAP (PSI) and Speed (MPH) - no color thresholds
      break;
    case 3: // LS Fuel PSI and Injector Duty %
      // Fuel pressure: red if low (<40 PSI) while engine is running (value > 0)
      if (left_val > 0 && left_val < 40)         { left_r = 255; left_g = 0;   left_b = 0;   }
      // Injector duty cycle: red if high (≥85%)
      if (right_val >= 85)                        { right_r = 255; right_g = 0; right_b = 0;  }
      break;
    case 4: // Ethanol (%) and Battery (V)
      break;
  }
  
  // Update left label
  if (left_val != last_left) {
    char text[10];
    // Integer display for ECT, LS Fuel PSI, Ethanol %
    if (mode == 0 || mode == 3 || mode == 4) {
      format_value_with_padding(text, (int)left_val);
    } else {
      format_float_value(text, left_val);
    }
    lv_label_set_text(left_label, text);
    last_left = left_val;
  }
  if (left_r != last_left_r || left_g != last_left_g || left_b != last_left_b) {
    lv_obj_set_style_text_color(left_label, lv_color_make(left_r, left_g, left_b), LV_PART_MAIN);
    last_left_r = left_r; last_left_g = left_g; last_left_b = left_b;
  }
  
  // Update right label
  if (right_val != last_right) {
    char text[10];
    // Integer display for Oil PSI, Speed, DI Fuel PSI
    if (mode == 0 || mode == 2 || mode == 3) {
      format_value_with_padding(text, (int)right_val);
    } else {
      format_float_value(text, right_val);
    }
    lv_label_set_text(right_label, text);
    last_right = right_val;
  }
  if (right_r != last_right_r || right_g != last_right_g || right_b != last_right_b) {
    lv_obj_set_style_text_color(right_label, lv_color_make(right_r, right_g, right_b), LV_PART_MAIN);
    last_right_r = right_r; last_right_g = right_g; last_right_b = right_b;
  }
}

void update_display_from_can_data(void) {
  static unsigned long last_update_time = 0;
  const unsigned long UPDATE_INTERVAL_MS = 100;
  
  unsigned long now = millis();
  if (now - last_update_time < UPDATE_INTERVAL_MS) return;
  
  // Expire max recall (only when button is not held)
  if (max_recall_active && !max_recall_button_held && 
      (now - max_recall_start_time >= MAX_RECALL_DISPLAY_MS)) {
    max_recall_active = false;
    max_clear_active = false;
  }
  
  float left_val = 0.0f, right_val = 0.0f;
  bool has_update = false;
  uint8_t mode = get_current_screen_mode();

  // --- Snapshot display_data under mutex, no nested locks ---
  xSemaphoreTake(display_data_mutex, portMAX_DELAY);

  // Timeout checks
  if (display_data.last_update_0x649 > 0 && now - display_data.last_update_0x649 > CAN_DATA_TIMEOUT_MS) {
    display_data.coolant_temp_raw = 40;
    display_data.battery_volts_raw = 0;
    display_data.updated_0x649 = true;
  }
  if (display_data.last_update_0x644 > 0 && now - display_data.last_update_0x644 > CAN_DATA_TIMEOUT_MS) {
    display_data.oil_press_raw = 0;
    display_data.updated_0x644 = true;
  }
  if (display_data.last_update_0x651 > 0 && now - display_data.last_update_0x651 > CAN_DATA_TIMEOUT_MS) {
    display_data.lambda_bank1_raw = 0;
    display_data.lambda_bank2_raw = 0;
    display_data.updated_0x651 = true;
  }
  if (display_data.last_update_0x640 > 0 && now - display_data.last_update_0x640 > CAN_DATA_TIMEOUT_MS) {
    display_data.map_raw = 0;
    display_data.updated_0x640 = true;
  }
  if (display_data.last_update_0x659 > 0 && now - display_data.last_update_0x659 > CAN_DATA_TIMEOUT_MS) {
    display_data.speed_raw = 0;
    display_data.updated_0x659 = true;
    current_speed = 0; // speed_mutex not needed here - main loop only
  }
  if (display_data.last_update_0x641 > 0 && now - display_data.last_update_0x641 > CAN_DATA_TIMEOUT_MS) {
    display_data.ls_fuel_press_raw = 0;
    display_data.updated_0x641 = true;
  }
  if (display_data.last_update_0x641_duty > 0 && now - display_data.last_update_0x641_duty > CAN_DATA_TIMEOUT_MS) {
    display_data.inj_duty_cycle_raw = 0;
    display_data.updated_0x641_duty = true;
  }
  if (display_data.last_update_0x670 > 0 && now - display_data.last_update_0x670 > CAN_DATA_TIMEOUT_MS) {
    display_data.ethanol_pct_raw = 0;
    display_data.updated_0x670 = true;
  }

  // Snapshot the values we need for this screen
  switch (mode) {
    case 0:
      if (display_data.updated_0x649 || display_data.updated_0x644 || max_recall_active) {
        left_val  = display_data.coolant_temp_raw;
        right_val = display_data.oil_press_raw;
        display_data.updated_0x649 = false;
        display_data.updated_0x644 = false;
        has_update = true;
      }
      break;
    case 1:
      if (display_data.updated_0x651 || max_recall_active) {
        left_val  = display_data.lambda_bank1_raw;
        right_val = display_data.lambda_bank2_raw;
        display_data.updated_0x651 = false;
        has_update = true;
      }
      break;
    case 2:
      if (display_data.updated_0x640 || display_data.updated_0x659 || max_recall_active) {
        left_val  = display_data.map_raw;
        right_val = display_data.speed_raw;
        display_data.updated_0x640 = false;
        display_data.updated_0x659 = false;
        has_update = true;
      }
      break;
    case 3:
      if (display_data.updated_0x641 || display_data.updated_0x641_duty || max_recall_active) {
        left_val  = display_data.ls_fuel_press_raw;
        right_val = display_data.inj_duty_cycle_raw;
        display_data.updated_0x641 = false;
        display_data.updated_0x641_duty = false;
        has_update = true;
      }
      break;
    case 4:
      if (display_data.updated_0x670 || display_data.updated_0x649 || max_recall_active) {
        left_val  = display_data.ethanol_pct_raw;
        right_val = display_data.battery_volts_raw;
        display_data.updated_0x670 = false;
        display_data.updated_0x649 = false;
        has_update = true;
      }
      break;
  }

  xSemaphoreGive(display_data_mutex);
  // --- End of critical section ---

  if (!has_update) return;

  // Apply scaling outside the mutex - read max_values directly (volatile, no lock needed)
  if (max_recall_active) {
    switch (mode) {
      case 0:
        left_val  = ((max_values.coolant_temp_max - 40) * 9.0f / 5.0f) + 32.0f;
        right_val = max_values.oil_press_max * 0.1f / 6.895f;
        break;
      case 1:
        left_val  = max_values.lambda_bank1_max * 0.01f * 14.7f;
        right_val = max_values.lambda_bank2_max * 0.01f * 14.7f;
        break;
      case 2:
        left_val  = (max_values.map_max * 0.1f - 105.0f) / 6.895f;
        right_val = max_values.speed_max * 0.1f / 1.60934f;
        break;
      case 3:
        left_val  = max_values.ls_fuel_press_max * 0.1f / 6.895f;
        right_val = max_values.inj_duty_cycle_max;
        break;
      case 4:
        left_val  = max_values.ethanol_pct_max;
        right_val = max_values.battery_volts_max * 0.1f;
        break;
    }
  } else {
    switch (mode) {
      case 0:
        left_val  = ((left_val - 40) * 9.0f / 5.0f) + 32.0f;
        right_val = right_val * 0.1f / 6.895f;
        break;
      case 1:
        left_val  = left_val * 0.01f * 14.7f;
        right_val = right_val * 0.01f * 14.7f;
        break;
      case 2:
        left_val  = (left_val * 0.1f - 105.0f) / 6.895f;  // MAP: x0.1 kPa, -105 kPa offset → PSI
        right_val = right_val * 0.1f / 1.60934f;
        break;
      case 3:
        left_val  = left_val * 0.1f / 6.895f;
        // right_val already in % (x1), no conversion needed
        break;
      case 4:
        // left_val (ethanol) needs no scaling
        right_val = right_val * 0.1f;
        break;
    }
  }

  update_display_values(mode, left_val, right_val);
  last_update_time = now;
}

void setup(void) {
  Serial.begin(115200);
  delay(100);
  Serial.println("1: Serial init");

  // Create mutexes before starting any tasks
  display_data_mutex = xSemaphoreCreateMutex();
  
  // Load persistent data from NVS
  load_persistent_data();
  Serial.println("2: NVS loaded");
  
  drivers_init();
  Serial.println("3: Drivers init");
  
  set_backlight(40);
  Serial.println("4: Backlight set");
  
  screens_init();
  Serial.println("5: Screens init");
  
  // Update odometer/trip display with loaded values before boot screen shows
  delay(50); // Small delay to ensure LVGL is ready
  update_odometer_display();
  
  set_exio(EXIO_PIN4, Low);
  
  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("Reset reason: %d\n", reason);

  xTaskCreatePinnedToCore(receive_can_task, "RX_CAN", 4096, NULL, 1, NULL, 0);  // Core 0, priority 1
  xTaskCreatePinnedToCore(delayed_can_init_task, "Init_CAN", 2048, NULL, 1, NULL, 1);  
  xTaskCreatePinnedToCore(watchdog_task, "Watchdog", 2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(periodic_save_task, "Save_NVS", 2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(restore_screen_mode_task, "Restore_Mode", 2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(odometer_update_task, "Odometer", 4096, NULL, 1, NULL, 0);
  
  // Don't update display here - will be done after boot screen in restore task
  
  Serial.println("Setup complete");
}

// Update ECU warning display (background color + warning text)
void update_ecu_warnings() {
  static uint8_t last_flags = 0;
  static bool last_warning_active = false;
  
  unsigned long now = millis();
  
  // Timeout: clear warnings if no 0x64C message received recently
  uint8_t flags = ecu_warning_flags;
  if (last_warning_time > 0 && (now - last_warning_time > WARNING_TIMEOUT_MS)) {
    flags = 0;
  }
  
  lv_obj_t* warn_left = get_warning_label_left();
  lv_obj_t* warn_right = get_warning_label_right();
  lv_obj_t* screen = get_main_screen();
  lv_obj_t* left_container = get_left_gauge_container();
  lv_obj_t* right_container = get_right_gauge_container();
  
  if (warn_left == NULL || warn_right == NULL || screen == NULL ||
      left_container == NULL || right_container == NULL) return;
  
  bool warning_active = (flags != 0);
  
  // Only update if state changed
  if (flags == last_flags && warning_active == last_warning_active) return;
  
  if (warning_active) {
    // Set background red on screen and both gauge containers
    lv_obj_set_style_bg_color(screen, lv_color_make(80, 0, 0), 0);
    lv_obj_set_style_bg_color(left_container, lv_color_make(80, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_color(right_container, lv_color_make(80, 0, 0), LV_PART_MAIN);
    
    // Build warning message — show highest priority warning
    const char* warn_text = "";
    if (flags & WARN_KNOCK)            warn_text = "Knock Retard Active";
    else if (flags & WARN_OIL_PRESSURE)     warn_text = "Oil PSI Low";
    else if (flags & WARN_FUEL_PRESSURE)    warn_text = "Fuel PSI Low";
    else if (flags & WARN_COOLANT_TEMP)     warn_text = "Coolant Temp High";
    else if (flags & WARN_OIL_TEMP)         warn_text = "Oil Temp High";
    else if (flags & WARN_COOLANT_PRESSURE) warn_text = "Coolant PSI High";
    else if (flags & WARN_CRANKCASE_PRESS)  warn_text = "Crankcase PSI High";
    else if (flags & WARN_ENGINE_SPEED)     warn_text = "Engine RPM High";
    
    // Show same message on both gauges
    lv_label_set_text(warn_left, warn_text);
    lv_label_set_text(warn_right, warn_text);
    lv_obj_clear_flag(warn_left, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(warn_right, LV_OBJ_FLAG_HIDDEN);
  } else {
    // Clear: restore black background and hide labels
    lv_obj_set_style_bg_color(screen, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_bg_color(left_container, lv_color_make(0, 0, 0), LV_PART_MAIN);
    lv_obj_set_style_bg_color(right_container, lv_color_make(0, 0, 0), LV_PART_MAIN);
    lv_obj_add_flag(warn_left, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(warn_right, LV_OBJ_FLAG_HIDDEN);
  }
  
  last_flags = flags;
  last_warning_active = warning_active;
}

// Update status icon visibility
void update_status_icons() {
  unsigned long now = millis();
  
  lv_obj_t* cruise_icon = get_cruise_icon();
  lv_obj_t* tcs_icon = get_tcs_icon();
  lv_obj_t* launch_icon = get_launch_icon();
  lv_obj_t* two_step_icon = get_two_step_icon();
  lv_obj_t* exhaust_bypass_icon = get_exhaust_bypass_icon();
  lv_obj_t* peak_recall_icon = get_peak_recall_icon();
  
  if (cruise_icon == NULL || tcs_icon == NULL || launch_icon == NULL || 
      two_step_icon == NULL || exhaust_bypass_icon == NULL || peak_recall_icon == NULL) {
    return; // Icons not ready yet
  }
  
  // Show icons during startup period or when active
  bool show_cruise = icons_startup_shown || (cruise_active && (now - last_cruise_time < ICON_TIMEOUT_MS));
  bool show_tcs = icons_startup_shown || (tcs_active && (now - last_tcs_time < ICON_TIMEOUT_MS));
  bool show_launch = icons_startup_shown || (launch_active && (now - last_launch_time < ICON_TIMEOUT_MS));
  bool show_two_step = icons_startup_shown || (two_step_active && (now - last_two_step_time < ICON_TIMEOUT_MS));
  bool show_exhaust_bypass = icons_startup_shown || (exhaust_bypass_active && (now - last_exhaust_bypass_time < ICON_TIMEOUT_MS));
  bool show_peak_recall = icons_startup_shown || max_recall_active; // Show on startup or when max recall is active
  
  // Update visibility
  if (show_cruise) {
    lv_obj_clear_flag(cruise_icon, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(cruise_icon, LV_OBJ_FLAG_HIDDEN);
    cruise_active = false; // Reset state after timeout
  }
  
  if (show_tcs) {
    lv_obj_clear_flag(tcs_icon, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(tcs_icon, LV_OBJ_FLAG_HIDDEN);
    tcs_active = false;
  }
  
  if (show_launch) {
    lv_obj_clear_flag(launch_icon, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(launch_icon, LV_OBJ_FLAG_HIDDEN);
    launch_active = false;
  }
  
  if (show_two_step) {
    lv_obj_clear_flag(two_step_icon, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(two_step_icon, LV_OBJ_FLAG_HIDDEN);
    two_step_active = false;
  }
  
  if (show_exhaust_bypass) {
    lv_obj_clear_flag(exhaust_bypass_icon, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(exhaust_bypass_icon, LV_OBJ_FLAG_HIDDEN);
    exhaust_bypass_active = false;
  }
  
  if (show_peak_recall) {
    // Swap image source based on clear vs recall mode
    if (max_clear_active) {
      lv_image_set_src(peak_recall_icon, &ClearPeakRecall);
    } else {
      lv_image_set_src(peak_recall_icon, &PeakRecall);
    }
    lv_obj_clear_flag(peak_recall_icon, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(peak_recall_icon, LV_OBJ_FLAG_HIDDEN);
  }
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
  
  // Check if trip switch was requested
  process_trip_switch();
  
  // Check TCA9554 P5-P8 input states periodically
  if (now - last_tca_check >= TCA_CHECK_INTERVAL_MS) {
    last_tca_check = now;
    uint8_t current_state = read_exios(TCA9554_INPUT_REG);
    
    // Check each pin (P5-P8) for falling edge (high to low transition = pulled to ground)
    // P5 = bit 4, P6 = bit 5, P7 = bit 6, P8 = bit 7
    for (int pin = 5; pin <= 8; pin++) {
      uint8_t bit_mask = (1 << (pin - 1));
      bool last_state = (tca_inputs_last_state & bit_mask) != 0;
      bool curr_state = (current_state & bit_mask) != 0;
      
      // Detect falling edge
      if (last_state && !curr_state) {
        Serial.printf("TCA9554 P%d triggered - pulled to ground!\n", pin);
        
        // P5 triggers screen change
        if (pin == 5) {
          uint8_t new_mode = (get_current_screen_mode() + 1) % 5;
          update_screen_labels(new_mode);
          last_screen_mode = new_mode;
          Serial.printf("Screen changed to mode %d\n", new_mode);
        }
        // P6 triggers trip reset
        else if (pin == 6) {
          trip_reset_pending = true;
          Serial.println("Trip reset triggered");
        }
        // P7 triggers trip switch
        else if (pin == 7) {
          trip_switch_pending = true;
          Serial.println("Trip switch triggered");
        }
      }
    }
    
    tca_inputs_last_state = current_state;
  }
  
  // Run LVGL timer handler at consistent intervals
  if (now - last_lvgl_time >= LVGL_INTERVAL_MS) {
    lv_timer_handler();
    last_lvgl_time = now;
  }
  
  // Update status icon visibility
  update_status_icons();
  
  // Update ECU warning display
  update_ecu_warnings();
  
  // Update data values
  update_display_from_can_data();
  
  // Small delay to prevent CPU hogging, but don't block LVGL timing
  vTaskDelay(pdMS_TO_TICKS(1));
}