#include "Display_ST7701.h"  
#include "driver/gpio.h"
      
spi_device_handle_t SPI_handle = NULL;     
esp_lcd_panel_handle_t panel_handle = NULL;            
void st7701_write_command(uint8_t cmd) {
  spi_transaction_t spi_tran = {
    .cmd = 0,
    .addr = cmd,
    .length = 0,
    .rxlength = 0,
  };
  spi_device_transmit(SPI_handle, &spi_tran);
}

void st7701_write_data(uint8_t data) {
  spi_transaction_t spi_tran = {
    .cmd = 1,
    .addr = data,
    .length = 0,
    .rxlength = 0,
  };
  spi_device_transmit(SPI_handle, &spi_tran);
}

void st7701_cs_en(){
  set_exio(EXIO_PIN3, Low);

  vTaskDelay(pdMS_TO_TICKS(10));
}

void st7701_cs_dis(){
  set_exio(EXIO_PIN3, High);

  vTaskDelay(pdMS_TO_TICKS(10));
}

void st7701_reset(){
  set_exio(EXIO_PIN1, Low);
  vTaskDelay(pdMS_TO_TICKS(10));
  set_exio(EXIO_PIN1, High);
  vTaskDelay(pdMS_TO_TICKS(50));
}

void st7701_init() {
  spi_bus_config_t buscfg = {
    .mosi_io_num = LCD_MOSI_PIN,
    .miso_io_num = -1,
    .sclk_io_num = LCD_CLK_PIN,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 64,
  };

  spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
  spi_device_interface_config_t devcfg = {
    .command_bits = 1,
    .address_bits = 8,
    .mode = SPI_MODE0,
    .clock_speed_hz = 40000000,
    .spics_io_num = -1,                      
    .queue_size = 1,
  };
  spi_bus_add_device(SPI2_HOST, &devcfg, &SPI_handle);            

  // gpio_reset_pin(LCD_CS_EX_PIN);
  // gpio_set_direction(LCD_CS_EX_PIN, GPIO_MODE_OUTPUT);

  st7701_cs_en();

  st7701_write_command(0xff);
  st7701_write_data(0x77);
  st7701_write_data(0x01);
  st7701_write_data(0x00);
  st7701_write_data(0x00);
  st7701_write_data(0x13);
  st7701_write_command(0xef);
  st7701_write_data(0x08);
  st7701_write_command(0xff);
  st7701_write_data(0x77);
  st7701_write_data(0x01);
  st7701_write_data(0x00);
  st7701_write_data(0x00);
  st7701_write_data(0x10);
  st7701_write_command(0xc0);
  st7701_write_data(0x77);
  st7701_write_data(0x00);
  st7701_write_command(0xc1);
  st7701_write_data(0x11);
  st7701_write_data(0x0c);
  st7701_write_command(0xc2);
  st7701_write_data(0x07);
  st7701_write_data(0x02);
  st7701_write_command(0xcc);
  st7701_write_data(0x30);
  st7701_write_command(0xB0);
  st7701_write_data(0x06);
  st7701_write_data(0xCF);
  st7701_write_data(0x14);
  st7701_write_data(0x0C);
  st7701_write_data(0x0F);
  st7701_write_data(0x03);
  st7701_write_data(0x00);
  st7701_write_data(0x0A);
  st7701_write_data(0x07);
  st7701_write_data(0x1B);
  st7701_write_data(0x03);
  st7701_write_data(0x12);
  st7701_write_data(0x10);
  st7701_write_data(0x25);
  st7701_write_data(0x36);
  st7701_write_data(0x1E);
  st7701_write_command(0xB1);
  st7701_write_data(0x0C);
  st7701_write_data(0xD4);
  st7701_write_data(0x18);
  st7701_write_data(0x0C);
  st7701_write_data(0x0E);
  st7701_write_data(0x06);
  st7701_write_data(0x03);
  st7701_write_data(0x06);
  st7701_write_data(0x08);
  st7701_write_data(0x23);
  st7701_write_data(0x06);
  st7701_write_data(0x12);
  st7701_write_data(0x10);
  st7701_write_data(0x30);
  st7701_write_data(0x2F);
  st7701_write_data(0x1F);
  st7701_write_command(0xff);
  st7701_write_data(0x77);
  st7701_write_data(0x01);
  st7701_write_data(0x00);
  st7701_write_data(0x00);
  st7701_write_data(0x11);
  st7701_write_command(0xb0);
  st7701_write_data(0x73);
  st7701_write_command(0xb1);
  st7701_write_data(0x7C);
  st7701_write_command(0xb2);
  st7701_write_data(0x83);
  st7701_write_command(0xb3);
  st7701_write_data(0x80);
  st7701_write_command(0xb5);
  st7701_write_data(0x49);
  st7701_write_command(0xb7);
  st7701_write_data(0x87);
  st7701_write_command(0xb8);
  st7701_write_data(0x33);
  st7701_write_command(0xb9);
  st7701_write_data(0x10);
  st7701_write_data(0x1f);
  st7701_write_command(0xbb);
  st7701_write_data(0x03);
  st7701_write_command(0xc1);
  st7701_write_data(0x08);
  st7701_write_command(0xc2);
  st7701_write_data(0x08);
  st7701_write_command(0xd0);
  st7701_write_data(0x88);
  st7701_write_command(0xe0);
  st7701_write_data(0x00);
  st7701_write_data(0x00);
  st7701_write_data(0x02);
  st7701_write_data(0x00);
  st7701_write_data(0x00);
  st7701_write_data(0x0c);
  st7701_write_command(0xe1);
  st7701_write_data(0x05);
  st7701_write_data(0x96);
  st7701_write_data(0x07);
  st7701_write_data(0x96);
  st7701_write_data(0x06);
  st7701_write_data(0x96);
  st7701_write_data(0x08);
  st7701_write_data(0x96);
  st7701_write_data(0x00);
  st7701_write_data(0x44);
  st7701_write_data(0x44);
  st7701_write_command(0xe2);
  st7701_write_data(0x00);
  st7701_write_data(0x00);
  st7701_write_data(0x03);
  st7701_write_data(0x03);
  st7701_write_data(0x00);
  st7701_write_data(0x00);
  st7701_write_data(0x02);
  st7701_write_data(0x00);
  st7701_write_data(0x00);
  st7701_write_data(0x00);
  st7701_write_data(0x02);
  st7701_write_data(0x00);
  st7701_write_command(0xe3);
  st7701_write_data(0x00);
  st7701_write_data(0x00);
  st7701_write_data(0x33);
  st7701_write_data(0x33);
  st7701_write_command(0xe4);
  st7701_write_data(0x44);
  st7701_write_data(0x44);
  st7701_write_command(0xe5);
  st7701_write_data(0x0d);
  st7701_write_data(0xd4);
  st7701_write_data(0x28);
  st7701_write_data(0x8c);
  st7701_write_data(0x0f);
  st7701_write_data(0xd6);
  st7701_write_data(0x28);
  st7701_write_data(0x8c);
  st7701_write_data(0x09);
  st7701_write_data(0xd0);
  st7701_write_data(0x28);
  st7701_write_data(0x8c);
  st7701_write_data(0x0b);
  st7701_write_data(0xd2);
  st7701_write_data(0x28);
  st7701_write_data(0x8c);
  st7701_write_command(0xe6);
  st7701_write_data(0x00);
  st7701_write_data(0x00);
  st7701_write_data(0x33);
  st7701_write_data(0x33);
  st7701_write_command(0xe7);
  st7701_write_data(0x44);
  st7701_write_data(0x44);
  st7701_write_command(0xe8);
  st7701_write_data(0x0e);
  st7701_write_data(0xd5);
  st7701_write_data(0x28);
  st7701_write_data(0x8c);
  st7701_write_data(0x10);
  st7701_write_data(0xd7);
  st7701_write_data(0x28);
  st7701_write_data(0x8c);
  st7701_write_data(0x0a);
  st7701_write_data(0xd1);
  st7701_write_data(0x28);
  st7701_write_data(0x8c);
  st7701_write_data(0x0c);
  st7701_write_data(0xd3);
  st7701_write_data(0x28);
  st7701_write_data(0x8c);
  st7701_write_command(0xeb);
  st7701_write_data(0x00);
  st7701_write_data(0x01);
  st7701_write_data(0xe4);
  st7701_write_data(0xe4);
  st7701_write_data(0x44);
  st7701_write_data(0x00);
  st7701_write_command(0xed);
  st7701_write_data(0xf3);
  st7701_write_data(0xc1);
  st7701_write_data(0xba);
  st7701_write_data(0x0f);
  st7701_write_data(0x66);
  st7701_write_data(0x77);
  st7701_write_data(0x44);
  st7701_write_data(0x55);
  st7701_write_data(0x55);
  st7701_write_data(0x44);
  st7701_write_data(0x77);
  st7701_write_data(0x66);
  st7701_write_data(0xf0);
  st7701_write_data(0xab);
  st7701_write_data(0x1c);
  st7701_write_data(0x3f);
  st7701_write_command(0xef);
  st7701_write_data(0x10);
  st7701_write_data(0x0d);
  st7701_write_data(0x04);
  st7701_write_data(0x08);
  st7701_write_data(0x3f);
  st7701_write_data(0x1f);
  st7701_write_command(0xff);
  st7701_write_data(0x77);
  st7701_write_data(0x01);
  st7701_write_data(0x00);
  st7701_write_data(0x00);
  st7701_write_data(0x13);
  st7701_write_command(0xe8);
  st7701_write_data(0x00);
  st7701_write_data(0x0e);
  st7701_write_command(0x11);
  vTaskDelay(pdMS_TO_TICKS(120));
  st7701_write_command(0xe8);
  st7701_write_data(0x00);
  st7701_write_data(0x0c);
  vTaskDelay(pdMS_TO_TICKS(10));
  st7701_write_command(0xe8);
  st7701_write_data(0x40);
  st7701_write_data(0x00);
  st7701_write_command(0xff);
  st7701_write_data(0x77);
  st7701_write_data(0x01);
  st7701_write_data(0x00);
  st7701_write_data(0x00);
  st7701_write_data(0x00);
  st7701_write_command(0x36);  // MADCTL - Memory Access Control
  st7701_write_data(0x00);     // 0x00 = no rotation (default)
  st7701_write_command(0x3A);
  st7701_write_data(0x66);
  st7701_write_command(0x29);
  vTaskDelay(pdMS_TO_TICKS(20));
  
 /* 
  st7701_write_command(0xff);
  st7701_write_data(0x77);
  st7701_write_data(0x01);
  st7701_write_data(0x00);
  st7701_write_data(0x00);
  st7701_write_data(0x10);
  st7701_write_command(0xe5);
  st7701_write_data(0x00);
  st7701_write_data(0x00);
*/
  st7701_cs_dis();

  //  RGB
  esp_lcd_rgb_panel_config_t rgb_config = {
    .clk_src = LCD_CLK_SRC_XTAL,
    .timings =  {
      .pclk_hz = ESP_PANEL_LCD_RGB_TIMING_FREQ_HZ,
      .h_res = ESP_PANEL_LCD_HEIGHT,
      .v_res = ESP_PANEL_LCD_WIDTH,
      .hsync_pulse_width = ESP_PANEL_LCD_RGB_TIMING_HPW,
      .hsync_back_porch = ESP_PANEL_LCD_RGB_TIMING_HBP,
      .hsync_front_porch = ESP_PANEL_LCD_RGB_TIMING_HFP,
      .vsync_pulse_width = ESP_PANEL_LCD_RGB_TIMING_VPW,
      .vsync_back_porch = ESP_PANEL_LCD_RGB_TIMING_VBP,
      .vsync_front_porch = ESP_PANEL_LCD_RGB_TIMING_VFP,
      .flags = {
        .hsync_idle_low = 0,
        .vsync_idle_low = 0,
        .de_idle_high = 0,
        .pclk_active_neg = false,
        .pclk_idle_high = 0,
      },
    },
    .data_width = ESP_PANEL_LCD_RGB_DATA_WIDTH,
    .bits_per_pixel = ESP_PANEL_LCD_RGB_PIXEL_BITS,
    .num_fbs = ESP_PANEL_LCD_RGB_FRAME_BUF_NUM,
    .bounce_buffer_size_px = ESP_PANEL_LCD_RGB_BOUNCE_BUF_SIZE,
    .psram_trans_align = 64,
    .hsync_gpio_num = ESP_PANEL_LCD_PIN_NUM_RGB_HSYNC,
    .vsync_gpio_num = ESP_PANEL_LCD_PIN_NUM_RGB_VSYNC,
    .de_gpio_num = ESP_PANEL_LCD_PIN_NUM_RGB_DE,
    .pclk_gpio_num = ESP_PANEL_LCD_PIN_NUM_RGB_PCLK,
    .disp_gpio_num = ESP_PANEL_LCD_PIN_NUM_RGB_DISP,
    .data_gpio_nums = {
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA0,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA1,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA2,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA3,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA4,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA5,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA6,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA7,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA8,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA9,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA10,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA11,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA12,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA13,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA14,
      ESP_PANEL_LCD_PIN_NUM_RGB_DATA15,
    },
    .flags = {
      .disp_active_low = 0,
      .refresh_on_demand = 0,
      .fb_in_psram = true,
      .double_fb = true,
      .no_fb = 0,
      .bb_invalidate_cache = 0,
    },
  };
  esp_lcd_new_rgb_panel(&rgb_config, &panel_handle); 
  esp_lcd_panel_reset(panel_handle);
  esp_lcd_panel_init(panel_handle);
}

void lcd_init() {
  st7701_reset();
  st7701_init();
  backlight_init();
}

void lcd_add_window(uint16_t Xstart, uint16_t Xend, uint16_t Ystart, uint16_t Yend, uint8_t *color) {
  Xend = Xend + 1;
  Yend = Yend + 1;
  if (Xend >= ESP_PANEL_LCD_WIDTH)
    Xend = ESP_PANEL_LCD_WIDTH;
  if (Yend >= ESP_PANEL_LCD_HEIGHT)
    Yend = ESP_PANEL_LCD_HEIGHT;
  
  // Validate coordinates before calling draw_bitmap
  if (Xstart >= Xend || Ystart >= Yend) {
    return; // Skip invalid coordinates
  }
   
  esp_lcd_panel_draw_bitmap(panel_handle, Xstart, Ystart, Xend, Yend, color);
}

void backlight_init() {
  ledcAttach(LCD_BACKLIGHT_PIN, frequency, resolution);  
}

void set_backlight(uint8_t light) {
  if (light > backlight_max) {
    light = backlight_max;
  }

  uint32_t backlight = (light * 255) / backlight_max;
  ledcWrite(LCD_BACKLIGHT_PIN, backlight);
}


