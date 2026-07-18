/**
 * @file touch_test.c
 * @brief 初始化 FT6336U 触摸控制器，并将其注册为 LVGL 输入设备。
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"

#include "esp_lcd_touch_ft6x36.h"
#include "esp_lvgl_port.h"

#include "touch_test.h"

#define TOUCH_TEST_I2C_PORT      I2C_NUM_0
#define TOUCH_TEST_I2C_CLOCK_HZ  (100 * 1000)
#define TOUCH_TEST_GPIO_SCL      GPIO_NUM_9
#define TOUCH_TEST_GPIO_SDA      GPIO_NUM_10
#define TOUCH_TEST_GPIO_RST      GPIO_NUM_11
#define TOUCH_TEST_H_RES         320
#define TOUCH_TEST_V_RES         480
#define TOUCH_TEST_RESET_HIGH_MS 20
#define TOUCH_TEST_RESET_LOW_MS  20
#define TOUCH_TEST_BOOT_WAIT_MS  500

static const char *TAG = "TOUCH_TEST";

static esp_err_t touch_test_reset_with_vendor_timing(void)
{
    const gpio_config_t reset_gpio_config = {
        .pin_bit_mask = 1ULL << TOUCH_TEST_GPIO_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&reset_gpio_config), TAG, "Touch reset GPIO configuration failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(TOUCH_TEST_GPIO_RST, 1), TAG, "Touch reset GPIO set high failed");
    vTaskDelay(pdMS_TO_TICKS(TOUCH_TEST_RESET_HIGH_MS));
    ESP_RETURN_ON_ERROR(gpio_set_level(TOUCH_TEST_GPIO_RST, 0), TAG, "Touch reset GPIO set low failed");
    vTaskDelay(pdMS_TO_TICKS(TOUCH_TEST_RESET_LOW_MS));
    ESP_RETURN_ON_ERROR(gpio_set_level(TOUCH_TEST_GPIO_RST, 1), TAG, "Touch reset GPIO release failed");
    vTaskDelay(pdMS_TO_TICKS(TOUCH_TEST_BOOT_WAIT_MS));

    ESP_LOGI(TAG, "FT6336U reset complete: HIGH %d ms, LOW %d ms, boot wait %d ms",
             TOUCH_TEST_RESET_HIGH_MS, TOUCH_TEST_RESET_LOW_MS, TOUCH_TEST_BOOT_WAIT_MS);
    return ESP_OK;
}

esp_err_t touch_test_init(lv_display_t *display)
{
    i2c_master_bus_handle_t i2c_bus = NULL;
    esp_lcd_panel_io_handle_t touch_io = NULL;
    esp_lcd_touch_handle_t touch_handle = NULL;
    const i2c_master_bus_config_t i2c_config = {
        .i2c_port = TOUCH_TEST_I2C_PORT,
        .sda_io_num = TOUCH_TEST_GPIO_SDA,
        .scl_io_num = TOUCH_TEST_GPIO_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    const esp_lcd_touch_config_t touch_config = {
        .x_max = TOUCH_TEST_H_RES,
        .y_max = TOUCH_TEST_V_RES,
        /* 已按厂商要求完成 500 ms 复位等待，禁止组件再次执行短复位。 */
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
    };

    ESP_RETURN_ON_FALSE(display != NULL, ESP_ERR_INVALID_ARG, TAG, "LVGL display is NULL");

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_config, &i2c_bus), TAG, "I2C bus initialization failed");
    ESP_LOGI(TAG, "I2C bus initialized: SCL=%d SDA=%d, %d kHz",
             TOUCH_TEST_GPIO_SCL, TOUCH_TEST_GPIO_SDA, TOUCH_TEST_I2C_CLOCK_HZ / 1000);

    ESP_RETURN_ON_ERROR(touch_test_reset_with_vendor_timing(), TAG, "FT6336U hardware reset failed");

    esp_lcd_panel_io_i2c_config_t touch_io_config = ESP_LCD_TOUCH_IO_I2C_FT6x36_CONFIG();
    touch_io_config.scl_speed_hz = TOUCH_TEST_I2C_CLOCK_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(i2c_bus, &touch_io_config, &touch_io), TAG,
                        "Touch I2C IO initialization failed");
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft6x36(touch_io, &touch_config, &touch_handle), TAG,
                        "FT6336U initialization failed");
    ESP_LOGI(TAG, "FT6336U initialized at I2C address 0x38");

    ESP_RETURN_ON_FALSE(lvgl_port_add_touch(&(lvgl_port_touch_cfg_t) {
        .disp = display,
        .handle = touch_handle,
    }) != NULL, ESP_ERR_NO_MEM, TAG, "LVGL touch input registration failed");
    ESP_LOGI(TAG, "LVGL touch input registered (polling mode)");

    return ESP_OK;
}
