/**
 * @file bsp_lcd.h
 * @brief 提供当前开发板上 MSP3526/ST7796 显示模块的板级初始化接口。
 */

#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#include "bsp_board.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 SPI 总线、ST7796 面板并打开显示。
 *
 * 该接口按单实例设计。重复调用不会重新初始化硬件，而是返回第一次创建的句柄。
 *
 * @param[out] out_io 返回 LCD Panel IO 句柄。
 * @param[out] out_panel 返回 ST7796 Panel 句柄。
 * @return ESP_OK 初始化成功；其他值表示初始化失败。
 */
esp_err_t bsp_lcd_init(esp_lcd_panel_io_handle_t *out_io,
                       esp_lcd_panel_handle_t *out_panel);

/**
 * @brief 打开或关闭 LCD 背光。
 *
 * GPIO9 在 bsp_lcd_init() 中完成配置。在此之前调用会返回
 * ESP_ERR_INVALID_STATE；重复设置相同状态不会产生额外影响。
 *
 * @param on true 打开背光，false 关闭背光。
 * @return ESP_OK 设置成功；其他值表示 GPIO 尚未初始化或设置失败。
 */
esp_err_t bsp_lcd_set_backlight(bool on);

#ifdef __cplusplus
}
#endif
