/**
 * @file bsp_lcd.h
 * @brief 提供当前开发板上 MSP3526/ST7796 显示模块的板级初始化接口。
 */

#pragma once

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

#ifdef __cplusplus
}
#endif

