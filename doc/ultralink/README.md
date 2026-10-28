# MonoUI-port

把 UltraLink 固件里 `3rdParty/mono_ui/` 那套 128×128 单色 UI 框架，按作者真实的
API 形态重新整理并实现出来，可以在 PC 上跑通、也可以原样搬到 MCU。

- **框架**：页面 / 列表 / 图标轮播 / 覆盖层 / 弹簧动画 / 输入事件，约 1500 行
- **应用**：`mono_user_app.cpp` 就是作者示例那个文件，改这里就能改界面
- **资源**：字库与图标从固件按字节 dump，不是占位素材
- **验收**：PC 上跑真 u8g2 字体解码，可与官方实现逐像素对拍

技术细节见 [`docs/MonoUI-技术手册.md`](docs/MonoUI-技术手册.md)。

---

## 目录

```
MonoUI-port/
├── mono_ui/                 框架 + 应用（对应 3rdParty/mono_ui）
│   ├── mono_defs.h          布局/动画/主题常量、ItemType、monoToUptr
│   ├── mono_item.h          列表项（字段对齐固件偏移）
│   ├── mono_page.h/.cpp     页面 + 静态页池 + AddPage/AddItem
│   ├── mono_ui.h/.cpp       帧循环、渲染、导航、弹簧、覆盖层
│   ├── mono_input.h/.cpp    按键去抖 / 长按 / 重复
│   ├── mono_widget.h        MsgBox / Toast / ProgressBar / TextEditor / TextReader
│   ├── resources.h/.cpp     字库 2 套 + 图标 8 枚 + 状态图标 3 枚
│   └── mono_user_app.cpp    ★ 应用层：InitLayout + 回调（一般只改这个）
├── uvector/uvector.h        替 std::vector 的极简容器（扩容 1.5 倍）
├── third_party/u8g2_stub/   PC 验收用的 u8g2 头
├── test/
│   ├── u8g2_sim.cpp         手写的 u8g2 后端 + 真字体解码器（128×128）
│   ├── render_test.cpp      跑真界面、打 ASCII、存 BMP 截图
│   └── ref_u8g2.cpp         用官方 u8g2 画同一份字库，用于对拍
├── port/mono_port_esp32.cpp ESP32-S3 + Sharp Memory LCD 移植参考
└── legacy-v1/               早期按固件逆向猜的 API，已废弃，仅存档
```

## 构建（PC）

```bash
# 只要 PC 验收：自带 u8g2 桩 + 手写解码器
cmake -B build -G Ninja
cmake --build build
./build/render_test            # 打 ASCII + 生成 shot_XX.bmp

# 额外产出官方参照：指向本机 u8g2 的 csrc
cmake -B build-ref -G Ninja -DU8G2_CSRC=/path/to/u8g2/csrc
cmake --build build-ref
./build-ref/ref_test           # 官方渲染，用来和 sim 对拍
```

Windows + MSYS2 用户注意：Bash 里先 `export PATH="/c/msys64/ucrt64/bin:$PATH"`，
否则 `cc1plus` 找不到 libisl 之类的 DLL，只会静默返回 1（真实退出码 0xC0000135）。

`render_test` 的几种用法：

| 命令 | 作用 |
|---|---|
| `./render_test` | 跑 9 个界面场景，每帧存 `shot_XX.bmp` |
| `./render_test glyph A` | 单字形点阵 1:1 打印（对着看笔画） |
| `./render_test alpha` | 全可打印 ASCII 铺一屏，存 `alpha.bmp` |

## C++17 与目标平台

**不需要为 MCU 把标准降回 C++11。** 实测：

| 平台 | 工具链 | 默认标准 |
|---|---|---|
| STM32 | `arm-none-eabi-g++ 13.3.1`（STM32CubeCLT 1.20） | **`__cplusplus == 201703L`**，不加 flag 就是 C++17 |
| ESP32-S3 | ESP-IDF v6.0 → GCC 15.1.0 | **`-std=gnu++23`**（官方默认） |
| PC 验收 | MSYS2 g++ 16.2 | 本工程 `CMAKE_CXX_STANDARD 17` |

原固件写 C++11 是时代原因：REF-STM32F4-fw 那套代码成于 2017 前后，
当时 arm-none-eabi-gcc 主流是 4.9/6.3，C++17 标准当年才定稿、GCC 8 才完整支持。

### 框架整体交叉编译到 Cortex-M4 的实测开销

```bash
arm-none-eabi-g++ -std=c++17 -mcpu=cortex-m4 -mthumb -Os \
    -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections
```

| 文件 | .text | .data | .bss |
|---|---|---|---|
| mono_ui.cpp | 4944 | 0 | 8 |
| mono_page.cpp | 661 | 4 | 644 |
| mono_input.cpp | 379 | 0 | 0 |
| resources.cpp | 3405 | 0 | 0 |
| mono_user_app.cpp | 1604 | 32 | 184 |
| **合计** | **10993** | **36** | **836** |

约 **11 KB Flash + 872 B RAM**（其中 `resources.cpp` 的 3.4 KB 是字体与图标数据）。

### 真正该担心的不是标准，是这四类写法

`nm -u` 实测：`std::array` / `std::optional` / `std::variant` / `std::get_if` /
`if constexpr` **零未定义符号** —— 纯头文件模板，不碰运行库、不碰堆。
会拖东西的是：

| 写法 | 交叉编译后拖进来 |
|---|---|
| `std::string` / `std::vector` / `std::function` | `operator new` `operator delete` `malloc` `free` `__throw_bad_function_call` |
| 函数局部 `static` | `__cxa_guard_acquire` / `__cxa_guard_release`（线程安全守卫） |
| 全局对象（非平凡析构） | `__dso_handle` / `__cxa_atexit` |
| `snprintf` 家族 | newlib 格式化代码，一次 6~10 KB |

本框架有意只用第一列安全的那些。启动期唯一的堆分配是
`MonoPage::AddItemRaw` 里的 `new MonoItem()` —— 照固件原样复刻，
只在 `InitLayout` 里跑一次、运行期不释放，不产生碎片。

### 上板编译选项

STM32（CMake）：

```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
target_compile_options(app PRIVATE
    -fno-exceptions -fno-rtti
    -fno-threadsafe-statics    # 去掉 __cxa_guard_*：InitLayout 只跑一次
    -fno-use-cxa-atexit        # 去掉 __dso_handle：程序不会正常退出
    -ffunction-sections -fdata-sections
    -Os)
target_link_libraries(app PRIVATE -Wl,--gc-sections)
```

ESP-IDF（组件 `CMakeLists.txt`）：

```cmake
idf_component_register(SRCS "mono_ui" "port" REQUIRES ...)
# 默认已是 gnu++23，显式钉到 17 也完全没问题
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
```

加上 `-fno-threadsafe-statics -fno-use-cxa-atexit` 后实测：
`__cxa_guard` / `__dso_handle` 未定义符号**归零**，`.text` 10993 → 10965。

## 最小用法

```cpp
static u8g2_t g2;
MonoInput input;
MonoUI    ui(&g2, &input);

void MonoUI::InitLayout() {
    auto page = MonoPage::AddPage(MonoPage::LIST, "MonoUI");
    page->AddItem(ItemType::ITEM_CALL_FUNCTION, "Hello", nullptr, 0, 0, OnHello);
}

void OnHello(MonoUI* ui, void*) {
    MonoWidget::MsgBox(*ui, "Alert", "Hello from MonoUI");
}

// 平台层
void setup() {
    u8g2_SetupDisplay(&g2, ...);
    u8g2_SetupBuffer(&g2, buf, 16, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);

    MonoUI::SetInstance(&ui);
    MonoUI::SetMillisSource(millis);
    ui.SetFonts(MonoRes::kFontData6x12, MonoRes::kFontBig);
    ui.Begin();
}

void loop() {
    input.SetKey(MonoInput::KEY_DOWN, digitalRead(PIN_DOWN), millis());
    ui.Tick(millis());
    ui.Frame();
    pushToPanel(u8g2_GetBufferPtr(&g2));
}
```

## 已知偏差

以下几处是为了在 128px 上不叠字而**有意**偏离固件常量的，改回去只需动一行：

| 位置 | 固件 | 这里 | 原因 |
|---|---|---|---|
| 行内右侧控件右边界 | `0x77`(119) | `96` | 119 落在选中行右侧色块里，会被整块盖住 |
| 标签过长 | 直接画 | 逐字截断到控件左边界 | 128px 放不下 "Progress Bar" + 进度条 |
| 复选框 | 位置未知 | 从 `10` 起画，标签 `+12` | `x-7` 会探进左侧选中色块 |
| 跑马灯终点 | `0x5A` 参与减法，符号不确定 | `offset <= 90 - textWidth` | 几何推导；写成 `-90-w` 会空跑十几秒 |

## 许可

`u8g2` 为 olikraus 的作品（BSD-2）；字库/图标数据来自设备固件，仅作技术研究。
