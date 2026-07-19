/**
 * @file bsp_lcd.c
 * @brief 基于 esp_lcd_st7796 组件实现 MSP3526 显示模块的板级驱动。
 */

#include <stdint.h>

#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

#include "esp_lcd_st7796.h"

#include "bsp_lcd.h"

static const char *TAG = "BSP_LCD";

static bool s_spi_bus_initialized;
static esp_lcd_panel_io_handle_t s_io_handle;
static esp_lcd_panel_handle_t s_panel_handle;

/*
 * MSP3526 模块专用的 ST7796S 初始化序列，移植自厂家 ESP32 示例。
 * 0x11、0x36、0x3A 和 0x29 由 esp_lcd_st7796 根据面板配置自动发送。
 * 每一项依次表示：命令码、参数数组、参数字节数、命令执行后的等待时间。
 */
static const st7796_lcd_init_cmd_t s_msp3526_init_cmds[] = {
    {0xF0, (uint8_t[]){0xC3}, 1, 0},
    {0xF0, (uint8_t[]){0x96}, 1, 0},
    {0xB4, (uint8_t[]){0x02}, 1, 0},
    {0xB7, (uint8_t[]){0xC6}, 1, 0},
    {0xC0, (uint8_t[]){0xC0, 0x00}, 2, 0},
    {0xC1, (uint8_t[]){0x13}, 1, 0},
    {0xC2, (uint8_t[]){0xA7}, 1, 0},
    {0xC5, (uint8_t[]){0x21}, 1, 0},
    {0xE8, (uint8_t[]){0x40, 0x8A, 0x1B, 0x1B, 0x23, 0x0A, 0xAC, 0x33}, 8, 0},
    {0xE0, (uint8_t[]){0xD2, 0x05, 0x08, 0x06, 0x05, 0x02, 0x2A, 0x44, 0x46, 0x39, 0x15, 0x15, 0x2D, 0x32}, 14, 0},
    {0xE1, (uint8_t[]){0x96, 0x08, 0x0C, 0x09, 0x09, 0x25, 0x2E, 0x43, 0x42, 0x35, 0x11, 0x11, 0x28, 0x2E}, 14, 0},
    {0xF0, (uint8_t[]){0x3C}, 1, 0},
    {0xF0, (uint8_t[]){0x69}, 1, 0},
    {0x21, NULL, 0, 120},
};

/* 初始化失败时按依赖关系逆序释放已经创建的硬件资源。 */
static void bsp_lcd_cleanup(void)
{
    esp_err_t result;

    if (s_panel_handle != NULL) {
        result = esp_lcd_panel_del(s_panel_handle);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "释放 ST7796 面板失败：%s", esp_err_to_name(result));
        }
        s_panel_handle = NULL;
    }

    if (s_io_handle != NULL) {
        result = esp_lcd_panel_io_del(s_io_handle);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "释放 LCD Panel IO 失败：%s", esp_err_to_name(result));
        }
        s_io_handle = NULL;
    }

    if (s_spi_bus_initialized) {
        result = spi_bus_free(BSP_LCD_SPI_HOST);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "释放 LCD SPI 总线失败：%s", esp_err_to_name(result));
        }
        s_spi_bus_initialized = false;
    }
}

esp_err_t bsp_lcd_init(esp_lcd_panel_io_handle_t *out_io,
                       esp_lcd_panel_handle_t *out_panel)
{
    esp_err_t result;
    const spi_bus_config_t bus_config = {
        .sclk_io_num = BSP_LCD_GPIO_SCLK,
        .mosi_io_num = BSP_LCD_GPIO_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = BSP_LCD_H_RES * BSP_LCD_DRAW_BUFFER_LINES * sizeof(uint16_t),
    };
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = BSP_LCD_GPIO_CS,
        .dc_gpio_num = BSP_LCD_GPIO_DC,
        .spi_mode = BSP_LCD_SPI_MODE,
        .pclk_hz = BSP_LCD_SPI_CLOCK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    st7796_vendor_config_t vendor_config = {
        .init_cmds = s_msp3526_init_cmds,
        .init_cmds_size = sizeof(s_msp3526_init_cmds) / sizeof(s_msp3526_init_cmds[0]),
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_GPIO_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };

    if (out_io == NULL || out_panel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_io = NULL;
    *out_panel = NULL;

    if (s_io_handle != NULL && s_panel_handle != NULL) {
        *out_io = s_io_handle;
        *out_panel = s_panel_handle;
        ESP_LOGI(TAG, "LCD 已初始化，直接返回现有句柄");
        return ESP_OK;
    }

    if (s_io_handle != NULL || s_panel_handle != NULL || s_spi_bus_initialized) {
        bsp_lcd_cleanup();
    }

    ESP_LOGI(TAG, "正在初始化 LCD：CS=%d RST=%d DC=%d MOSI=%d SCLK=%d",
             BSP_LCD_GPIO_CS, BSP_LCD_GPIO_RST, BSP_LCD_GPIO_DC,
             BSP_LCD_GPIO_MOSI, BSP_LCD_GPIO_SCLK);

    result = spi_bus_initialize(BSP_LCD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "LCD SPI 总线初始化失败：%s", esp_err_to_name(result));
        goto cleanup;
    }
    s_spi_bus_initialized = true;

    result = esp_lcd_new_panel_io_spi(BSP_LCD_SPI_HOST, &io_config, &s_io_handle);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "LCD Panel IO 创建失败：%s", esp_err_to_name(result));
        goto cleanup;
    }

    result = esp_lcd_new_panel_st7796(s_io_handle, &panel_config, &s_panel_handle);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "ST7796 面板驱动创建失败：%s", esp_err_to_name(result));
        goto cleanup;
    }

    result = esp_lcd_panel_reset(s_panel_handle);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "ST7796 硬件复位失败：%s", esp_err_to_name(result));
        goto cleanup;
    }

    result = esp_lcd_panel_init(s_panel_handle);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "ST7796 初始化失败：%s", esp_err_to_name(result));
        goto cleanup;
    }

    result = esp_lcd_panel_swap_xy(s_panel_handle, BSP_LCD_SWAP_XY);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "ST7796 交换坐标轴失败：%s", esp_err_to_name(result));
        goto cleanup;
    }

    result = esp_lcd_panel_mirror(s_panel_handle, BSP_LCD_MIRROR_X, BSP_LCD_MIRROR_Y);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "ST7796 镜像配置失败：%s", esp_err_to_name(result));
        goto cleanup;
    }

    result = esp_lcd_panel_disp_on_off(s_panel_handle, true);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "ST7796 打开显示失败：%s", esp_err_to_name(result));
        goto cleanup;
    }

    *out_io = s_io_handle;
    *out_panel = s_panel_handle;
    ESP_LOGI(TAG, "LCD 初始化完成：%dx%d，SPI %d MHz，RGB565/BGR",
             BSP_LCD_H_RES, BSP_LCD_V_RES, BSP_LCD_SPI_CLOCK_HZ / 1000000);
    return ESP_OK;

cleanup:
    bsp_lcd_cleanup();
    return result;
}

