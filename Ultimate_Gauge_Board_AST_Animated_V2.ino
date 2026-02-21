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
#define CAN_QUEUE_LENGTH 64
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
        case 0x551:
          display_data.water_temp = message.data[0];
          display_data.oil_press = message.data[1];
          display_data.updated_0x551 = true;
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
          break;
        case 0x554:
          display_data.map_press = message.data[0];
          display_data.coolant_press = message.data[1];
          display_data.updated_0x554 = true;
          break;
        case 0x555:
          display_data.ls_fuel_press = message.data[0];
          display_data.hs_fuel_press = message.data[1];
          display_data.updated_0x555 = true;
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

void update_display_values(uint8_t mode, uint8_t left_val, uint8_t right_val) {
  static int last_left = -999;
  static int last_right = -999;
  static uint8_t last_left_r = 255, last_left_g = 255, last_left_b = 255;
  static uint8_t last_right_r = 255, last_right_g = 255, last_right_b = 255;
  
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
      
    case 2: // Pressures
    case 3: // Fuel
      left_processed = left_val;
      right_processed = right_val;
      break;
  }
  
  // Update left value
  if (left_processed != last_left) {
    char text[4];
    itoa(left_processed, text, 10);
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
    char text[4];
    itoa(right_processed, text, 10);
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
  
  // Check for screen change request
  bool screen_change = false;
  portENTER_CRITICAL(&display_data_mutex);
  if (display_data.screen_change_requested) {
    display_data.screen_change_requested = false;
    screen_change = true;
  }
  portEXIT_CRITICAL(&display_data_mutex);
  
  if (screen_change) {
    uint8_t new_mode = (get_current_screen_mode() + 1) % 4;
    update_screen_labels(new_mode);
    Serial.printf("Switched to mode %d\n", new_mode);
  }
  
  // Rate limit data updates
  if (now - last_update_time < UPDATE_INTERVAL_MS) {
    return;
  }
  
  // Get data based on current mode
  uint8_t left_val = 0, right_val = 0;
  bool has_update = false;
  uint8_t mode = get_current_screen_mode();
  
  portENTER_CRITICAL(&display_data_mutex);
  switch (mode) {
    case 0:
      if (display_data.updated_0x551) {
        left_val = display_data.water_temp;
        right_val = display_data.oil_press;
        display_data.updated_0x551 = false;
        has_update = true;
      }
      break;
    case 1:
      if (display_data.updated_0x553) {
        left_val = display_data.left_afr;
        right_val = display_data.right_afr;
        display_data.updated_0x553 = false;
        has_update = true;
      }
      break;
    case 2:
      if (display_data.updated_0x554) {
        left_val = display_data.map_press;
        right_val = display_data.coolant_press;
        display_data.updated_0x554 = false;
        has_update = true;
      }
      break;
    case 3:
      if (display_data.updated_0x555) {
        left_val = display_data.ls_fuel_press;
        right_val = display_data.hs_fuel_press;
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
  vTaskDelay(pdMS_TO_TICKS(16));
}
