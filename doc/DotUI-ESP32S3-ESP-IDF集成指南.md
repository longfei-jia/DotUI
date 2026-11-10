# DotUI ESP32-S3 移植集成指南（ESP-IDF）

> 适用屏幕：**LS013B7DH03（128×128）**、**LS013B7DH05（144×168）**（Sharp 内存型液晶）
> 适用主控：ESP32-S3-N16R8（16MB Flash + 8MB Octal PSRAM），ESP-IDF v5.x
> 源码库：[DotUI](../inc) 框架（`inc/` + `src/`），u8g2 全缓冲模式

---

## 1. 移植总览

DotUI 的分层设计使移植工作量收敛为 **一块 BSP（板级支持包）+ 一个入口文件**：

```
┌─────────────────────────────────────────────────────┐
│ 应用层  my_user_app.cpp（参考 dot_user_app.cpp 编写） │  ← 自己写
├─────────────────────────────────────────────────────┤
│ 框架层  DotUI（inc/ 全部 + src/ 5 个文件）            │  ← 原样复制，零修改
│         u8g2 csrc（显示库）                           │  ← 原样复制，零修改
├─────────────────────────────────────────────────────┤
│ 平台层  bsp_display.c  SPI 回调 + VCOM + DISP        │  ← 本文提供完整代码
│         bsp_keys.c     按键扫描 → DotInput            │  ← 本文提供完整代码
│         main.cpp       app_main 入口                 │  ← 本文提供完整代码
└─────────────────────────────────────────────────────┘
```

SDL 模拟器（[main.cpp](../src/main.cpp)）与真机入口的职责对照：


| SDL 模拟器                                | ESP32 真机                                                              |
| ------------------------------------------- | ------------------------------------------------------------------------- |
| `u8g2_SetupBuffer_SDL_128x128 / _144x168` | `u8g2_Setup_ls013b7dh03_128x128_f` / `u8g2_Setup_ls013b7dh05_144x168_f` |
| `SDL_GetTicks()` 毫秒源                   | `esp_timer_get_time()/1000`                                             |
| `u8g_sdl_get_key()` 事件泵                | GPIO 按键扫描 → `DotInput::SetKey()`                                   |
| `SDL_Delay(16)` ~60FPS                    | `vTaskDelayUntil` 30FPS                                                 |
| `u8g_sdl_set_inverse()` 深色模式          | 见 §9（可选小改造）                                                    |

---

## 2. 屏幕特性速览（必须理解，否则会踩坑）

LS013B7DH03/05 是 **内存型液晶（Memory-in-Pixel）**，与 OLED/TFT 完全不同：


| 特性                         | 说明                                                      | 移植影响                                                                                                                                                                        |
| ------------------------------ | ----------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **无显示控制器**             | 屏内只有行驱动，没有显存、没有指令集                      | 每次 `u8g2_SendBuffer` 把**整帧**逐行发过去                                                                                                                                     |
| **断电保持 / 通电残影**      | 像素是记忆单元，静态画面不刷新也能保持                    | 画面内容无需持续重画；但 VCOM 极性仍须周期维持（§6.3），静止时仅 10Hz 低频重发即可                                                                                             |
| **VCOM 必须周期翻转**        | 否则液晶直流偏置累积，产生残影甚至永久损伤                | u8g2 驱动**不管这事**。本方案：EXTCOMIN 引脚**接 GND**（禁用硬件翻转路径），VCOM 极性由**数据流中的 VCOM 位**（命令字节 bit1）软件翻转，由独立 FreeRTOS 任务维持 10Hz，见 §6.3 |
| **SCS 高有效**               | 片选 1 = 选中（与常规 SPI 屏相反）                        | u8g2 驱动已内置（`chip_enable_level=1`），BSP 按消息驱动 GPIO 即可                                                                                                              |
| **SPI mode 0，时钟 ≤ 2MHz** | SCLK 空闲低电平，上升沿采样                               | IDF 设备配置 `mode=0`，`clock_speed_hz=2MHz`                                                                                                                                    |
| **逐行数据包协议**           | 每行 = 命令(8bit) + 行地址(8bit) + 行数据 + trailer(8bit) | u8g2 的 [u8x8_d_ls013b7dh03.c](../lib/u8g2/csrc/u8x8_d_ls013b7dh03.c) 已封装，BSP 只管搬运字节                                                                                  |
| **无 RESET / 无背光**        | DISP 引脚=1 使能显示即可                                  | 接 GPIO 拉高，省一个复位脚                                                                                                                                                      |

**带宽预算**（全帧一次 `SendBuffer`）：


| 屏       | 行包大小 | 全帧字节数 | @2MHz      | @1MHz      |
| ---------- | ---------- | ------------ | ------------ | ------------ |
| 128×128 | 19 B     | 2432 B     | ≈ 9.7 ms  | ≈ 19.5 ms |
| 144×168 | 21 B     | 3528 B     | ≈ 14.1 ms | ≈ 28.2 ms |

结论：**30FPS 持续刷会占掉一半 SPI 时间**，推荐策略见 §10（静止不刷、动时按需刷）。

---

## 3. 硬件接线（示例引脚，可自行调整）

```
ESP32-S3              LS013B7DH03/05
GPIO11  ────────────  SI     （MOSI，串行数据输入）
GPIO12  ────────────  SCLK   （SPI 时钟，≤2MHz）
GPIO10  ────────────  SCS    （片选，高有效）
GND     ────────────  EXTCOMIN（固定拉低：禁用硬件 VCOM 翻转，改走软件 VCOM 位）
GPIO14  ────────────  DISP   （显示使能，拉高=显示）
3V3     ────────────  VDDA / VDDIO / VDD（三路电源都要接）
GND     ────────────  GND

按键（低有效，内部上拉）：
GPIO4=上  GPIO5=下  GPIO6=左  GPIO7=右  GPIO15=OK  GPIO16=返回
```

> 注意：SI 是**输入**方向，屏没有 MISO，SPI 总线全双工配置但 MISO 置 -1。

---

## 4. 工程结构（IDF 组件化）

```
esp32s3_dotui/
├── CMakeLists.txt                  # 顶层：project(dotui_demo)
├── sdkconfig.defaults              # 见 §11
├── main/
│   ├── CMakeLists.txt
│   ├── main.cpp                    # app_main 入口（§8）
│   ├── bsp_display.h
│   ├── bsp_display.c               # u8g2 平台回调 + VCOM（§6）
│   ├── bsp_keys.h
│   ├── bsp_keys.c                  # 按键扫描（§7）
│   └── my_user_app.cpp             # 应用菜单：实现 InitLayout/HomeScreen/SaveConfig
└── components/
    ├── u8g2/                       # u8g2 库（§5.1）
    │   ├── CMakeLists.txt
    │   └── csrc/                   # 从 lib/u8g2/csrc 复制
    └── DotUI/                      # DotUI 框架（§5.2）
        ├── CMakeLists.txt
        ├── inc/                    # 从 inc/ 全部复制
        └── src/                    # 从 src/ 复制 5 个文件（见下表）
```

### 5.1 u8g2 组件：只带 csrc

从本仓库 [lib/u8g2/csrc](../lib/u8g2/csrc) 复制全部 `.c/.h` 到 `components/u8g2/csrc/`（SDL 相关的 `sys/sdl/` 不要带）。

`components/u8g2/CMakeLists.txt`：

```cmake
idf_component_register(
    SRCS
        "csrc/u8g2_d_setup.c"      # 所有屏幕的 setup 函数（含 ls013b7dh03/05）
        "csrc/u8g2_d_memory.c"     # 全缓冲模式内存管理
        "csrc/u8g2_setup.c"
        "csrc/u8g2.c"
        "csrc/u8g2_buffer.c"
        "csrc/u8g2_font.c"
        "csrc/u8g2_hvline.c"
        "csrc/u8g2_line.c"
        "csrc/u8g2_box.c"
        "csrc/u8g2_circle.c"
        "csrc/u8g2_polygon.c"
        "csrc/u8g2_bitmap.c"
        "csrc/u8g2_string.c"
        "csrc/u8g2_intersection.c"
        "csrc/u8g2_cleardraw.c"
        "csrc/u8g2_font_utft8.c"   # UTF-8 解码（中文显示必需）
        "csrc/u8g2_rotation.c"
        "csrc/u8g2_ll_hvline.c"
        "csrc/u8x8.c"
        "csrc/u8x8_u8g2.c"
        "csrc/u8x8_setup.c"
        "csrc/u8x8_d_ls013b7dh03.c"  # 两块 Sharp 屏共用此驱动
        "csrc/u8x8_cad.c"            # 命令/数据层（CS 由它经 GPIO 消息管理）
        "csrc/u8x8_byte.c"
        "csrc/u8x8_gpio.c"
        "csrc/u8x8_display.c"
    INCLUDE_DIRS "csrc"
)
```

> 省事的替代方案：ESP 组件库有托管组件，在 `main/idf_component.yml` 写
> `dependencies: espressif/u8g2: "~1.0"`，`idf.py` 自动拉取，无需手动复制。
> 自带 csrc 的好处是与模拟器共用同一份源码，行为完全一致。

### 5.2 DotUI 组件：复制清单


| 源文件                              | 处理           | 说明                                        |
| ------------------------------------- | ---------------- | --------------------------------------------- |
| `inc/*.h`（8 个头文件 + uvector.h） | **原样复制**   | 平台无关                                    |
| `src/dot_ui.cpp`                    | **原样复制**   | 框架核心                                    |
| `src/dot_item.cpp`                  | **原样复制**   | 条目行为                                    |
| `src/dot_page.cpp`                  | **原样复制**   | 页面池                                      |
| `src/dot_input.cpp`                 | **原样复制**   | 按键状态机（纯逻辑，无平台依赖）            |
| `src/resources.cpp`                 | **原样复制**   | 字体/图标常量表                             |
| `src/dot_user_app.cpp`              | **参考后替换** | 演示菜单（`time()/srand` 仅用于随机测试页） |
| `src/main.cpp`                      | **不复制**     | SDL 模拟器入口                              |

`components/DotUI/CMakeLists.txt`：

```cmake
idf_component_register(
    SRCS
        "src/dot_ui.cpp"
        "src/dot_item.cpp"
        "src/dot_page.cpp"
        "src/dot_input.cpp"
        "src/resources.cpp"
        "../../main/my_user_app.cpp"   # 或放到 DotUI src 下
    INCLUDE_DIRS "inc"
    REQUIRES u8g2
)
```

> DotUI 源码使用 C++17 特性（`if constexpr`），ESP-IDF v5.x 默认即满足，无需额外配置。

---

## 6. 平台层：bsp_display（完整代码）

### 6.1 u8g2 平台回调原理

u8g2 与硬件的全部交互走**两条消息通道**，本仓库模拟器（`lib/u8g2/sys/sdl/`）也是同样机制：

- **byte 回调**：搬运数据。Sharp 驱动（`u8x8_d_ls013b7dh03.c`）经 CAD 层（`u8x8_cad_001`）按 **8 行一组**发出 `START_TRANSFER → 数据 → END_TRANSFER`，一组 128 宽 ≈146B / 144 宽 ≈162B。
- **gpio_and_delay 回调**：管理 SCS 片选（注意**高有效**，u8g2 会按 `chip_enable_level=1` 驱动）与延时。

### 6.2 bsp_display.c

```c
// bsp_display.c —— ESP32-S3 + Sharp 内存液晶 BSP
#include "bsp_display.h"
#include "u8g2.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdbool.h>

// ---- 引脚定义（与接线表一致）----
#define PIN_MOSI     GPIO_NUM_11
#define PIN_SCLK     GPIO_NUM_12
#define PIN_SCS      GPIO_NUM_10   // 片选，高有效
#define PIN_DISP     GPIO_NUM_14   // 显示使能，高有效
// 注意：EXTCOMIN 不由 MCU 驱动，硬件上直接接 GND（软件 VCOM 方案）

// ---- 软件 VCOM ----
// 写行命令字节：0x01（SWAP8 后 0x80），bit1 是 VCOM 位 → 极性 1 时为 0xC0。
// EXTCOMIN 拉低后，屏内 VCOM 极性完全由数据流中该位决定。
// bsp_vcom_task 每 100ms 翻转一次极性并重发当前缓冲（画面不变，纯保极性）。
static volatile bool s_vcom = false;

// ---- u8g2 互斥锁：UI 任务渲染 + VCOM 任务重发共用 ----
// VCOM 任务只读缓冲重发，若与 UI 的 ClearBuffer/绘制并发，会把画到一半
// 的半帧发出去（撕裂）。用锁把两边都保护起来。
static SemaphoreHandle_t s_u8g2_lock;

void bsp_display_lock(void)   { xSemaphoreTake(s_u8g2_lock, portMAX_DELAY); }
void bsp_display_unlock(void) { xSemaphoreGive(s_u8g2_lock); }

static spi_device_handle_t s_spi;
static uint8_t  s_buf[512];       // 单次传输缓冲（8 行一组最大 ~162B，512 富余）
static size_t   s_len;

// ---------------------------------------------------------------------------
// u8g2 byte 回调：CAD 层按 8 行一组发 START → 若干 BYTE_SEND → END，
// 这里把数据攒进 s_buf，END_TRANSFER 时作为一次 SPI 事务发出。
// 每组的第一个字节是写行命令 0x80，按当前极性替换（注入 VCOM 位）；
// 其余字节（行地址/像素数据/trailer）原样搬运。
// SCS 由 gpio 回调管理，SPI 硬件 CS 置 -1 避免二次驱动。
// ---------------------------------------------------------------------------
uint8_t u8x8_byte_esp32_spi(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    switch (msg) {
    case U8X8_MSG_BYTE_START_TRANSFER:
        s_len = 0;
        break;
    case U8X8_MSG_BYTE_SEND:
        // 传输组首字节 = 写行命令 0x80（s_len==0 且单字节且值匹配才替换，
        // ALL_CLEAR 的 0x20 等其他命令不受影响）
        if (s_len == 0 && arg_int == 1 &&
            ((const uint8_t *)arg_ptr)[0] == 0x80) {
            s_buf[s_len++] = s_vcom ? 0xC0 : 0x80;
        } else {
            memcpy(&s_buf[s_len], arg_ptr, arg_int);
            s_len += arg_int;
        }
        break;
    case U8X8_MSG_BYTE_END_TRANSFER: {
        spi_transaction_t t = {
            .length = s_len * 8,
            .tx_buffer = s_buf,
        };
        ESP_ERROR_CHECK(spi_device_transmit(s_spi, &t));  // 阻塞式，约 0.6ms/组
        break;
    }
    case U8X8_MSG_BYTE_INIT:      // SPI 在 bsp_display_init 里初始化
    case U8X8_MSG_BYTE_SET_DC:    // Sharp 屏无 DC 线，忽略
    default:
        break;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// u8g2 gpio/延时回调：SCS 片选（高有效！）+ 各级延时
// ---------------------------------------------------------------------------
uint8_t u8x8_gpio_delay_esp32(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    switch (msg) {
    case U8X8_MSG_GPIO_AND_DELAY_INIT:
        // 引脚方向在 bsp_display_init 里统一配置
        break;
    case U8X8_MSG_GPIO_CS:
        gpio_set_level(PIN_SCS, arg_int);    // chip_enable_level=1 → SCS 高有效
        break;
    case U8X8_MSG_DELAY_MILLI:
        vTaskDelay(pdMS_TO_TICKS(arg_int));
        break;
    case U8X8_MSG_DELAY_10MICROS:
        esp_rom_delay_us(10);
        break;
    case U8X8_MSG_DELAY_100NANOS:
        esp_rom_delay_us(1);                 // 取整到 1us，u8g2 初始化足够
        break;
    default:
        break;                               // 其余 GPIO 消息（未接的脚）忽略
    }
    return 1;
}

// ---------------------------------------------------------------------------
// VCOM 翻转任务：10Hz（datasheet 要求 1~30Hz）。
// 每 100ms 翻转极性 → 拿锁 → 重发当前缓冲。画面内容不变，
// 但每一帧数据流里的 VCOM 位随 s_vcom 交替，实现软件翻转。
// 拿锁发送保证不会与 UI 渲染撞车发半帧。
// ---------------------------------------------------------------------------
static void bsp_vcom_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        s_vcom = !s_vcom;

        bsp_display_lock();
        u8g2_SendBuffer(&g_u8g2);            // 重发缓冲，画面保持不变
        bsp_display_unlock();
    }
}

// ---------------------------------------------------------------------------
// 总初始化：SPI 总线 → u8g2 setup → DISP 使能 → 锁 + VCOM 任务
// screen 取 0 = LS013B7DH03(128x128)，1 = LS013B7DH05(144x168)
// ---------------------------------------------------------------------------
u8g2_t g_u8g2;

void bsp_display_init(int screen)
{
    // 0. u8g2 互斥锁（UI 任务与 VCOM 任务共用）
    s_u8g2_lock = xSemaphoreCreateMutex();
    configASSERT(s_u8g2_lock);

    // 1. SPI 总线（APB，DMA 可选；帧小，轮询发送足够）
    spi_bus_config_t bus = {
        .mosi_io_num     = PIN_MOSI,
        .sclk_io_num     = PIN_SCLK,
        .miso_io_num     = -1,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED));

    spi_device_interface_config_t dev = {
        .clock_speed_hz = 2 * 1000 * 1000,   // Sharp 上限 2MHz
        .mode           = 0,                 // CPOL=0 CPHA=0
        .spics_io_num   = -1,                // CS 交给 u8g2 gpio 回调（高有效）
        .queue_size     = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &s_spi));

    // 2. SCS / DISP 引脚
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_SCS) | (1ULL << PIN_DISP),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(PIN_DISP, 1);             // 使能显示

    // 3. u8g2 全缓冲 setup（分辨率与 DotLayout::init 自动匹配）
    if (screen == 0)
        u8g2_Setup_ls013b7dh03_128x128_f(&g_u8g2, U8G2_R0,
                                         u8x8_byte_esp32_spi, u8x8_gpio_delay_esp32);
    else
        u8g2_Setup_ls013b7dh05_144x168_f(&g_u8g2, U8G2_R0,
                                         u8x8_byte_esp32_spi, u8x8_gpio_delay_esp32);

    u8g2_InitDisplay(&g_u8g2);               // 内部会发 ALL_CLEAR 清屏
    u8g2_SetPowerSave(&g_u8g2, 0);           // Sharp 屏无省电指令，此调用为空操作

    // 4. VCOM 翻转任务：优先级低于 UI 任务，栈 2KB 足够
    xTaskCreate(bsp_vcom_task, "vcom", 2048, NULL,
                tskIDLE_PRIORITY + 2, NULL);
}
```

`bsp_display.h`：

```c
#ifndef BSP_DISPLAY_H
#define BSP_DISPLAY_H
#include "u8g2.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

extern u8g2_t g_u8g2;
void bsp_display_init(int screen);   // 0 = 128x128, 1 = 144x168
void bsp_display_lock(void);         // UI 任务在渲染+发送临界区使用
void bsp_display_unlock(void);

#ifdef __cplusplus
}
#endif
#endif
```

### 6.3 软件 VCOM 的工作机制（重要）

本方案 EXTCOMIN 硬件拉低，VCOM 极性完全由数据流控制，涉及三处配合：

```
bsp_vcom_task (10Hz)          UI 任务 (30FPS)
      │                             │
 翻转 s_vcom ────────┐        拿锁 → Tick → Frame（渲染+SendBuffer）→ 放锁
      │              │                 ▲
 拿锁 → SendBuffer ──┼─────────────────┘ 互斥：避免发送画到一半的缓冲
      │              ▼
 byte 回调：每个 8 行传输组的首字节 0x80 按极性替换为 0x80/0xC0
      ▼
 屏内 VCOM 极性每 100ms 交替一次 → 满足 datasheet 1~30Hz
```

关键点：

1. **不改 u8g2 驱动**——VCOM 位在 BSP 的 byte 回调里注入，判定条件 `s_len==0 && 单字节 && 值==0x80` 精确命中每个传输组的写行命令，不会误伤行地址、像素数据和 ALL_CLEAR 命令。
2. **UI 帧与 VCOM 帧共用同一极性变量**——UI 的 `SendBuffer` 发送时也带着当前极性（正确），极性翻转节奏恒定由 VCOM 任务维持 10Hz，不随 UI 帧率波动。
3. **静止时 VCOM 任务是唯一刷新来源**——UI 无交互时画面不变，但 VCOM 重发必须继续（每 100ms 一帧，SPI 占用约 14%），这是软件 VCOM 相对硬件 PWM 的代价，见 §10。
4. **帧内极性一致**——一帧的 16~21 个传输组都在同一 `s_vcom` 极性下发送（翻转只发生在任务里），不会出现半屏极性不同。

---

## 7. 平台层：bsp_keys（按键扫描）

[dot_input.h](../inc/dot_input.h) 的状态机负责长按/连发/事件队列，BSP 只需在**固定周期**里喂电平：

```c
// bsp_keys.c —— GPIO 按键 → DotInput 电平
#include "bsp_keys.h"
#include "dot_input.h"
#include "driver/gpio.h"

static const struct { gpio_num_t gpio; DotInput::Key key; } s_keys[] = {
    { GPIO_NUM_4,  DotInput::KEY_UP    },
    { GPIO_NUM_5,  DotInput::KEY_DOWN  },
    { GPIO_NUM_6,  DotInput::KEY_LEFT  },
    { GPIO_NUM_7,  DotInput::KEY_RIGHT },
    { GPIO_NUM_15, DotInput::KEY_OK    },
    { GPIO_NUM_16, DotInput::KEY_BACK  },
};
#define KEY_NUM (sizeof(s_keys) / sizeof(s_keys[0]))

void bsp_keys_init(void)
{
    uint64_t mask = 0;
    for (unsigned i = 0; i < KEY_NUM; ++i) mask |= 1ULL << s_keys[i].gpio;
    gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,    // 按键接 GND，低有效
    };
    gpio_config(&io);
}

void bsp_keys_scan(uint32_t now_ms, DotInput *input)
{
    for (unsigned i = 0; i < KEY_NUM; ++i) {
        // 直接把原始电平喂给状态机，消抖/长按/连发由 DotInput 内部完成
        input->SetKey(s_keys[i].key, gpio_get_level(s_keys[i].gpio) == 0, now_ms);
    }
}
```

> DotInput 的消抖阈值在 [dot_input.cpp](../src/dot_input.cpp) 内部（`changedAt` 时间戳），周期调用即可。扫描周期建议 ≤10ms（跟随 UI 任务 33ms 周期也可，手感稍钝）。

---

## 8. 应用入口 main.cpp（对照 SDL 版改写）

```cpp
// main.cpp —— ESP32-S3 真机入口
#include "bsp_display.h"
#include "bsp_keys.h"
#include "dot_defs.h"
#include "dot_input.h"
#include "dot_ui.h"
#include "resources.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static DotInput g_input;
static DotUI    g_ui(&g_u8g2, &g_input);

extern "C" void app_main(void)
{
    bsp_display_init(1);        // 0=LS013B7DH03 128x128，1=LS013B7DH05 144x168
    bsp_keys_init();

    DotUI::SetInstance(&g_ui);
    DotUI::SetMillisSource([] {
        return (uint32_t)(esp_timer_get_time() / 1000ULL);   // 对应 SDL_GetTicks
    });
    g_ui.SetFonts(DotRes::kFontSmall, DotRes::kFontBig);
    g_ui.Begin();               // InitLayout 由 my_user_app.cpp 提供

    TickType_t last_wake = xTaskGetTickCount();
    for (;;) {
        const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);

        bsp_keys_scan(now, &g_input);   // 替代 SDL 的事件泵

        // 渲染临界区：Frame 内含 ClearBuffer→绘制→SendBuffer，
        // 必须与 VCOM 任务的重发互斥，否则会发出去画到一半的半帧
        bsp_display_lock();
        g_ui.Tick(now);                 // 消费事件、推进弹簧
        g_ui.Frame();                   // 渲染整帧并发送（约 10~14ms）
        bsp_display_unlock();

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(33));   // 30FPS
    }
}
```

`main/CMakeLists.txt`：

```cmake
idf_component_register(
    SRCS "main.cpp" "bsp_display.c" "bsp_keys.c" "my_user_app.cpp"
    INCLUDE_DIRS "."
    REQUIRES DotUI u8g2 driver esp_timer
)
```

> 与 SDL 版的两处结构差异：
> ① 真机没有事件泵概念——`bsp_keys_scan` 每 33ms 喂一次电平即可；
> ② SDL 版每帧必发缓冲没问题（桌面带宽无限），真机按 §10 的策略控制刷新率。

---

## 9. 深色模式（可选小改造）

Sharp 反射屏没有反显指令，深色模式用**整帧 XOR** 实现：渲染完成后把整个缓冲区反相一次。在 [dot_ui.cpp](../src/dot_ui.cpp) 加 3 行：

```cpp
// dot_ui.h 的 DotUI 类中加：
    void SetInvert(bool v) { invert_ = v; }
// private 成员区加：
    bool invert_ = false;
```

```cpp
// dot_ui.cpp 的 Frame() 中，drawOverlays() 之后、SendBuffer() 之前：
    if (invert_) {
        u8g2_SetDrawColor(g2_, 2);   // 画色 2 = XOR 模式
        u8g2_DrawBox(g2_, 0, 0, DotLayout::kScreenW, DotLayout::kScreenH);
    }
    u8g2_SendBuffer(g2_);
```

应用层 `dot_set_dark_mode` 改为一行：

```cpp
void dot_set_dark_mode(bool dark) {
    DotUI::Instance().SetInvert(dark);
}
```

> XOR 一次覆盖页面+覆盖层全部像素，XOR 选中框等绘制色 2 的元素在反相后视觉上依旧正确。代价是每帧多一次全屏 XOR（u8g2 内部按字节异或，<1ms）。

---

## 10. 刷新策略与性能优化

### 10.1 现状与预算

> 软件 VCOM 方案下，**画面静止时也必须由 VCOM 任务以 10Hz 重发缓冲**（VCOM 位只在数据流里），"零刷新"不再存在。下表按此基线重新评估：


| 策略                                              | SPI 占用（144×168 @2MHz）   | 评价                             |
| --------------------------------------------------- | ------------------------------ | ---------------------------------- |
| 30FPS 无条件重画+重发                             | 30 × 14.1ms ≈ 42% 总线时间 | 可用，浪费                       |
| **VCOM 任务 10Hz 重发 + UI 按需重画（推荐基线）** | 10 × 14.1ms ≈ 14%          | 静止时只有 VCOM 重发，无渲染开销 |
| 30FPS 无条件 + VCOM 10Hz（本文默认）              | ≈ 42% + 14%                 | 先跑通，再按 §10.2 优化         |

### 10.2 按需重画改法（推荐，改动很小）

软件 VCOM 下 `u8g2_SendBuffer` 重发**不重画**（缓冲内容原样出），所以省的是渲染 CPU，不是 SPI 带宽。给 `DotUI::Frame()` 加条件渲染，VCOM 任务照旧 10Hz 重发：

```cpp
// dot_ui.cpp Frame() 末段改为：
    const bool animActive = !focusS_.settled() || !scroll_.settled()
                         || !selX_.settled()   || !selW_.settled();
    if (animActive || overlayDepth_ > 0 || dirty_) {
        renderPage();               // 只在画面有变化时重画
        dirty_ = false;
    }
    u8g2_SendBuffer(g2_);           // 每帧必发：VCOM 极性随任务 10Hz 交替
```

`dirty_` 在 `Tick()` 里消费到任何输入事件时置位（按键必有反馈）：

```cpp
// Tick() 的事件循环里：
    while (in_->Pop(ev, k)) {
        dirty_ = true;
        ...
    }
```

> 应用层主动改数据（如 VALUE 档位切换）调用的回调都在事件处理之后、下一帧渲染前，`dirty_` 已置位，无需逐处标记。跑马灯动画期间若整页静止（焦点不动），可在 marquee 活动时也置 dirty，一行判断即可。

### 10.3 功耗评估

软件 VCOM 的最低刷新底线是 10Hz 重发，**不能像硬件 PWM 方案那样完全静止**。但 `vTaskDelayUntil` + tickless idle 仍可在两次唤醒的间隙进 light sleep（每次 ~86ms），平均功耗仍远低于持续刷屏方案；EXTCOMIN 硬件 PWM 方案则支持无限期静止+深度睡眠。若产品对功耗极敏感，建议硬件上把 EXTCOMIN 接一个可切换的 GPIO/LEDC 引脚：平时软件 VCOM，深度睡眠前改用 LEDC 方波维持，兼顾两种场景。

---

## 11. sdkconfig 与字体资源

### 11.1 sdkconfig.defaults（N16R8）

```
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y            # N16R8 是 Octal PSRAM（R8）
CONFIG_SPIRAM_SPEED_120M=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
```

> 若不用 PSRAM（本方案用不到，帧缓冲在内部 RAM），可删掉 SPIRAM 两行。

### 11.2 内存占用核对（ESP32-S3-N16R8 完全无压力）


| 资源                                         | 大小    | 位置                                                     |
| ---------------------------------------------- | --------- | ---------------------------------------------------------- |
| u8g2 全缓冲 128×128                         | 2 KB    | 内部 RAM                                                 |
| u8g2 全缓冲 144×168                         | 3.1 KB  | 内部 RAM                                                 |
| DotUI 对象（页面池 16 页 + 条目 + 控件池 4） | ~15 KB  | 内部 RAM                                                 |
| UI 任务栈（建议）                            | 6 KB    | 内部 RAM                                                 |
| `kFontChinese`（wqy12 全字库）               | ~202 KB | **Flash**（const 数组进 rodata，地址映射读取，不占 RAM） |
| `kFontSmall/Big` + 图标                      | ~30 KB  | Flash                                                    |
| 程序 + u8g2                                  | ~800 KB | Flash（16MB 的 5%）                                      |

结论：中文全字库直接用（`resources.cpp` 已是 `u8g2_font_wqy12_t_gb2312`），无需裁剪。若追求极致（固定几十条文案），可用取模工具生成子集压到几 KB。

---

## 12. 验证清单

按顺序验证，每步通过再进行下一步：

1. **编译烧录**：`idf.py build flash monitor`，无警告通过
2. **清屏**：上电后屏幕整体白色（u8g2 的 ALL_CLEAR 已执行）——证明 SPI/SCS/DISP 正确
3. **VCOM**：静止 30 秒后观察是否出现残影/变灰——正常应由 VCOM 任务 10Hz 重发翻转极性，无残影。可用逻辑分析仪抓 SCS，确认静止时每 100ms 有一轮整帧传输
4. **首帧 UI**：主页（图标页）出现，图标+名称垂直居中——证明 u8g2 渲染与 DotLayout 正确
5. **导航**：上下键移动焦点，弹簧动画平滑、选中框跟随——证明 Tick/Frame 节拍与毫秒源正确
6. **长按**：按住 OK 500ms 返回主页——证明 DotInput 长按状态机工作
7. **设置页**：左右键切语言/屏幕方向/阻尼档位，即刻生效
8. **中文**：切到中文界面，汉字显示正常——证明 UTF-8 字体通路正确

### 常见问题排查


| 现象                                             | 原因                                                                                                                |
| -------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| 全白不亮                                         | DISP 未拉高；SCS 接错（注意高有效）；电源 VDDA/VDDIO/VDD 有遗漏                                                     |
| 乱条纹/错位                                      | SCLK 超 2MHz；SPI mode 不是 0；SCLK/SI 接反                                                                         |
| 显示内容上下偏移一行                             | trailer 字节时序问题——检查是否用的是原版 `u8x8_d_ls013b7dh03.c` 未改动                                            |
| 残影越来越重                                     | VCOM 没翻转——VCOM 任务未创建/被饿死（优先级过低）、EXTCOMIN 悬空误触发（应接 GND）、byte 回调未命中 0x80 替换条件 |
| 偶发半帧撕裂                                     | 渲染与 VCOM 重发未互斥——确认 UI 任务 Frame() 外套了 `bsp_display_lock/unlock`                                     |
| UI 卡死不动                                      | `SetMillisSource` 未设置（弹簧拿不到 dt）；按键扫描没调用                                                           |
| 动画卡顿                                         | 任务被更高优先级抢占；或 15ms 发送期间长阻塞——用脏标记减少发送                                                    |
| 编译报 `u8g2_Setup_ls013b7dh05_144x168_f` 未定义 | 组件 SRCS 漏了 `u8g2_d_setup.c` 或 `u8x8_d_ls013b7dh03.c`                                                           |
| 中文乱码                                         | 字体没切到 `kFontChinese`（中文模式由 `applyLanguage` 处理），或用了 `DrawText` 而非 UTF-8 通路                     |

---

## 13. 后续可做的优化（移植跑通后）

1. **按需重画（脏标记）**（§10.2）——省渲染 CPU；SPI 侧因软件 VCOM 底线 10Hz，收益约 25%
2. **DotItem 静态池**——`dot_page.cpp` 的 `new DotItem()` 换成静态池，与页面池/控件池一致，启动时内存占用确定
3. **帧率自适应**——按键无事件时把 UI 任务唤醒周期拉长（如 100ms），渲染空转消除；VCOM 任务保持 10Hz 独立节拍
4. **深色模式 PATCH 合入**——§9 的 3 行改造建议提交回框架，模拟器与真机行为统一
5. **掉电保存**——设置页已有 `SaveConfig` 钩子，接 NVS 即可持久化语言/方向/阻尼
