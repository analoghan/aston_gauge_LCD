#include <Arduino.h>
#include "CANBus_Driver.h"
#include "LVGL_Driver.h"
#include "I2C_Driver.h"
#include "Screens.h"

#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/queue.h"

QueueHandle_t canMsgQueue;
#define CAN_QUEUE_LENGTH 1024
#define CAN_QUEUE_ITEM_SIZE sizeof(twai_message_t)

#define TAG "TWAI"

// CONTROL VARIABLE INIT
bool can_initiated = false;

// Watchdog tracking
unsigned long last_can_message_time = 0;
unsigned long last_loop_time = 0;
#define CAN_TIMEOUT_MS 5000
#define LOOP_TIMEOUT_MS 1000

// Thread-safe data structure for passing CAN data to UI
typedef struct {
  uint8_t water_temp;
  uint8_t oil_press;
  uint8_t left_afr;
  uint8_t right_afr;
  uint8_t map_press;
  uint8_t coolant_press;
  uint8_t ls_fuel_press;
  uint8_t hs_fuel_press;
  bool updated_0x551;
  bool updated_0x553;
  bool updated_0x554;
  bool updated_0x555;
  bool screen_change_requested;
} DisplayData;

volatile DisplayData display_data = {0, 0, 0, 0, 0, 0, 0, 0, false, false, false, false, false};
portMUX_TYPE display_data_mutex = portMUX_INITIALIZER_UNLOCKED;

// Current screen index for cycling
volatile uint8_t current_screen_index = 0; // 0=main_scr, 1=main_scr2, 2=main_scr3, 3=main_scr4

void drivers_init(void) {
  i2c_init();

  Serial.println("Scanning for TCA9554...");
  bool found = false;
  for (int attempt = 0; attempt < 10; attempt++) {
  if (i2c_scan_address(0x20)) { // 0x20 is default for TCA9554
      found = true;
      break;
    }
    delay(50); // wait a bit before retrying
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
  vTaskDelete(NULL); // Delete this task after it runs once
}

void watchdog_task(void *arg) {
  vTaskDelay(pdMS_TO_TICKS(10000)); // Wait 10 seconds before starting watchdog
  
  Serial.println("Watchdog task started");
  
  while (1) {
    unsigned long now = millis();
    
    // Check if loop is still running
    if (now - last_loop_time > LOOP_TIMEOUT_MS) {
      Serial.printf("WARNING: Main loop appears frozen! Last run: %lu ms ago\n", now - last_loop_time);
    }
    
    // Check CAN health if it's been initialized and receiving data
    if (can_initiated && last_can_message_time > 0) {
      if (now - last_can_message_time > CAN_TIMEOUT_MS) {
        Serial.println("WARNING: No CAN messages received recently. Bus may be down.");
      }
    }
    
    // Check task stack usage
    UBaseType_t highWaterMark = uxTaskGetStackHighWaterMark(NULL);
    if (highWaterMark < 256) {
      Serial.printf("WARNING: Watchdog task low on stack! Free: %u bytes\n", highWaterMark);
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000)); // Check every second
  }
}

void receive_can_task(void *arg) {
  // Wait for CAN to be initialized
  while (!can_initiated) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  
  Serial.println("CAN receive task starting...");
  
  uint32_t alerts;
  uint32_t msg_count = 0;
  uint32_t overflow_count = 0;
  
  while (1) {
    // Check for CAN bus alerts (errors, overflows, etc.)
    twai_read_alerts(&alerts, 0);
    if (alerts & TWAI_ALERT_RX_QUEUE_FULL) {
      overflow_count++;
      if (overflow_count % 100 == 0) {
        Serial.printf("WARNING: CAN RX overflow detected! Count: %lu\n", overflow_count);
      }
      // Clear the queue to recover
      twai_message_t dummy;
      while (twai_receive(&dummy, 0) == ESP_OK) {
        // Drain queue
      }
    }
    
    if (alerts & TWAI_ALERT_BUS_ERROR) {
      Serial.println("CAN bus error detected, attempting recovery...");
      twai_initiate_recovery();
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    
    twai_message_t message;
    esp_err_t err = twai_receive(&message, pdMS_TO_TICKS(10));
    
    if (err == ESP_OK) {
      msg_count++;
      last_can_message_time = millis();
      
      // Process messages we care about
      if (message.identifier == 0x551) {
        // Update data in thread-safe manner
        portENTER_CRITICAL(&display_data_mutex);
        display_data.water_temp = message.data[0];
        display_data.oil_press = message.data[1];
        display_data.updated_0x551 = true;
        portEXIT_CRITICAL(&display_data_mutex);
      }
      else if (message.identifier == 0x552) {
        // Screen change request - only if byte 0 is 1
        if (message.data[0] == 1) {
          portENTER_CRITICAL(&display_data_mutex);
          display_data.screen_change_requested = true;
          portEXIT_CRITICAL(&display_data_mutex);
        }
      }
      else if (message.identifier == 0x553) {
        // AFR data
        portENTER_CRITICAL(&display_data_mutex);
        display_data.left_afr = message.data[0];
        display_data.right_afr = message.data[1];
        display_data.updated_0x553 = true;
        portEXIT_CRITICAL(&display_data_mutex);
      }
      else if (message.identifier == 0x554) {
        // MAP and coolant pressure data
        portENTER_CRITICAL(&display_data_mutex);
        display_data.map_press = message.data[0];
        display_data.coolant_press = message.data[1];
        display_data.updated_0x554 = true;
        portEXIT_CRITICAL(&display_data_mutex);
      }
      else if (message.identifier == 0x555) {
        // Fuel pressure data
        portENTER_CRITICAL(&display_data_mutex);
        display_data.ls_fuel_press = message.data[0];
        display_data.hs_fuel_press = message.data[1];
        display_data.updated_0x555 = true;
        portEXIT_CRITICAL(&display_data_mutex);
      }
      
      // Yield to prevent starving other tasks during high traffic
      if (msg_count % 10 == 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    } else if (err == ESP_ERR_TIMEOUT) {
      // Normal timeout, just continue
      vTaskDelay(pdMS_TO_TICKS(5));
    } else {
      ESP_LOGE(TAG, "Message reception failed: %s", esp_err_to_name(err));
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

void process_coolant_temp(uint8_t raw_value) {
  static int last_temp = -999; // Track last value to avoid redundant updates
  static uint8_t last_color_r = 0, last_color_g = 0, last_color_b = 0;
  
  int temp = raw_value - 40;
  
  // Only update if value changed
  if (temp == last_temp) {
    return;
  }
  last_temp = temp;

  // Determine color
  uint8_t r, g, b;
  if (temp < 100) {
    r = 0; g = 0; b = 255;
  } else if (temp < 210) {
    r = 255; g = 255; b = 255;
  } else {
    r = 255; g = 0; b = 0;
  }
  
  // Only update color if it changed
  if (r != last_color_r || g != last_color_g || b != last_color_b) {
    lv_obj_set_style_text_color(coolant_temp_scr1, lv_color_make(r, g, b), LV_PART_MAIN);
    last_color_r = r;
    last_color_g = g;
    last_color_b = b;
  }

  char coolant_temp_text[4];
  itoa(temp, coolant_temp_text, 10);
  lv_label_set_text(coolant_temp_scr1, coolant_temp_text);
}

void process_oil_press(uint8_t raw_value) {
  static int last_pressure = -999; // Track last value to avoid redundant updates
  static uint8_t last_color_r = 0, last_color_g = 0, last_color_b = 0;
  
  int pressure = raw_value;
  
  // Only update if value changed
  if (pressure == last_pressure) {
    return;
  }
  last_pressure = pressure;

  // Determine color
  uint8_t r, g, b;
  if (pressure < 20) {
    r = 255; g = 0; b = 0;
  } else {
    r = 255; g = 255; b = 255;
  }
  
  // Only update color if it changed
  if (r != last_color_r || g != last_color_g || b != last_color_b) {
    lv_obj_set_style_text_color(oil_press_scr1, lv_color_make(r, g, b), LV_PART_MAIN);
    last_color_r = r;
    last_color_g = g;
    last_color_b = b;
  }

  char oil_press_text[4];
  itoa(pressure, oil_press_text, 10);
  lv_label_set_text(oil_press_scr1, oil_press_text);
}

void process_left_afr(uint8_t raw_value) {
  static int last_afr = -999;
  static uint8_t last_color_r = 0, last_color_g = 0, last_color_b = 0;
  
  int afr = raw_value; // Adjust scaling as needed
  
  if (afr == last_afr) {
    return;
  }
  last_afr = afr;

  // Determine color (example: green for good AFR range)
  uint8_t r, g, b;
  if (afr < 12 || afr > 16) {
    r = 255; g = 0; b = 0; // Red for out of range
  } else {
    r = 0; g = 255; b = 0; // Green for good range
  }
  
  if (r != last_color_r || g != last_color_g || b != last_color_b) {
    lv_obj_set_style_text_color(left_afr_scr2, lv_color_make(r, g, b), LV_PART_MAIN);
    last_color_r = r;
    last_color_g = g;
    last_color_b = b;
  }

  char afr_text[4];
  itoa(afr, afr_text, 10);
  lv_label_set_text(left_afr_scr2, afr_text);
}

void process_right_afr(uint8_t raw_value) {
  static int last_afr = -999;
  static uint8_t last_color_r = 0, last_color_g = 0, last_color_b = 0;
  
  int afr = raw_value; // Adjust scaling as needed
  
  if (afr == last_afr) {
    return;
  }
  last_afr = afr;

  // Determine color
  uint8_t r, g, b;
  if (afr < 12 || afr > 16) {
    r = 255; g = 0; b = 0;
  } else {
    r = 0; g = 255; b = 0;
  }
  
  if (r != last_color_r || g != last_color_g || b != last_color_b) {
    lv_obj_set_style_text_color(right_afr_scr2, lv_color_make(r, g, b), LV_PART_MAIN);
    last_color_r = r;
    last_color_g = g;
    last_color_b = b;
  }

  char afr_text[4];
  itoa(afr, afr_text, 10);
  lv_label_set_text(right_afr_scr2, afr_text);
}

void process_map_press(uint8_t raw_value) {
  static int last_press = -999;
  
  int pressure = raw_value; // Adjust scaling as needed
  
  if (pressure == last_press) {
    return;
  }
  last_press = pressure;

  char press_text[4];
  itoa(pressure, press_text, 10);
  lv_label_set_text(map_press_scr3, press_text);
}

void process_coolant_press(uint8_t raw_value) {
  static int last_press = -999;
  
  int pressure = raw_value; // Adjust scaling as needed
  
  if (pressure == last_press) {
    return;
  }
  last_press = pressure;

  char press_text[4];
  itoa(pressure, press_text, 10);
  lv_label_set_text(coolant_press_scr3, press_text);
}

void process_ls_fuel_press(uint8_t raw_value) {
  static int last_press = -999;
  
  int pressure = raw_value; // Adjust scaling as needed
  
  if (pressure == last_press) {
    return;
  }
  last_press = pressure;

  char press_text[4];
  itoa(pressure, press_text, 10);
  lv_label_set_text(ls_fuel_press_scr4, press_text);
}

void process_hs_fuel_press(uint8_t raw_value) {
  static int last_press = -999;
  
  int pressure = raw_value; // Adjust scaling as needed
  
  if (pressure == last_press) {
    return;
  }
  last_press = pressure;

  char press_text[4];
  itoa(pressure, press_text, 10);
  lv_label_set_text(hs_fuel_press_scr4, press_text);
}

void update_display_from_can_data(void) {
  static unsigned long last_update_time = 0;
  const unsigned long UPDATE_INTERVAL_MS = 100; // 100ms = 10 updates per second
  
  unsigned long now = millis();
  
  // Check for screen change request (no rate limiting on this)
  bool screen_change = false;
  portENTER_CRITICAL(&display_data_mutex);
  if (display_data.screen_change_requested) {
    display_data.screen_change_requested = false;
    screen_change = true;
  }
  portEXIT_CRITICAL(&display_data_mutex);
  
  if (screen_change) {
    cycle_screens();
  }
  
  // Rate limit data updates to 10Hz
  if (now - last_update_time < UPDATE_INTERVAL_MS) {
    return;
  }
  
  // Check if there's new data to display
  bool has_update_0x551 = false;
  bool has_update_0x553 = false;
  bool has_update_0x554 = false;
  bool has_update_0x555 = false;
  uint8_t temp, press, left_afr, right_afr, map_press, coolant_press, ls_fuel, hs_fuel;
  
  portENTER_CRITICAL(&display_data_mutex);
  if (display_data.updated_0x551) {
    temp = display_data.water_temp;
    press = display_data.oil_press;
    display_data.updated_0x551 = false;
    has_update_0x551 = true;
  }
  if (display_data.updated_0x553) {
    left_afr = display_data.left_afr;
    right_afr = display_data.right_afr;
    display_data.updated_0x553 = false;
    has_update_0x553 = true;
  }
  if (display_data.updated_0x554) {
    map_press = display_data.map_press;
    coolant_press = display_data.coolant_press;
    display_data.updated_0x554 = false;
    has_update_0x554 = true;
  }
  if (display_data.updated_0x555) {
    ls_fuel = display_data.ls_fuel_press;
    hs_fuel = display_data.hs_fuel_press;
    display_data.updated_0x555 = false;
    has_update_0x555 = true;
  }
  portEXIT_CRITICAL(&display_data_mutex);
  
  if (has_update_0x551) {
    process_coolant_temp(temp);
    process_oil_press(press);
    last_update_time = now;
  }
  
  if (has_update_0x553) {
    process_left_afr(left_afr);
    process_right_afr(right_afr);
    last_update_time = now;
  }
  
  if (has_update_0x554) {
    process_map_press(map_press);
    process_coolant_press(coolant_press);
    last_update_time = now;
  }
  
  if (has_update_0x555) {
    process_ls_fuel_press(ls_fuel);
    process_hs_fuel_press(hs_fuel);
    last_update_time = now;
  }
}

void cycle_screens(void) {
  static unsigned long last_screen_change = 0;
  unsigned long now = millis();
  
  // Debounce screen changes (minimum 500ms between changes)
  if (now - last_screen_change < 500) {
    return;
  }
  last_screen_change = now;
  
  // Cycle to next screen
  current_screen_index = (current_screen_index + 1) % 4;
  
  lv_obj_t *next_screen = NULL;
  switch (current_screen_index) {
    case 0:
      next_screen = main_scr;
      Serial.println("Switching to main_scr");
      break;
    case 1:
      next_screen = main_scr2;
      Serial.println("Switching to main_scr2");
      break;
    case 2:
      next_screen = main_scr3;
      Serial.println("Switching to main_scr3");
      break;
    case 3:
      next_screen = main_scr4;
      Serial.println("Switching to main_scr4");
      break;
  }
  
  if (next_screen != NULL) {
    lv_screen_load(next_screen); // Instant screen change, no animation
  }
}

void setup(void) {
  Serial.begin(115200);
  delay(100);
  Serial.println("Setup starting...");
  
  drivers_init();  
  set_backlight(40);  
  screens_init();
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
  
  Serial.println("Setup complete");
}

void loop(void) {
  last_loop_time = millis();
  lv_timer_handler();
  update_display_from_can_data();
  vTaskDelay(pdMS_TO_TICKS(16)); // ~60Hz refresh rate
}