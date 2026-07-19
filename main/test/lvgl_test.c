/**
 * @file lvgl_test.c
 * @brief 使用 BSP 显示和触摸驱动创建 LVGL 综合交互验证页面。
 */

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "bsp_board.h"
#include "bsp_lcd.h"
#include "lvgl_test.h"
#include "touch_test.h"

static const char *TAG = "LVGL_TEST";

static esp_lcd_panel_io_handle_t s_io_handle;
static esp_lcd_panel_handle_t s_panel_handle;
static lv_display_t *s_display;

/* ===== 触摸状态机：INIT → 初始化结果 → READY 或 FAILED ===== */
typedef enum {
    TOUCH_STATE_INIT,
    TOUCH_STATE_READY,
    TOUCH_STATE_FAILED,
} touch_state_t;

typedef struct {
    const char *name;
} touch_region_t;

/* 四个触摸按钮区域：左上、右上、左下、右下。 */
static const touch_region_t s_touch_regions[] = {
    {.name = "TL"},
    {.name = "TR"},
    {.name = "BL"},
    {.name = "BR"},
};

/* UI 对象由 LVGL 任务更新，引用保存在此处供事件回调使用。 */
static lv_obj_t *s_touch_name_label;
static lv_obj_t *s_touch_position_label;
static lv_obj_t *s_touch_count_label;
static lv_obj_t *s_touch_status_card;
static uint32_t s_touch_count;

/* 创建一个小色块，用于验证 RGB565 颜色与字节顺序。 */
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

/* 根据触摸初始化结果切换状态卡片的颜色和文字。 */
static void lvgl_update_touch_state(touch_state_t state)
{
    switch (state) {
    case TOUCH_STATE_READY:
        lv_obj_set_style_bg_color(s_touch_status_card, lv_color_hex(0x123A2B), 0);
        lv_obj_set_style_border_color(s_touch_status_card, lv_color_hex(0x39D98A), 0);
        lv_label_set_text(s_touch_name_label, "Touch: READY");
        lv_obj_set_style_text_color(s_touch_name_label, lv_color_hex(0x70F2A6), 0);
        lv_label_set_text(s_touch_position_label, "X: ---  Y: ---");
        lv_label_set_text(s_touch_count_label, "Count: 0");
        break;

    case TOUCH_STATE_FAILED:
        lv_obj_set_style_bg_color(s_touch_status_card, lv_color_hex(0x4A1F24), 0);
        lv_obj_set_style_border_color(s_touch_status_card, lv_color_hex(0xFF657A), 0);
        lv_label_set_text(s_touch_name_label, "Touch: FAILED");
        lv_obj_set_style_text_color(s_touch_name_label, lv_color_hex(0xFF8A9A), 0);
        lv_label_set_text(s_touch_position_label, "See serial log");
        lv_label_set_text(s_touch_count_label, "Display remains active");
        break;

    case TOUCH_STATE_INIT:
    default:
        lv_obj_set_style_bg_color(s_touch_status_card, lv_color_hex(0x42351E), 0);
        lv_obj_set_style_border_color(s_touch_status_card, lv_color_hex(0xF5B942), 0);
        lv_label_set_text(s_touch_name_label, "Touch: INIT");
        lv_obj_set_style_text_color(s_touch_name_label, lv_color_hex(0xFFD166), 0);
        lv_label_set_text(s_touch_position_label, "Waiting for FT6336U");
        lv_label_set_text(s_touch_count_label, "Count: 0");
        break;
    }
}

/* 读取点击坐标、将显示用 Y 坐标转换为向上增大，并更新状态数据。 */
static void lvgl_touch_button_event_cb(lv_event_t *event)
{
    const touch_region_t *region = lv_event_get_user_data(event);
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t point = {0};
    int32_t displayed_y;

    if (indev != NULL) {
        lv_indev_get_point(indev, &point);
    }

    /* LVGL 内部坐标保持向下增大，只转换界面与串口中展示的 Y 坐标。 */
    displayed_y = BSP_LCD_V_RES - 1 - point.y;
    s_touch_count++;
    lv_label_set_text_fmt(s_touch_name_label, "Touch: %s", region->name);
    lv_label_set_text_fmt(s_touch_position_label, "X: %ld  Y: %ld",
                          (long)point.x, (long)displayed_y);
    lv_label_set_text_fmt(s_touch_count_label, "Count: %lu", (unsigned long)s_touch_count);
    ESP_LOGI(TAG, "触摸区域 %s：X=%ld Y=%ld，累计次数=%lu",
             region->name, (long)point.x, (long)displayed_y, (unsigned long)s_touch_count);
}

/* 在指定位置创建一个测试按钮，点击时触发触摸事件回调。 */
static void lvgl_add_touch_button(lv_obj_t *parent,
                                  int32_t x,
                                  int32_t y,
                                  const touch_region_t *region)
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

/* 每秒更新一次运行时间。 */
static void lvgl_uptime_timer_cb(lv_timer_t *timer)
{
    static uint32_t uptime_seconds;
    lv_obj_t *uptime_label = lv_timer_get_user_data(timer);

    uptime_seconds++;
    lv_label_set_text_fmt(uptime_label, "Uptime: %lu s", (unsigned long)uptime_seconds);
}

/* 创建四色、四角按钮、触摸状态和运行时间组成的综合测试页面。 */
static void lvgl_create_test_screen(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *title;
    lv_obj_t *info;
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
    lv_label_set_text_fmt(info, "LVGL RUNNING\nSPI: %d MHz  I2C: %d kHz",
                          BSP_LCD_SPI_CLOCK_HZ / 1000000,
                          BSP_TOUCH_I2C_CLOCK_HZ / 1000);
    lv_obj_set_style_text_color(info, lv_color_hex(0xB8C7D9), 0);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 102);

    s_touch_status_card = lv_obj_create(screen);
    lv_obj_set_size(s_touch_status_card, 280, 84);
    lv_obj_set_pos(s_touch_status_card, 20, 346);
    lv_obj_set_style_bg_opa(s_touch_status_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_touch_status_card, 2, 0);
    lv_obj_set_style_radius(s_touch_status_card, 12, 0);
    lv_obj_set_flex_flow(s_touch_status_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_touch_status_card,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_touch_status_card, 4, 0);
    lv_obj_clear_flag(s_touch_status_card, LV_OBJ_FLAG_SCROLLABLE);

    s_touch_name_label = lv_label_create(s_touch_status_card);
    s_touch_position_label = lv_label_create(s_touch_status_card);
    lv_obj_set_style_text_color(s_touch_position_label, lv_color_hex(0xB8C7D9), 0);
    s_touch_count_label = lv_label_create(s_touch_status_card);
    lv_obj_set_style_text_color(s_touch_count_label, lv_color_hex(0xB8C7D9), 0);

    lvgl_update_touch_state(TOUCH_STATE_INIT);

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

/* 使用 BSP 返回的 LCD 句柄建立 LVGL 显示器，然后创建页面并注册触摸。 */
static void lvgl_init_port(void)
{
    esp_err_t touch_result;
    const lvgl_port_cfg_t lvgl_config = ESP_LVGL_PORT_INIT_CONFIG();
    const lvgl_port_display_cfg_t display_config = {
        .io_handle = s_io_handle,
        .panel_handle = s_panel_handle,
        .buffer_size = BSP_LCD_H_RES * BSP_LCD_DRAW_BUFFER_LINES,
        .double_buffer = false,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = BSP_LCD_SWAP_XY,
            .mirror_x = BSP_LCD_MIRROR_X,
            .mirror_y = BSP_LCD_MIRROR_Y,
        },
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,
        },
    };

    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_config));
    ESP_LOGI(TAG, "LVGL 端口初始化完成");

    s_display = lvgl_port_add_disp(&display_config);
    ESP_ERROR_CHECK(s_display == NULL ? ESP_FAIL : ESP_OK);
    ESP_LOGI(TAG, "LVGL 显示器注册完成：RGB565，%dx%d DMA 缓冲区",
             BSP_LCD_H_RES, BSP_LCD_DRAW_BUFFER_LINES);

    configASSERT(lvgl_port_lock(0));
    lvgl_create_test_screen();
    lvgl_port_unlock();
    ESP_LOGI(TAG, "LVGL 测试页面创建完成，开始初始化触摸");

    touch_result = touch_test_init(s_display);

    configASSERT(lvgl_port_lock(0));
    lvgl_update_touch_state(touch_result == ESP_OK ? TOUCH_STATE_READY : TOUCH_STATE_FAILED);
    lvgl_port_unlock();

    if (touch_result != ESP_OK) {
        ESP_LOGE(TAG, "触摸初始化失败：%s；显示测试继续运行", esp_err_to_name(touch_result));
    } else {
        ESP_LOGI(TAG, "触摸初始化完成，交互测试可以使用");
    }
}

/* 初始化 BSP LCD，再启动 LVGL 综合显示与触摸测试。 */
void lvgl_test_run(void)
{
    ESP_ERROR_CHECK(bsp_lcd_init(&s_io_handle, &s_panel_handle));
    lvgl_init_port();
}
