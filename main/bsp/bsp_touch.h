/**
 * @file bsp_touch.h
 * @brief 提供当前开发板上 FT6336U 电容触摸控制器的板级初始化接口。
 */

#pragma once

#include "esp_err.h"
#include "esp_lcd_touch.h"

#include "bsp_board.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 I2C 总线和 FT6336U 触摸控制器。
 *
 * 该函数只创建硬件驱动并返回触摸句柄，不负责注册 LVGL 输入设备。
 * 重复调用时返回第一次创建的句柄。
 *
 * @param[out] out_touch 返回 FT6336U 触摸句柄。
 * @return ESP_OK 初始化成功；其他值表示初始化失败。
 */
esp_err_t bsp_touch_init(esp_lcd_touch_handle_t *out_touch);

#ifdef __cplusplus
}
#endif

