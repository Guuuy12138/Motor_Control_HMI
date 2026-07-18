/**
 * @file lvgl_test.c
 * @brief 初始化 ST7796、LVGL 和触摸输入，并显示综合交互验证页面。
 */

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

#include "esp_lcd_st7796.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "lvgl_test.h"
#include "touch_test.h"

#define LVGL_TEST_H_RES             320
#define LVGL_TEST_V_RES             480
#define LVGL_TEST_DRAW_BUFFER_LINES 40
#define LVGL_TEST_SPI_HOST          SPI2_HOST
#define LVGL_TEST_SPI_CLOCK_HZ      (60 * 1000 * 1000)

#define LVGL_TEST_GPIO_CS           GPIO_NUM_4
#define LVGL_TEST_GPIO_RST          GPIO_NUM_5
#define LVGL_TEST_GPIO_DC           GPIO_NUM_6
#define LVGL_TEST_GPIO_MOSI         GPIO_NUM_7
#define LVGL_TEST_GPIO_SCLK         GPIO_NUM_8

static const char *TAG = "LVGL_TEST";
static lv_display_t *s_display;
static esp_err_t s_touch_init_result = ESP_FAIL;

typedef struct {
    const char *name;
} touch_region_t;

static const touch_region_t s_touch_regions[] = {
    {.name = "TL"},
    {.name = "TR"},
    {.name = "BL"},
    {.name = "BR"},
};

static lv_obj_t *s_touch_name_label;
static lv_obj_t *s_touch_position_label;
static lv_obj_t *s_touch_count_label;
static uint32_t s_touch_count;

/* MSP3526 模块专用的 ST7796S 初始化命令。 */
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

static esp_lcd_panel_io_handle_t s_io_handle;
static esp_lcd_panel_handle_t s_panel_handle;

static void lvgl_add_color_block(lv_obj_t *parent, int32_t x, lv_color_t color)
{
    lv_obj_t *block = lv_obj_create(parent);

    lv_obj_set_size(block, 54, 28);
    lv_obj_set_pos(block, x, 62);
    lv_obj_set_style_bg_color(block, color, 0);
    lv_obj_set_style_bg_opa(block, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(block, 0, 0);
    lv_obj_set_style_radius(block, 8, 0);
    lv_obj_clear_flag(block, LV_OBJ_FLAG_SCROLLABLE);
}

static void lvgl_touch_button_event_cb(lv_event_t *event)
{
    const touch_region_t *region = lv_event_get_user_data(event);
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t point = {0};

    if (indev != NULL) {
        lv_indev_get_point(indev, &point);
    }

    s_touch_count++;
    lv_label_set_text_fmt(s_touch_name_label, "Touch: %s", region->name);
    lv_label_set_text_fmt(s_touch_position_label, "X: %ld  Y: %ld", (long)point.x, (long)point.y);
    lv_label_set_text_fmt(s_touch_count_label, "Count: %lu", (unsigned long)s_touch_count);
    ESP_LOGI(TAG, "Touch %s: X=%ld Y=%ld Count=%lu", region->name, (long)point.x, (long)point.y,
             (unsigned long)s_touch_count);
}

static void lvgl_add_touch_button(lv_obj_t *parent, int32_t x, int32_t y, const touch_region_t *region)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label = lv_label_create(button);

    lv_obj_set_size(button, 125, 82);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x204866), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x3A8BC2), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, lv_color_hex(0x70BFEF), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_radius(button, 10, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(button, lvgl_touch_button_event_cb, LV_EVENT_CLICKED, (void *)region);

    lv_label_set_text(label, region->name);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
}

static void lvgl_uptime_timer_cb(lv_timer_t *timer)
{
    static uint32_t uptime_seconds;
    lv_obj_t *uptime_label = lv_timer_get_user_data(timer);

    uptime_seconds++;
    lv_label_set_text_fmt(uptime_label, "Uptime: %lu s", (unsigned long)uptime_seconds);
}

static void lvgl_create_test_screen(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *title;
    lv_obj_t *info;
    lv_obj_t *status_card;
    lv_obj_t *status_label;
    lv_obj_t *uptime_label;

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    title = lv_label_create(screen);
    lv_label_set_text(title, "TOUCH TEST");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    lvgl_add_color_block(screen, 31, lv_color_hex(0xF00000));
    lvgl_add_color_block(screen, 99, lv_color_hex(0x00D000));
    lvgl_add_color_block(screen, 167, lv_color_hex(0x0060FF));
    lvgl_add_color_block(screen, 235, lv_color_white());

    info = lv_label_create(screen);
    lv_label_set_text(info, "LVGL RUNNING\nSPI: 60 MHz  I2C: 100 kHz");
    lv_obj_set_style_text_color(info, lv_color_hex(0xB8C7D9), 0);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 102);

    status_card = lv_obj_create(screen);
    lv_obj_set_size(status_card, 280, 84);
    lv_obj_set_pos(status_card, 20, 346);
    lv_obj_set_style_bg_color(status_card,
                              s_touch_init_result == ESP_OK ? lv_color_hex(0x123A2B) : lv_color_hex(0x4A1F24), 0);
    lv_obj_set_style_bg_opa(status_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(status_card,
                                  s_touch_init_result == ESP_OK ? lv_color_hex(0x39D98A) : lv_color_hex(0xFF657A), 0);
    lv_obj_set_style_border_width(status_card, 2, 0);
    lv_obj_set_style_radius(status_card, 12, 0);
    lv_obj_clear_flag(status_card, LV_OBJ_FLAG_SCROLLABLE);

    status_label = lv_label_create(status_card);
    lv_label_set_text(status_label, s_touch_init_result == ESP_OK ? "Touch: --" : "TOUCH INIT FAILED");
    lv_obj_set_style_text_color(status_label,
                                s_touch_init_result == ESP_OK ? lv_color_hex(0x70F2A6) : lv_color_hex(0xFF8A9A), 0);
    lv_obj_set_pos(status_label, 12, 8);
    s_touch_name_label = status_label;

    s_touch_position_label = lv_label_create(status_card);
    lv_label_set_text(s_touch_position_label,
                      s_touch_init_result == ESP_OK ? "X: ---  Y: ---" : "See serial log");
    lv_obj_set_style_text_color(s_touch_position_label, lv_color_hex(0xB8C7D9), 0);
    lv_obj_set_pos(s_touch_position_label, 12, 30);

    s_touch_count_label = lv_label_create(status_card);
    lv_label_set_text(s_touch_count_label,
                      s_touch_init_result == ESP_OK ? "Count: 0" : "Display remains active");
    lv_obj_set_style_text_color(s_touch_count_label, lv_color_hex(0xB8C7D9), 0);
    lv_obj_set_pos(s_touch_count_label, 12, 52);

    lvgl_add_touch_button(screen, 20, 150, &s_touch_regions[0]);
    lvgl_add_touch_button(screen, 175, 150, &s_touch_regions[1]);
    lvgl_add_touch_button(screen, 20, 246, &s_touch_regions[2]);
    lvgl_add_touch_button(screen, 175, 246, &s_touch_regions[3]);

    uptime_label = lv_label_create(screen);
    lv_label_set_text(uptime_label, "Uptime: 0 s");
    lv_obj_set_style_text_color(uptime_label, lv_color_hex(0xB8C7D9), 0);
    lv_obj_align(uptime_label, LV_ALIGN_BOTTOM_MID, 0, -22);
    lv_timer_create(lvgl_uptime_timer_cb, 1000, uptime_label);
}

static void lvgl_init_lcd(void)
{
    const spi_bus_config_t bus_config = {
        .sclk_io_num = LVGL_TEST_GPIO_SCLK,
        .mosi_io_num = LVGL_TEST_GPIO_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LVGL_TEST_H_RES * LVGL_TEST_DRAW_BUFFER_LINES * sizeof(uint16_t),
    };
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LVGL_TEST_GPIO_CS,
        .dc_gpio_num = LVGL_TEST_GPIO_DC,
        .spi_mode = 0,
        .pclk_hz = LVGL_TEST_SPI_CLOCK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    st7796_vendor_config_t vendor_config = {
        .init_cmds = s_msp3526_init_cmds,
        .init_cmds_size = sizeof(s_msp3526_init_cmds) / sizeof(s_msp3526_init_cmds[0]),
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LVGL_TEST_GPIO_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };

    ESP_LOGI(TAG, "SPI pins: CS=%d RST=%d DC=%d MOSI=%d SCLK=%d",
             LVGL_TEST_GPIO_CS, LVGL_TEST_GPIO_RST, LVGL_TEST_GPIO_DC,
             LVGL_TEST_GPIO_MOSI, LVGL_TEST_GPIO_SCLK);
    ESP_ERROR_CHECK(spi_bus_initialize(LVGL_TEST_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LVGL_TEST_SPI_HOST, &io_config, &s_io_handle));
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(s_io_handle, &panel_config, &s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    ESP_LOGI(TAG, "ST7796 initialized: %dx%d, SPI %d MHz",
             LVGL_TEST_H_RES, LVGL_TEST_V_RES, LVGL_TEST_SPI_CLOCK_HZ / 1000000);
}

static void lvgl_init_port(void)
{
    const lvgl_port_cfg_t lvgl_config = ESP_LVGL_PORT_INIT_CONFIG();
    const lvgl_port_display_cfg_t display_config = {
        .io_handle = s_io_handle,
        .panel_handle = s_panel_handle,
        .buffer_size = LVGL_TEST_H_RES * LVGL_TEST_DRAW_BUFFER_LINES,
        .double_buffer = false,
        .hres = LVGL_TEST_H_RES,
        .vres = LVGL_TEST_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = true,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,
        },
    };

    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_config));
    ESP_LOGI(TAG, "LVGL port initialized");

    s_display = lvgl_port_add_disp(&display_config);
    ESP_ERROR_CHECK(s_display == NULL ? ESP_FAIL : ESP_OK);
    ESP_LOGI(TAG, "LVGL display registered: RGB565, 320x40 DMA buffer");

    s_touch_init_result = touch_test_init(s_display);
    if (s_touch_init_result != ESP_OK) {
        ESP_LOGE(TAG, "Touch initialization failed: %s; continuing display test",
                 esp_err_to_name(s_touch_init_result));
    }

    configASSERT(lvgl_port_lock(0));
    lvgl_create_test_screen();
    lvgl_port_unlock();
    ESP_LOGI(TAG, "LVGL test screen created");
}

void lvgl_test_run(void)
{
    lvgl_init_lcd();
    lvgl_init_port();
}
