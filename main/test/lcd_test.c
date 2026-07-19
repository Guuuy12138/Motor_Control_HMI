/**
 * @file lcd_test.c
 * @brief 通过 BSP 驱动执行 ST7796 底层显示验证，循环显示纯色和四象限色块。
 */

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

#include "bsp_board.h"
#include "bsp_lcd.h"
#include "lcd_test.h"

/* 底层测试每次只准备 20 行 DMA 数据，BSP 支持的最大传输块为 40 行。 */
#define LCD_TEST_DRAW_LINES 20

#define RGB565_RED          0xF800
#define RGB565_GREEN        0x07E0
#define RGB565_BLUE         0x001F
#define RGB565_WHITE        0xFFFF

static const char *TAG = "LCD_TEST";
static SemaphoreHandle_t s_transfer_done;

/* SPI DMA 传输完成后释放信号量，通知测试任务可以安全复用颜色缓冲区。 */
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

/* 使用 320×20 行缓冲区分块填充指定矩形，并在每块发送完成后等待 DMA 回调。 */
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
    const int pixels_per_buffer = BSP_LCD_H_RES * LCD_TEST_DRAW_LINES;

    /* 按 ST7796 所需的高字节在前顺序准备 RGB565 数据。 */
    for (int i = 0; i < pixels_per_buffer; i++) {
        uint8_t *pixel = (uint8_t *)&draw_buffer[i];
        pixel[0] = color_high;
        pixel[1] = color_low;
    }

    for (int line = 0; line < height; line += LCD_TEST_DRAW_LINES) {
        const int lines_to_draw = (height - line > LCD_TEST_DRAW_LINES)
                                      ? LCD_TEST_DRAW_LINES
                                      : height - line;
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle,
                                                   x_start,
                                                   y_start + line,
                                                   x_start + width,
                                                   y_start + line + lines_to_draw,
                                                   draw_buffer));
        xSemaphoreTake(s_transfer_done, portMAX_DELAY);
    }
}

/* 全屏填充指定颜色并停留 2 秒。 */
static void lcd_show_fullscreen(esp_lcd_panel_handle_t panel_handle,
                                uint16_t *draw_buffer,
                                const char *name,
                                uint16_t color)
{
    ESP_LOGI(TAG, "显示纯色：%s", name);
    lcd_draw_solid_rect(panel_handle, draw_buffer, 0, 0,
                        BSP_LCD_H_RES, BSP_LCD_V_RES, color);
    vTaskDelay(pdMS_TO_TICKS(2000));
}

/* 显示左上红、右上绿、左下蓝、右下白四个象限并停留 4 秒。 */
static void lcd_show_quadrants(esp_lcd_panel_handle_t panel_handle, uint16_t *draw_buffer)
{
    const int half_width = BSP_LCD_H_RES / 2;
    const int half_height = BSP_LCD_V_RES / 2;

    ESP_LOGI(TAG, "显示四象限：红、绿、蓝、白");
    lcd_draw_solid_rect(panel_handle, draw_buffer, 0, 0, half_width, half_height, RGB565_RED);
    lcd_draw_solid_rect(panel_handle, draw_buffer, half_width, 0, half_width, half_height, RGB565_GREEN);
    lcd_draw_solid_rect(panel_handle, draw_buffer, 0, half_height, half_width, half_height, RGB565_BLUE);
    lcd_draw_solid_rect(panel_handle, draw_buffer, half_width, half_height, half_width, half_height, RGB565_WHITE);
    vTaskDelay(pdMS_TO_TICKS(4000));
}

/* 初始化 BSP LCD、注册测试专用 DMA 回调，然后持续运行色彩与方向验证。 */
void lcd_test_run(void)
{
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_handle_t panel_handle = NULL;
    const esp_lcd_panel_io_callbacks_t io_callbacks = {
        .on_color_trans_done = lcd_color_transfer_done,
    };

    ESP_ERROR_CHECK(bsp_lcd_init(&io_handle, &panel_handle));

    ESP_LOGI(TAG, "开始背光开关测试：关闭 500 ms 后重新打开");
    ESP_ERROR_CHECK(bsp_lcd_set_backlight(false));
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_ERROR_CHECK(bsp_lcd_set_backlight(true));
    ESP_LOGI(TAG, "背光开关测试完成");

    s_transfer_done = xSemaphoreCreateBinary();
    configASSERT(s_transfer_done != NULL);
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &io_callbacks, NULL));
    ESP_LOGI(TAG, "LCD 底层测试已注册 DMA 传输完成回调");

    uint16_t *draw_buffer = heap_caps_malloc(
        BSP_LCD_H_RES * LCD_TEST_DRAW_LINES * sizeof(uint16_t), MALLOC_CAP_DMA);
    configASSERT(draw_buffer != NULL);

    for (;;) {
        lcd_show_fullscreen(panel_handle, draw_buffer, "红", RGB565_RED);
        lcd_show_fullscreen(panel_handle, draw_buffer, "绿", RGB565_GREEN);
        lcd_show_fullscreen(panel_handle, draw_buffer, "蓝", RGB565_BLUE);
        lcd_show_fullscreen(panel_handle, draw_buffer, "白", RGB565_WHITE);
        lcd_show_quadrants(panel_handle, draw_buffer);
    }
}
