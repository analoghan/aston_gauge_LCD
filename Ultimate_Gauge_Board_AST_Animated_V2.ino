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
// millis() each status was last seen active on CAN (0 = never); icon shows within ICON_TIMEOUT_MS
volatile unsigned long last_cruise_time = 0;
volatile unsigned long last_tcs_time = 0;
volatile unsigned long last_launch_time = 0;
volatile unsigned long last_two_step_time = 0;
volatile unsigned long last_exhaust_bypass_time = 0;
unsigned long main_screen_loaded_time = 0; // 0 = still on boot screen
#define ICON_STARTUP_SHOW_MS 2000  // Show all icons for 2s after the main screen loads
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

// Every value shown on a gauge. Raw values are stored as received from the M1 ECU (pre-scaling).
enum SignalId {
  SIG_COOLANT_TEMP,   // 0x649 B0,    x1 -40 offset → °C
  SIG_OIL_PRESS,      // 0x644 B6-7,  x0.1 kPa
  SIG_LAMBDA_B1,      // 0x651 B2,    x0.01 LA
  SIG_LAMBDA_B2,      // 0x651 B3,    x0.01 LA
  SIG_MAP,            // 0x640 B2-3,  x0.1 kPa absolute
  SIG_SPEED,          // 0x659 B4-5,  x0.1 km/h
  SIG_FUEL_PRESS,     // 0x641 B4-5,  x0.1 kPa
  SIG_INJ_DUTY,       // 0x641 B6,    x1 %
  SIG_ETHANOL,        // 0x670 B5,    x1 %
  SIG_BATTERY,        // 0x649 B5,    x0.1 V
  SIG_COUNT
};

// Live state of one signal. Shared between CAN task and main loop: hold display_data_mutex.
typedef struct {
  uint16_t raw;                 // Latest raw value
  uint16_t max;                 // Peak raw value (for peak recall)
  bool     updated;             // New data since the display last consumed it
  unsigned long last_update;    // millis() of last CAN update (0 = never received)
} SignalState;

SignalState signals[SIG_COUNT] = {};
SemaphoreHandle_t display_data_mutex = NULL;

#define CAN_DATA_TIMEOUT_MS 500 // Reset values after 500ms of no updates

// Raw value a signal falls back to after CAN_DATA_TIMEOUT_MS of silence (coolant raw 40 = 0°C)
#define SIGNAL_TIMEOUT_RAW(id) ((id) == SIG_COOLANT_TEMP ? 40 : 0)

// ============================================================================
// SCREEN DEFINITIONS
// ============================================================================

#define COLOR_WHITE lv_color_hex(0xFFFFFF)
#define COLOR_RED   lv_color_hex(0xFF0000)
#define COLOR_GREEN lv_color_hex(0x00FF00)
#define COLOR_BLUE  lv_color_hex(0x0000FF)

// One half of the display: what it shows and how
typedef struct {
  const char *title;
  SignalId    signal;
  float     (*scale)(float raw);      // Raw CAN value → display units
  uint8_t     decimals;               // 0 = integer, 1 = one decimal place
  lv_color_t (*color)(float value);   // Text color for a scaled value (NULL = always white)
} GaugeDef;

typedef struct {
  GaugeDef left;
  GaugeDef right;
} ScreenDef;

// Lambdas rather than functions: the Arduino IDE inserts auto-generated prototypes above
// the first function in the sketch, which must come after these type definitions.
static lv_color_t (*const afr_color)(float) = [](float v) {
  if (v <= 0) return COLOR_WHITE;
  return (v < 12 || v > 16) ? COLOR_RED : COLOR_GREEN;
};

// Screen order here is the cycle order, and the index is the saved screen mode
static const ScreenDef SCREENS[] = {
  { // 0: Coolant temp / oil pressure
    { "ECT °F",      SIG_COOLANT_TEMP, [](float r) { return (r - 40) * 9.0f / 5.0f + 32.0f; }, 0,
      [](float v) { return (v > 0 && v < 100) ? COLOR_BLUE : (v >= 210) ? COLOR_RED : COLOR_WHITE; } },
    { "Oil PSI",     SIG_OIL_PRESS,    [](float r) { return r * 0.1f / 6.895f; }, 0,
      [](float v) { return (v > 0 && v < 20) ? COLOR_RED : COLOR_WHITE; } },
  },
  { // 1: AFR (Lambda × 14.7), green 12-16, red otherwise
    { "AFR B1",      SIG_LAMBDA_B1,    [](float r) { return r * 0.01f * 14.7f; }, 1, afr_color },
    { "AFR B2",      SIG_LAMBDA_B2,    [](float r) { return r * 0.01f * 14.7f; }, 1, afr_color },
  },
  { // 2: Boost / speed
    { "MAP PSI",     SIG_MAP,          [](float r) { return (r * 0.1f - 105.0f) / 6.895f; }, 1, NULL }, // -105 kPa offset → gauge PSI
    { "Speed MPH",   SIG_SPEED,        [](float r) { return r * 0.1f / 1.60934f; }, 0, NULL },
  },
  { // 3: Fuel pressure / injector duty
    { "LS Fuel PSI", SIG_FUEL_PRESS,   [](float r) { return r * 0.1f / 6.895f; }, 0,
      [](float v) { return (v > 0 && v < 40) ? COLOR_RED : COLOR_WHITE; } },  // Low while running
    { "Inj Duty %",  SIG_INJ_DUTY,     [](float r) { return r; }, 0,
      [](float v) { return (v >= 85) ? COLOR_RED : COLOR_WHITE; } },
  },
  { // 4: Ethanol / battery
    { "Ethanol %",   SIG_ETHANOL,      [](float r) { return r; }, 0, NULL },
    { "Battery V",   SIG_BATTERY,      [](float r) { return r * 0.1f; }, 1, NULL },
  },
};

const uint8_t NUM_SCREENS = sizeof(SCREENS) / sizeof(SCREENS[0]);

// Last value/color written to a gauge's label, so LVGL is only touched on change
typedef struct {
  float      value;
  lv_color_t color;
  bool       valid;
} GaugeCache;

// Screen navigation pending flags (set from CAN task, consumed in main loop)
volatile bool screen_next_pending = false;
volatile bool screen_prev_pending = false;

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

// Persistent storage variables (hundredths of miles)
// Written by the odometer task (core 0) and the main loop (trip reset, core 1):
// any read-modify-write must hold odometer_mux.
volatile uint32_t odometer_miles = 0;
volatile uint32_t trip_miles = 0;
volatile uint32_t trip2_miles = 0;
portMUX_TYPE odometer_mux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t save_task_handle = NULL; // Notify this task to request an immediate NVS save
uint8_t last_screen_mode = 0;
uint8_t current_trip_display = 1; // 1 or 2, always starts at 1

// ============================================================================
// ODOMETER TRACKING
// ============================================================================

// Odometer tracking variables
volatile uint16_t current_speed_x10 = 0; // Current speed in tenths of MPH (keeps fraction for odometer accuracy)
portMUX_TYPE speed_mutex = portMUX_INITIALIZER_UNLOCKED;
volatile bool trip_reset_pending = false; // Flag to trigger trip reset from main loop
volatile bool trip_switch_pending = false; // Flag to trigger trip display switch from main loop
volatile bool odometer_display_pending = false; // Flag to trigger odometer display update
volatile bool force_odometer_refresh = false;   // Force cache bypass on next odometer update

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

// Save values to NVS flash. Only called from periodic_save_task (Preferences is not thread-safe);
// other code should call request_save() instead.
void save_persistent_data(uint32_t odo, uint32_t trip1, uint32_t trip2, uint8_t mode) {
  preferences.begin("gauge", false); // Open in read-write mode

  preferences.putUInt("odometer", odo);
  preferences.putUInt("trip", trip1);
  preferences.putUInt("trip2", trip2);
  preferences.putUChar("screen_mode", mode);

  preferences.end();
}

// Ask the save task to write to NVS now instead of waiting for the next 10s check
void request_save() {
  if (save_task_handle != NULL) {
    xTaskNotifyGive(save_task_handle);
  }
}

// Periodic save task - saves every 10 seconds (or immediately on request) if values changed
void periodic_save_task(void *arg) {
  uint32_t last_odometer = odometer_miles;
  uint32_t last_trip = trip_miles;
  uint32_t last_trip2 = trip2_miles;
  uint8_t last_mode = last_screen_mode;

  while (1) {
    // Wake every 10 seconds, or early when request_save() notifies us
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000));

    // Take a consistent snapshot of the counters
    portENTER_CRITICAL(&odometer_mux);
    uint32_t odo = odometer_miles;
    uint32_t trip1 = trip_miles;
    uint32_t trip2 = trip2_miles;
    portEXIT_CRITICAL(&odometer_mux);
    uint8_t mode = last_screen_mode;

    // Only save if values have changed
    if (odo != last_odometer || trip1 != last_trip ||
        trip2 != last_trip2 || mode != last_mode) {
      save_persistent_data(odo, trip1, trip2, mode);
      last_odometer = odo;
      last_trip = trip1;
      last_trip2 = trip2;
      last_mode = mode;
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
  
  if (odometer_miles != last_displayed_odometer || force_odometer_refresh) {
    force_odometer_refresh = false;
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
    portENTER_CRITICAL(&odometer_mux);
    if (current_trip_display == 1) {
      trip_miles = 0;
    } else {
      trip2_miles = 0;
    }
    portEXIT_CRITICAL(&odometer_mux);
    Serial.printf("Trip %d meter reset\n", current_trip_display);

    update_odometer_display();
    request_save(); // Save immediately
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

// Called from Screens.cpp when the main screen finishes loading after the boot screen
// (LV_EVENT_SCREEN_LOADED, so this runs in the LVGL/main loop context)
void on_main_screen_loaded() {
  Serial.printf("Boot complete, restoring mode %d\n", last_screen_mode);
  show_screen(last_screen_mode);

  force_odometer_refresh = true;  // Bypass cache — screen just loaded fresh
  update_odometer_display();

  main_screen_loaded_time = millis();  // Starts the startup icon display
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
      speed_samples[i] = current_speed_x10;
      portEXIT_CRITICAL(&speed_mutex);
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Calculate average speed over the last second
    uint32_t speed_sum = 0;
    for (int i = 0; i < SAMPLES_PER_SECOND; i++) {
      speed_sum += speed_samples[i];
    }
    float avg_speed_mph = speed_sum / (float)SAMPLES_PER_SECOND / 10.0f;
    
    // Calculate distance traveled in this second
    // Distance = Speed * Time
    // Speed is in MPH, time is 1 second = 1/3600 hour
    float distance_miles = avg_speed_mph / 3600.0;
    
    // Accumulate distance
    accumulated_distance += distance_miles;
    
    // Update odometer and trip when we've accumulated at least 0.01 miles
    if (accumulated_distance >= 0.01) {
      uint32_t miles_to_add = (uint32_t)(accumulated_distance * 100); // Convert to hundredths
      portENTER_CRITICAL(&odometer_mux);
      odometer_miles += miles_to_add;
      trip_miles += miles_to_add;
      trip2_miles += miles_to_add;
      portEXIT_CRITICAL(&odometer_mux);
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

// Big-endian 16-bit value from two CAN bytes (M1 signals are Motorola byte order)
static inline uint16_t be16(const uint8_t *bytes) {
  return ((uint16_t)bytes[0] << 8) | bytes[1];
}

// Store a new raw reading and track its peak. Caller must hold display_data_mutex.
void update_signal(SignalId id, uint16_t raw, unsigned long now) {
  SignalState &sig = signals[id];
  sig.raw = raw;
  sig.updated = true;
  sig.last_update = now;
  if (raw > sig.max) sig.max = raw;
}

void receive_can_task(void *arg) {
  while (!can_initiated) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  
  Serial.println("CAN receive task starting...");
  
  uint32_t alerts;
  uint32_t msg_count = 0;
  uint32_t overflow_count = 0;
  uint32_t bus_error_count = 0;
  uint32_t last_stats_time = millis();
  bool bus_recovering = false;

  // Previous state of the 0x17C screen nav bits, for rising-edge detection
  bool nav_next_last = false;
  bool nav_prev_last = false;

  while (1) {
    twai_read_alerts(&alerts, 0);

    // Bus-off: controller stops participating until recovery completes (128 x 11 recessive bits)
    if (alerts & TWAI_ALERT_BUS_OFF) {
      Serial.println("CAN bus-off, initiating recovery...");
      twai_initiate_recovery();
      bus_recovering = true;
    }
    if (alerts & TWAI_ALERT_BUS_RECOVERED) {
      Serial.println("CAN bus recovered, restarting driver");
      twai_start();
      bus_recovering = false;
    }
    if (bus_recovering) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    if (alerts & TWAI_ALERT_BUS_ERROR) {
      // Individual bus errors are handled by the controller; just count them
      bus_error_count++;
    }

    if (alerts & TWAI_ALERT_RX_QUEUE_FULL) {
      overflow_count++;
      twai_message_t dummy;
      while (twai_receive(&dummy, 0) == ESP_OK) {}
      vTaskDelay(pdMS_TO_TICKS(10));
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

        case 0x640:
          // Inlet_Manifold_Pressure: bit 23|16@0+ = B2-B3, x0.1 kPa
          update_signal(SIG_MAP, be16(&message.data[2]), now_msg);
          break;

        case 0x641:
          // Fuel_Pressure_Sensor: bit 39|16@0+ = B4-B5, x0.1 kPa
          update_signal(SIG_FUEL_PRESS, be16(&message.data[4]), now_msg);
          // Fuel_Injector_Primary_Duty_Cycle: bit 55|8@0+ = B6, x1 %
          update_signal(SIG_INJ_DUTY, message.data[6], now_msg);
          break;

        case 0x644:
          // Engine_Oil_Pressure: bit 55|16@0+ = B6-B7, x0.1 kPa
          update_signal(SIG_OIL_PRESS, be16(&message.data[6]), now_msg);
          break;

        case 0x649:
          // Coolant_Temperature: B0, x1 -40 °C; ECU_Battery_Voltage: B5, x0.1 V
          update_signal(SIG_COOLANT_TEMP, message.data[0], now_msg);
          update_signal(SIG_BATTERY, message.data[5], now_msg);
          break;

        case 0x651:
          // Exhaust_Lambda_Bank_1: B2, Exhaust_Lambda_Bank_2: B3, x0.01 LA
          update_signal(SIG_LAMBDA_B1, message.data[2], now_msg);
          update_signal(SIG_LAMBDA_B2, message.data[3], now_msg);
          break;

        case 0x659:
          // Vehicle_Speed: bit 39|16@0+ = B4-B5, x0.1 km/h
          update_signal(SIG_SPEED, be16(&message.data[4]), now_msg);
          break;

        case 0x670:
          // Fuel_Composition: B5, x1 %
          update_signal(SIG_ETHANOL, message.data[5], now_msg);
          break;

        case 0x17C: {
          // Screen navigation: byte 1, bit 4 = next screen, bit 5 = prev screen.
          // Bits stay set while the button is held, so only act on the 0 -> 1 transition.
          bool nav_next = (message.data[1] >> 4) & 0x01;
          bool nav_prev = (message.data[1] >> 5) & 0x01;
          if (nav_next && !nav_next_last) {
            screen_next_pending = true;
          }
          if (nav_prev && !nav_prev_last) {
            screen_prev_pending = true;
          }
          nav_next_last = nav_next;
          nav_prev_last = nav_prev;

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
              const ScreenDef &screen = SCREENS[get_current_screen_mode()];
              signals[screen.left.signal].max = 0;
              signals[screen.right.signal].max = 0;
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
            last_launch_time = now_msg;
          }
          if ((message.data[3] >> 4) & 0x01) {
            last_tcs_time = now_msg;
          }
          break;
        }

        case 0x650: {
          if ((message.data[7] >> 6) & 0x01) {
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
            last_cruise_time = now_msg;
          }
          if (message.data[5]) {
            last_exhaust_bypass_time = now_msg;
          }
          break;
        }
      }
      xSemaphoreGive(display_data_mutex);

      // Update current_speed_x10 outside the critical section (avoids nested lock with speed_mutex)
      if (message.identifier == 0x659) {
        uint16_t spd_raw = be16(&message.data[4]);
        portENTER_CRITICAL(&speed_mutex);
        // raw is 0.1 km/h, so raw / 1.60934 = tenths of MPH
        current_speed_x10 = (uint16_t)(spd_raw / 1.60934f + 0.5f);
        portEXIT_CRITICAL(&speed_mutex);
      }

      if (msg_count % 10 == 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
      }
      
      if (millis() - last_stats_time > 10000) {
        Serial.printf("CAN: %lu msgs, %lu overflows, %lu bus errors\n", msg_count, overflow_count, bus_error_count);
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

// Format, color, and write one gauge value to its label (skips unchanged text/color)
void update_gauge_label(lv_obj_t *label, GaugeCache &cache, const GaugeDef &gauge, float value) {
  lv_color_t color = gauge.color ? gauge.color(value) : COLOR_WHITE;

  if (!cache.valid || value != cache.value) {
    // Right-aligned so digits don't shift: 4 chars for integers ("   0"), 5 for decimals (" 14.7")
    char text[12];
    if (gauge.decimals == 0) {
      snprintf(text, sizeof(text), "%4d", (int)value);
    } else {
      snprintf(text, sizeof(text), "%5.1f", value);
    }
    lv_label_set_text(label, text);
    cache.value = value;
  }
  if (!cache.valid || !lv_color_eq(color, cache.color)) {
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    cache.color = color;
  }
  cache.valid = true;
}

void update_display_from_can_data(void) {
  static unsigned long last_update_time = 0;
  static GaugeCache left_cache = {}, right_cache = {};
  static uint8_t last_mode = 255;
  const unsigned long UPDATE_INTERVAL_MS = 100;
  
  unsigned long now = millis();
  if (now - last_update_time < UPDATE_INTERVAL_MS) return;
  last_update_time = now;
  
  // Expire max recall (only when button is not held)
  if (max_recall_active && !max_recall_button_held && 
      (now - max_recall_start_time >= MAX_RECALL_DISPLAY_MS)) {
    max_recall_active = false;
    max_clear_active = false;
  }
  
  uint8_t mode = get_current_screen_mode();
  const ScreenDef &screen = SCREENS[mode];

  // update_screen_labels() resets the labels on a mode change, so the cache is stale
  if (mode != last_mode) {
    left_cache.valid = false;
    right_cache.valid = false;
    last_mode = mode;
  }

  uint16_t left_raw = 0, right_raw = 0;
  bool has_update = false;

  // --- Snapshot signals under mutex, no nested locks ---
  xSemaphoreTake(display_data_mutex, portMAX_DELAY);

  // Fall back to the timeout value for any signal that has gone quiet
  for (int i = 0; i < SIG_COUNT; i++) {
    SignalState &sig = signals[i];
    if (sig.last_update > 0 && now - sig.last_update > CAN_DATA_TIMEOUT_MS) {
      sig.raw = SIGNAL_TIMEOUT_RAW(i);
      sig.updated = true;
      if (i == SIG_SPEED) {
        // Stop the odometer accumulating at the last known speed
        portENTER_CRITICAL(&speed_mutex);
        current_speed_x10 = 0;
        portEXIT_CRITICAL(&speed_mutex);
      }
    }
  }

  SignalState &left_sig = signals[screen.left.signal];
  SignalState &right_sig = signals[screen.right.signal];
  if (left_sig.updated || right_sig.updated || max_recall_active) {
    left_raw  = max_recall_active ? left_sig.max  : left_sig.raw;
    right_raw = max_recall_active ? right_sig.max : right_sig.raw;
    left_sig.updated = false;
    right_sig.updated = false;
    has_update = true;
  }

  xSemaphoreGive(display_data_mutex);
  // --- End of critical section ---

  if (!has_update) return;

  update_gauge_label(get_left_value_label(), left_cache, screen.left, screen.left.scale(left_raw));
  update_gauge_label(get_right_value_label(), right_cache, screen.right, screen.right.scale(right_raw));
}

// Switch to a screen by index: sets titles, resets values, and remembers it for NVS
void show_screen(uint8_t mode) {
  if (mode >= NUM_SCREENS) mode = 0; // Guard against a bad value loaded from NVS
  update_screen_labels(mode, SCREENS[mode].left.title, SCREENS[mode].right.title);
  last_screen_mode = mode;
}

// Step forward (+1) or back (-1) through the screens, wrapping around
void change_screen(int delta) {
  uint8_t new_mode = (get_current_screen_mode() + NUM_SCREENS + delta) % NUM_SCREENS;
  show_screen(new_mode);
  Serial.printf("Screen changed to mode %d\n", new_mode);
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
  xTaskCreatePinnedToCore(periodic_save_task, "Save_NVS", 2048, NULL, 1, &save_task_handle, 0);
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

// Show/hide an object only when its visibility changes (last: -1 = unknown, 0 = hidden, 1 = shown)
void set_icon_visible(lv_obj_t *obj, bool show, int8_t &last) {
  if (last == (int8_t)show) return;
  if (show) {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
  last = show;
}

// True if a status was seen on CAN within ICON_TIMEOUT_MS
bool status_recent(unsigned long last_seen, unsigned long now) {
  return last_seen != 0 && (now - last_seen < ICON_TIMEOUT_MS);
}

// Update status icon visibility
void update_status_icons() {
  static int8_t cruise_shown = -1, tcs_shown = -1, launch_shown = -1;
  static int8_t two_step_shown = -1, exhaust_bypass_shown = -1, peak_recall_shown = -1;
  static const lv_image_dsc_t *peak_recall_src = NULL;

  lv_obj_t* peak_recall_icon = get_peak_recall_icon();
  if (main_screen_loaded_time == 0 || peak_recall_icon == NULL) {
    return; // Main screen not up yet
  }

  unsigned long now = millis();
  bool startup = (now - main_screen_loaded_time < ICON_STARTUP_SHOW_MS);

  set_icon_visible(get_cruise_icon(),         startup || status_recent(last_cruise_time, now),         cruise_shown);
  set_icon_visible(get_tcs_icon(),            startup || status_recent(last_tcs_time, now),            tcs_shown);
  set_icon_visible(get_launch_icon(),         startup || status_recent(last_launch_time, now),         launch_shown);
  set_icon_visible(get_two_step_icon(),       startup || status_recent(last_two_step_time, now),       two_step_shown);
  set_icon_visible(get_exhaust_bypass_icon(), startup || status_recent(last_exhaust_bypass_time, now), exhaust_bypass_shown);

  // Peak recall icon swaps image between recall and clear modes
  bool show_peak_recall = startup || max_recall_active;
  if (show_peak_recall) {
    const lv_image_dsc_t *src = max_clear_active ? &ClearPeakRecall : &PeakRecall;
    if (src != peak_recall_src) {
      lv_image_set_src(peak_recall_icon, src);
      peak_recall_src = src;
    }
  }
  set_icon_visible(peak_recall_icon, show_peak_recall, peak_recall_shown);
}

void loop(void) {
  static unsigned long last_lvgl_time = 0;
  const unsigned long LVGL_INTERVAL_MS = 16; // Run LVGL at ~60Hz
  
  last_loop_time = millis();
  unsigned long now = millis();
  
  // CAN-triggered screen navigation (next screen: bit 4, prev screen: bit 5 of 0x17C byte 1)
  if (screen_next_pending) {
    screen_next_pending = false;
    change_screen(+1);
  }
  if (screen_prev_pending) {
    screen_prev_pending = false;
    change_screen(-1);
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
  uint8_t current_state;
  bool tca_poll_due = (now - last_tca_check >= TCA_CHECK_INTERVAL_MS);
  if (tca_poll_due) last_tca_check = now;
  // Skip the edge check entirely if the I2C read fails, so a bad read can't fake a button press
  if (tca_poll_due && i2c_read(TCA9554_ADDRESS, TCA9554_INPUT_REG, &current_state, 1)) {

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
          change_screen(+1);
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