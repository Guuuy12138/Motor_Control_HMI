/**
 * @file bsp_board.h
 * @brief 集中定义当前 ESP32-S3 开发板与 MSP3526 屏幕模块的硬件连接和固定参数。
 */

#pragma once

#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"

/*
 * ===== MSP3526 模块 14 针接口顺序 =====
 *
 * 模块针脚从 14 到 3、开发板针脚从上到下保持相同顺序，便于 PCB 走线：
 * SD_CS→GPIO4，CTP_INT→GPIO5，CTP_SDA→GPIO6，CTP_RST→GPIO7，
 * CTP_SCL→GPIO15，SDO/MISO→GPIO16，LED→GPIO9，SCK→GPIO10，
 * SDI/MOSI→GPIO11，LCD_RS/DC→GPIO12，LCD_RST→GPIO13，LCD_CS→GPIO14。
 */

/* ===== ST7796 显示及共享 SPI 接口 ===== */
#define BSP_LCD_H_RES                320
#define BSP_LCD_V_RES                480
#define BSP_LCD_DRAW_BUFFER_LINES    40
#define BSP_LCD_SPI_HOST             SPI2_HOST
#define BSP_LCD_SPI_MODE             0
#define BSP_LCD_SPI_CLOCK_HZ         (60 * 1000 * 1000)
#define BSP_LCD_GPIO_CS              GPIO_NUM_14
#define BSP_LCD_GPIO_RST             GPIO_NUM_13
#define BSP_LCD_GPIO_DC              GPIO_NUM_12
#define BSP_LCD_GPIO_MOSI            GPIO_NUM_11
#define BSP_LCD_GPIO_MISO            GPIO_NUM_16
#define BSP_LCD_GPIO_SCLK            GPIO_NUM_10
#define BSP_LCD_GPIO_BACKLIGHT       GPIO_NUM_9
#define BSP_LCD_BACKLIGHT_ON_LEVEL   1
#define BSP_LCD_BACKLIGHT_OFF_LEVEL  0
#define BSP_LCD_SWAP_XY              false
#define BSP_LCD_MIRROR_X             true
#define BSP_LCD_MIRROR_Y             false

/* SD 卡与 LCD 共用 SCLK、MOSI 和 MISO，本阶段只保证 SD 卡保持未选中。 */
#define BSP_SD_GPIO_CS               GPIO_NUM_4

/* ===== FT6336U 触摸接口 ===== */
#define BSP_TOUCH_I2C_PORT           I2C_NUM_0
#define BSP_TOUCH_I2C_CLOCK_HZ       (100 * 1000)
#define BSP_TOUCH_I2C_ADDRESS        0x38
#define BSP_TOUCH_GPIO_SCL           GPIO_NUM_15
#define BSP_TOUCH_GPIO_SDA           GPIO_NUM_6
#define BSP_TOUCH_GPIO_RST           GPIO_NUM_7
#define BSP_TOUCH_GPIO_INT           GPIO_NUM_5
#define BSP_TOUCH_SWAP_XY            false
#define BSP_TOUCH_MIRROR_X           false
#define BSP_TOUCH_MIRROR_Y           false
