/**
 * @file bsp_touch.c
 * @brief 基于 FT6x36 组件实现 FT6336U 触摸控制器的板级驱动。
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"

#include "esp_lcd_touch_ft6x36.h"

#include "bsp_touch.h"

#define BSP_TOUCH_RESET_HIGH_MS     20
#define BSP_TOUCH_RESET_LOW_MS      20
#define BSP_TOUCH_BOOT_WAIT_MS      500
#define BSP_TOUCH_PROBE_TIMEOUT_MS  100
#define BSP_TOUCH_PROBE_RETRIES     3

static const char *TAG = "BSP_TOUCH";

static i2c_master_bus_handle_t s_i2c_bus;
static esp_lcd_panel_io_handle_t s_touch_io;
static esp_lcd_touch_handle_t s_touch_handle;

/* 按厂家时序执行 RST 高 20 ms、低 20 ms、重新拉高并等待芯片启动。 */
static esp_err_t bsp_touch_reset(void)
{
    esp_err_t result;
    const gpio_config_t reset_gpio_config = {
        .pin_bit_mask = 1ULL << BSP_TOUCH_GPIO_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    result = gpio_config(&reset_gpio_config);
    if (result != ESP_OK) {
        return result;
    }
    result = gpio_set_level(BSP_TOUCH_GPIO_RST, 1);
    if (result != ESP_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(BSP_TOUCH_RESET_HIGH_MS));

    result = gpio_set_level(BSP_TOUCH_GPIO_RST, 0);
    if (result != ESP_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(BSP_TOUCH_RESET_LOW_MS));

    result = gpio_set_level(BSP_TOUCH_GPIO_RST, 1);
    if (result != ESP_OK) {
        return result;
    }
    vTaskDelay(pdMS_TO_TICKS(BSP_TOUCH_BOOT_WAIT_MS));

    ESP_LOGI(TAG, "FT6336U 复位完成：高 %d ms，低 %d ms，启动等待 %d ms",
             BSP_TOUCH_RESET_HIGH_MS, BSP_TOUCH_RESET_LOW_MS, BSP_TOUCH_BOOT_WAIT_MS);
    return ESP_OK;
}

/* 探测失败后恢复 I2C 总线并重新复位 FT6336U，最多尝试三次。 */
static esp_err_t bsp_touch_probe(void)
{
    esp_err_t result = ESP_FAIL;

    for (int attempt = 1; attempt <= BSP_TOUCH_PROBE_RETRIES; attempt++) {
        ESP_LOGI(TAG, "正在探测 I2C 地址 0x%02X（第 %d/%d 次）",
                 BSP_TOUCH_I2C_ADDRESS, attempt, BSP_TOUCH_PROBE_RETRIES);
        result = i2c_master_probe(s_i2c_bus, BSP_TOUCH_I2C_ADDRESS, BSP_TOUCH_PROBE_TIMEOUT_MS);
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "I2C 地址 0x%02X 探测成功", BSP_TOUCH_I2C_ADDRESS);
            return ESP_OK;
        }

        ESP_LOGW(TAG, "I2C 地址 0x%02X 探测失败：%s",
                 BSP_TOUCH_I2C_ADDRESS, esp_err_to_name(result));
        if (attempt == BSP_TOUCH_PROBE_RETRIES) {
            break;
        }

        result = i2c_master_bus_reset(s_i2c_bus);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "I2C 总线恢复失败：%s", esp_err_to_name(result));
            return result;
        }
        ESP_LOGI(TAG, "I2C 总线恢复完成，重新复位 FT6336U");

        result = bsp_touch_reset();
        if (result != ESP_OK) {
            return result;
        }
    }

    return result;
}

/* 初始化失败时按触摸驱动、Panel IO、I2C 总线的顺序逆序释放。 */
static void bsp_touch_cleanup(void)
{
    esp_err_t result;

    if (s_touch_handle != NULL) {
        result = esp_lcd_touch_del(s_touch_handle);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "释放触摸驱动失败：%s", esp_err_to_name(result));
        }
        s_touch_handle = NULL;
    }

    if (s_touch_io != NULL) {
        result = esp_lcd_panel_io_del(s_touch_io);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "释放触摸 I2C 通信对象失败：%s", esp_err_to_name(result));
        }
        s_touch_io = NULL;
    }

    if (s_i2c_bus != NULL) {
        result = i2c_del_master_bus(s_i2c_bus);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "释放触摸 I2C 总线失败：%s", esp_err_to_name(result));
        }
        s_i2c_bus = NULL;
    }
}

esp_err_t bsp_touch_init(esp_lcd_touch_handle_t *out_touch)
{
    esp_err_t result;
    const i2c_master_bus_config_t i2c_config = {
        .i2c_port = BSP_TOUCH_I2C_PORT,
        .sda_io_num = BSP_TOUCH_GPIO_SDA,
        .scl_io_num = BSP_TOUCH_GPIO_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };
    const esp_lcd_touch_config_t touch_config = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        /* BSP 已执行厂家复位时序，禁止组件再次执行较短的内部复位。 */
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = BSP_TOUCH_GPIO_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = BSP_TOUCH_SWAP_XY,
            .mirror_x = BSP_TOUCH_MIRROR_X,
            .mirror_y = BSP_TOUCH_MIRROR_Y,
        },
    };

    if (out_touch == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_touch = NULL;

    if (s_touch_handle != NULL) {
        *out_touch = s_touch_handle;
        ESP_LOGI(TAG, "触摸控制器已初始化，直接返回现有句柄");
        return ESP_OK;
    }

    if (s_i2c_bus != NULL || s_touch_io != NULL) {
        bsp_touch_cleanup();
    }

    ESP_LOGI(TAG, "正在初始化触摸：SCL=%d SDA=%d RST=%d INT=%d，I2C %d kHz",
             BSP_TOUCH_GPIO_SCL, BSP_TOUCH_GPIO_SDA, BSP_TOUCH_GPIO_RST,
             BSP_TOUCH_GPIO_INT, BSP_TOUCH_I2C_CLOCK_HZ / 1000);

    result = i2c_new_master_bus(&i2c_config, &s_i2c_bus);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "触摸 I2C 总线初始化失败：%s", esp_err_to_name(result));
        goto cleanup;
    }

    result = bsp_touch_reset();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "FT6336U 硬件复位失败：%s", esp_err_to_name(result));
        goto cleanup;
    }

    result = bsp_touch_probe();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "FT6336U 地址探测最终失败：%s", esp_err_to_name(result));
        goto cleanup;
    }

    esp_lcd_panel_io_i2c_config_t touch_io_config = ESP_LCD_TOUCH_IO_I2C_FT6x36_CONFIG();
    touch_io_config.scl_speed_hz = BSP_TOUCH_I2C_CLOCK_HZ;
    result = esp_lcd_new_panel_io_i2c(s_i2c_bus, &touch_io_config, &s_touch_io);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "触摸 I2C 通信对象创建失败：%s", esp_err_to_name(result));
        goto cleanup;
    }

    result = esp_lcd_touch_new_i2c_ft6x36(s_touch_io, &touch_config, &s_touch_handle);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "FT6336U 驱动初始化失败：%s", esp_err_to_name(result));
        goto cleanup;
    }

    *out_touch = s_touch_handle;
    ESP_LOGI(TAG, "FT6336U 初始化完成：地址 0x%02X，GPIO%d 低电平中断",
             BSP_TOUCH_I2C_ADDRESS, BSP_TOUCH_GPIO_INT);
    return ESP_OK;

cleanup:
    bsp_touch_cleanup();
    return result;
}

