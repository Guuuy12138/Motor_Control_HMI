/**
 * @file touch_test.h
 * @brief 声明 FT6336U 触摸驱动与 LVGL 的注册接口。
 */

#pragma once

#include "esp_err.h"
#include "lvgl.h"

/**
 * @brief 初始化 FT6336U，并注册为指定显示器的 LVGL 输入设备。
 * @param display 已完成注册的 LVGL 显示器句柄。
 * @return 成功返回 ESP_OK，失败返回对应的 ESP-IDF 错误码。
 */
esp_err_t touch_test_init(lv_display_t *display);
