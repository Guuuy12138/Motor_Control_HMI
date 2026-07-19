/**
 * @file touch_test.c
 * @brief 将 BSP 提供的 FT6336U 触摸句柄注册为 LVGL 输入设备。
 */

#include "esp_check.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"

#include "esp_lvgl_port.h"

#include "bsp_touch.h"
#include "touch_test.h"

static const char *TAG = "TOUCH_TEST";

/* BSP 负责触摸硬件，测试层只负责把触摸句柄绑定到指定 LVGL 显示器。 */
esp_err_t touch_test_init(lv_display_t *display)
{
    esp_err_t result;
    esp_lcd_touch_handle_t touch_handle = NULL;

    ESP_RETURN_ON_FALSE(display != NULL, ESP_ERR_INVALID_ARG, TAG, "LVGL 显示器句柄为空");

    result = bsp_touch_init(&touch_handle);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "BSP 触摸初始化失败：%s", esp_err_to_name(result));
        return result;
    }

    ESP_LOGI(TAG, "正在将 FT6336U 注册为 LVGL 触摸输入设备");
    if (lvgl_port_add_touch(&(lvgl_port_touch_cfg_t) {
        .disp = display,
        .handle = touch_handle,
    }) == NULL) {
        ESP_LOGE(TAG, "LVGL 触摸输入设备注册失败：内存不足");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "LVGL 触摸输入设备注册完成，GPIO%d 中断可立即唤醒 LVGL",
             BSP_TOUCH_GPIO_INT);
    return ESP_OK;
}
