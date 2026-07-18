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
#define TOUCH_TEST_I2C_ADDRESS    0x38
#define TOUCH_TEST_PROBE_TIMEOUT_MS 100
#define TOUCH_TEST_PROBE_RETRIES  3

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

    ESP_RETURN_ON_ERROR(gpio_config(&reset_gpio_config), TAG, "触摸复位 GPIO 配置失败");
    ESP_RETURN_ON_ERROR(gpio_set_level(TOUCH_TEST_GPIO_RST, 1), TAG, "触摸复位 GPIO 输出高电平失败");
    vTaskDelay(pdMS_TO_TICKS(TOUCH_TEST_RESET_HIGH_MS));
    ESP_RETURN_ON_ERROR(gpio_set_level(TOUCH_TEST_GPIO_RST, 0), TAG, "触摸复位 GPIO 输出低电平失败");
    vTaskDelay(pdMS_TO_TICKS(TOUCH_TEST_RESET_LOW_MS));
    ESP_RETURN_ON_ERROR(gpio_set_level(TOUCH_TEST_GPIO_RST, 1), TAG, "触摸复位 GPIO 释放失败");
    vTaskDelay(pdMS_TO_TICKS(TOUCH_TEST_BOOT_WAIT_MS));

    ESP_LOGI(TAG, "FT6336U 复位完成：高电平 %d ms，低电平 %d ms，启动等待 %d ms",
             TOUCH_TEST_RESET_HIGH_MS, TOUCH_TEST_RESET_LOW_MS, TOUCH_TEST_BOOT_WAIT_MS);
    return ESP_OK;
}

static esp_err_t touch_test_probe_device(i2c_master_bus_handle_t i2c_bus)
{
    esp_err_t result = ESP_FAIL;

    for (int attempt = 1; attempt <= TOUCH_TEST_PROBE_RETRIES; attempt++) {
        ESP_LOGI(TAG, "正在探测 I2C 地址 0x%02X（第 %d/%d 次）",
                 TOUCH_TEST_I2C_ADDRESS, attempt, TOUCH_TEST_PROBE_RETRIES);
        result = i2c_master_probe(i2c_bus, TOUCH_TEST_I2C_ADDRESS, TOUCH_TEST_PROBE_TIMEOUT_MS);
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "I2C 地址 0x%02X 探测成功", TOUCH_TEST_I2C_ADDRESS);
            return ESP_OK;
        }

        ESP_LOGW(TAG, "I2C 地址 0x%02X 探测失败：%s",
                 TOUCH_TEST_I2C_ADDRESS, esp_err_to_name(result));
        if (attempt == TOUCH_TEST_PROBE_RETRIES) {
            break;
        }

        result = i2c_master_bus_reset(i2c_bus);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "I2C 总线恢复失败：%s", esp_err_to_name(result));
            return result;
        }
        ESP_LOGI(TAG, "I2C 总线恢复完成，重新复位 FT6336U");

        result = touch_test_reset_with_vendor_timing();
        if (result != ESP_OK) {
            return result;
        }
    }

    return result;
}

static void touch_test_cleanup(i2c_master_bus_handle_t i2c_bus,
                               esp_lcd_panel_io_handle_t touch_io,
                               esp_lcd_touch_handle_t touch_handle)
{
    esp_err_t result;

    if (touch_handle != NULL) {
        result = esp_lcd_touch_del(touch_handle);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "释放触摸驱动失败：%s", esp_err_to_name(result));
        }
    }
    if (touch_io != NULL) {
        result = esp_lcd_panel_io_del(touch_io);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "释放触摸 I2C 通信对象失败：%s", esp_err_to_name(result));
        }
    }
    if (i2c_bus != NULL) {
        result = i2c_del_master_bus(i2c_bus);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "释放 I2C 总线失败：%s", esp_err_to_name(result));
        }
    }
}

esp_err_t touch_test_init(lv_display_t *display)
{
    esp_err_t result;
    i2c_master_bus_handle_t i2c_bus = NULL;
    esp_lcd_panel_io_handle_t touch_io = NULL;
    esp_lcd_touch_handle_t touch_handle = NULL;
    const i2c_master_bus_config_t i2c_config = {
        .i2c_port = TOUCH_TEST_I2C_PORT,
        .sda_io_num = TOUCH_TEST_GPIO_SDA,
        .scl_io_num = TOUCH_TEST_GPIO_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
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

    ESP_RETURN_ON_FALSE(display != NULL, ESP_ERR_INVALID_ARG, TAG, "LVGL 显示器句柄为空");

    result = i2c_new_master_bus(&i2c_config, &i2c_bus);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "I2C 总线初始化失败：%s", esp_err_to_name(result));
        return result;
    }
    ESP_LOGI(TAG, "I2C 总线初始化完成：SCL=%d SDA=%d，%d kHz，内部上拉已启用",
             TOUCH_TEST_GPIO_SCL, TOUCH_TEST_GPIO_SDA, TOUCH_TEST_I2C_CLOCK_HZ / 1000);

    result = touch_test_reset_with_vendor_timing();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "FT6336U 硬件复位失败：%s", esp_err_to_name(result));
        goto cleanup;
    }

    result = touch_test_probe_device(i2c_bus);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "FT6336U 地址探测最终失败：%s", esp_err_to_name(result));
        goto cleanup;
    }

    esp_lcd_panel_io_i2c_config_t touch_io_config = ESP_LCD_TOUCH_IO_I2C_FT6x36_CONFIG();
    touch_io_config.scl_speed_hz = TOUCH_TEST_I2C_CLOCK_HZ;
    ESP_LOGI(TAG, "正在创建触摸 I2C 通信对象");
    result = esp_lcd_new_panel_io_i2c(i2c_bus, &touch_io_config, &touch_io);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "触摸 I2C 通信对象创建失败：%s", esp_err_to_name(result));
        goto cleanup;
    }
    ESP_LOGI(TAG, "触摸 I2C 通信对象创建完成");

    ESP_LOGI(TAG, "正在初始化 FT6336U 驱动并读取芯片信息");
    result = esp_lcd_touch_new_i2c_ft6x36(touch_io, &touch_config, &touch_handle);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "FT6336U 驱动初始化失败：%s", esp_err_to_name(result));
        goto cleanup;
    }
    ESP_LOGI(TAG, "FT6336U 初始化完成，I2C 地址为 0x%02X", TOUCH_TEST_I2C_ADDRESS);

    ESP_LOGI(TAG, "正在注册 LVGL 触摸输入设备");
    if (lvgl_port_add_touch(&(lvgl_port_touch_cfg_t) {
        .disp = display,
        .handle = touch_handle,
    }) == NULL) {
        result = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "LVGL 触摸输入设备注册失败：内存不足");
        goto cleanup;
    }
    ESP_LOGI(TAG, "LVGL 触摸输入设备注册完成，当前使用轮询模式");

    return ESP_OK;

cleanup:
    touch_test_cleanup(i2c_bus, touch_io, touch_handle);
    return result;
}
