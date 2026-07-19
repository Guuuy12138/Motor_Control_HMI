# Motor Control HMI

本工程当前是 Motor Control HMI 正式开发前的硬件与 BSP 技术基线，用于验证 ESP32-S3 与 MSP3526 屏幕模块之间的显示、图形界面和电容触摸链路。

当前默认运行 `lvgl_test_run()`，可以同时验证 ST7796 显示、LVGL 界面、FT6336U 触摸和 GPIO5 触摸中断。本工程目前不是已经完成的电机控制产品，电机、编码器、PID 和正式 HMI 功能将在后续项目阶段确定。

## 当前验证结果

| 验证项目 | 当前结果 |
| --- | --- |
| ST7796 SPI 显示 | 正常显示纯色和四象限色块 |
| LVGL 9.5 | 界面显示、局部刷新和定时更新正常 |
| FT6336U I2C 触摸 | 坐标读取和四角点击正常 |
| GPIO5 触摸中断 | 已配置为低电平触摸中断，用于响应快速点击 |
| BSP 整理 | LCD 和触摸硬件初始化已集中到 `main/bsp/` |
| 工程构建 | BSP 整理后的工程已构建通过 |

显示、LVGL 和触摸链路已经在原型接线中完成验证；改用本页的新 GPIO 映射后，必须重新进行一次实机回归。固件大小和内存占用会随代码变化，因此不在 README 中固定记录。每次构建后应以 ESP-IDF 输出的 Memory Type Usage Summary 为准。

## 硬件连接

当前使用 ESP32-S3-DEV-KIT-N8R8 和 3.5 英寸 MSP3526 模块，显示控制器为 ST7796，电容触摸控制器为 FT6336U。

> 当前固件不再兼容旧 GPIO4～12 接线。更换接线前必须同时断开开发板和屏幕电源，不要在新旧接线混合的状态下上电。

| 模块针脚 | 屏幕模块信号 | ESP32-S3 连接 | 用途 |
| ---: | --- | --- | --- |
| 14 | SD_CS | GPIO4 | SD 卡片选，未使用时保持高电平 |
| 13 | CTP_INT | GPIO5 | 触摸中断，低电平有效 |
| 12 | CTP_SDA | GPIO6 | 触摸 I2C 数据 |
| 11 | CTP_RST | GPIO7 | 触摸硬件复位 |
| 10 | CTP_SCL | GPIO15 | 触摸 I2C 时钟 |
| 9 | LCD_MISO / SDO | GPIO16 | LCD 与 SD 卡共享的 SPI 返回数据 |
| 8 | LED | GPIO9 | 背光开关，未来可改为 PWM |
| 7 | LCD_SCLK | GPIO10 | LCD 与 SD 卡共享的 SPI 时钟 |
| 6 | LCD_MOSI / SDI | GPIO11 | LCD 与 SD 卡共享的 SPI 输出数据 |
| 5 | LCD_DC / RS | GPIO12 | 命令与数据选择 |
| 4 | LCD_RST | GPIO13 | LCD 硬件复位 |
| 3 | LCD_CS | GPIO14 | LCD 片选 |
| 2 | GND | GND | 与开发板共地 |
| 1 | VCC | 5V | 模块供电 |

> ESP32-S3 的 GPIO 使用 3.3V 逻辑电平，不要把 5V 直接接入 GPIO。只有模块 VCC 连接 5V，LED 必须从原来的 5V 连线移到 GPIO9，所有设备必须共地。

GPIO9 当前只实现背光开关：高电平点亮、低电平关闭。硬件已经为未来 PWM 调光保留连接，但程序尚未配置 LEDC。SD 卡的 CS 和 MISO 已接入共享 SPI 总线，本阶段只保证 SD_CS 保持高电平，不挂载或读写 SD 卡。

## 固定运行参数

所有 GPIO、总线频率、分辨率和坐标方向集中定义在 [`main/bsp/bsp_board.h`](main/bsp/bsp_board.h)，测试文件不再重复保存这些配置。

| 项目 | 当前配置 |
| --- | --- |
| LCD 分辨率 | 320 × 480，竖屏 |
| LCD 像素格式 | RGB565，BGR 色序 |
| LCD 总线 | SPI2，mode 0 |
| LCD SPI 时钟 | 60 MHz |
| LCD/SD MISO | GPIO16，当前只完成总线基础配置 |
| SD 卡片选 | GPIO4，默认输出高电平 |
| 背光控制 | GPIO9，高电平点亮 |
| LCD 方向 | `swap_xy=false`、`mirror_x=true`、`mirror_y=false` |
| 触摸总线 | I2C0，100 kHz |
| FT6336U 地址 | `0x38` |
| 触摸中断 | GPIO5，低电平有效 |
| 触摸方向 | `swap_xy=false`、`mirror_x=false`、`mirror_y=false` |

LVGL 内部坐标以屏幕左上角为原点，Y 坐标向下增大。当前测试页面为了按照直角坐标习惯显示数据，使用 `479 - LVGL内部Y` 转换展示值，因此屏幕上方显示的 Y 值较大，屏幕下方显示的 Y 值较小。该转换只影响页面和串口中显示的数字，不改变 LVGL 内部的坐标系统。

## 工程结构与分层

```text
Motor_Control_HMI/
├── CMakeLists.txt                 ESP-IDF 工程入口
├── dependencies.lock             已解析并锁定的组件版本
├── sdkconfig                     当前 ESP32-S3 工程配置
├── main/
│   ├── main.c                    选择并启动当前测试入口
│   ├── CMakeLists.txt            main 组件源文件列表
│   ├── idf_component.yml         第三方组件依赖声明
│   ├── bsp/
│   │   ├── bsp_board.h           接线、频率、分辨率和方向配置
│   │   ├── bsp_lcd.c/.h          MSP3526/ST7796 板级显示驱动
│   │   └── bsp_touch.c/.h        FT6336U 板级触摸驱动
│   └── test/
│       ├── lcd_test.c/.h         ST7796 底层显示测试
│       ├── lvgl_test.c/.h        LVGL 显示与触摸综合测试
│       └── touch_test.c/.h       FT6336U 与 LVGL 的注册适配
├── managed_components/           组件管理器下载的第三方组件
└── build/                        构建生成目录
```

主要调用关系如下：

```text
app_main()
    ↓
测试层 main/test/
    ↓
板级支持层 main/bsp/
    ↓
ESP-IDF 与第三方显示、触摸、LVGL 组件
    ↓
ST7796 与 FT6336U 硬件
```

`main/bsp/` 当前是 `main` 组件内部的板级源码目录，并不是可以单独安装的 ESP-IDF 组件。BSP 只管理硬件初始化，不创建正式 LVGL 页面。

`managed_components/` 和 `build/` 都是工具自动生成的目录：前者由 ESP-IDF Component Manager 根据依赖清单下载，后者保存编译产物。不要在这两个目录中编写或长期维护项目源码。

## BSP 公共接口

### LCD 初始化

```c
esp_err_t bsp_lcd_init(esp_lcd_panel_io_handle_t *out_io,
                       esp_lcd_panel_handle_t *out_panel);
```

该接口初始化 SPI 总线、LCD Panel IO 和 ST7796 面板，并返回原始绘图或 LVGL Port 可以使用的句柄。LCD 按单实例设计，重复调用时返回第一次创建的句柄，不会重复初始化 SPI Host。

### 背光开关

```c
esp_err_t bsp_lcd_set_backlight(bool on);
```

GPIO9 在 LCD BSP 初始化期间配置为输出。传入 `true` 打开背光，传入 `false` 关闭背光；在辅助 GPIO 完成初始化前调用会返回 `ESP_ERR_INVALID_STATE`。当前接口只控制开关，不提供亮度百分比或 PWM 调光。

### 触摸初始化

```c
esp_err_t bsp_touch_init(esp_lcd_touch_handle_t *out_touch);
```

该接口初始化 I2C 总线和 FT6336U，并返回触摸句柄。触摸 BSP 只负责硬件驱动，不负责将触摸设备注册到 LVGL；LVGL 注册由测试层中的 `touch_test_init()` 完成。

LCD 和触摸 BSP 当前都没有公开 `deinit` 接口，不支持在程序运行期间反复销毁、切换或重新创建硬件实例。

## 测试程序

三个测试源模块都会参与编译，但它们不会同时独立运行。每次上电只执行 `app_main()` 中选择的入口。

### ST7796 底层显示测试

入口：

```c
void lcd_test_run(void);
```

该测试不依赖 LVGL，循环执行以下内容：

1. 关闭背光 500 ms 后重新打开，验证 GPIO9 背光开关；
2. 全屏红、绿、蓝、白，各显示 2 秒；
3. 显示红、绿、蓝、白四象限 4 秒；
4. 重复色彩与方向测试。

它用于独立检查 SPI 通信、ST7796 初始化、RGB565 字节序、BGR 色序和显示方向。

如需运行该测试，将 [`main/main.c`](main/main.c) 的入口改为：

```c
#include "lcd_test.h"

void app_main(void)
{
    lcd_test_run();
}
```

### LVGL 显示与触摸综合测试

入口：

```c
void lvgl_test_run(void);
```

这是当前 `main.c` 使用的默认入口。测试页面包含四色验证、四角按钮、SPI/I2C 参数、触摸状态、触摸坐标、点击次数和运行时间，用于综合验证 LVGL 刷新、ST7796 显示和 FT6336U 触摸链路。

触摸初始化失败时，页面会显示 `Touch: FAILED`，但 LCD 和 LVGL 会继续运行，不会因为触摸故障停留在白屏。

### LVGL 触摸注册模块

接口：

```c
esp_err_t touch_test_init(lv_display_t *display);
```

该函数不是可以从 `app_main()` 单独启动的第三个 `run()`。它由 `lvgl_test_run()` 调用，负责取得 BSP 创建的 FT6336U 句柄，并通过 `esp_lvgl_port` 将触摸设备绑定到指定 LVGL 显示器。

## 依赖和开发环境

当前工程锁定并完成构建验证的版本如下：

| 组件 | 版本 |
| --- | --- |
| ESP-IDF | 6.0.2 |
| LVGL | 9.5.0 |
| `espressif/esp_lvgl_port` | 2.8.0~1 |
| `espressif/esp_lcd_st7796` | 1.4.0 |
| `lambage/esp_lcd_touch_ft6336u` | 1.0.8 |

[`main/idf_component.yml`](main/idf_component.yml) 中的 ESP-IDF 版本条件目前仍写为 `>=4.1.0`，但这不代表工程已经在所有 4.1.0 及以上版本中完成兼容性验证。当前确定可用的开发环境是 ESP-IDF 6.0.2，复现工程时应优先使用该版本。

## 构建、烧录与串口监视

### 使用 VS Code ESP-IDF 插件

1. 使用 VS Code 打开工程根目录；
2. 确认 ESP-IDF 插件已经选择正确的 ESP-IDF 6.0.2 环境；
3. 选择目标芯片 `esp32s3` 和实际串口；
4. 执行“构建项目”；
5. 构建成功后执行“烧录项目”；
6. 打开串口监视器查看 BSP、LCD、LVGL 和触摸初始化日志。

### 使用命令行

在已经加载 ESP-IDF 环境的 PowerShell 中执行：

```powershell
# 仅在首次配置工程或需要重新选择芯片时执行
idf.py set-target esp32s3

idf.py build
idf.py -p COMx flash monitor
```

将 `COMx` 替换为开发板的实际串口，例如 `COM5`。串口监视器中可以使用 `Ctrl+]` 退出。

构建完成后，应用固件会由 ESP-IDF 自动生成，例如：

```text
build/Motor_Control_HMI.bin
```

该 `.bin` 文件是根据当前源码重新构建得到的烧录产物，不是手工编写的源文件，也不需要提交到 Git 仓库。

## 常见问题

### LCD 亮白屏但没有界面

- 确认当前 `main.c` 调用了预期的测试入口；
- 检查 VCC、GND、CS、DC、RST、MOSI 和 SCLK；
- 查看串口是否出现 SPI、ST7796 或 LVGL 初始化错误；
- 背光亮起只能证明 LED 已供电，不能证明 LCD 已经收到初始化命令。

### 颜色或显示方向错误

不要先改硬件接线。检查 `main/bsp/bsp_board.h` 中的镜像、轴交换配置，以及 `main/bsp/bsp_lcd.c` 中的 RGB/BGR 和 MSP3526 初始化参数。

### 页面显示 `Touch: FAILED`

- 检查 CTP_SCL、CTP_SDA、CTP_RST 和 CTP_INT 是否分别连接 GPIO15、GPIO6、GPIO7 和 GPIO5；
- 确认 FT6336U 地址为 `0x38`；
- 检查杜邦线是否松动、接触不良或内部断线；
- 根据串口日志确认失败发生在地址探测、驱动初始化还是 LVGL 注册阶段。

触摸失败不会阻止 LCD 和 LVGL 页面继续运行。

### 快速点击漏检或必须长按

确认屏幕的 `CTP_INT` 已连接 GPIO5。GPIO5 中断用于在触摸发生时立即唤醒 LVGL；如果只依赖周期轮询，持续时间很短的点击可能在两次轮询之间被漏掉。

## 待正式阶段确定

以下内容属于 Motor Control HMI 的正式项目设计，将在需求和硬件方案明确后再补充：

- 电机驱动与编码器的硬件接口；
- 位置环、速度环和 PID 参数结构；
- 正式 HMI 页面、导航方式、字体和视觉风格；
- 页面、控制任务和其他模块之间的通信方式；
- 参数保存、设备校准、故障处理和安全策略；
- SD 卡挂载与文件读写、LCD 读屏、背光 PWM 调节和多点触摸等扩展能力。
