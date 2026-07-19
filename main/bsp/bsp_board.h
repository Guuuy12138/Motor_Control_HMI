/**
 * @file bsp_board.h
 * @brief 集中定义当前 ESP32-S3 开发板与 MSP3526 屏幕模块的硬件连接和固定参数。
 */

#pragma once

#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"

/* ===== ST7796 显示接口 ===== */
#define BSP_LCD_H_RES                320
#define BSP_LCD_V_RES                480
#define BSP_LCD_DRAW_BUFFER_LINES    40
#define BSP_LCD_SPI_HOST             SPI2_HOST
#define BSP_LCD_SPI_MODE             0
#define BSP_LCD_SPI_CLOCK_HZ         (60 * 1000 * 1000)
#define BSP_LCD_GPIO_CS              GPIO_NUM_4
#define BSP_LCD_GPIO_RST             GPIO_NUM_5
#define BSP_LCD_GPIO_DC              GPIO_NUM_6
#define BSP_LCD_GPIO_MOSI            GPIO_NUM_7
#define BSP_LCD_GPIO_SCLK            GPIO_NUM_8
#define BSP_LCD_SWAP_XY              false
#define BSP_LCD_MIRROR_X             true
#define BSP_LCD_MIRROR_Y             false

/* ===== FT6336U 触摸接口 ===== */
#define BSP_TOUCH_I2C_PORT           I2C_NUM_0
#define BSP_TOUCH_I2C_CLOCK_HZ       (100 * 1000)
#define BSP_TOUCH_I2C_ADDRESS        0x38
#define BSP_TOUCH_GPIO_SCL           GPIO_NUM_9
#define BSP_TOUCH_GPIO_SDA           GPIO_NUM_10
#define BSP_TOUCH_GPIO_RST           GPIO_NUM_11
#define BSP_TOUCH_GPIO_INT           GPIO_NUM_12
#define BSP_TOUCH_SWAP_XY            false
#define BSP_TOUCH_MIRROR_X           false
#define BSP_TOUCH_MIRROR_Y           false

