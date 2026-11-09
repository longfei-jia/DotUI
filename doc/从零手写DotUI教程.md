# 从零手写 DotUI —— 小白友好分步实现教程

> 目标：不看成品代码也能从空目录开始，手敲出整个 MonoUI 框架。
> 每一步都**可以编译、可以运行、可以验证**——验证通过才进入下一步。

---

## 怎么用这份文档

1. **每章 = 一次可验证的小成品。** 先看"本章目标"，照着敲代码，然后执行"编译验证"，现象对得上才算过关。
2. 文档中的代码即成品代码（摘自本仓库 `src/`、`inc/`），注释里有设计原因。**建议手敲而不是复制**——敲的过程就是理解的过程。纯数据表（如图标位图、词条表）可以例外，直接从仓库复制。
3. 遇到编译错误优先检查：文件是否保存为 **UTF-8 编码**（中文字符串必需）、头文件是否加了 `#ifndef` 保护、CMake 里是否忘了加新 `.cpp`。
4. 所有章节完成后，你敲出来的东西应当与本仓库 `src/` `inc/` 完全一致（仅注释详略不同）。

编译命令（仓库根目录，配置一次，以后只需第二条）：

```bash
cmake -S . -B build -G Ninja
ninja -C build
./build/dotui_sim.exe          # 144x168 分辨率：./build/dotui_sim.exe 144x168
```

---

## 第 0 章 架构总览（先看地图，再上路）

整个框架只做一件事：**在 1-bit 小屏上，用列表/图标页展示菜单，接受按键，带平滑动画地切换焦点、滚动、弹窗。**

```
┌─────────────────────────────────────────────────────┐
│ 应用层  main.cpp + mono_user_app.cpp                 │
│   · 初始化 u8g2/SDL 窗口，把 SDL 按键转成虚拟电平      │
│   · 实现 MonoUI 的三个钩子：InitLayout/HomeScreen/SaveConfig │
├─────────────────────────────────────────────────────┤
│ 框架层  MonoUI（核心调度）                            │
│   ├── MonoPage  页面池（LIST/ICON/CUSTOM 三种模式）   │
│   │     └── MonoItem  菜单里的一行（8 种控件类型）    │
│   ├── MonoInput 按键去抖/长按/连发 → 事件队列          │
│   ├── MonoWidget 覆盖层（MsgBox/Toast/ProgressBar）   │
│   ├── MonoSpring 弹簧动画（焦点/滚动/选中框都靠它）    │
│   └── MonoMarquee 跑马灯状态机                        │
├─────────────────────────────────────────────────────┤
│ 绘图层  u8g2（C 库）：帧缓冲 + 字体 + 几何图元         │
├─────────────────────────────────────────────────────┤
│ 显示后端  PC：SDL2 窗口 ｜ 真机：SPI 屏驱动            │
└─────────────────────────────────────────────────────┘
```

主循环一帧发生的事（记住这张图，后面每章都在填其中一块）：

```
SDL事件 → 虚拟按键电平 → MonoInput(去抖/长按判定) → 事件队列
                                                      │
MonoUI::Tick(now) ──消费事件──► 覆盖层优先处理，否则导航(移动焦点/确认/返回)
MonoUI::Frame()  ──渲染──► 清屏 → 页面(弹簧推进+画行) → 覆盖层 → 提交
```

**路线图**（每章一个验证点）：


| 章 | 产出                               | 验证现象                            |
| ---- | ------------------------------------ | ------------------------------------- |
| 1  | CMake 骨架                         | `ninja` 编译通过                    |
| 2  | u8g2 + SDL 窗口                    | 黑窗口里出现文字和移动方块          |
| 3  | uvector / MonoSpring / MonoMarquee | 方块带弹簧手感地跟手移动            |
| 4  | MonoInput 按键系统                 | 屏上打印 CLICK/LONG/REPEAT          |
| 5  | resources 资源层                   | 中文正常显示                        |
| 6  | MonoItem + MonoPage 页面池         | 屏上画出手动注册的菜单文本          |
| 7  | MonoUI 最小核心                    | ↑↓ 跳动选行，OK 进子页，BACK 返回 |
| 8  | 弹簧动画                           | 焦点/滚动变成平滑惯性运动           |
| 9  | 全部行内控件                       | 开关/数值/进度条/勾选/跑马灯可用    |
| 10 | XOR 选中框                         | 选中框平滑伸缩滑动、无角部空洞      |
| 11 | 图标页                             | 图标横向轮播                        |
| 12 | MonoWidget 覆盖层                  | 弹窗/气泡/进度框，按键被模态拦截    |
| 13 | 应用层组装 + 多语言                | 完整 demo，中英切换                 |

---

## 第 1 章 环境与 CMake 骨架

**本章目标**：工程能编译出一个空 exe。

新建目录结构：

```
DotUI/
├── CMakeLists.txt
├── inc/            # 框架头文件（后面各章创建）
├── src/            # 框架实现 + main.cpp
└── lib/u8g2/       # 从 https://github.com/olikraus/u8g2 整体克隆进来
```

`CMakeLists.txt` —— 本章**一次写全**，但把还没创建的源文件注释掉，后面逐步放开：

```cmake
cmake_minimum_required(VERSION 3.21)

# ---- 工具链：用 CLion 自带的 MinGW（按你机器实际路径改）----
set(CLION_MINGW_ROOT "E:/Program Files/JetBrains/CLion 2026.2.4/bin/mingw"
    CACHE PATH "CLion 自带 MinGW 根目录")
if(NOT EXISTS "${CLION_MINGW_ROOT}/bin/gcc.exe")
    file(GLOB _roots "E:/Program Files/JetBrains/CLion*/bin/mingw")
    list(SORT _roots)
    list(GET _roots -1 _latest)
    set(CLION_MINGW_ROOT "${_latest}" CACHE PATH "" FORCE)
endif()
set(CMAKE_C_COMPILER   "${CLION_MINGW_ROOT}/bin/gcc.exe" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${CLION_MINGW_ROOT}/bin/g++.exe" CACHE FILEPATH "" FORCE)

project(DotUI VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)   # 默认 Debug 方便断点
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- u8g2 核心 + SDL 后端 + 中文字体 ----
set(U8G2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/u8g2")
file(GLOB U8G2_CORE_SRC CONFIGURE_DEPENDS "${U8G2_DIR}/csrc/*.c")
add_library(u8g2 STATIC
    ${U8G2_CORE_SRC}
    "${U8G2_DIR}/sys/sdl/common/u8x8_d_sdl_128x64.c"     # SDL 显示驱动
    "${U8G2_DIR}/sys/sdl/common/u8x8_sdl_key.c"          # SDL 按键
    "${U8G2_DIR}/tools/font/build/single_font_files/u8g2_font_wqy12_t_gb2312.c")
set_source_files_properties(
    "${U8G2_DIR}/tools/font/build/single_font_files/u8g2_font_wqy12_t_gb2312.c"
    PROPERTIES COMPILE_DEFINITIONS "U8G2_USE_LARGE_FONTS"
               COMPILE_OPTIONS "-include;${U8G2_DIR}/csrc/u8g2.h")
target_include_directories(u8g2 PUBLIC "${U8G2_DIR}/csrc")

# ---- SDL2（VC 版开发包，路径按你机器改）----
set(SDL2_DIR "E:/Software/InstallPackage/SDL2-devel-2.32.10-VC/SDL2-2.32.10/cmake"
    CACHE PATH "VC 版 SDL2 的 CMake 配置目录")
find_package(SDL2 CONFIG REQUIRED)
target_link_libraries(u8g2 PUBLIC SDL2::SDL2)

# ---- MonoUI 框架（C++17）----
# ★ 每完成一章，放开对应行
add_library(mono_ui STATIC
    src/mono_ui.cpp
    src/mono_page.cpp
    src/mono_item.cpp
    src/mono_input.cpp
    src/resources.cpp
    src/mono_user_app.cpp)
target_include_directories(mono_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/inc")
target_link_libraries(mono_ui PUBLIC u8g2)

add_executable(dotui_sim src/main.cpp)
target_link_libraries(dotui_sim PRIVATE mono_ui)

# ---- 复制运行期 DLL，双击即可运行 ----
if(MINGW)
    set(DLLS "$<TARGET_FILE:SDL2::SDL2>")
    get_filename_component(MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(d libwinpthread-1.dll libstdc++-6.dll libgcc_s_seh-1.dll)
        if(EXISTS "${MINGW_BIN}/${d}")
            list(APPEND DLLS "${MINGW_BIN}/${d}")
        endif()
    endforeach()
    add_custom_command(TARGET dotui_sim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DLLS}
                "$<TARGET_FILE_DIR:dotui_sim>")
endif()
```

本章验证前，先建一个最小的 `src/main.cpp`：

```cpp
int main() { return 0; }
```

然后把 CMake 里 `mono_ui` 整段（add_library 到 target_link_libraries）和 `dotui_sim` 的链接行**暂时注释掉**，只留：

```cmake
add_executable(dotui_sim src/main.cpp)
```

**✅ 验证 1**：`cmake -S . -B build -G Ninja && ninja -C build` 编译通过，`build/` 下出现 `dotui_sim.exe`。

> 排错：SDL2 找不到 → 检查 `SDL2_DIR` 是否指向含 `SDL2Config.cmake` 的目录；
> u8g2 编译报错 → 确认 `lib/u8g2/csrc/u8g2.h` 存在。

---

## 第 2 章 点亮屏幕：u8g2 + SDL

**本章目标**：弹出 SDL 窗口，用 u8g2 在帧缓冲上画文字和一个会动的方块。

u8g2 的工作方式：所有绘制都画在一块**内存帧缓冲**里，`u8g2_SendBuffer` 时才一次性送到"屏幕"（PC 上就是 SDL 窗口）。真机上换成 SPI 驱动即可，框架代码一行不改——这就是选 u8g2 的理由。

替换 `src/main.cpp`：

```cpp
// main.cpp —— DotUI 桌面模拟器入口
#define SDL_MAIN_HANDLED   // 不链接 SDL2main，自己接管入口

#include "u8g2.h"
#include <SDL.h>
#include <stdio.h>

// SDL 后端的显示初始化函数（不在 u8g2 公共头里声明，手动 extern）
extern "C" void u8g2_SetupBuffer_SDL_128x128(u8g2_t *u8g2, const u8g2_cb_t *u8g2_cb);
extern "C" void u8g2_SetupBuffer_SDL_144x168(u8g2_t *u8g2, const u8g2_cb_t *u8g2_cb);
extern "C" int  u8g_sdl_get_key(void);   // SDL 事件泵 + 按键轮询（无事件返回 -1）

u8g2_t g_u8g2;

int main(int argc, char *argv[]) {
    SDL_SetMainReady();

    // 命令行选分辨率：默认 128x128，带 144x168 参数则用大屏
    if (argc > 1 && strcmp(argv[1], "144x168") == 0)
        u8g2_SetupBuffer_SDL_144x168(&g_u8g2, &u8g2_cb_r0);
    else
        u8g2_SetupBuffer_SDL_128x128(&g_u8g2, &u8g2_cb_r0);

    u8x8_InitDisplay(u8g2_GetU8x8(&g_u8g2));   // 初始化"显示器"
    u8x8_SetPowerSave(u8g2_GetU8x8(&g_u8g2), 0); // 退出省电模式

    u8g2_SetFont(&g_u8g2, u8g2_font_6x12_tf);

    // ---- 主循环：~60 FPS ----
    while (1) {
        // 泵送 SDL 事件：排空队列，处理窗口系统消息。
        // 缺了这句窗口会"未响应"（点击卡死），SDL_GetKeyboardState 也不刷新。
        int dummy;
        while ((dummy = u8g_sdl_get_key()) >= 0) {}

        const uint32_t now = SDL_GetTicks();
        u8g2_ClearBuffer(&g_u8g2);                  // 1. 清帧缓冲

        u8g2_DrawUTF8(&g_u8g2, 4, 12, "Hello DotUI"); // 2. 画内容
        // 移动方块：x 随时间往复
        int x = (now / 10) % 200;  if (x > 100) x = 200 - x;
        u8g2_DrawBox(&g_u8g2, (int16_t)x, 60, 10, 10);
        u8g2_DrawFrame(&g_u8g2, 0, 0, 128, 128);

        u8g2_SendBuffer(&g_u8g2);                   // 3. 提交显示
        SDL_Delay(16);
    }
    return 0;
}
```

（需要 `#include <string.h>` 供 `strcmp` 使用。）

**✅ 验证 2**：运行后出现 128x128 的窗口，左上角显示 "Hello DotUI"，一个方块来回移动，边框完整。

> 概念卡：`u8g2_cb_r0` 是旋转 0° 的裁剪回调；`DrawBox` 是实心矩形，`DrawFrame` 是空心框。记住"Clear → 画 → Send"三段式，框架每帧都是这个结构。

---

## 第 3 章 基础件：uvector、MonoSpring、MonoMarquee

**本章目标**：写出 `inc/uvector.h` 和 `inc/mono_defs.h`（地基），并用弹簧重写方块移动，体会"弹簧动画"手感。

### 3.1 uvector —— 极简动态数组

嵌入式风格代码不用 `std::vector`（省体积、行为可控），我们只封装用得到的接口。新建 `inc/uvector.h`：

```cpp
// uvector.h —— 极简动态数组，替代 std::vector
#ifndef DOTUI_UVECTOR_H
#define DOTUI_UVECTOR_H

#include <stddef.h>
#include <stdlib.h>

template <typename T>
class uvector {
public:
    uvector() = default;
    ~uvector() { free(data_); }

    uvector(const uvector&) = delete;            // 指针语义数组，禁止浅拷贝
    uvector& operator=(const uvector&) = delete;

    size_t size()  const { return size_; }
    bool   empty() const { return size_ == 0; }

    T&       operator[](size_t i)       { return data_[i]; }
    const T& operator[](size_t i) const { return data_[i]; }

    void clear() { size_ = 0; }   // 只清长度不释放——页面池复用内存

    void push_back(const T& value) {
        if (size_ == cap_) {
            const size_t next = cap_ ? cap_ + cap_ / 2 : 4;  // 1.5 倍扩容
            T* p = static_cast<T*>(realloc(data_, next * sizeof(T)));
            if (!p) abort();                          // UI 初始化期失败即可视
            data_ = p;
            cap_  = next;
        }
        data_[size_++] = value;
    }

private:
    T*     data_ = nullptr;
    size_t size_ = 0;
    size_t cap_  = 0;
};

#endif
```

### 3.2 mono_defs.h —— 地基（一次写全，逐步用到）

新建 `inc/mono_defs.h`。它包含五部分：控件类型枚举、布局参数、动画常量、MonoSpring、MonoMarquee、工具函数。**本步只验证 MonoSpring**，其余后面章节自然会用到。

```cpp
// mono_defs.h —— MonoUI 公共定义
#ifndef MONO_DEFS_H
#define MONO_DEFS_H

#include <stdint.h>
#include <stddef.h>
#include <type_traits>

// 前置声明：各头文件互相只引用指针，避免循环包含
class MonoUI;
class MonoPage;
class MonoItem;
class MonoWidget;
class MonoInput;

// 通用回调：菜单项/弹窗关闭时由框架调用
typedef void (*MonoCallback)(MonoUI* ui, void* arg);

// 控件类型：决定列表项的绘制方式与按下行为
enum class ItemType : uint8_t {
    ITEM_CALL_PAGE     = 0,   // 进入子页面（param1 = MonoPage*，行首显示 "+"）
    ITEM_CALL_FUNCTION = 1,   // 触发回调（行首显示 "-"）
    ITEM_TEXT          = 2,   // 纯文本行
    ITEM_SWITCH        = 3,   // 开关：右侧显示 开/关
    ITEM_VALUE         = 4,   // 数值：OK 进入/退出编辑，左右键调值
    ITEM_TEXT_MARQUEE  = 5,   // 长文本：选中时跑马灯滚动
    ITEM_PROGRESS_BAR  = 6,   // 进度条
    ITEM_CHECKBOX      = 7,   // 勾选框
};

// ---------------------------------------------------------------------------
// 运行时布局参数：全部 inline 全局变量，默认按 128x128 初始化；
// MonoUI::Begin() 会用实际显示尺寸调 init() 重算——128x128 / 144x168 共用
// 同一套渲染代码的秘密就在这里。
// ---------------------------------------------------------------------------
namespace MonoLayout {
inline int kScreenW = 128;
inline int kScreenH = 128;
inline int kRowH    = 16;      // 列表行高（行数 = kScreenH / kRowH）
inline int kListTop = 0;       // 行数不整除时上下居中的留白
inline int kPadItem = 10;      // 列表正文左起点（前缀 +/- 之后）
inline int kPrefixX = 1;       // 前缀 +/- 的 x 起点

inline int kNavW       = 8;    // 右侧导航栏宽度
inline int kNavX       = 120;  // 导航栏左边界（= kScreenW - kNavW）
inline int kScrollBarX = 122;
inline int kScrollBarW = 4;

inline int kWidgetRight = 116; // 右侧控件右对齐到这里（导航栏左再留 4px）
inline int kMarqueeGate = 120; // 跑马灯触发门槛：文本超过此宽才滚动
inline int kSelectionOverhang = 3;
inline int kSelectionRadius   = 3;
inline int kBarW = 32, kBarH = 7;
inline int kStatusIconX = 110, kStatusIconS = 10;
inline int kIconCell = 56, kIconLogical = 14, kIconTop = 6;

inline void init(int width, int height) {
    kScreenW = width;
    kScreenH = height;
    kRowH    = 16;
    kPadItem = 10;
    kPrefixX = 1;
    kNavW       = 8;
    kNavX       = kScreenW - kNavW;
    kScrollBarX = kNavX + 2;
    kScrollBarW = 4;
    kWidgetRight = kNavX - 4;
    kMarqueeGate = kNavX;
    kSelectionOverhang = 3;
    kSelectionRadius   = 3;
    kBarW = 32; kBarH = 7;
    kStatusIconX = kNavX - 10;
    kStatusIconS = 10;
    kIconCell    = 56;
    kIconLogical = 14;
    kIconTop     = (kScreenH > 128) ? 24 : 6;
    // 168/16=10 行余 8px → 上下各留 4px；整除时留白为 0
    kListTop = (kScreenH - (kScreenH / kRowH) * kRowH) / 2;
}
} // namespace MonoLayout

// 动画与输入时间常量
namespace MonoAnim {
// 弹簧：x'' = ωn²·(target-x) - 2ζωn·x'
//   ωn 越大响应越快（稳定时间≈4/(ζ·ωn)），ζ=1 临界阻尼最快无过冲
constexpr float kSpringOmega   = 22.0f;
constexpr float kSpringZeta    = 1.5f;
constexpr float kSpringSnapPos = 0.001f;
constexpr float kSpringSnapVel = 0.05f;

constexpr uint32_t kMarqueeWaitMs = 1000;  // 跑马灯两端停留
constexpr uint32_t kMarqueeStepMs = 50;    // 滚动一步间隔
constexpr int      kMarqueeEndGap = 90;    // 滚出后尾部间隙

constexpr uint32_t kLongPressMs   = 500;   // 长按阈值
constexpr uint32_t kRepeatDelayMs = 400;   // 长按后开始连发的延迟
constexpr uint32_t kRepeatRateMs  = 80;    // 连发间隔
constexpr uint32_t kToastMs       = 1200;  // Toast 存活时间
} // namespace MonoAnim

// 主题：1-bit 屏只有前景/背景
struct MonoTheme {
    uint8_t fg = 1, bg = 0, accent = 1;
};

// ---------------------------------------------------------------------------
// MonoSpring —— 二阶弹簧阻尼动画（框架所有平滑运动的发动机）
// ---------------------------------------------------------------------------
struct MonoSpring {
    float pos = 0, vel = 0, target = 0;
    float omega = MonoAnim::kSpringOmega;
    float zeta  = MonoAnim::kSpringZeta;

    void reset(float v) { pos = target = v; vel = 0.0f; }  // 瞬移
    void setTarget(float t) { target = t; }                // 带动画逼近

    // 位置和速度都收敛才算到位（只看位置会在高速穿过目标时误判）
    bool settled() const {
        const float e = target - pos;
        return (e < MonoAnim::kSpringSnapPos && e > -MonoAnim::kSpringSnapPos &&
                vel < MonoAnim::kSpringSnapVel && vel > -MonoAnim::kSpringSnapVel);
    }

    // 推进 dt 秒：半隐式欧拉积分，大 dt 拆成 ≤1/240s 的子步保证稳定
    void step(float dt) {
        if (dt <= 0.0f) return;
        if (settled()) { pos = target; vel = 0; return; }

        const float kMaxSub = 1.0f / 240.0f;
        const int   n = (int)(dt / kMaxSub) + 1;
        const float h = dt / (float)n;
        const float w2 = omega * omega;
        const float c  = 2.0f * zeta * omega;

        for (int i = 0; i < n; ++i) {
            vel += (w2 * (target - pos) - c * vel) * h;  // 先速度
            pos += vel * h;                              // 后位置（半隐式）
        }
        if (settled()) { pos = target; vel = 0.0f; }     // 消除亚像素残差
    }
};

// ---------------------------------------------------------------------------
// MonoMarquee —— 跑马灯四态机：WAIT→SLIDE→HOLD→BACK→WAIT
// ---------------------------------------------------------------------------
class MonoMarquee {
public:
    enum State : uint8_t { ST_WAIT = 0, ST_SLIDE = 1, ST_HOLD = 2, ST_BACK = 3 };

    void reset() { state_ = ST_WAIT; offset_ = 0; stamp_ = 0; }

    // 返回当前水平偏移（≤0，绘制时加到文本 x 上）
    int16_t step(int textWidth, uint32_t nowMs) {
        switch (state_) {
        case ST_WAIT:
            if (nowMs - stamp_ > MonoAnim::kMarqueeWaitMs)
                { state_ = ST_SLIDE; stamp_ = nowMs; }
            break;
        case ST_SLIDE:
            if (nowMs - stamp_ > MonoAnim::kMarqueeStepMs) {
                --offset_;
                if (offset_ <= MonoAnim::kMarqueeEndGap - textWidth)
                    state_ = ST_HOLD;
                stamp_ = nowMs;
            }
            break;
        case ST_HOLD:
            if (nowMs - stamp_ > MonoAnim::kMarqueeWaitMs)
                { state_ = ST_BACK; stamp_ = nowMs; }
            break;
        case ST_BACK:
            if (nowMs - stamp_ > MonoAnim::kMarqueeStepMs) {
                ++offset_;
                if (offset_ >= 0) { offset_ = 0; state_ = ST_WAIT; }
                stamp_ = nowMs;
            }
            break;
        }
        return offset_;
    }

private:
    State    state_  = ST_WAIT;
    int16_t  offset_ = 0;
    uint32_t stamp_  = 0;
};

// ---------------------------------------------------------------------------
// 工具函数
// ---------------------------------------------------------------------------
// 类型擦除：AddItem 的模板参数 → uintptr_t，只接受指针/整数/布尔/nullptr
template <typename T>
constexpr uintptr_t monoToUptr(T v) noexcept
{
    if constexpr (std::is_pointer_v<T>)      return reinterpret_cast<uintptr_t>(v);
    else if constexpr (std::is_same_v<T, std::nullptr_t>) return 0u;
    else if constexpr (std::is_same_v<T, bool>) return v ? 1u : 0u;
    else if constexpr (std::is_enum_v<T>)
        return static_cast<uintptr_t>(static_cast<intptr_t>(v));
    else {
        static_assert(std::is_integral_v<T>,
                      "AddItem 的 param 只接受指针 / 整数 / 布尔 / nullptr");
        return static_cast<uintptr_t>(static_cast<intptr_t>(v));
    }
}

// uintptr_t → 指针（与 monoToUptr 配对）
template <typename T>
inline T monoFromUptr(uintptr_t v) { return reinterpret_cast<T>(v); }

inline float monoClampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
inline int monoClampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

#endif
```

### 3.3 用弹簧重写方块

修改 main.cpp 的主循环（`#include "mono_defs.h"`）：

```cpp
MonoSpring sq;          // 方块的 x 坐标弹簧
sq.reset(10);

while (1) {
  // 泵送 SDL 事件：排空队列，处理窗口系统消息。
    // 缺了这句窗口会"未响应"，且 SDL_GetKeyboardState 不刷新导致按键无效。
    int dummy;
    while ((dummy = u8g_sdl_get_key()) >= 0) {}
    const uint32_t now = SDL_GetTicks();
    static uint32_t last = now;
    float dt = (now - last) * 0.001f;   // 帧间隔（秒）
    if (dt <= 0) dt = 0.001f;
    last = now;

    // ← → 键（SDL 扫描码 276/275）改目标位置
    int n; const Uint8* keys = SDL_GetKeyboardState(&n);
    if (keys[SDL_SCANCODE_LEFT])  sq.setTarget(sq.target - 60 * dt);
    if (keys[SDL_SCANCODE_RIGHT]) sq.setTarget(sq.target + 60 * dt);
    sq.target = monoClampf(sq.target, 0, 118);

    sq.step(dt);

    u8g2_ClearBuffer(&g_u8g2);
    u8g2_DrawBox(&g_u8g2, (int16_t)sq.pos, 60, 10, 10);
    u8g2_SendBuffer(&g_u8g2);
    SDL_Delay(16);
}
```

**✅ 验证 3**：方块跟手移动且有"滑行惯性感"。做三个实验感受参wo 数：

1. 把 `kSpringZeta` 改成 `0.6f` → 方块会**过冲回弹**（欠阻尼）；
2. 改回 `1.0f` → 最快且无过冲（临界阻尼）；
3. 把 `kSpringOmega` 改成 `8.0f` → 明显变"软、慢"。

> 概念卡：为什么不用 lerp（`x += (t-x)*0.1`）？弹簧是二阶系统，有速度记忆，停止时自然减速；还能做"到达后回弹"等表现。半隐式欧拉（先更新速度再更新位置）比显式欧拉能量稳定，不会越振越大。

---

## 第 4 章 输入层 MonoInput

**本章目标**：写出按键去抖/长按/连发系统，屏上显示收到的事件。

框架不直接碰 SDL——移植层只负责报告"某键现在按着没"（电平），框架负责把电平变成**事件**。这样真机上把 `SetKey` 接到 GPIO 扫描即可。

新建 `inc/mono_input.h`：

```cpp
// mono_input.h —— 按键输入
#ifndef MONO_INPUT_H
#define MONO_INPUT_H

#include "mono_defs.h"

class MonoInput {
public:
    enum Key : uint8_t {
        KEY_NONE = 0, KEY_UP = 1, KEY_DOWN = 2,
        KEY_LEFT = 3, KEY_RIGHT = 4, KEY_OK = 5, KEY_BACK = 6,
        KEY_COUNT = 7,
    };
    enum Event : uint8_t {
        EV_NONE = 0,
        EV_CLICK  = 1,  // 按下后在长按阈值内松开
        EV_LONG   = 2,  // 按住超过 kLongPressMs（每按一次只触发一回）
        EV_REPEAT = 3,  // 长按后周期连发
    };

    void Begin();                                  // 清空状态与队列
    void SetKey(Key k, bool down, uint32_t now);   // 移植层报告电平
    void Update(uint32_t now);                     // 每帧调用：判长按/连发
    bool Pop(Event& ev, Key& k);                   // 取一个事件，无则 false
    bool IsDown(Key k) const;

private:
    struct KeyState {
        bool     down = false;
        bool     longFired = false;   // 本轮按住是否已触发过 EV_LONG
        uint32_t downAt = 0, lastRep = 0, changedAt = 0;
    };
    struct Slot { Event ev; Key k; };

    void push(Event ev, Key k);    // 入队（队满丢弃）

    static constexpr int kQueueLen = 8;
    KeyState st_[KEY_COUNT];
    Slot     q_[kQueueLen];
    uint8_t  head_ = 0, tail_ = 0;
};

#endif
```

新建 `src/mono_input.cpp`：

```cpp
// mono_input.cpp —— 按键去抖与事件判定
#include "mono_input.h"

namespace { constexpr uint32_t kDebounceMs = 8; }

void MonoInput::Begin() {
    for (int i = 0; i < KEY_COUNT; ++i) st_[i] = KeyState();
    head_ = tail_ = 0;
}

// 电平变化入口：去抖通过后记录。松开且未长按过 → 一次 EV_CLICK
void MonoInput::SetKey(Key k, bool down, uint32_t now) {
    if (k <= KEY_NONE || k >= KEY_COUNT) return;

    KeyState& s = st_[k];
    if (down == s.down) return;                                    // 电平未变
    if (s.changedAt != 0 && (now - s.changedAt) < kDebounceMs) return; // 抖动

    s.changedAt = now;
    s.down      = down;

    if (down) {
        s.downAt    = now;
        s.longFired = false;
        s.lastRep   = now;
    } else {
        if (!s.longFired) push(EV_CLICK, k);   // 短按在松开时确认
    }
}

// 每帧扫描按住的键：先判长按，之后按周期连发
void MonoInput::Update(uint32_t now) {
    for (int i = KEY_UP; i < KEY_COUNT; ++i) {
        KeyState& s = st_[i];
        if (!s.down) continue;

        const uint32_t held = now - s.downAt;

        if (!s.longFired) {
            if (held >= MonoAnim::kLongPressMs) {
                s.longFired = true;
                s.lastRep   = now;
                push(EV_LONG, (Key)i);
            }
            continue;
        }
        if (held >= MonoAnim::kRepeatDelayMs &&
            (now - s.lastRep) >= MonoAnim::kRepeatRateMs) {
            s.lastRep = now;
            push(EV_REPEAT, (Key)i);
        }
    }
}

void MonoInput::push(Event ev, Key k) {
    const uint8_t next = (uint8_t)((tail_ + 1) % kQueueLen);
    if (next == head_) return;          // 队满丢弃
    q_[tail_].ev = ev;
    q_[tail_].k  = k;
    tail_ = next;
}

bool MonoInput::Pop(Event& ev, Key& k) {
    if (head_ == tail_) return false;
    ev = q_[head_].ev;
    k  = q_[head_].k;
    head_ = (uint8_t)((head_ + 1) % kQueueLen);
    return true;
}

bool MonoInput::IsDown(Key k) const {
    if (k <= KEY_NONE || k >= KEY_COUNT) return false;
    return st_[k].down;
}
```

接进 main.cpp。SDL 后端只告诉我们"按了哪个键"，**没有松开事件**，所以用"虚拟电平"技巧：按下时记一个 60ms 后的截止时刻，期间保持 `SetKey(k, true)`，到期自动"松开"。按住不放时 SDL 的按键重复会不断刷新这个时刻，等效于"一直按着"。

```cpp
#include "mono_input.h"
extern "C" int u8g_sdl_get_key(void);   // 从 SDL 后端取一个按键码，无则 -1

MonoInput g_input;
uint32_t  g_pressUntil[MonoInput::KEY_COUNT] = {0};

// SDL 键码 → MonoInput 键
void queueKey(int key, uint32_t now) {
    MonoInput::Key k = MonoInput::KEY_NONE;
    switch (key) {
    case 273: k = MonoInput::KEY_UP;    break;
    case 274: k = MonoInput::KEY_DOWN;  break;
    case 276: k = MonoInput::KEY_LEFT;  break;
    case 275: k = MonoInput::KEY_RIGHT; break;
    case 's': case ' ': k = MonoInput::KEY_OK;   break;
    case 'b': case 'q': k = MonoInput::KEY_BACK; break;
    default: return;
    }
    g_pressUntil[k] = now + 60;    // 维持 60ms 电平 → 产生一次 CLICK
}
```

主循环里（画方块之前）：

```cpp
    // 1. 取空 SDL 按键队列
    int key;
    while ((key = u8g_sdl_get_key()) >= 0)
        if (key != 0) queueKey(key, now);

    // 2. 维持虚拟电平：到期自动松开
    for (int i = MonoInput::KEY_UP; i < MonoInput::KEY_COUNT; ++i)
        g_input.SetKey((MonoInput::Key)i, now < g_pressUntil[i], now);

    g_input.Update(now);

    // 3. 消费事件并显示
    MonoInput::Event ev; MonoInput::Key k;
    static char lines[8][32]; static int li = 0;
    while (g_input.Pop(ev, k)) {
        const char* es = ev == MonoInput::EV_CLICK ? "CLICK" :
                         ev == MonoInput::EV_LONG  ? "LONG"  : "REPEAT";
        snprintf(lines[li % 8], 32, "%s key=%d", es, (int)k);
        ++li;
    }
    // 画最近 8 条事件
    u8g2_SetFont(&g_u8g2, u8g2_font_6x12_tf);
    for (int i = 0; i < 8; ++i)
        u8g2_DrawUTF8(&g_u8g2, 2, 12 + i * 12, lines[(li - 8 + i + 64) % 8 + 64 >= 64 ? (li - 8 + i + 64) % 8 : 0]);
```

（上面最后一行的环形取数有点绕，直接换成更朴素的写法：`for (int i = 0; i < 8; ++i) { int idx = li - 8 + i; if (idx < 0) continue; u8g2_DrawUTF8(&g_u8g2, 2, 12 + i * 12, lines[idx % 8]); }`）

**✅ 验证 4**：

- 短按 ↑ → 屏上出现 `CLICK key=1`；
- 按住 ↓ 不放 → 先出一条 `LONG key=2`，随后每 80ms 刷一条 `REPEAT key=2`；
- 快速抖动按键不会多出事件（去抖生效）。

> 概念卡：为什么 CLICK 在**松开**时才确认？因为按下瞬间还不知道这是短按还是长按。事件用环形队列（head/tail 指针）缓冲，帧率不稳时也不丢。

---

---

## 第 5 章 资源层 resources

**本章目标**：字体指针、图标、文本常量集中到一个命名空间，中文正常显示。

框架不写死字体和文案，全部从 `MonoRes` 命名空间取——换语言、换屏字体只改这一处。

新建 `inc/resources.h`：

```cpp
// resources.h —— 字体与图标资源
#ifndef MONO_RESOURCES_H
#define MONO_RESOURCES_H

#include <stdint.h>

namespace MonoRes {

extern const uint8_t* const kFontSmall;     // 列表正文 6x12
extern const uint8_t* const kFontBig;       // 大字 7x13B
extern const uint8_t* const kFontChinese;   // 中文 wqy12（仅 PC 用，约 202KB）

extern int kLanguage;                       // 0 = English，1 = 中文

// 图标：14x14，1bpp，首字节为边长，位 1 = 透明（固件格式）
extern const uint8_t kIcon0[];
extern const uint8_t kIcon1[];
extern const uint8_t kIcon2[];
extern const uint8_t kIcon3[];
extern const uint8_t kIcon4[];
extern const uint8_t kIcon5[];
extern const uint8_t kIcon6[];
extern const uint8_t kIcon7[];
extern const uint8_t* const kIcons[8];
constexpr int kIconCount = 8;
constexpr int kIconLogicalSize = 14;

extern const char* const kPrefixPage;       // 子页面前缀 "+"
extern const char* const kPrefixFunction;   // 动作项前缀 "-"
extern const char* kTextOn;                 // 可写指针：语言切换时改指向
extern const char* kTextOff;
extern const char* kTextOk;
extern const char* const kFormatValue;      // "%.2f"

} // namespace MonoRes
#endif
```

新建 `src/resources.cpp`。**图标数组是纯数据（每个 32 字节），直接从本仓库 `src/resources.cpp` 复制，不用手敲**。骨架如下：

```cpp
// resources.cpp —— 字体与图标资源
#include "resources.h"
#include "u8g2.h"

namespace MonoRes {

const uint8_t kIcon0[32] = { /* 从仓库复制 */ };
// ... kIcon1..kIcon7 同样从仓库复制 ...

const uint8_t* const kIcons[8] = {
    kIcon0, kIcon1, kIcon2, kIcon3, kIcon4, kIcon5, kIcon6, kIcon7
};

const uint8_t* const kFontSmall   = u8g2_font_6x12_tf;
const uint8_t* const kFontBig     = u8g2_font_7x13B_tr;
const uint8_t* const kFontChinese = u8g2_font_wqy12_t_gb2312;

int kLanguage = 1;                 // 默认中文

const char* const kPrefixPage     = "+";
const char* const kPrefixFunction = "-";
const char* kTextOn               = "ON";
const char* kTextOff              = "OFF";
const char* kTextOk               = "OK";
const char* const kFormatValue    = "%.2f";

} // namespace MonoRes
```

**✅ 验证 5**：main.cpp 里用 `u8g2_SetFont(&g_u8g2, MonoRes::kFontChinese)` + `u8g2_DrawUTF8` 画一行 `"中文显示 OK"`，字体切换编译通过、中文不乱码（**文件必须 UTF-8 编码保存**）。

> 概念卡：`kTextOn/kTextOff/kTextOk` 是**可写指针**——切换语言时应用层改它们的指向（"ON"→"开"），所有引用处自动生效，无需通知任何人。这是 C 里最常见的"全局配置"模式。

---

## 第 6 章 数据模型：MonoItem 与 MonoPage 页面池

**本章目标**：菜单数据结构成形——"一行"和"页面池"，并在屏上手动画出注册的菜单。

### 6.1 设计决策：为什么用"页面池"而不是页面树？

- 所有页面在 `InitLayout()` 里**静态注册**（上限 16 个），运行期只做切换，不创建不销毁 → 无碎片、无悬空指针，MCU 友好；
- 页面间的层级（子页记住父页）通过 `ITEM_CALL_PAGE` 项**单向回填** `backPage` 索引实现，BACK 时照着返回。

### 6.2 MonoItem —— 菜单里的一行

新建 `inc/mono_item.h`：

```cpp
// mono_item.h —— 列表项
#ifndef MONO_ITEM_H
#define MONO_ITEM_H

#include "mono_defs.h"
#include <string.h>

class MonoItem {
public:
    // ---- 基本属性（由 MonoPage::AddItemRaw 初始化）----
    ItemType       type       = ItemType::ITEM_TEXT;
    const char*    text       = "";      // 零拷贝指针
    const uint8_t* iconSrc    = nullptr; // 图标（图标页用）
    const uint8_t* statusIcon = nullptr; // 行尾状态位图（可选）
    MonoCallback   callback   = nullptr;
    uintptr_t      param1     = 0;       // 类型相关参数（见 AddItemRaw 注释）
    uintptr_t      param2     = 0;

    // 数值类控件（ITEM_VALUE / ITEM_PROGRESS_BAR）
    float value = 0.0f, valueMin = 0.0f, valueMax = 100.0f;

    // 状态位：bit0 = 开/勾选，bit1 = 数值编辑中
    uint8_t state = 0;

    char textBuf[48]  = {0};   // SetTextCopy 的目标缓冲
    char valueBuf[16] = {0};   // 每帧格式化数值用（避免临时分配）

    MonoMarquee marquee;       // 跑马灯状态机

    bool IsOn()  const { return (state & 0x01) != 0; }
    void SetOn(bool v) { if (v) state |= 0x01; else state &= (uint8_t)~0x01; }
    void Toggle()      { state ^= 0x01; }

    bool IsEditing() const  { return (state & 0x02) != 0; }
    void SetEditing(bool v) { if (v) state |= 0x02; else state &= (uint8_t)~0x02; }

    // ---- 文本：零拷贝 vs 拷贝 ----
    void SetText(const char* src) { text = src ? src : ""; }
    void SetTextCopy(const char* src) {
        if (!src) src = "";
        strncpy(textBuf, src, sizeof(textBuf) - 1);
        textBuf[sizeof(textBuf) - 1] = '\0';
        text = textBuf;
    }
    const char* GetText() const { return text ? text : ""; }

    // ---- 数值 ----
    void SetRange(float lo, float hi) { valueMin = lo; valueMax = hi; }
    void SetValue(float v)            { value = monoClampf(v, valueMin, valueMax); }
    // 左右键调值：步长 = 量程 / 100（任何量程 100 步走完）
    void Adjust(int dir) {
        const float span = valueMax - valueMin;
        const float step = span > 0.0f ? span / 100.0f : 1.0f;
        SetValue(value + dir * step);
    }

    MonoPage* TargetPage() const;                            // 跳转目标页
    void*     Context() const { return monoFromUptr<void*>(param2); }

    bool Activate(MonoUI* ui);   // 按下处理（实现在 mono_item.cpp）
};

#endif
```

> param1/param2 用 `uintptr_t` 做**类型擦除**：跳页时装 `MonoPage*`，开关时装初始状态，数值时装量程——一个字段通吃，靠 `ItemType` 解释。`monoToUptr` 在编译期拒绝浮点等不支持的类型。

新建 `src/mono_item.cpp`：

```cpp
// mono_item.cpp —— MonoItem 的按下行为
#include "mono_item.h"
#include "mono_ui.h"

MonoPage* MonoItem::TargetPage() const {
    return monoFromUptr<MonoPage*>(param1);
}

// 按下处理：根据控件类型分派动作
bool MonoItem::Activate(MonoUI* ui) {
    if (!ui) return false;

    switch (type) {
    case ItemType::ITEM_CALL_PAGE: {
        MonoPage* tp = TargetPage();
        if (tp) {
            const int t = (int)(intptr_t)param2;   // >0 时定位到目标页第 t 项
            ui->GotoPage(tp, t > 0 ? t : -1);
        }
        return true;
    }
    case ItemType::ITEM_CALL_FUNCTION:
    case ItemType::ITEM_TEXT_MARQUEE:
        if (callback) callback(ui, Context());
        return true;

    case ItemType::ITEM_SWITCH:
    case ItemType::ITEM_CHECKBOX:
        Toggle();
        if (callback) callback(ui, Context());
        return true;

    case ItemType::ITEM_VALUE:
        SetEditing(!IsEditing());   // OK 进入/退出编辑
        return true;

    case ItemType::ITEM_PROGRESS_BAR:
        return true;                // 按下无动作（左右键直接调值）

    default:
        return false;
    }
}
```

### 6.3 MonoPage —— 页面池

新建 `inc/mono_page.h`：

```cpp
// mono_page.h —— 页面与页面池
#ifndef MONO_PAGE_H
#define MONO_PAGE_H

#include "mono_defs.h"
#include "mono_item.h"
#include "uvector.h"

class MonoPage {
public:
    enum Mode : uint8_t {
        LIST   = 0,   // 垂直列表
        ICON   = 1,   // 图标横向轮播
        CUSTOM = 2,   // 完全交给 drawCustom 绘制
    };

    static constexpr int kMaxPages = 16;

    // ---- 页面池管理 ----
    static MonoPage* AddPage(Mode mode, const char* name);
    static void      ResetPages();
    static int       PageCount();
    static MonoPage* GetPage(int index);      // 越界返回 nullptr

    // ---- 添加列表项：param1/param2 做类型擦除 ----
    template <typename P1, typename P2>
    MonoItem* AddItem(ItemType type, const char* description,
                      const uint8_t* iconSrc, P1 param1, P2 param2,
                      MonoCallback callback) {
        return AddItemRaw(type, description, iconSrc,
                          monoToUptr(param1), monoToUptr(param2), callback);
    }
    MonoItem* AddItemRaw(ItemType type, const char* description,
                         const uint8_t* iconSrc, uintptr_t param1,
                         uintptr_t param2, MonoCallback callback);

    Mode        GetMode() const { return mode_; }
    const char* Name()    const { return name_ ? name_ : ""; }
    int         Index()   const { return index_; }

    int       ItemCount() const { return (int)items_.size(); }
    MonoItem* Item(int i) const {
        if (i < 0 || i >= (int)items_.size()) return nullptr;
        return items_[i];
    }
    int IndexOf(const MonoItem* it) const;

    // ---- 导航状态（MonoUI 读写）----
    float savedFocus = 0.0f;   // 离开页面时的焦点（返回时恢复）
    int   backPage   = -1;     // 父页索引，-1 表示回主页

    // ---- 自定义绘制 ----
    typedef void (*DrawFn)(MonoUI& ui, MonoPage& page);
    DrawFn drawCustom = nullptr;

private:
    MonoPage() {}
    ~MonoPage() {}
    MonoPage(const MonoPage&) = delete;
    MonoPage& operator=(const MonoPage&) = delete;

    Mode        mode_  = LIST;
    const char* name_  = "";
    int         index_ = -1;
    uvector<MonoItem*> items_;

    static MonoPage s_pool_[kMaxPages];
    static int      s_count_;
};

#endif
```

新建 `src/mono_page.cpp`：

```cpp
// mono_page.cpp —— 页面池与列表项注册
#include "mono_page.h"
#include "mono_ui.h"

MonoPage MonoPage::s_pool_[MonoPage::kMaxPages];
int      MonoPage::s_count_ = 0;
constexpr int MonoPage::kMaxPages;

MonoPage* MonoPage::AddPage(Mode mode, const char* name) {
    if (s_count_ >= kMaxPages) return nullptr;

    MonoPage* p = &s_pool_[s_count_];
    p->mode_  = mode;
    p->name_  = name ? name : "";
    p->index_ = s_count_;
    p->savedFocus = 0.0f;
    p->backPage   = -1;
    p->drawCustom = nullptr;
    p->items_.clear();     // 只清长度，复用已分配内存

    ++s_count_;
    return p;
}

void MonoPage::ResetPages() {
    for (int i = 0; i < s_count_; ++i) {
        MonoPage& p = s_pool_[i];
        for (size_t k = 0; k < p.items_.size(); ++k)
            delete p.items_[k];      // 列表项是堆分配的，要回收
        p.items_.clear();
        p.savedFocus = 0.0f;
        p.backPage   = -1;
        p.drawCustom = nullptr;
    }
    s_count_ = 0;
}

int MonoPage::PageCount() { return s_count_; }

MonoPage* MonoPage::GetPage(int index) {
    if (index < 0 || index >= s_count_) return nullptr;
    return &s_pool_[index];
}

// 注册列表项：按类型解释 param1/param2
//   ITEM_CALL_PAGE     param1 = MonoPage*（自动回填其 backPage）
//   ITEM_SWITCH/CHECK  param1 = 初始开/关（非 0 即开）
//   ITEM_VALUE         param1 = 最小值，param2 = 最大值（0 视为 100）
//   ITEM_PROGRESS_BAR  param1 = 初始进度（0~100）
MonoItem* MonoPage::AddItemRaw(ItemType type, const char* description,
                               const uint8_t* iconSrc, uintptr_t param1,
                               uintptr_t param2, MonoCallback callback) {
    MonoItem* it = new MonoItem();
    if (!it) return nullptr;

    it->type     = type;
    it->iconSrc  = iconSrc;
    it->param1   = param1;
    it->param2   = param2;
    it->callback = callback;
    it->SetText(description);

    switch (type) {
    case ItemType::ITEM_CALL_PAGE: {
        MonoPage* target = monoFromUptr<MonoPage*>(param1);
        if (target) target->backPage = index_;   // 子页记住父页
        break;
    }
    case ItemType::ITEM_SWITCH:
    case ItemType::ITEM_CHECKBOX:
        it->SetOn(param1 != 0);
        break;
    case ItemType::ITEM_VALUE:
        it->valueMin = (float)(intptr_t)param1;
        it->valueMax = param2 ? (float)(intptr_t)param2 : 100.0f;
        it->value    = it->valueMin;
        break;
    case ItemType::ITEM_PROGRESS_BAR:
        it->valueMin = 0.0f;
        it->valueMax = 100.0f;
        it->value    = monoClampf((float)(intptr_t)param1, 0.0f, 100.0f);
        break;
    default:
        break;
    }

    items_.push_back(it);
    return it;
}

int MonoPage::IndexOf(const MonoItem* it) const {
    if (!it) return -1;
    for (size_t i = 0; i < items_.size(); ++i)
        if (items_[i] == it) return (int)i;
    return -1;
}
```

**✅ 验证 6**：在 main.cpp 里加一段临时测试代码（渲染菜单文本，焦点行画个方框）：

```cpp
#include "mono_page.h"
// CMake 里本步只放开 mono_page.cpp 一行（mono_item.cpp 见下方警示）

// 临时测试：注册页面并手动画出文本
MonoPage* home = MonoPage::AddPage(MonoPage::LIST, "Home");
MonoPage* sub  = MonoPage::AddPage(MonoPage::LIST, "Sub");
home->AddItem(ItemType::ITEM_CALL_PAGE,  "Enter Sub", nullptr, sub, 0, nullptr);
home->AddItem(ItemType::ITEM_TEXT,       "Plain Row", nullptr, 0, 0, nullptr);
sub ->AddItem(ItemType::ITEM_CALL_FUNCTION, "Do It",  nullptr, 0, 0, nullptr);

// 主循环里（清屏后）：
u8g2_SetFont(&g_u8g2, MonoRes::kFontSmall);
for (int i = 0; i < home->ItemCount(); ++i) {
    const int y = i * 16;
    u8g2_DrawUTF8(&g_u8g2, 10, y + 12, home->Item(i)->GetText());
}
u8g2_DrawFrame(&g_u8g2, 0, 0, 120, 16);   // 手画的"焦点框"
```

> ⚠️ **本步不要把 mono_item.cpp 放进构建**：它的 `Activate()` 调用了
> `ui->GotoPage()` 并 `#include "mono_ui.h"`，两者都是第 7 章的产出，
> 提前链接会报 `undefined reference to MonoUI::GotoPage`。
> 验证 6 只需要 mono_page.cpp——AddItem/ItemCount/GetText 都不依赖 MonoUI。
> 第 7 章写完 mono_ui.cpp 后再把 mono_item.cpp 加回构建。

运行应看到两行菜单文字和一个框。编译通过即过关——交互留给下一章。

---

## 第 7 章 MonoUI 最小核心：生命周期 + 列表 + 键盘导航

**本章目标**：框架心脏开始跳动——↑↓ 选行（瞬间跳）、OK 进子页、BACK 返回。还没有动画，选中框是普通方框。

### 7.1 mono_ui.h（一次写全，★ 标注后续章节才会用到的成员）

新建 `inc/mono_ui.h`：

```cpp
// mono_ui.h —— MonoUI 核心
#ifndef MONO_UI_H
#define MONO_UI_H

#include "mono_defs.h"
#include "mono_page.h"
#include "mono_item.h"
#include "mono_input.h"
#include "u8g2.h"

class MonoUI {
public:
    static constexpr int kMaxOverlays = 4;   // 覆盖层栈深度（STEP 12 用）

    explicit MonoUI(u8g2_t* g2, MonoInput* input);

    // ---- 毫秒时钟：移植层注入（MCU 上接 HAL_GetTick 之类）----
    typedef uint32_t (*MillisFn)(void);
    static void SetMillisSource(MillisFn fn);

    // ---- 单例：控件池/回调需要全局访问 ----
    static MonoUI& Instance();
    static void    SetInstance(MonoUI* ui);

    // ---- 生命周期 ----
    void Begin();       // 初始化显示、布局、首页与输入
    void Frame();       // 渲染一帧（清屏→页面→覆盖层→提交）
    void Tick(uint32_t now);  // 消费输入事件（每帧调用）
    void Run();         // MCU 一体式主循环（PC 上不用）

    // ---- 应用层钩子：由应用文件实现 ----
    void InitLayout();
    void HomeScreen(void* arg = nullptr);
    void SaveConfig(void* arg = nullptr);

    // ---- 页面导航 ----
    void GotoPage(MonoPage* page, int item = -1);
    void GotoPage(int index, int item = -1);
    void Back();

    // ---- 焦点 ----
    void SetFocus(int index);
    void MoveFocus(int delta);
    void Press();
    void AdjustCurrent(int dir);

    MonoPage* CurrentPage();
    MonoItem* CurrentItem();
    int FocusIndex()  const;    // 焦点弹簧当前位置（渲染用，向下取整）
    int TargetIndex() const;    // 焦点目标索引（逻辑判定用，四舍五入）

    // ---- 覆盖层（STEP 12）----
    void        PushOverlay(MonoWidget* w);
    void        PopOverlay();
    MonoWidget* TopOverlay();
    bool        HasOverlay() const { return overlayDepth_ > 0; }

    // ---- 访问器 ----
    u8g2_t*    G2()      const { return g2_; }
    MonoInput& Input()         { return *in_; }
    MonoTheme& Theme()         { return theme_; }
    uint32_t   Now()     const { return now_; }
    uint32_t   Uptime()  const { return uptime_; }
    uint8_t    Fps()     const { return fps_; }

    void SetFonts(const uint8_t* listFont, const uint8_t* bigFont);

    // ---- 文本工具（自动绑定列表字体，UTF-8 安全）----
    int  TextWidth(const char* s);
    int  FontAscent();
    void DrawText(int x, int baselineY, const char* s);
    void DrawTextCenter(int cx, int baselineY, const char* s);
    void DrawTextRight(int rightX, int baselineY, const char* s);
    void DrawTextClipped(int x, int baselineY, const char* s, int maxX);

    // ---- 几何工具 ----
    void FillBox   (int x, int y, int w, int h);
    void FillRound (int x, int y, int w, int h, int r);       // STEP 9
    void FrameBox  (int x, int y, int w, int h);
    void FrameRound(int x, int y, int w, int h, int r);       // STEP 9
    void FillRoundOnce(int x, int y, int w, int h, int r);    // STEP 10
    void DrawIcon(const uint8_t* icon, int x, int y, int scale); // STEP 11

private:
    // ---- 渲染 ----
    void renderPage();
    void renderList(MonoPage& p);
    void renderIconPage(MonoPage& p);     // STEP 11
    void renderCustom(MonoPage& p);
    void drawRow(MonoItem& it, float rowF, bool selected, float slide);
    void selectionMetrics(MonoItem& it, int& x, int& w);      // STEP 10
    void syncSelectionTarget();                               // STEP 10
    void resetSelectionAnim();                                // STEP 10
    void drawScrollbar(int count, int rows, float maxScroll); // STEP 8
    void DrawRawBmp(const uint8_t* bmp, int x, int y, int w, int h); // STEP 9
    void drawOverlays();                  // STEP 12
    int  RowBaseline(int rowTop);         // 行内文本垂直居中基线

    // ---- 输入与行为 ----
    void handleEvent(MonoInput::Event ev, MonoInput::Key k);
    void switchItem(MonoItem& it);        // STEP 9
    void adjustItem(MonoItem& it, int dir); // STEP 9

    void applyPage(MonoPage* p, int item);

    // ---- 状态 ----
    u8g2_t*    g2_;
    MonoInput* in_;
    MonoTheme  theme_;

    int        curPage_ = -1;
    MonoSpring focusS_;    // 焦点行弹簧（单位：行）        ★ STEP 8 起生效
    MonoSpring scroll_;    // 滚动弹簧（单位：行）          ★ STEP 8 起生效
    MonoSpring selX_;      // 选中框 x 弹簧（像素）         ★ STEP 10 起生效
    MonoSpring selW_;      // 选中框宽度弹簧（像素）        ★ STEP 10 起生效
    float      slide_  = 1.0f;   // 页面进入动画进度 0→1      ★ STEP 8 起生效
    float      dt_     = 0.016f; // 上一帧间隔（秒）

    const uint8_t* fontList_ = nullptr;
    const uint8_t* fontBig_  = nullptr;

    uint32_t now_ = 0, uptime_ = 0, lastFrame_ = 0, frameCount_ = 0;
    uint8_t  fps_ = 0;

    MonoWidget* overlay_[kMaxOverlays] = {nullptr};   // ★ STEP 12
    int         overlayDepth_ = 0;

    static MonoUI*  s_inst_;
    static MillisFn s_millis_;
};

#endif
```

### 7.2 mono_ui.cpp —— 本步实现的部分

新建 `src/mono_ui.cpp`，先写生命周期、导航和最小渲染（**含 STEP 7 专用的"瞬时焦点"写法**）：

```cpp
// mono_ui.cpp —— MonoUI 核心实现
#include "mono_ui.h"
#include "resources.h"
#include <stdio.h>
#include <string.h>

MonoUI*          MonoUI::s_inst_   = nullptr;
MonoUI::MillisFn MonoUI::s_millis_ = nullptr;
constexpr int    MonoUI::kMaxOverlays;

MonoUI::MonoUI(u8g2_t* g2, MonoInput* input) : g2_(g2), in_(input) {}

MonoUI& MonoUI::Instance()       { return *s_inst_; }
void    MonoUI::SetInstance(MonoUI* ui)  { s_inst_ = ui; }
void    MonoUI::SetMillisSource(MillisFn fn) { s_millis_ = fn; }

void MonoUI::SetFonts(const uint8_t* listFont, const uint8_t* bigFont) {
    if (listFont) fontList_ = listFont;
    if (bigFont)  fontBig_  = bigFont;
}

// ============================ 1. 生命周期 ============================
void MonoUI::Begin() {
    if (!fontList_) fontList_ = MonoRes::kFontSmall;
    if (!fontBig_)  fontBig_  = MonoRes::kFontBig;

    if (g2_) {
        u8g2_InitDisplay(g2_);
        u8g2_SetPowerSave(g2_, 0);
        u8g2_ClearBuffer(g2_);
        // 用实际显示尺寸重算布局（128x128 / 144x168 共用一套逻辑）
        MonoLayout::init((int)u8g2_GetDisplayWidth(g2_),
                         (int)u8g2_GetDisplayHeight(g2_));
    }

    MonoPage::ResetPages();
    InitLayout();                       // 应用层注册页面

    if (MonoPage::PageCount() > 0)
        applyPage(MonoPage::GetPage(0), 0);

    if (in_) in_->Begin();
}

void MonoUI::Frame() {
    if (!g2_) return;

    // 帧间隔 dt（秒）：限幅 1~50ms，防除零 & 防挂起后大步长破坏积分
    if (lastFrame_ != 0 && now_ > lastFrame_) {
        float dtMs = (float)(now_ - lastFrame_);
        if (dtMs < 1.0f)  dtMs = 1.0f;
        if (dtMs > 50.0f) dtMs = 50.0f;
        dt_ = dtMs * 0.001f;
    }
    uptime_ = now_;

    u8g2_ClearBuffer(g2_);
    u8g2_SetDrawColor(g2_, theme_.fg);
    renderPage();
    u8g2_SendBuffer(g2_);

    if (lastFrame_ != 0 && now_ > lastFrame_) {
        const uint32_t dt = now_ - lastFrame_;
        if (dt > 0) fps_ = (uint8_t)(1000u / dt);
    }
    lastFrame_ = now_;
    ++frameCount_;
}

void MonoUI::Tick(uint32_t now) {
    now_ = now;
    if (!in_) return;

    in_->Update(now);                   // 长按/连发判定

    MonoInput::Event ev;
    MonoInput::Key   k;
    while (in_->Pop(ev, k))
        handleEvent(ev, k);
}

void MonoUI::Run() {                    // MCU 一体式主循环
    for (;;) {
        const uint32_t t = s_millis_ ? s_millis_() : (now_ + 1);
        Tick(t);
        Frame();
    }
}

// ============================ 2. 导航与焦点 ============================
MonoPage* MonoUI::CurrentPage() { return MonoPage::GetPage(curPage_); }

MonoItem* MonoUI::CurrentItem() {
    MonoPage* p = CurrentPage();
    return p ? p->Item(TargetIndex()) : nullptr;
}

int MonoUI::FocusIndex() const {           // 渲染用：向下取整
    const int i = (int)focusS_.pos;
    return i < 0 ? 0 : i;
}
int MonoUI::TargetIndex() const {          // 逻辑用：四舍五入
    const int i = (int)(focusS_.target + 0.5f);
    return i < 0 ? 0 : i;
}

// STEP 7 版：瞬移（reset）。STEP 8 会改成 setTarget + 每帧 step
void MonoUI::SetFocus(int index) {
    MonoPage* p = CurrentPage();
    if (!p) return;
    const int last = p->ItemCount() > 0 ? p->ItemCount() - 1 : 0;
    index = monoClampi(index, 0, last);
    focusS_.reset((float)index);
    p->savedFocus = (float)index;
}

void MonoUI::MoveFocus(int delta) {
    MonoPage* p = CurrentPage();
    if (!p || p->ItemCount() == 0) return;
    SetFocus(TargetIndex() + delta);
}

void MonoUI::GotoPage(MonoPage* page, int item) {
    if (!page) return;
    MonoPage* cur = CurrentPage();
    if (cur && item < 0) cur->savedFocus = focusS_.target;  // 记住离开位置
    applyPage(page, item);
}

void MonoUI::GotoPage(int index, int item) {
    GotoPage(MonoPage::GetPage(index), item);
}

// 切页：焦点/滚动全部就位；焦点在底部时滚动直接定位到包含它的页
void MonoUI::applyPage(MonoPage* p, int item) {
    if (!p) return;
    curPage_ = p->Index();

    const float f = (item >= 0) ? (float)item : p->savedFocus;
    focusS_.reset(f);

    const int   rows      = MonoLayout::kScreenH / MonoLayout::kRowH;
    const int   count     = p->ItemCount();
    const float maxScroll = (count > rows) ? (float)(count - rows) : 0.0f;
    float st = 0.0f;
    if (f >= (float)rows) st = f - (float)(rows - 1);
    if (st < 0.0f) st = 0.0f;
    if (st > maxScroll) st = maxScroll;
    scroll_.reset(st);

    slide_ = 0.0f;   // 页面进入动画（STEP 8 生效，现在无害）
}

void MonoUI::Back() {
    MonoPage* p = CurrentPage();
    if (!p) return;
    if (p->backPage >= 0) GotoPage(p->backPage, -1);
    else                  HomeScreen(nullptr);
}

void MonoUI::Press() {
    MonoItem* it = CurrentItem();
    if (it) it->Activate(this);
}

void MonoUI::AdjustCurrent(int dir) {
    // STEP 9 实现：左右键调值
    (void)dir;
}

// ============================ 3. 输入分派 ============================
void MonoUI::handleEvent(MonoInput::Event ev, MonoInput::Key k) {
    MonoPage* page     = CurrentPage();
    const bool iconPage = page && page->GetMode() == MonoPage::ICON;

    if (ev == MonoInput::EV_CLICK || ev == MonoInput::EV_REPEAT) {
        const bool click = (ev == MonoInput::EV_CLICK);

        switch (k) {
        case MonoInput::KEY_UP:    MoveFocus(-1); break;
        case MonoInput::KEY_DOWN:  MoveFocus(+1); break;
        case MonoInput::KEY_LEFT:
            if (iconPage) MoveFocus(-1); else AdjustCurrent(-1);
            break;
        case MonoInput::KEY_RIGHT:
            if (iconPage) MoveFocus(+1); else AdjustCurrent(+1);
            break;
        case MonoInput::KEY_OK:    if (click) Press(); break;
        case MonoInput::KEY_BACK:  if (click) Back();  break;
        default: break;
        }
        return;
    }

    // 长按 OK 等效返回
    if (ev == MonoInput::EV_LONG && k == MonoInput::KEY_OK) Back();
}

// ============================ 5. 列表页渲染（最小版） ============================
void MonoUI::renderPage() {
    MonoPage* p = CurrentPage();
    if (!p) return;

    switch (p->GetMode()) {
    case MonoPage::ICON:   renderIconPage(*p); break;   // STEP 11
    case MonoPage::CUSTOM: renderCustom(*p);   break;
    default:               renderList(*p);     break;
    }
}

// 临时桩：renderIconPage 第 11 章实现，本章先空实现保证链接通过
void MonoUI::renderIconPage(MonoPage& p) { (void)p; }

void MonoUI::renderCustom(MonoPage& p) {
    if (p.drawCustom) p.drawCustom(*this, p);
}

void MonoUI::renderList(MonoPage& p) {
    const int rows  = MonoLayout::kScreenH / MonoLayout::kRowH;
    const int count = p.ItemCount();
    const int first = (int)scroll_.pos;      // STEP 7：scroll_ 恒为整数

    // 画可见行
    for (int i = first; i < first + rows && i < count; ++i) {
        MonoItem* it = p.Item(i);
        if (!it) continue;
        drawRow(*it, (float)(i - first), (i == FocusIndex()), 1.0f);
    }

    // STEP 7 临时焦点框；STEP 10 换成 XOR 反色选中框
    if (count > 0) {
        const int fy = MonoLayout::kListTop +
                       (FocusIndex() - first) * MonoLayout::kRowH;
        FrameBox(0, fy, MonoLayout::kNavX, MonoLayout::kRowH);
    }
}

// 本步只处理纯文本 / 跳页 / 动作三类，其余 STEP 9 补齐
void MonoUI::drawRow(MonoItem& it, float rowF, bool selected, float slide) {
    (void)selected; (void)slide;

    const int y    = MonoLayout::kListTop + (int)(rowF * MonoLayout::kRowH);
    const int x    = MonoLayout::kPadItem;
    const int base = RowBaseline(y);

    u8g2_SetDrawColor(g2_, theme_.fg);

    const char* prefix = (it.type == ItemType::ITEM_CALL_PAGE)
                             ? MonoRes::kPrefixPage
                             : MonoRes::kPrefixFunction;
    DrawText(MonoLayout::kPrefixX, base, prefix);

    switch (it.type) {
    case ItemType::ITEM_TEXT:
    case ItemType::ITEM_CALL_PAGE:
    case ItemType::ITEM_CALL_FUNCTION:
        DrawTextClipped(x, base, it.GetText(), MonoLayout::kNavX);
        break;
    default:
        DrawTextClipped(x, base, it.GetText(), MonoLayout::kNavX);
        break;
    }
}

// ============================ 7. 绘制工具 ============================
int MonoUI::TextWidth(const char* s) {
    if (!s || !g2_) return 0;
    if (fontList_) u8g2_SetFont(g2_, fontList_);
    return (int)u8g2_GetUTF8Width(g2_, s);
}

// 字体上延：从 u8g2 字体头第 13 字节读（有符号）
int MonoUI::FontAscent() {
    if (!fontList_) return 8;
    return (int)(int8_t)fontList_[13];
}

void MonoUI::DrawText(int x, int y, const char* s) {
    if (!s || !g2_) return;
    if (fontList_) u8g2_SetFont(g2_, fontList_);
    u8g2_DrawUTF8(g2_, (int16_t)x, (int16_t)y, s);
}

void MonoUI::DrawTextCenter(int cx, int y, const char* s) {
    DrawText(cx - TextWidth(s) / 2, y, s);
}
void MonoUI::DrawTextRight(int rightX, int y, const char* s) {
    DrawText(rightX - TextWidth(s), y, s);
}

// 逐 UTF-8 码点绘制并在 maxX 截断。
// 不能按字节截：中文一个码点 3 字节，按字节切会产生乱码。
void MonoUI::DrawTextClipped(int x, int y, const char* s, int maxX) {
    if (!s || !g2_) return;
    if (fontList_) u8g2_SetFont(g2_, fontList_);

    char one[5] = {0};
    int  cx = x;
    const char* p = s;

    while (*p) {
        const unsigned char c = (unsigned char)*p;
        int len = 1;                       // 由首字节判断码点长度
        if (c >= 0xF0)      len = 4;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;

        for (int i = 0; i < len && p[i]; ++i) one[i] = p[i];
        one[len] = '\0';

        const int w = (int)u8g2_GetUTF8Width(g2_, one);
        if (cx + w > maxX) break;          // 放不下就停

        u8g2_DrawUTF8(g2_, (int16_t)cx, (int16_t)y, one);
        cx += w;
        p += len;
    }
}

void MonoUI::FillBox(int x, int y, int w, int h) {
    u8g2_DrawBox(g2_, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h);
}
void MonoUI::FrameBox(int x, int y, int w, int h) {
    u8g2_DrawFrame(g2_, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h);
}

// 行内文本垂直居中基线：按字体度量算，不写死偏移。
//   6x12 : ascent 7, descent -2 → 基线 y+10；wqy12: ascent 8 → y+11
// 汉字墨迹比 ascent_A 更高，中文再补 1px 才视觉居中。
int MonoUI::RowBaseline(int rowTop) {
    const uint8_t* f = fontList_ ? fontList_ : MonoRes::kFontSmall;
    const int ascent  = (int)(int8_t)f[13];
    const int descent = (int)(int8_t)f[14];
    const int glyphH  = ascent - descent;

    int top = (MonoLayout::kRowH - glyphH) / 2;
    if (top < 0) top = 0;

    int base = rowTop + top + ascent;
    if (MonoRes::kLanguage) base += 1;     // 中文光学补偿
    return base;
}
```

CMake 里放开 `mono_ui.cpp mono_page.cpp mono_item.cpp mono_input.cpp resources.cpp` 和 `target_link_libraries(dotui_sim PRIVATE mono_ui)`（`mono_user_app.cpp` 仍注释，STEP 13 放开）。

### 7.3 临时应用钩子（放 main.cpp，STEP 13 删掉）

```cpp
#include "mono_ui.h"

MonoUI g_ui(&g_u8g2, &g_input);

// 临时钩子：三个测试页面
void MonoUI::InitLayout() {
    MonoPage* home = MonoPage::AddPage(MonoPage::LIST, "Home");
    MonoPage* set  = MonoPage::AddPage(MonoPage::LIST, "Settings");
    MonoPage* sub  = MonoPage::AddPage(MonoPage::LIST, "Sub");

    home->AddItem(ItemType::ITEM_CALL_PAGE, "Settings", nullptr, set, 0, nullptr);
    home->AddItem(ItemType::ITEM_CALL_PAGE, "Sub",      nullptr, sub, 0, nullptr);
    for (int i = 1; i <= 14; ++i) {   // 14 行 > 8 行可视区 → 逼出滚动
        static char buf[16][8];
        snprintf(buf[i], 8, "Row %d", i);
        home->AddItem(ItemType::ITEM_TEXT, buf[i], nullptr, 0, 0, nullptr);
    }
    sub->AddItem(ItemType::ITEM_CALL_FUNCTION, "Action", nullptr, 0, 0, nullptr);
    for (int i = 0; i < 3; ++i)
        sub->AddItem(ItemType::ITEM_TEXT, "filler", nullptr, 0, 0, nullptr);

    set->AddItem(ItemType::ITEM_TEXT, "nothing yet", nullptr, 0, 0, nullptr);
}
void MonoUI::HomeScreen(void*) { GotoPage(0, 0); }
void MonoUI::SaveConfig(void*) {}
```

main() 的主循环改成框架驱动（删掉上一章的临时菜单绘制）：

```cpp
    MonoUI::SetInstance(&g_ui);
    MonoUI::SetMillisSource([]{ return SDL_GetTicks(); });
    g_ui.SetFonts(MonoRes::kFontSmall, MonoRes::kFontBig);
    g_ui.Begin();

    while (1) {
        const uint32_t now = SDL_GetTicks();

        int key;
        while ((key = u8g_sdl_get_key()) >= 0)
            if (key != 0) queueKey(key, now);
        for (int i = MonoInput::KEY_UP; i < MonoInput::KEY_COUNT; ++i)
            g_input.SetKey((MonoInput::Key)i, now < g_pressUntil[i], now);

        g_ui.Tick(now);
        g_ui.Frame();
        SDL_Delay(16);
    }
```

**✅ 验证 7**（一条条试）：

1. 启动显示 Home 页，行首有 `+`/`-` 前缀，焦点框在第 1 行；
2. ↓ 按住不放 → 焦点**瞬间**逐行下跳，到底部时列表跟着翻页，右侧……还没有滚动条（STEP 8）；
3. OK 进 Settings/Sub，BACK 回来时焦点**停在你离开的那一行**（savedFocus 生效）；
4. 把终端窗口拉大看 stdout——没有报错。

> 概念卡：`FocusIndex()`（取整，渲染）和 `TargetIndex()`（四舍五入，逻辑）分开，是后面弹簧动画能"渲染在半路、逻辑已到位"的关键。现在弹簧没启用，两者相等。

---

---

## 第 8 章 弹簧动画：焦点与滚动

**本章目标**：把第 7 章的"瞬移"升级为平滑惯性运动，并补上滚动条和页面进入动画。

### 8.1 改 SetFocus：reset → setTarget

```cpp
void MonoUI::SetFocus(int index) {
    MonoPage* p = CurrentPage();
    if (!p) return;
    const int last = p->ItemCount() > 0 ? p->ItemCount() - 1 : 0;
    index = monoClampi(index, 0, last);
    focusS_.setTarget((float)index);   // ★ 改这里：带动画逼近
    p->savedFocus = (float)index;
}
```

### 8.2 升级 renderList：弹簧推进 + 分页滚动 + 多画一行 + 滚动条

用下面的完整版**替换**第 7 章的 `renderList`，并在文件里新增 `drawScrollbar`：

```cpp
void MonoUI::renderList(MonoPage& p) {
    const int rows  = MonoLayout::kScreenH / MonoLayout::kRowH;
    const int count = p.ItemCount();

    // 分页式滚动：按"目标索引"判断是否翻页。
    // 用目标而非当前值，保证底部继续下移时页面与选中框同帧起跳。
    const float maxScroll = (count > rows) ? (float)(count - rows) : 0.0f;
    float target = scroll_.target;
    const float f = (float)TargetIndex();
    if (f < target) {
        target = f;
    } else if (f >= target + (float)rows) {
        target = f - (float)(rows - 1);   // 焦点贴底：滚到正好显示它
    }
    if (target < 0.0f) target = 0.0f;
    if (target > maxScroll) target = maxScroll;
    scroll_.target = target;

    focusS_.step(dt_);        // ★ 弹簧推进（dt_ 在 Frame() 里已算好）
    scroll_.step(dt_);

    // 页面进入动画：slide_ 0→1（时间驱动，约 1/6 秒）
    if (slide_ < 1.0f) {
        slide_ += dt_ * 6.0f;
        if (slide_ > 1.0f) slide_ = 1.0f;
    }

    // 画可见行（多画一行作为滚动过渡）
    const int first = (int)scroll_.pos;
    const int last  = first + rows;
    for (int i = first; i <= last && i < count; ++i) {
        MonoItem* it = p.Item(i);
        if (!it) continue;

        const float rowF = (float)i - scroll_.pos;   // 浮点行号！
        if (rowF < -1.0f || rowF > (float)rows) continue;

        drawRow(*it, rowF, (i == FocusIndex()), slide_);
    }

    // 焦点框（仍是 STEP 10 前的临时版）：y 也要用浮点行号 × slide
    if (count > 0) {
        const float focusRow = focusS_.pos - scroll_.pos;
        const int barY = MonoLayout::kListTop +
            (int)((focusRow * (float)MonoLayout::kRowH) * slide_);
        FrameBox(0, barY, MonoLayout::kNavX, MonoLayout::kRowH);
    }

    // 右侧导航栏：铺底色盖住列表溢出，再画滚动条
    u8g2_SetDrawColor(g2_, theme_.bg);
    FillBox(MonoLayout::kNavX, 0, MonoLayout::kNavW, MonoLayout::kScreenH);

    if (count > rows) drawScrollbar(count, rows, maxScroll);
}

// 滚动条：轨道 + 按比例缩放的滑块，位置跟随滚动弹簧
void MonoUI::drawScrollbar(int count, int rows, float maxScroll) {
    const int trackX = MonoLayout::kScrollBarX;
    const int trackW = MonoLayout::kScrollBarW;
    const int trackY = MonoLayout::kListTop + 1;
    const int trackH = rows * MonoLayout::kRowH - 2;

    // 滑块高度 ∝ 可视行数/总行数，限制最小面积
    int thumbH = (trackH * rows) / count;
    if (thumbH < 8) thumbH = 8;
    if (thumbH > trackH) thumbH = trackH;

    float pos = scroll_.pos;
    if (pos < 0.0f) pos = 0.0f;
    if (pos > maxScroll) pos = maxScroll;

    const int thumbY = trackY +
        (int)((float)(trackH - thumbH) * (pos / maxScroll));

    u8g2_SetDrawColor(g2_, theme_.fg);
    u8g2_DrawFrame(g2_, (int16_t)trackX, (int16_t)trackY,
                   (int16_t)trackW, (int16_t)trackH);
    u8g2_DrawBox(g2_, (int16_t)(trackX + 1), (int16_t)thumbY,
                 (int16_t)(trackW - 2), (int16_t)thumbH);
}
```

**✅ 验证 8**：

1. ↓↓ 上下移动 → 焦点框**滑行**到目标行，带一点速度感；
2. 连续按 ↓ 到列表底部 → 列表整体上滚，焦点框和列表**同帧起跳**（因为滚动目标用 `TargetIndex()` 而非当前值）；
3. OK 进入子页 → 新页内容从下方轻微上滑入场（`slide_` 从 0 到 1）；
4. 运行 `./build/dotui_sim.exe 144x168` → 大屏 10 行，布局参数自动重算，底部无残留杂行（`kListTop` 居中留白）。

> 概念卡：滚动是"分页式"而不是"逐像素跟随"——焦点只在超出可视区时才把滚动目标推一格，避免每按一次键整个列表都在动。焦点和滚动各自是弹簧，两者相减 `focusS_.pos - scroll_.pos` 就是焦点在屏幕上的浮点行位置。

---

## 第 9 章 行内控件全家桶：drawRow 与左右键调值

**本章目标**：开关 / 数值 / 进度条 / 勾选框 / 跑马灯全部可用。

### 9.1 先补几何工具（带 u8g2 的坑规避）

```cpp
// u8g2_DrawRBox 的坑：
//   1. 内部用无符号类型算 w-2r，宽/高不足 2r+1 时下溢成 65535，画穿屏幕；
//   2. r=1 时 1/4 圆盘是 3 像素 L 形，拼出的"圆角"两端有 1px 毛刺。
// 所以 r<=1 或尺寸退化时一律降级为普通矩形。
void MonoUI::FillRound(int x, int y, int w, int h, int r) {
    if (r <= 1 || w <= 2 * r || h <= 2 * r) {
        FillBox(x, y, w, h);
        return;
    }
    u8g2_DrawRBox(g2_, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (int16_t)r);
}
void MonoUI::FrameRound(int x, int y, int w, int h, int r) {
    if (r > 0 && (w <= 2 * r || h <= 2 * r)) {
        FrameBox(x, y, w, h);
        return;
    }
    u8g2_DrawRFrame(g2_, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, (int16_t)r);
}

// 裸位图：无长度头，1 位着色（与 DrawIcon 相反）
void MonoUI::DrawRawBmp(const uint8_t* bmp, int x, int y, int w, int h) {
    if (!bmp) return;
    const int stride = (w + 7) >> 3;
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) {
            const uint8_t b = bmp[r * stride + (c >> 3)];
            if ((b >> (7 - (c & 7))) & 1)
                u8g2_DrawPixel(g2_, (int16_t)(x + c), (int16_t)(y + r));
        }
}
```

### 9.2 完整版 drawRow（替换第 7 章最小版）

```cpp
// 画一行：统一前缀 + 按类型绘制内容。
// rowF 为浮点行号（相对可视区首行），slide 为页面进入动画进度。
void MonoUI::drawRow(MonoItem& it, float rowF, bool selected, float slide) {
    const int y    = MonoLayout::kListTop +
                     (int)((rowF * (float)MonoLayout::kRowH) * slide);
    const int x    = MonoLayout::kPadItem;
    const int base = RowBaseline(y);

    u8g2_SetDrawColor(g2_, theme_.fg);

    const char* prefix = (it.type == ItemType::ITEM_CALL_PAGE)
                             ? MonoRes::kPrefixPage
                             : MonoRes::kPrefixFunction;
    DrawText(MonoLayout::kPrefixX, base, prefix);

    switch (it.type) {
    case ItemType::ITEM_TEXT:
        DrawTextClipped(x, base, it.GetText(), MonoLayout::kNavX);
        break;

    // 开关：左侧标签截断避让，右侧右对齐 开/关
    case ItemType::ITEM_SWITCH: {
        const char* state = it.IsOn() ? MonoRes::kTextOn : MonoRes::kTextOff;
        DrawTextClipped(x, base, it.GetText(),
                        MonoLayout::kWidgetRight - TextWidth(state) - 4);
        DrawTextRight(MonoLayout::kWidgetRight, base, state);
        break;
    }

    // 勾选框：9px 方块 + 3px 间距 + 文本
    case ItemType::ITEM_CHECKBOX: {
        const int bs = 9;
        FrameBox(x, y + 4, bs, bs);
        if (it.IsOn()) FillBox(x + 2, y + 6, bs - 4, bs - 4);
        DrawTextClipped(x + bs + 3, base, it.GetText(), MonoLayout::kNavX);
        break;
    }

    // 数值：右侧右对齐，编辑态加圆角框
    case ItemType::ITEM_VALUE: {
        snprintf(it.valueBuf, sizeof(it.valueBuf), MonoRes::kFormatValue, it.value);
        DrawTextClipped(x, base, it.GetText(),
                        MonoLayout::kWidgetRight - TextWidth(it.valueBuf) - 4);
        DrawTextRight(MonoLayout::kWidgetRight, base, it.valueBuf);
        if (it.IsEditing())
            FrameRound(x - 3, y, MonoLayout::kNavX - (x - 3),
                       MonoLayout::kRowH, 3);
        break;
    }

    // 进度条：右侧固定宽度圆角条
    case ItemType::ITEM_PROGRESS_BAR: {
        const int bw = MonoLayout::kBarW;
        const int bh = MonoLayout::kBarH;
        const int bx = MonoLayout::kWidgetRight - bw;
        const int by = y + (MonoLayout::kRowH - bh) / 2;

        DrawTextClipped(x, base, it.GetText(), bx - 4);

        FrameRound(bx, by, bw, bh, 2);
        const int fw = (bw - 4) * (int)it.value / 100;
        if (fw > 0) FillRound(bx + 2, by + 2, fw, bh - 4, 1);
        break;
    }

    // 跑马灯：仅"选中且超宽"时滚动；可选行尾状态图标
    case ItemType::ITEM_TEXT_MARQUEE: {
        const int tw = TextWidth(it.GetText());
        int mx = 0;
        if (tw > MonoLayout::kMarqueeGate && selected)
            mx = it.marquee.step(tw, now_);
        else
            it.marquee.reset();

        DrawTextClipped(x + mx, base, it.GetText(), MonoLayout::kNavX);

        if (it.statusIcon) {
            DrawRawBmp(it.statusIcon, MonoLayout::kStatusIconX, y + 3,
                       MonoLayout::kStatusIconS, MonoLayout::kStatusIconS);
        }
        break;
    }

    case ItemType::ITEM_CALL_PAGE:
    case ItemType::ITEM_CALL_FUNCTION:
        DrawTextClipped(x, base, it.GetText(), MonoLayout::kNavX);
        break;
    }
}
```

### 9.3 左右键调值与开关

把第 7 章 `AdjustCurrent` 的空实现换成真身，并新增 `switchItem`：

```cpp
void MonoUI::AdjustCurrent(int dir) {
    MonoItem* it = CurrentItem();
    if (it) adjustItem(*it, dir);
}

// 左右键调整：数值项需处于编辑态；进度条步进 5；开关按方向置位
void MonoUI::adjustItem(MonoItem& it, int dir) {
    switch (it.type) {
    case ItemType::ITEM_VALUE:
        if (it.IsEditing()) it.Adjust(dir);
        break;
    case ItemType::ITEM_PROGRESS_BAR:
        it.SetValue(it.value + (float)(dir * 5));
        if (it.callback) it.callback(this, it.Context());
        break;
    case ItemType::ITEM_SWITCH:
    case ItemType::ITEM_CHECKBOX:
        it.SetOn(dir > 0);
        if (it.callback) it.callback(this, it.Context());
        break;
    default:
        break;
    }
}

void MonoUI::switchItem(MonoItem& it) {   // 目前未被调用，STEP 13 应用层用
    if (it.type != ItemType::ITEM_SWITCH && it.type != ItemType::ITEM_CHECKBOX)
        return;
    it.Toggle();
    if (it.callback) it.callback(this, it.Context());
}
```

### 9.4 更新临时测试页

在临时 `InitLayout` 里加一条有全类型控件的页面：

```cpp
    MonoPage* ctrl = MonoPage::AddPage(MonoPage::LIST, "Controls");
    home->AddItem(ItemType::ITEM_CALL_PAGE, "Controls", nullptr, ctrl, 0, nullptr);

    ctrl->AddItem(ItemType::ITEM_SWITCH,       "Switch",   nullptr, false, 0, nullptr);
    ctrl->AddItem(ItemType::ITEM_CHECKBOX,     "Check",    nullptr, true,  0, nullptr);
    MonoItem* v = ctrl->AddItem(ItemType::ITEM_VALUE, "Value", nullptr, 0, 100, nullptr);
    v->SetValue(42.0f);
    ctrl->AddItem(ItemType::ITEM_PROGRESS_BAR, "Progress", nullptr, 60, 0, nullptr);
    ctrl->AddItem(ItemType::ITEM_TEXT_MARQUEE,
                  "A very long text to trigger marquee scrolling",
                  nullptr, 0, 0, nullptr);
```

**✅ 验证 9**：

1. 开关行：OK 翻转 ON/OFF；左右键也能直接置开/关；
2. 数值行：OK 进入编辑（出现圆角框），左右键调值 100 步走完量程，OK 退出编辑；
3. 进度条行：不用进编辑态，左右键直接 ±5；
4. 勾选行：OK 翻转方块内的填充；
5. 跑马灯行：选中并停留 1 秒后开始左滚，滚出后停 1 秒再滚回来；移开焦点立即复位；
6. 长文本行没被选中时不滚（`selected` 门槛）。

---

## 第 10 章 选中框：XOR 反色与 FillRoundOnce

**本章目标**：把临时方框换成"贴着文本宽度、平滑伸缩滑动"的选中框。

### 10.1 为什么是 XOR？

选中框要**同时盖住背景和文字**并反色。u8g2 的 `SetDrawColor(2)` 是 XOR 模式：画过的像素与底图异或——底色变前景、前景变底色，一次成形。若用"画黑底再画白字"两步走，文字会被自己盖掉。

但 XOR 有个天坑：u8g2 的 `DrawRBox` 内部由多个图元**拼接重叠**而成，重叠像素被反色两次 → 恢复原色 → 四角出现空洞。解法是自绘逐行填充、保证每像素只画一次。

### 10.2 FillRoundOnce —— 逐行圆角矩形

```cpp
// 逐行填充圆角矩形：每个像素只绘制一次。
// XOR 模式下必须用它代替 FillRound，否则角部被反色两次出现空洞。
void MonoUI::FillRoundOnce(int x, int y, int w, int h, int r) {
    if (!g2_ || w <= 0 || h <= 0) return;

    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 0) r = 0;

    for (int j = 0; j < h; ++j) {
        int inset = 0;
        if (j < r) {
            inset = r - 1 - j;                 // 上部圆角
        } else if (j >= h - r) {
            inset = r - 1 - (h - 1 - j);       // 下部圆角（对称）
        }
        if (inset < 0) inset = 0;

        const int sx = x + inset;
        const int sw = w - inset * 2;
        if (sw <= 0) continue;

        u8g2_DrawHLine(g2_, (u8g2_uint_t)sx,
                       (u8g2_uint_t)(y + j), (u8g2_uint_t)sw);
    }
}
```

### 10.3 选中框几何与弹簧

```cpp
// 选中框几何：x 固定为前缀起点，宽度 = 前缀 + 间距 + 文本内容。
// 开关/数值/进度条只选中标签部分，右侧控件不参与。
void MonoUI::selectionMetrics(MonoItem& it, int& x, int& w) {
    x = MonoLayout::kPrefixX;
    const int lead = MonoLayout::kPadItem - MonoLayout::kPrefixX;

    // 标签宽度，但不得与右侧控件重叠
    auto labelClamped = [&](int reserve) -> int {
        const int maxX = MonoLayout::kWidgetRight - reserve - 4;
        const int tw  = TextWidth(it.GetText());
        return (MonoLayout::kPadItem + tw > maxX)
                   ? (maxX - MonoLayout::kPadItem)
                   : tw;
    };

    int content = 0;
    switch (it.type) {
    case ItemType::ITEM_TEXT:
    case ItemType::ITEM_CALL_PAGE:
    case ItemType::ITEM_CALL_FUNCTION:
    case ItemType::ITEM_TEXT_MARQUEE:
        content = TextWidth(it.GetText());
        break;
    case ItemType::ITEM_CHECKBOX:
        content = 9 + 3 + TextWidth(it.GetText());
        break;
    case ItemType::ITEM_SWITCH:
        content = labelClamped(TextWidth(it.IsOn() ? MonoRes::kTextOn
                                                   : MonoRes::kTextOff));
        break;
    case ItemType::ITEM_VALUE: {
        char buf[16];
        snprintf(buf, sizeof(buf), MonoRes::kFormatValue, it.value);
        content = labelClamped(TextWidth(buf));
        break;
    }
    case ItemType::ITEM_PROGRESS_BAR:
        content = labelClamped(MonoLayout::kBarW);
        break;
    default:
        break;
    }

    if (content < 0) content = 0;
    w = lead + content;

    const int rightLimit = MonoLayout::kNavX;
    if (w < 0) w = 0;
    if (x + w > rightLimit) w = rightLimit - x;
    if (w < 0) w = 0;
}

// 把目标项的选中框 x/宽度同步到 selX_/selW_ 的 target。
// 每帧渲染前调用，语言切换、开关状态变化引起的宽度变化也能跟上。
void MonoUI::syncSelectionTarget() {
    MonoPage* p = CurrentPage();
    if (!p || p->ItemCount() == 0) {
        selX_.target = 0.0f;
        selW_.target = 0.0f;
        return;
    }

    int idx = TargetIndex();
    if (idx < 0) idx = 0;
    if (idx >= p->ItemCount()) idx = p->ItemCount() - 1;

    int x = 0, w = 0;
    selectionMetrics(*p->Item(idx), x, w);

    w += MonoLayout::kSelectionOverhang;   // 右侧外扩，包围感

    const int rightLimit = MonoLayout::kNavX;
    if (x < 0) x = 0;
    if (x > rightLimit) x = rightLimit;
    if (x + w > rightLimit) w = rightLimit - x;
    if (w < 0) w = 0;

    selX_.target = (float)x;
    selW_.target = (float)w;
}

// 页面切换时选中框瞬移（不做动画）
void MonoUI::resetSelectionAnim() {
    syncSelectionTarget();
    selX_.reset(selX_.target);
    selW_.reset(selW_.target);
}
```

### 10.4 接线（三处小改动）

1. `SetFocus` 末尾加 `syncSelectionTarget();`
2. `applyPage` 里 `slide_ = 0.0f;` 之前加 `resetSelectionAnim();`
3. `renderList` 里把临时焦点框块**整体替换**为：

```cpp
    // 选中框：x/y/宽度三个弹簧同步运动；XOR 反色覆盖整行文本
    const float focusRow = focusS_.pos - scroll_.pos;
    int barY = MonoLayout::kListTop +
               (int)((focusRow * (float)MonoLayout::kRowH) * slide_);

    if (count > 0) {
        syncSelectionTarget();
        selX_.step(dt_);
        selW_.step(dt_);

        // 到位兜底：所有弹簧收敛后强制对齐整数行。
        // 浮点积分收敛时仍可能残留亚像素误差（如 8.0001），
        // 而文本行 y 用整数索引算，相减会造成 1px 视觉偏移。
        if (slide_ >= 1.0f && focusS_.settled() && scroll_.settled()) {
            barY = MonoLayout::kListTop +
                   (FocusIndex() - (int)(scroll_.pos + 0.5f)) * MonoLayout::kRowH;
        }

        int sx = (int)(selX_.pos + 0.5f);
        int sw = (int)(selW_.pos + 0.5f);
        const int rightLimit = MonoLayout::kNavX;

        if (sx < 0) sx = 0;
        if (sx > rightLimit) sx = rightLimit;
        if (sw < 1) sw = 1;
        if (sx + sw > rightLimit) sw = rightLimit - sx;

        u8g2_SetDrawColor(g2_, 2);   // XOR：底、字一起反色
        FillRoundOnce(sx, barY, sw, MonoLayout::kRowH,
                      MonoLayout::kSelectionRadius);
        u8g2_SetDrawColor(g2_, theme_.fg);
    }
```

**✅ 验证 10**：

1. ↑↓ 时选中框**平滑滑动 + 宽度随文本伸缩**（从短行滑到长行时框会拉长）；
2. 开关翻转（ON↔OFF）→ 选中框宽度跟着微调（labelClamped 生效）；
3. 仔细看框的四个角：**无空洞、无毛刺**（把 `FillRoundOnce` 换回 `u8g2_DrawRBox` 对比一次，亲眼看到角部缺陷再换回来，印象最深）;
4. 静止后选中框与文字行**严格对齐**，无 1px 偏移（到位兜底逻辑）。

---

---

## 第 11 章 图标页：横向轮播

**本章目标**：图标页模式——焦点图标固定屏幕中心，其余图标随弹簧水平滑动。

### 11.1 DrawIcon —— 固件格式图标

```cpp
// 固件格式图标：icon[0]=边长 n，位图 MSB-first，0 位着色（1 = 透明）
void MonoUI::DrawIcon(const uint8_t* icon, int x, int y, int scale) {
    if (!icon) return;
    const int n = icon[0];
    if (n <= 0 || n > 32) return;

    const int stride = (n + 7) >> 3;

    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c) {
            const uint8_t b = icon[1 + r * stride + (c >> 3)];
            if ((b >> (7 - (c & 7))) & 1) continue;   // 1 = 透明，跳过

            if (scale <= 1)
                u8g2_DrawPixel(g2_, (int16_t)(x + c), (int16_t)(y + r));
            else
                u8g2_DrawBox(g2_, (int16_t)(x + c * scale),
                             (int16_t)(y + r * scale),
                             (int16_t)scale, (int16_t)scale);
        }
}
```

### 11.2 renderIconPage

```cpp
// 图标副标题：按语言取中/英文（应用相关数据，先硬编码在框架侧）
static const char* iconSubtitle(int index) {
    static const char* const kSubsCn[] = {
        "『芯片信息』", "『网络状态』", "『接口检测』", "『返回上一级』",
        "『路径设置』", "『阶梯波形』", "『噪声测试』", "『四向导航』"
    };
    static const char* const kSubsEn[] = {
        "[Chip Info]", "[Network]", "[Connector]", "[Back]",
        "[Path]", "[Waveform]", "[Noise]", "[Navigation]"
    };
    constexpr int n = (int)(sizeof(kSubsCn) / sizeof(kSubsCn[0]));
    if (index < 0 || index >= n) return nullptr;
    return MonoRes::kLanguage ? kSubsCn[index] : kSubsEn[index];
}

// 横向轮播：焦点图标固定屏幕中心，其余图标按弹簧水平滑动。
// 不画选择框，焦点图标下方显示名称 + 副标题。
void MonoUI::renderIconPage(MonoPage& p) {
    const int count   = p.ItemCount();
    const int centerX = MonoLayout::kScreenW / 2;

    // 图标页的水平"滚动量"直接等于焦点位置
    scroll_.target = focusS_.pos;
    focusS_.step(dt_);
    scroll_.step(dt_);

    const int iconSize = MonoLayout::kIconLogical * 3;   // 放大 3 倍 = 42px
    const int iconY    = MonoLayout::kIconTop;
    const int cell     = MonoLayout::kIconCell;

    // 逐个画图标：相对位置决定 x，超出屏幕的跳过
    for (int i = 0; i < count; ++i) {
        const float rel = (float)i - scroll_.pos;
        const int x = centerX - iconSize / 2 + (int)(rel * (float)cell);
        if (x < -iconSize || x > MonoLayout::kScreenW) continue;

        MonoItem* it = p.Item(i);
        if (it && it->iconSrc) {
            u8g2_SetDrawColor(g2_, theme_.fg);
            DrawIcon(it->iconSrc, x, iconY, 3);
        }
    }

    // 焦点图标的名称（大字）与副标题（中文字体），居中显示
    MonoItem* cur = p.Item(FocusIndex());
    if (cur) {
        u8g2_SetDrawColor(g2_, theme_.fg);

        if (fontBig_) {
            u8g2_SetFont(g2_, fontBig_);
            const int tw = (int)u8g2_GetUTF8Width(g2_, cur->GetText());
            u8g2_DrawUTF8(g2_, (int16_t)(centerX - tw / 2),
                          (int16_t)(MonoLayout::kIconTop + 60), cur->GetText());
        }

        const char* sub = iconSubtitle(FocusIndex());
        if (sub && MonoRes::kFontChinese) {
            u8g2_SetFont(g2_, MonoRes::kFontChinese);
            const int sw = (int)u8g2_GetUTF8Width(g2_, sub);
            u8g2_DrawUTF8(g2_, (int16_t)(centerX - sw / 2),
                          (int16_t)(MonoLayout::kIconTop + 82), sub);
        }

        if (fontList_) u8g2_SetFont(g2_, fontList_);   // 恢复列表字体
    }

    u8g2_SetDrawColor(g2_, theme_.fg);
}
```

### 11.3 临时测试

`InitLayout` 里加图标页（图标数据从仓库 `src/resources.cpp` 复制）：

```cpp
    MonoPage* icons = MonoPage::AddPage(MonoPage::ICON, "Icons");
    home->AddItem(ItemType::ITEM_CALL_PAGE, "Icons", nullptr, icons, 0, nullptr);
    for (int i = 0; i < MonoRes::kIconCount; ++i)
        icons->AddItem(ItemType::ITEM_CALL_PAGE, "Icon", MonoRes::kIcons[i],
                       nullptr, 0, nullptr);
```

**✅ 验证 11**：进图标页后，左右键（或上下键）切换，图标串水平滑动、焦点图标始终居中，下方显示名称与副标题。注意图标页里 `KEY_LEFT/RIGHT` 走 `MoveFocus`（第 7 章 `handleEvent` 里 `iconPage` 分支）。

> 概念卡：图标页复用了 `scroll_` 弹簧，只是语义变成"水平位置 = 焦点位置"。一行代码 `scroll_.target = focusS_.pos` 就把垂直列表的导航机制平移到了横向轮播——弹簧系统设计成与坐标轴无关的好处。

---

## 第 12 章 覆盖层：MonoWidget（MsgBox / Toast / ProgressBar）

**本章目标**：页面之上可叠弹窗；模态弹窗吃掉按键，Toast 自动消失。

### 12.1 设计

- 控件从**静态池**（容量 4）分配，用完即还——与页面池同理，无堆碎片；
- MonoUI 持有一个覆盖层**栈**：渲染时逐层叠画（最上面的可见），按键时**只有栈顶**处理；
- Toast 不吃按键（任意键只是提前关掉它），MsgBox/ProgressBar 是模态。

新建 `inc/mono_widget.h`（完整实现都在头文件里，无需 .cpp）：

```cpp
// mono_widget.h —— 覆盖层控件（MsgBox / Toast / ProgressBar）
#ifndef MONO_WIDGET_H
#define MONO_WIDGET_H

#include "mono_defs.h"
#include "mono_ui.h"
#include "resources.h"
#include <stdio.h>

class MonoWidget {
public:
    enum Kind : uint8_t {
        KIND_NONE = 0, KIND_MSG = 1, KIND_TOAST = 2, KIND_PROGRESS = 3,
    };

    static constexpr int kPoolSize = 4;

    static MonoWidget* pool() {
        static MonoWidget s[kPoolSize];
        return s;
    }
    static MonoWidget* Alloc() {
        MonoWidget* p = pool();
        for (int i = 0; i < kPoolSize; ++i) {
            if (!p[i].used_) { p[i].reset(); p[i].used_ = true; return &p[i]; }
        }
        return nullptr;                       // 池耗尽
    }
    static void Free(MonoWidget* w) { if (w) w->used_ = false; }

    // ---- 工厂：弹出各类控件 ----
    static void MsgBox(MonoUI& ui, const char* title, const char* text,
                       MonoCallback onClose = nullptr) {
        MonoWidget* w = Alloc();
        if (!w) return;
        w->kind_    = KIND_MSG;
        w->title_   = title;
        w->text_    = text;
        w->onClose_ = onClose;
        ui.PushOverlay(w);
    }

    static void Toast(MonoUI& ui, const char* text,
                      uint32_t ms = MonoAnim::kToastMs) {
        MonoWidget* w = Alloc();
        if (!w) return;
        w->kind_     = KIND_TOAST;
        w->text_     = text;
        w->expireAt_ = ui.Now() + ms;
        ui.PushOverlay(w);
    }

    static MonoWidget* ProgressBar(MonoUI& ui, const char* title, int percent = 0) {
        MonoWidget* w = Alloc();
        if (!w) return nullptr;
        w->kind_    = KIND_PROGRESS;
        w->title_   = title;
        w->percent_ = monoClampi(percent, 0, 100);
        ui.PushOverlay(w);
        return w;                              // 返回句柄可 SetPercent 更新
    }

    // ---- 运行期更新 ----
    int  GetPercent() const { return percent_; }
    void SetPercent(int p)  { percent_ = monoClampi(p, 0, 100); }
    void SetText(const char* t) { text_ = t; }

    // 超时判断：差值比较，避免毫秒回绕
    bool IsExpired(uint32_t now) const {
        return kind_ == KIND_TOAST && (int32_t)(now - expireAt_) > 0;
    }

    void Update(MonoUI& ui) {
        if (IsExpired(ui.Now())) ui.PopOverlay();
    }

    // 按键处理。返回 true 表示事件已被吃掉（模态）
    bool OnKey(MonoUI& ui, MonoInput::Key k) {
        switch (kind_) {
        case KIND_TOAST:
            ui.PopOverlay();
            return false;                      // 不吃按键
        case KIND_MSG:
            if (k == MonoInput::KEY_OK || k == MonoInput::KEY_BACK) {
                if (onClose_) onClose_(&ui, nullptr);
                ui.PopOverlay();
            }
            return true;
        case KIND_PROGRESS:
            // 模态：←/→ 调整进度（长按连发自然生效），OK/B 关闭
            if (k == MonoInput::KEY_RIGHT)
                SetPercent(percent_ + 10);
            else if (k == MonoInput::KEY_LEFT)
                SetPercent(percent_ - 10);
            else if (k == MonoInput::KEY_BACK || k == MonoInput::KEY_OK)
                ui.PopOverlay();
            return true;   // 其余按键仍被吃掉（模态）
        default:
            return false;
        }
    }

    void Draw(MonoUI& ui) {
        switch (kind_) {
        case KIND_MSG:      drawMsg(ui);      break;
        case KIND_TOAST:    drawToast(ui);    break;
        case KIND_PROGRESS: drawProgress(ui); break;
        default: break;
        }
    }

private:
    MonoWidget() {}
    MonoWidget(const MonoWidget&) = delete;
    MonoWidget& operator=(const MonoWidget&) = delete;

    void reset() {
        kind_ = KIND_NONE; title_ = nullptr; text_ = nullptr;
        percent_ = 0; expireAt_ = 0; onClose_ = nullptr;
    }

    static void setColor(MonoUI& ui, uint8_t c) {
        u8g2_SetDrawColor(ui.G2(), c);
    }

    // 消息框：居中卡片 = 标题 + 分隔线 + 单行正文 + OK 按钮
    void drawMsg(MonoUI& ui) {
        const int w = 108, h = 52;
        const int x = (MonoLayout::kScreenW - w) / 2;
        const int y = (MonoLayout::kScreenH - h) / 2;

        setColor(ui, ui.Theme().bg);
        ui.FillRound(x, y, w, h, 4);
        setColor(ui, ui.Theme().fg);
        ui.FrameRound(x, y, w, h, 4);

        if (title_) {
            ui.DrawTextCenter(x + w / 2, y + 14, title_);
            ui.FillBox(x + 5, y + 16, w - 10, 1);
        }
        if (text_) ui.DrawTextCenter(x + w / 2, y + 32, text_);

        const int bw = 30, bh = 12;
        const int bx = x + w - bw - 6, by = y + h - bh - 3;
        setColor(ui, ui.Theme().bg);
        ui.FillRound(bx, by, bw, bh, 3);
        setColor(ui, ui.Theme().fg);
        ui.FrameRound(bx, by, bw, bh, 3);
        ui.DrawTextCenter(bx + bw / 2, by + 9, MonoRes::kTextOk);
    }

    // 气泡：宽度随文本自适应，屏幕下 1/4 处居中，实底反色
    void drawToast(MonoUI& ui) {
        if (!text_) return;
        const int tw = ui.TextWidth(text_);
        const int w  = tw + 14, h = 20;
        const int x  = (MonoLayout::kScreenW - w) / 2;
        const int y  = MonoLayout::kScreenH * 3 / 4 - h / 2;

        setColor(ui, ui.Theme().fg);
        ui.FillRound(x, y, w, h, 4);
        setColor(ui, ui.Theme().bg);
        ui.DrawText(x + 7, y + 14, text_);
        setColor(ui, ui.Theme().fg);
    }

    // 进度框：标题 + 圆角进度条 + 百分比
    void drawProgress(MonoUI& ui) {
        const int w = 100, h = 44;
        const int x = (MonoLayout::kScreenW - w) / 2;
        const int y = (MonoLayout::kScreenH - h) / 2;
        const int asc = ui.FontAscent();

        setColor(ui, ui.Theme().bg);
        ui.FillRound(x, y, w, h, 4);
        setColor(ui, ui.Theme().fg);
        ui.FrameRound(x, y, w, h, 4);

        if (title_) ui.DrawTextCenter(x + w / 2, y + asc + 3, title_);

        const int tx = x + 8, tw = w - 16, ty = y + h - 20, th = 8;
        ui.FrameRound(tx, ty, tw, th, 2);
        const int fill = (tw - 4) * percent_ / 100;
        if (fill > 0) ui.FillRound(tx + 2, ty + 2, fill, th - 4, 1);

        char buf[8];
        snprintf(buf, sizeof buf, "%d%%", percent_);
        ui.DrawTextCenter(x + w / 2, y + h - 4, buf);
    }

    Kind        kind_ = KIND_NONE;
    const char* title_ = nullptr;
    const char* text_  = nullptr;
    int         percent_ = 0;
    uint32_t    expireAt_ = 0;
    MonoCallback onClose_ = nullptr;
    bool        used_ = false;
};

#endif
```

> 注意：`mono_widget.h` 是 **header-only**，不要加进 CMakeLists 的源文件列表；
> 使用它的 `main.cpp` / 应用文件里 `#include "mono_widget.h"` 即可。

---

### 12.2 接线：改 mono_ui.cpp 的哪两个函数

12.2 节的代码片段**不是**追加到文件末尾的成品，而是对两个**已有函数**的修改。
每段片段都标了目的地，请打开对应函数、把片段内容替换进函数体：

**① `PushOverlay / PopOverlay / TopOverlay / drawOverlays`（4 个新函数）**
→ 直接添加到 `mono_ui.cpp` 文件末尾即可（它们是完整函数，不是片段）：

```cpp
void MonoUI::PushOverlay(MonoWidget* w) {
    if (!w || overlayDepth_ >= kMaxOverlays) return;
    overlay_[overlayDepth_++] = w;
}
void MonoUI::PopOverlay() {
    if (overlayDepth_ <= 0) return;
    MonoWidget* w = overlay_[--overlayDepth_];
    overlay_[overlayDepth_] = nullptr;
    MonoWidget::Free(w);
}
MonoWidget* MonoUI::TopOverlay() {
    if (overlayDepth_ <= 0) return nullptr;
    return overlay_[overlayDepth_ - 1];
}

void MonoUI::drawOverlays() {
    for (int i = 0; i < overlayDepth_; ++i)
        if (overlay_[i]) overlay_[i]->Draw(*this);
    u8g2_SetDrawColor(g2_, theme_.fg);
}
```

**② 修改已有的 `Tick()` 函数**（事件先给栈顶覆盖层，没吃掉才走导航）：

```cpp
void MonoUI::Tick(uint32_t now) {
    now_ = now;
    if (!in_) return;

    in_->Update(now);                   // 长按/连发判定

    MonoInput::Event ev;
    MonoInput::Key   k;
    // ★ 替换原来的 while (in_->Pop(ev, k)) handleEvent(ev, k);
    while (in_->Pop(ev, k)) {
        bool consumed = false;
        if (overlayDepth_ > 0) {
            MonoWidget* w = TopOverlay();
            if (w) consumed = w->OnKey(*this, k);
        }
        if (!consumed) handleEvent(ev, k);
    }

    // ★ 循环后新增：顶层 Toast 超时检查（到期自动弹出）
    if (overlayDepth_ > 0) {
        MonoWidget* w = TopOverlay();
        if (w) w->Update(*this);
    }
}
```

**③ 修改已有的 `Frame()` 函数**（`renderPage()` 之后、`SendBuffer` 之前加一行）：

```cpp
    u8g2_ClearBuffer(g2_);
    u8g2_SetDrawColor(g2_, theme_.fg);
    renderPage();
    if (overlayDepth_ > 0) drawOverlays();   // ★ 新增这一行
    u8g2_SendBuffer(g2_);
```

> ⚠️ 常见错误：把 ②③ 的片段原样粘贴到文件末尾——片段是**裸语句**，
> 游离在函数体外会报 `expected unqualified-id`；且原函数没改，
> 覆盖层实际没接上。

---

### 12.3 临时测试：加在哪里

三个回调 + 一个测试页，全部加在 **main.cpp**（临时代码，第 13 章删除）。
位置分两处：

**位置 A：`InitLayout()` 函数之外、它上方**——三个回调函数（文件作用域，
必须定义在 `InitLayout()` 之前才能被引用）：

```cpp
#include "mono_widget.h"   // 文件头部 include 区

static void onMsg(MonoUI* ui, void*) {
    MonoWidget::MsgBox(*ui, "Alert", "Hello");
}
static void onToast(MonoUI* ui, void*) {
    MonoWidget::Toast(*ui, "Saved!");
}
static void onProg(MonoUI* ui, void*) {
    // 只负责弹出进度框；数值由弹窗内的 ←/→ 方向键调整（见 MonoWidget::OnKey）
    MonoWidget::ProgressBar(*ui, "Loading", 0);
}
```

**位置 B：`InitLayout()` 函数内部**——新建一个 Widgets 测试页，
用 `ITEM_CALL_FUNCTION` 把回调挂到菜单项上：

```cpp
    // 第 12 章验证页：三个覆盖层控件，各挂一个回调。
    // 关键：AddItem 的最后一个参数就是回调指针——
    // 传 nullptr 则按 OK 毫无反应，覆盖层永远弹不出来。
    MonoPage* wdg = MonoPage::AddPage(MonoPage::LIST, "Widgets");
    wdg->AddItem(ItemType::ITEM_CALL_FUNCTION, "MsgBox",   nullptr, 0, 0, onMsg);
    wdg->AddItem(ItemType::ITEM_CALL_FUNCTION, "Toast",    nullptr, 0, 0, onToast);
    wdg->AddItem(ItemType::ITEM_CALL_FUNCTION, "Progress", nullptr, 0, 0, onProg);
    home->AddItem(ItemType::ITEM_CALL_PAGE, "Widgets", nullptr, wdg, 0, nullptr);
```

回调的触发链路：按 OK → `handleEvent` → `Press()` → `DotItem::Activate()`
的 `ITEM_CALL_FUNCTION` 分支 → `callback(ui, Context())` → 弹出覆盖层。

---

### 12.4 ✅ 验证 12

编译运行（**先点一下 SDL 窗口获取键盘焦点**），Home → Widgets：

1. **MsgBox**：弹出居中卡片；按 ↑↓ **焦点纹丝不动**（模态吃键）；
   S 或 B 关闭，之后导航恢复正常；
2. **Toast**：屏幕下 1/4 处弹出气泡，1.2 秒自动消失；
   期间按任意键提前关掉，且**按键继续传给页面**（焦点会跟着移动）；
3. **ProgressBar**：← 进度 −10、→ 进度 +10（按住不放靠连发连续变化），
   S 或 B 关闭；关闭后再按 S 重新从 0 开始；
   注意回调只在"弹出"那一刻执行一次——弹出后的交互全部由
   `MonoWidget::OnKey` 的模态分支处理，这就是"入口拦截"；
4. 弹窗出现时，页面列表在弹窗外的区域仍然可见（覆盖层是"叠画"，
   不是全屏清屏）。

> 概念卡：覆盖层的优先级靠"入口拦截"（Tick 里栈顶先吃事件）+
> "出口叠加"（Frame 里最后画）实现，页面本身完全不知道有弹窗存在。
> 弹出后的按键交互（如 ProgressBar 的 ←/→ 调值）写在控件自己的
> `OnKey` 模态分支里，而不是再靠弹出时的回调——回调只负责"创建"。

## 第 13 章 应用层组装与多语言

**本章目标**：删掉所有临时代码，用真实的 `mono_user_app.cpp` 组装出完整 demo。

### 13.1 应用钩子

应用层只实现框架声明的三个虚位：`InitLayout()`（注册页面）、`HomeScreen()`（回主页）、`SaveConfig()`（存配置）。把仓库 `src/mono_user_app.cpp` 全文敲入（或对照复制），它演示了：

- **多语言**：`reg(item, en, cn)` 把每个 item 的中英文对登记进 `g_texts` 表，`applyLanguage()` 一键改写所有文本指针 + 系统文案（`MonoRes::kTextOn` 等）+ 字体（中文换 wqy12）；
- **回调**：`onDarkMode` 调 `dotui_set_dark_mode(it->IsOn())`，`onLanguage` 切语言，`onTest` 弹窗回显；
- **随机压力测试**：`addRandomItem` 随机塞入全类型控件，主页 13 项 / Settings 17 项 / More 17 项，专门逼出分页滚动与选中框伸缩。

新建 `inc/dotui_theme.h`（反色接口，PC 实现在 main.cpp）：

```cpp
// dotui_theme.h —— 主题（反色）控制
// 反色固定在显示驱动层实现，应用层只调用本接口，不感知后端细节
#ifndef DOTUI_THEME_H
#define DOTUI_THEME_H
#ifdef __cplusplus
extern "C" {
#endif
void dotui_set_dark_mode(bool dark);   // false 浅色 / true 深色
#ifdef __cplusplus
}
#endif
#endif
```

### 13.2 最终版 main.cpp

对照仓库 `src/main.cpp` 整理：加命令行参数解析（`144x168`、`--debug-nav` 自动上下移动测弹簧边界）、`dotui_set_dark_mode` 实现（调 `u8g_sdl_set_inverse`）、删除全部临时代码。CMake 里放开 `src/mono_user_app.cpp`。

**✅ 验证 13（最终验收清单）**：

1. 主页 Settings → 深色模式开关：整屏立即反色（白底黑字 ↔ 黑底白字）；
2. Language 开关：OK 后**所有文本**（含 ON/OFF、弹窗按钮、行高基线）切换中英文；中文行视觉居中（RowBaseline 的 +1px 补偿）；
3. More 页长文本跑马灯滚动；Progress 项每按一次 +20%；
4. Icons 页 8 个图标轮播，任意图标 OK 进 Test 子页（16 项，测分页滚动），BACK 一路返回焦点位置都正确；
5. 长按 OK = 返回；
6. `--debug-nav` 跑 1 分钟，观察弹簧在列表头尾反复折返无抖动、无错位。

到这里，你已经从空目录手敲出了整个 DotUI。**对照第 0 章的架构图，每一层你都应该能说出"它为什么存在"**。

---

## 第 14 章 真机移植清单

PC 模拟器和真机的分界线只有三处：


| 移植点   | PC 模拟器                      | 真机（如 LS013B7DH03）                      |
| ---------- | -------------------------------- | --------------------------------------------- |
| 显示后端 | `u8g2_SetupBuffer_SDL_128x128` | 换成目标屏的 `u8g2_Setup..._SPI` setup 回调 |
| 毫秒时钟 | `SDL_GetTicks`                 | `HAL_GetTick` 等注入 `SetMillisSource`      |
| 按键电平 | SDL 键码 → 60ms 虚拟电平      | GPIO 扫描直接调 `in.SetKey(k, down, now)`   |

另外注意：

1. **字体**：真机没有 202KB 的 wqy12，按实际用到的字符裁剪字库，或只用英文 6x12；
2. **`Run()`**：MCU 上不写主循环，直接调 `ui.Run()`；PC 上自管循环是为了插 SDL 事件泵；
3. **堆**：`MonoItem` 仍是 `new` 出来的（InitLayout 期一次性分配）。内存极度紧张时可以改成 `MonoItem` 也进静态池，框架其余代码不用动；
4. **反色**：在屏驱动层实现 `dotui_set_dark_mode`（例如写 SPI 命令反转极性），不要在渲染层画反色矩形。

---

## 附录 A 常见问题


| 现象                            | 原因                                                     | 解法                                         |
| --------------------------------- | ---------------------------------------------------------- | ---------------------------------------------- |
| 中文乱码/缺字                   | 源文件不是 UTF-8，或字体不含汉字                         | 编辑器存 UTF-8；中文必须用 wqy12 字体        |
| 运行报缺 SDL2.dll               | POST_BUILD 复制没生效                                    | 重新 `cmake -S . -B build` 再 ninja          |
| 选中框角部有空洞                | XOR 模式用了 `u8g2_DrawRBox`                             | 换 `FillRoundOnce`                           |
| 圆角矩形画穿屏幕右缘            | w-2r 无符号下溢                                          | 用 `FillRound/FrameRound` 的降级封装         |
| 文本行偏上/偏下 1px             | 基线写死常量                                             | 用 `RowBaseline` 按字体度量计算              |
| 弹簧到位后又多走一段            | 只判位置没判速度                                         | `settled()` 同时判 pos 和 vel                |
| 程序结束崩溃                    | uvector 被浅拷贝后 double free                           | 保持 `= delete` 拷贝，传指针                 |
| undefined reference 到 MonoUI:: | 提前链接了依赖后续章节的 .cpp（如第6章链 mono_item.cpp） | 按章节顺序放开 CMake 源文件（见第 6 章警示） |
| 窗口点击"未响应"                | 主循环没有泵送 SDL 事件                                  | 每帧 `while (u8g_sdl_get_key() >= 0) {}`     |

## 附录 B 术语速查

- **半隐式欧拉**：先更新速度再更新位置的数值积分，能量特性稳定。
- **XOR 反色**：绘制色设为 2，画过像素与底图异或，一次完成"盖底+反字"。
- **类型擦除**：用 `uintptr_t` 存任意指针/整数，靠另一个字段（ItemType）解释。
- **页面池**：对象预分配在静态数组里，运行期只取用/归还，不做堆分配。
- **零拷贝**：`SetText` 只存指针不复制，要求字符串活得比 item 久；临时文本用 `SetTextCopy`。

---

*完 —— 敲完这份教程，你就拥有了一个能跑在自己 1-bit 屏上的完整 UI 框架。*
