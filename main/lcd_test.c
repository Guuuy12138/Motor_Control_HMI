#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

#include "esp_lcd_st7796.h"
#include "lcd_test.h"

#define LCD_TEST_H_RES              320
#define LCD_TEST_V_RES              480
#define LCD_TEST_DRAW_LINES         20
#define LCD_TEST_SPI_HOST           SPI2_HOST
#define LCD_TEST_SPI_CLOCK_HZ       (60 * 1000 * 1000)

#define LCD_TEST_GPIO_CS            GPIO_NUM_4
#define LCD_TEST_GPIO_RST           GPIO_NUM_5
#define LCD_TEST_GPIO_DC            GPIO_NUM_6
#define LCD_TEST_GPIO_MOSI          GPIO_NUM_7
#define LCD_TEST_GPIO_SCLK          GPIO_NUM_8

#define RGB565_RED                  0xF800
#define RGB565_GREEN                0x07E0
#define RGB565_BLUE                 0x001F
#define RGB565_WHITE                0xFFFF

static const char *TAG = "LCD_TEST";

/*
 * MSP3526 module-specific ST7796S initialization commands. MADCTL and COLMOD
 * are intentionally omitted because esp_lcd_st7796 sets them from panel_config.
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
    {0xF0, (uint8_t[]){0x69}, 1, 120},
    {0x21, NULL, 0, 0},
};

static SemaphoreHandle_t s_transfer_done;

static bool IRAM_ATTR lcd_color_transfer_done(esp_lcd_panel_io_handle_t panel_io,
                                               esp_lcd_panel_io_event_data_t *edata,
                                               void *user_ctx)
{
    BaseType_t high_task_woken = pdFALSE;

    (void)panel_io;
    (void)edata;
    (void)user_ctx;
    xSemaphoreGiveFromISR(s_transfer_done, &high_task_woken);
    return high_task_woken == pdTRUE;
}

static esp_lcd_panel_handle_t lcd_panel_init(void)
{
    ESP_LOGI(TAG, "SPI pins: CS=%d RST=%d DC=%d MOSI=%d SCLK=%d",
             LCD_TEST_GPIO_CS, LCD_TEST_GPIO_RST, LCD_TEST_GPIO_DC,
             LCD_TEST_GPIO_MOSI, LCD_TEST_GPIO_SCLK);

    const spi_bus_config_t bus_config = {
        .sclk_io_num = LCD_TEST_GPIO_SCLK,
        .mosi_io_num = LCD_TEST_GPIO_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_TEST_H_RES * LCD_TEST_DRAW_LINES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_TEST_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO));

    s_transfer_done = xSemaphoreCreateBinary();
    configASSERT(s_transfer_done != NULL);

    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_TEST_GPIO_CS,
        .dc_gpio_num = LCD_TEST_GPIO_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_TEST_SPI_CLOCK_HZ,
        .trans_queue_depth = 1,
        .on_color_trans_done = lcd_color_transfer_done,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_TEST_SPI_HOST, &io_config, &io_handle));

    st7796_vendor_config_t vendor_config = {
        .init_cmds = s_msp3526_init_cmds,
        .init_cmds_size = sizeof(s_msp3526_init_cmds) / sizeof(s_msp3526_init_cmds[0]),
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_TEST_GPIO_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };

    esp_lcd_panel_handle_t panel_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "ST7796 initialized: %dx%d, SPI %d MHz",
             LCD_TEST_H_RES, LCD_TEST_V_RES, LCD_TEST_SPI_CLOCK_HZ / 1000000);
    return panel_handle;
}

static void lcd_draw_solid_rect(esp_lcd_panel_handle_t panel_handle,
                                 uint16_t *draw_buffer,
                                 int x_start,
                                 int y_start,
                                 int width,
                                 int height,
                                 uint16_t color)
{
    const uint8_t color_high = color >> 8;
    const uint8_t color_low = color & 0xFF;
    const int pixels_per_buffer = LCD_TEST_H_RES * LCD_TEST_DRAW_LINES;

    for (int i = 0; i < pixels_per_buffer; i++) {
        uint8_t *pixel = (uint8_t *)&draw_buffer[i];
        pixel[0] = color_high;
        pixel[1] = color_low;
    }

    for (int line = 0; line < height; line += LCD_TEST_DRAW_LINES) {
        const int lines_to_draw = (height - line > LCD_TEST_DRAW_LINES) ? LCD_TEST_DRAW_LINES : height - line;
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle,
                                                   x_start,
                                                   y_start + line,
                                                   x_start + width,
                                                   y_start + line + lines_to_draw,
                                                   draw_buffer));
        xSemaphoreTake(s_transfer_done, portMAX_DELAY);
    }
}

static void lcd_show_fullscreen(esp_lcd_panel_handle_t panel_handle,
                                uint16_t *draw_buffer,
                                const char *name,
                                uint16_t color)
{
    ESP_LOGI(TAG, "Display %s", name);
    lcd_draw_solid_rect(panel_handle, draw_buffer, 0, 0,
                        LCD_TEST_H_RES, LCD_TEST_V_RES, color);
    vTaskDelay(pdMS_TO_TICKS(2000));
}

static void lcd_show_quadrants(esp_lcd_panel_handle_t panel_handle, uint16_t *draw_buffer)
{
    const int half_width = LCD_TEST_H_RES / 2;
    const int half_height = LCD_TEST_V_RES / 2;

    ESP_LOGI(TAG, "Display quadrants: red, green, blue, white");
    lcd_draw_solid_rect(panel_handle, draw_buffer, 0, 0, half_width, half_height, RGB565_RED);
    lcd_draw_solid_rect(panel_handle, draw_buffer, half_width, 0, half_width, half_height, RGB565_GREEN);
    lcd_draw_solid_rect(panel_handle, draw_buffer, 0, half_height, half_width, half_height, RGB565_BLUE);
    lcd_draw_solid_rect(panel_handle, draw_buffer, half_width, half_height, half_width, half_height, RGB565_WHITE);
    vTaskDelay(pdMS_TO_TICKS(4000));
}

void lcd_test_run(void)
{
    esp_lcd_panel_handle_t panel_handle = lcd_panel_init();
    uint16_t *draw_buffer = heap_caps_malloc(LCD_TEST_H_RES * LCD_TEST_DRAW_LINES * sizeof(uint16_t), MALLOC_CAP_DMA);
    configASSERT(draw_buffer != NULL);

    for (;;) {
        lcd_show_fullscreen(panel_handle, draw_buffer, "red", RGB565_RED);
        lcd_show_fullscreen(panel_handle, draw_buffer, "green", RGB565_GREEN);
        lcd_show_fullscreen(panel_handle, draw_buffer, "blue", RGB565_BLUE);
        lcd_show_fullscreen(panel_handle, draw_buffer, "white", RGB565_WHITE);
        lcd_show_quadrants(panel_handle, draw_buffer);
    }
}
