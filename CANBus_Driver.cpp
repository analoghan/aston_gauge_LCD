#include "Arduino.h"
#include "CANBus_Driver.h"
#include <stdio.h>

// Install and start the TWAI driver. Returns false (with the driver uninstalled) on failure.
bool canbus_init(void) {

  // Configure TWAI (CAN) with larger RX queue for high traffic
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_NORMAL);
    g_config.rx_queue_len = 64;  // Increase from default 5 to handle bursts
    g_config.tx_queue_len = 10;
    g_config.alerts_enabled = TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_BUS_ERROR | TWAI_ALERT_ERR_PASS |
                              TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED;
    
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();

    // Hardware filter: accept all IDs (software filtering via switch statement)
    // Needed because we listen to both 0x17C (keypad) and the 0x600-0x6FF M1 range
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
 
    // Install and start TWAI driver
    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        Serial.println("TWAI driver installed.");
    } else {
        Serial.println("Failed to install TWAI driver.");
        return false;
    }

    if (twai_start() == ESP_OK) {
        Serial.println("TWAI driver started. Listening for messages...");
    } else {
        Serial.println("Failed to start TWAI driver.");
        twai_driver_uninstall();
        return false;
    }

    // Flush any messages that accumulated in the RX buffer during init
    twai_message_t dummy;
    uint32_t flushed = 0;
    while (twai_receive(&dummy, 0) == ESP_OK) { flushed++; }
    Serial.printf("Flushed %lu stale CAN messages after init\n", flushed);
    return true;
}