# MonoUI 技术手册

> **对象**：UltraLink 固件 `3rdParty/mono_ui/` 里的自研 128×128 单色 UI 框架
> **依据**：Ghidra 反编译（1,594 函数）+ 固件字节级 dump + 作者示例源码截图 + PC 端逐像素对拍
> **配套代码**：`MonoUI-port/`，`cmake --build build` 零警告，`render_test` 可直接跑
> **验证强度**：字库解码与官方 u8g2 **逐字节全等**（`alpha.bmp` 与 `ref_alpha.bmp` MD5 相同）

---

## 目录

- [0. 事实来源与可信度分级](#0-事实来源与可信度分级)
- [1. 三层结构与"强制分层"机制](#1-三层结构与强制分层机制)
- [2. API 契约](#2-api-契约)
- [3. 布局常数](#3-布局常数)
- [4. 动画系统](#4-动画系统)
- [5. 焦点双索引](#5-焦点双索引)
- [6. 输入与导航](#6-输入与导航)
- [7. 覆盖层与 widget](#7-覆盖层与-widget)
- [8. u8g2 深度剖析](#8-u8g2-深度剖析)
- [9. 资源格式](#9-资源格式)
- [10. 移植指南](#10-移植指南)
- [11. 验收方法论](#11-验收方法论)
- [12. 已知偏差](#12-已知偏差)
- [13. 固件地址对照表](#13-固件地址对照表)
- [附录 A：从零复刻清单](#附录-a从零复刻清单)
- [附录 B：文件规模与依赖](#附录-b文件规模与依赖)

---

## 0. 事实来源与可信度分级

这份手册里每个数字都有出处。但出处不同，可信度也不同，混在一起写会误导人。
先立规矩：

| 级别 | 依据 | 举例 | 置信度 |
|---|---|---|---|
| **A 实测字节** | 从固件二进制里按偏移读出来 | 字体头 `glyph_cnt = 0xbf = 191`；图标首字节 `14` | 确定 |
| **B 反编译常数** | Ghidra 反编译出的浮点/整型立即数 | `kIconCell = 56`（`0x0801453C`） | 高 |
| **C 逆向推断** | 从调用约定、字段偏移、分支结构推出来 | `param1` 对 `ITEM_SWITCH` 是初值 | 中 |
| **D 作者源码** | 截图里 `mono_user_app.cpp` 的真实代码 | `AddItem` 六参数、`AddPage` 静态 | 确定 |
| **E 工程决策** | 本复刻版为了能跑而做的取舍 | `kWidgetRight = 96` 而非 `119` | 见第 12 节 |

全文用 `0x08xxxxxx` 标固件地址（加载基址 `0x08008000`），用"实测"标 A 级结论。

**为什么这么较真**：这个框架最坑的地方恰恰是那些"看起来合理"的推断。
比如 u8g2 的 RLE 编码，我按常规思路实现了一遍，编译通过、运行不崩、
大多数短字符串还能看——但 `'g'` 的下半截永远错位。真正的原因在第 8.4 节，
是一个 `do-while` 的循环条件被理解反了。没有对拍，这种错永远发现不了。

---

## 1. 三层结构与"强制分层"机制

### 1.1 结构

```
┌──────────────────────────────────────────────────────────┐
│ mono_user_app.cpp        应用层（一般只改这个文件）        │
│   MonoUI::InitLayout()   ← 建页面、加条目、挂回调          │
│   MonoUI::HomeScreen()   ← 根页返回时去哪                  │
│   MonoUI::SaveConfig()   ← 写 Flash / 文件                 │
│   业务回调 TestMsgBox / TestTextEditor / ...              │
└────────────────────────┬─────────────────────────────────┘
                         │ 框架只声明、不实现
┌────────────────────────▼─────────────────────────────────┐
│ mono_ui.cpp              框架层                            │
│   Begin / Tick / Frame / Run                              │
│   GotoPage / Back / SetFocus / MoveFocus / Press          │
│   renderList / renderIconPage / renderCustom              │
│   弹簧积分、跑马灯、覆盖层栈                              │
├──────────────────────────────────────────────────────────┤
│ mono_page.cpp            页面池 + AddItem 参数解释         │
│ mono_item.cpp            （原项目没有此文件，寄生在 ui 里）│
│ mono_widget.h            MsgBox/Toast/Progress/Editor/Reader│
│ mono_input.cpp           去抖 / 长按 / 重复                │
│ resources.cpp            字库 + 图标（固件 dump）         │
└────────────────────────┬─────────────────────────────────┘
┌────────────────────────▼─────────────────────────────────┐
│ u8g2                     字形解码、圆角框、位图            │
├──────────────────────────────────────────────────────────┤
│ u8x8 + 你的驱动          SPI 字节流、DC 脚、VCOM 翻转      │
└──────────────────────────────────────────────────────────┘
```

### 1.2 分层是"编译期强制"的，不是约定

`mono_ui.h` 里有三行：

```cpp
void InitLayout();
void HomeScreen(void* arg = nullptr);
void SaveConfig(void* arg = nullptr);
```

**只有声明，没有定义。** `mono_ui.cpp` 会调用它们（`Begin()` 里调 `InitLayout()`，
`Back()` 里调 `HomeScreen()`），所以：

> 谁想用这个框架，就必须自己实现这三个函数，否则**链接阶段直接报
> undefined reference**，连烧录的机会都没有。

这是作者用编译器当"架构守卫"的手法。比写文档说"请在这里实现"有效得多 ——
文档可以跳过，链接错误跳不过。

在本复刻版里，`mono_ui/mono_user_app.cpp` 就是那个必须存在的文件。
试着把它从 `CMakeLists.txt` 里去掉，立刻会看到：

```
undefined reference to `MonoUI::InitLayout()'
undefined reference to `MonoUI::HomeScreen(void*)'
undefined reference to `MonoUI::SaveConfig(void*)'
```

### 1.3 各文件职责

| 文件 | 行数 | 职责 | 换屏/换主控要改吗 |
|---|---|---|---|
| `mono_defs.h` | 277 | 布局/动画常量、`ItemType`、`monoToUptr`、`MonoSpring`、`MonoMarquee` | 否 |
| `mono_item.h` | 100 | 列表项字段与状态位 | 否 |
| `mono_page.h/.cpp` | 111 / 120 | 静态页池、`AddPage`、`AddItem` 参数解释 | 否 |
| `mono_ui.h/.cpp` | 165 / 631 | 帧循环、三种渲染器、导航、弹簧 | 否 |
| `mono_input.h/.cpp` | 73 / 112 | 去抖 / 长按 / 重复 / 事件队列 | 否 |
| `mono_widget.h` | 502 | 五种覆盖层 + 对象池 | 否 |
| `resources.h/.cpp` | 72 / 323 | 2 套字库 + 8 枚图标 + 3 枚状态图标 | 否 |
| `mono_user_app.cpp` | 146 | **应用层，改界面只改这里** | 是 |
| `port/mono_port_esp32.cpp` | 235 | 显示初始化 + 输入采样 | **是（只有这个）** |
| `uvector/uvector.h` | 122 | 替 `std::vector` 的极简容器 | 否 |

**框架层与硬件完全解耦**：`mono_ui.cpp` 里没有一处 `#include "driver/gpio.h"`，
所有硬件访问都收敛到 `port/` 那一个文件。这是能在 PC 上跑 `render_test` 的前提。

---

## 2. API 契约

这一节的每一条都能在截图里的 `mono_user_app.cpp` 找到对应行。

### 2.1 `AddPage` 是静态方法 —— 页面不归实例所有

```cpp
auto pageMain = MonoPage::AddPage(MonoPage::LIST, "MonoUI");
auto pageShow = MonoPage::AddPage(MonoPage::LIST, "Show Widgets");
auto pageIcon = MonoPage::AddPage(MonoPage::ICON, "Icon Mode");
```

注意是 `MonoPage::AddPage`，**不是** `ui.AddPage`。这说明页面池是**类级静态存储**，
和 `MonoUI` 实例没有隶属关系。固件里也是这个结构：

```
MonoUI 单例 0x20014590
  +0x12C  pageCount   (int)     页面数
  +0x130  cap         (int)     容量
  +0x134  array       (ptr)     页面指针数组
```

对应本复刻版：

```cpp
// mono_page.h
static constexpr int kMaxPages = 16;
static MonoPage* AddPage(Mode mode, const char* name);
static MonoPage* GetPage(int index);
static int       PageCount();
static void      ResetPages();

// mono_page.cpp
MonoPage MonoPage::s_pool_[MonoPage::kMaxPages];   // ← 静态数组，零堆分配
int      MonoPage::s_count_ = 0;
```

`AddPage` 从 `s_pool_` 里取下一个空槽，返回 `nullptr` 表示超了 16 页。
**页面永不释放**（`ResetPages()` 只清内容不回收槽位复用逻辑，它把 `s_count_` 归零重来）。
在 MCU 上这是对的：页面数量在编译期就确定了，动态增删只会带来碎片。

### 2.2 三种页面模式

```cpp
enum Mode : uint8_t {
    LIST   = 0,   // 列表页，走 renderList
    ICON   = 1,   // 图标轮播，走 renderIconPage（固件主界面）
    CUSTOM = 2,   // 自绘页，交给 drawCustom（波形图/表盘）
};
```

`CUSTOM` 模式在本复刻版里留了钩子但示例没用：

```cpp
page->drawCustom = [](MonoUI& ui, MonoPage& p) {
    u8g2_DrawCircle(ui.G2(), 64, 64, 40, U8G2_DRAW_ALL);
};
```

`ui.G2()` 直接把 `u8g2_t*` 交出去，你想画什么就画什么 —— 这是"逃生舱口"。

### 2.3 `AddItem` 是六参数模板

截图里的调用长这样（`mode:` `description:` `param1:` 这些灰字是 Eclipse CDT 的
inlay hints，**不是代码语法**）：

```cpp
pageMain->AddItem(ItemType::ITEM_CALL_PAGE,  "Show Widgets", nullptr, pageShow, 0,     nullptr);
pageShow->AddItem(ItemType::ITEM_SWITCH,     "Switch",       nullptr, 0,        0,     nullptr);
pageShow->AddItem(ItemType::ITEM_CHECKBOX,   "Checkbox1",    nullptr, true,     false, nullptr);
pageShow->AddItem(ItemType::ITEM_CALL_FUNCTION, "Alert",     nullptr, 0,        0,     TestMsgBox);
```

六个实参，但第 4、5 个的类型在四次调用里分别是
`MonoPage*`、`int`、`bool`、`nullptr` —— **同一个位置混传四种类型**。
普通函数做不到，所以它是模板：

```cpp
template <typename P1, typename P2>
MonoItem* AddItem(ItemType       type,
                  const char*    description,
                  const uint8_t* iconSrc,
                  P1             param1,
                  P2             param2,
                  MonoCallback   callback) {
    return AddItemRaw(type, description, iconSrc,
                      monoToUptr(param1), monoToUptr(param2), callback);
}
```

`monoToUptr` 在 C++17 下是一个 `if constexpr` 模板，把任意实参压成 `uintptr_t`：

```cpp
template <typename T>
constexpr uintptr_t monoToUptr(T v) noexcept
{
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<uintptr_t>(v);
    } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
        return 0u;
    } else if constexpr (std::is_same_v<T, bool>) {
        return v ? 1u : 0u;
    } else if constexpr (std::is_enum_v<T>) {
        return static_cast<uintptr_t>(static_cast<intptr_t>(v));
    } else {
        static_assert(std::is_integral_v<T>,
                      "AddItem 的 param 只接受指针 / 整数 / 布尔 / nullptr");
        return static_cast<uintptr_t>(static_cast<intptr_t>(v));
    }
}
```

#### 为什么不能写成一组重载 ★

早期版本照 C++11 的惯例写了 6 个重载（`uintptr_t` / `int` / `unsigned` / `bool` /
`nullptr_t` / `T*`）。**在 64 位 PC 上编得过，交叉编译到 32 位 ARM 立刻炸**：

```
mono_defs.h:237: error: redefinition of 'uintptr_t monoToUptr(unsigned int)'
mono_defs.h:235: note: 'uintptr_t monoToUptr(uintptr_t)' previously defined here
```

根因：32 位平台 `uintptr_t` 就是 `unsigned int`，`monoToUptr(uintptr_t)` 与
`monoToUptr(unsigned)` **是同一个函数签名**。64 位下 `uintptr_t` 是
`unsigned long long`，两者不同，所以 PC 上永远不会暴露。

这类"只在换架构时才出现"的问题，是 ABI 差异里最难查的一类。用 `if constexpr`
收成一个函数后，不可能再有重定义，顺带还拿到了三个好处：

| 好处 | 说明 |
|---|---|
| 类型错误变成编译错误 | 传 `float` 会被 `static_assert` 挡住，旧重载版会静默走 `int` 分支截断 |
| 可以标 `constexpr` | `if constexpr` 把指针分支整条丢弃，函数体里不存在 `reinterpret_cast`，于是 `static_assert(monoToUptr(true) == 1)` 能编译期求值 |
| 枚举自动支持 | `std::is_enum_v` 分支兜住 `ItemType` 之类的实参 |

**为什么 `int` 要过一遍 `(intptr_t)`**：`ITEM_VALUE` 的 param1 可能是负数下限，
直接 `(uintptr_t)v` 会变成 40 亿。加一层有符号中间类型，`monoFromUptr<intptr_t>`
才能正确还原。

### 2.4 `param1` / `param2` 的语义表

这是整个 API 最不直观的地方 —— 参数含义随 `type` 完全变化，编译器管不了。
`AddItemRaw` 里集中解释：

| `ItemType` | `param1` | `param2` | 谁消费 |
|---|---|---|---|
| `ITEM_CALL_PAGE` | `MonoPage*` 目标页 | 进页后定焦的行号（0 = 用记忆值） | `AddItemRaw` 建返回链；`Activate` 跳页 |
| `ITEM_CALL_FUNCTION` | 未用 | 未用（当 `void*` 上下文） | `Activate` 调 `callback` |
| `ITEM_TEXT` | 未用 | 未用 | 无 |
| `ITEM_SWITCH` | 初值（`true` / `0`） | 未用 | `AddItemRaw` 写 `state` bit0 |
| `ITEM_VALUE` | 下限（整数） | 上限（整数，0 视作 100） | `AddItemRaw` 写 `valueMin/Max` |
| `ITEM_TEXT_MARQUEE` | 未用 | 未用 | 无 |
| `ITEM_PROGRESS_BAR` | 初值 0..100 | 未用 | `AddItemRaw` 写 `value` |
| `ITEM_CHECKBOX` | 初值（`true` / `false`） | 未用 | `AddItemRaw` 写 `state` bit0 |

`ITEM_CALL_PAGE` 有个副作用值得单独说：

```cpp
case ItemType::ITEM_CALL_PAGE: {
    MonoPage* target = monoFromUptr<MonoPage*>(param1);
    if (target) target->backPage = index_;    // ← 自动建立返回链
    break;
}
```

**加一条"进入子页"的条目，子页就自动记住了怎么回去。** 应用层不用配置任何栈，
`Back()` 直接读 `p->backPage`：

```cpp
void MonoUI::Back() {
    MonoPage* p = CurrentPage();
    if (!p) return;
    if (p->backPage >= 0) GotoPage(p->backPage, -1);
    else                  HomeScreen(nullptr);     // 根页交给应用层决定
}
```

固件里 `backPage` 是 `Page+0x49` 的一个字节 —— 说明原版最多支持 256 页，
本复刻版放宽成 `int`（`-1` 当"无父页"哨兵）。

### 2.5 回调签名

```cpp
typedef void (*MonoCallback)(MonoUI* ui, void* arg);
```

和截图 Outline 面板里的 `TestMsgBox(MonoUI *, void *)` 完全一致。
第一个参数是 `this`（框架在调用时把自己传进去），第二个是 `param2` 当上下文：

```cpp
void* MonoItem::Context() const { return monoFromUptr<void*>(param2); }
```

`TestMsgBox` 里 `(void)arg;` 直接丢掉，说明多数场景不需要上下文。
但需要的时候（比如"同一个回调服务多个条目"）它可以塞一个结构体指针。

### 2.6 八种 `ItemType` 一览

| 值 | 枚举 | 行首 | 行尾 | 按下 OK 的效果 |
|---|---|---|---|---|
| 0 | `ITEM_CALL_PAGE` | `+` (x=1) | — | 跳转到 `param1` 页 |
| 1 | `ITEM_CALL_FUNCTION` | `-` (x=1) | — | 调 `callback` |
| 2 | `ITEM_TEXT` | 无 | — | 什么都不做 |
| 3 | `ITEM_SWITCH` | 无 | `ON` / `OFF` | 翻转 |
| 4 | `ITEM_VALUE` | 无 | `%.2f` 数值 | 进/出编辑态 |
| 5 | `ITEM_TEXT_MARQUEE` | 无 | 10×10 状态图标 | 调 `callback` |
| 6 | `ITEM_PROGRESS_BAR` | 无 | 32×7 进度条 | 什么都不做 |
| 7 | `ITEM_CHECKBOX` | 9×9 勾选框 | — | 翻转 |

**0..5 有固件实证**（列表渲染器 `0x08014298` 的分支）。
**6..7 来自作者截图**：`ITEM_PROGRESS_BAR` 只在截图里出现过，
固件中能查到的是 `type == 7` 的一个判断（在 `TickTask` 里），语义确认为勾选框。

`+` / `-` 前缀的含义不是装饰 —— 它是"这一行会不会切页"的视觉标记。
`ITEM_CALL_PAGE` 能进去，`ITEM_CALL_FUNCTION` 只是执行，用户看符号就知道。

### 2.7 最小可运行骨架

```cpp
static u8g2_t    g2;
static uint8_t   buf[16 * 16 * 8];      // 128x128 / 8x8 图块
static MonoInput input;
static MonoUI    ui(&g2, &input);

void MonoUI::InitLayout() {
    auto page = MonoPage::AddPage(MonoPage::LIST, "MonoUI");
    page->AddItem(ItemType::ITEM_CALL_FUNCTION, "Hello", nullptr, 0, 0, OnHello);
}

void OnHello(MonoUI* ui, void*) {
    MonoWidget::MsgBox(*ui, "Alert", "Hello from MonoUI");
}

void setup() {
    /* ... 平台层初始化 u8g2 ... */
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

`SetInstance` 是必须的 —— 回调是自由函数，拿不到 `this`，只能靠全局指针。
固件里 UI 本来就是单例（`0x20014590`），这个设计与硬件现实一致。

---

## 3. 布局常数

### 3.1 没有标题栏

```
128 / 16 = 8 行，正好铺满整屏
```

这是刻意的：屏幕太小，标题栏那 16px 拿来多显示一行更值。页面的 `name` 字段
（`"MonoUI"` / `"Show Widgets"`）**从不显示**，只用于调试和 `render_test` 的状态输出。

### 3.2 常数表

| 常量 | 值 | 固件地址 | 含义 |
|---|---|---|---|
| `kScreenW` / `kScreenH` | 128 / 128 | — | 屏分辨率 |
| `kRowH` | 16 | `0x08014298` 的 `* 16.0` | 行高 |
| `kRowPadY` | 1 | 同上 `+ 1.0` | 行内 y 补偿 |
| `kPadText` | 3 | 同上 | `ITEM_TEXT` 缩进 |
| `kPadItem` | 10 | 同上 | 其余控件缩进（给图标列留位） |
| `kPrefixX` | 1 | 同上 | `+` / `-` 的 x |
| `kScrollBarX` | 100 | 同上 | 选中行右侧色块 x |
| `kScrollBarW` | 33 | 同上 | 右侧色块宽（`100+33=133` 超屏，由驱动裁剪） |
| `kMarkW` | 10 | 同上 | 选中行左侧色块宽 |
| `kStatusIconX` | 110 | `0x6E` | 跑马灯行尾图标 x |
| `kStatusIconS` | 10 | 同上 | 状态图标边长 |
| `kStatusHiS` | 12 | 同上 | 选中时高亮块边长 |
| `kMarqueeGate` | 96 | `0x60` | 触发跑马灯的文字宽度阈值 |

### 3.3 行坐标原式

反编译 `0x080145A8` 出来的原始表达式：

```
y = (row * 16.0 + 1.0) * slide + 1.0
```

抄进代码就是：

```cpp
// mono_ui.cpp: drawRow()
const int y = (int)((rowF * (float)MonoLayout::kRowH + 1.0f) * slide + 1.0f);
```

`slide` 是"进页展开"进度（0→1）。`slide = 0` 时所有行都挤在 `y = 1`，
随着 `slide` 涨到 1，各行按比例散开到自己的位置 —— 像拉开幕布。
`rowF` 是浮点行号（`i - scroll_.pos`），所以滚动时是**逐像素平移**而不是跳行。

### 3.4 选中行：必须"先画字，再盖色块"

```cpp
// 先画这一行的全部内容（文字、控件、图标）
switch (it.type) { ... }

// 再盖左右两块实心色块
if (selected) {
    u8g2_SetDrawColor(g2_, theme_.accent);
    FillBox(0, y, MonoLayout::kMarkW, MonoLayout::kRowH);          // 左块 0..9
    FillBox(MonoLayout::kScrollBarX, y,                              // 右块 100..132
            MonoLayout::kScrollBarW, MonoLayout::kRowH);
    u8g2_SetDrawColor(g2_, theme_.fg);
}
```

**顺序反了整个界面就废了** —— 先盖块后画字，文字会盖在色块上，
选中行变成"两个黑块夹一片白字"，在单色屏上等于全糊。

这个"色块压字"的设计是有意的：左块给用户一个强烈的"当前行"标记，
右块是滚动条占位（内容超出 8 行时视觉上暗示还有更多）。

**代价**：左右各吃掉 10px 和 28px，实际可用宽度只剩 **90px**。
这个约束直接导致第 12 节的三个"已知偏差"。

### 3.5 128px 的宽度预算（关键冲突分析）

一行 128px，扣掉：

```
左色块  0..9     10px
缩进    ..10
可用    10..99   90px   ← 文字 + 行尾控件共享
右色块  100..132 28px
```

"Progress Bar" 这个标签用 6px 字体量出来是 **72px**。进度条 32px。
`72 + 32 = 104 > 90`，**必然叠字**。

原版怎么处理的？反编译看不到"截断"逻辑 —— 它可能就是直接叠着画的，
在 128px 屏上被色块盖掉一部分用户也认了。本复刻版选择显式截断：

```cpp
void MonoUI::DrawTextClipped(int x, int y, const char* s, int maxX) {
    char one[2] = {0, 0};
    int  cx = x;
    for (const char* p = s; *p; ++p) {
        one[0] = *p;
        const int w = (int)u8g2_GetUTF8Width(g2_, one);
        if (cx + w > maxX) break;          // 放不下就停，宁可少一个字
        u8g2_DrawUTF8(g2_, (int16_t)cx, (int16_t)y, one);
        cx += w;
    }
}
```

逐字画而不是整串画，是因为 `u8g2_DrawUTF8` 没有"裁剪矩形"概念 ——
它会把字画到缓冲外面去（超出的部分被下一个图块覆盖，产生脏像素）。
逐字测量 + 提前停手是唯一可靠的做法。

于是 "Progress Bar" 显示成 "Progress"（8 个字符 × 6px = 48px，
48 + 32 + 4 间隙 = 84 < 90，刚好）。这是**故意的**，写在 README 的偏差表里。

### 3.6 八种 item 的绘制规则

```cpp
case ItemType::ITEM_TEXT:
    DrawText(x, base, it.GetText());              // x=3
    break;

case ItemType::ITEM_SWITCH: {
    const char* state = it.IsOn() ? MonoRes::kTextOn : MonoRes::kTextOff;
    DrawTextClipped(x, base, it.GetText(),
                    MonoLayout::kWidgetRight - TextWidth(state) - 4);
    DrawTextRight(MonoLayout::kWidgetRight, base, state);
    break;
}

case ItemType::ITEM_CHECKBOX: {
    const int bs = 9;
    FrameBox(x, y + 4, bs, bs);                   // 空心方框
    if (it.IsOn()) FillBox(x + 2, y + 6, bs - 4, bs - 4);   // 内缩 2px 实心
    DrawText(x + bs + 3, base, it.GetText());
    break;
}

case ItemType::ITEM_VALUE: {
    snprintf(it.valueBuf, sizeof(it.valueBuf), "%.2f", it.value);
    DrawTextClipped(x, base, it.GetText(),
                    MonoLayout::kWidgetRight - TextWidth(it.valueBuf) - 4);
    DrawTextRight(MonoLayout::kWidgetRight, base, it.valueBuf);
    if (it.IsEditing())                            // 编辑态画整行圆角框
        FrameRound(x - 3, y, MonoLayout::kScreenW - 6, MonoLayout::kRowH, 3);
    break;
}

case ItemType::ITEM_PROGRESS_BAR: {
    const int bx = MonoLayout::kWidgetRight - MonoLayout::kBarW;   // 64
    const int by = y + (MonoLayout::kRowH - MonoLayout::kBarH) / 2;
    DrawTextClipped(x, base, it.GetText(), bx - 4);
    FrameRound(bx, by, 32, 7, 2);                  // 空心圆角条
    const int fw = (32 - 4) * (int)it.value / 100; // 内缩 2px 后按比例填
    if (fw > 0) FillRound(bx + 2, by + 2, fw, 3, 1);
    break;
}

case ItemType::ITEM_TEXT_MARQUEE: {
    const int tw = TextWidth(it.GetText());
    int mx = 0;
    if (tw > MonoLayout::kMarqueeGate && selected)
        mx = it.marquee.step(tw, now_);            // 焦点行才跑
    else
        it.marquee.reset();                        // 失焦立刻归位
    DrawText(x + mx, base, it.GetText());
    /* 行尾 10x10 状态图标，选中时先挖一块背景色 */
    break;
}

case ItemType::ITEM_CALL_PAGE:
case ItemType::ITEM_CALL_FUNCTION:
    DrawText(MonoLayout::kPrefixX, base,
             (it.type == ItemType::ITEM_CALL_PAGE)
                 ? MonoRes::kPrefixPage      // "+"
                 : MonoRes::kPrefixFunction);// "-"
    DrawText(x, base, it.GetText());
    break;
```

基线统一是 `y + 11`：

```cpp
const int base = y + 11;   // 6x12 字体 ascent 7 / descent -2
```

`16px` 行高、`ascent = 7`、`descent = -2`，`(16 - (7 + 2)) / 2 ≈ 3.5`，
取 `11 - 7 = 4` 的顶部留白，`16 - 11 - 2 = 3` 的底部留白。看起来居中。

### 3.7 ICON 模式的几何

```cpp
constexpr int kIconCell    = 56;   // 相邻图标中心的间距（0x0801453C）
constexpr int kIconOriginY = -69;  // 第一格的中心 y      （0x08014540）
constexpr int kIconBox     = 48;   // 图标底板边长
constexpr int kIconInner   = 42;   // 位图放大后边长 = 14 * 3
constexpr int kIconLogical = 14;   // 位图原始边长
```

固件的原式是 `格顶 y = -69 + i * 56`，`i` 从 0 开始 —— 所以第 0 格在 `y = -69`，
第 1 格在 `y = -13`，第 2 格在 `y = 43`。**只有第 2 格在屏幕内**。

这说明原式的 `i` 不是"第几个图标"，而是"第几个相对位置"，
并且焦点永远对齐到第 2 格：

```cpp
// 焦点那格的顶边固定在 -69 + 2*56 = 43px
const int focusTop = MonoLayout::kIconOriginY + 2 * MonoLayout::kIconCell;

for (int i = 0; i < count; ++i) {
    const float rel = (float)i - scroll_.pos;
    const int   by  = focusTop + (int)(rel * (float)MonoLayout::kIconCell);
    if (by < -kIconBox || by > kScreenH) continue;   // 屏外跳过
    ...
}
```

于是屏幕上同时看到三格：焦点格在 `43..91`，上一格在 `-13..35`（露出下半截），
下一格在 `99..147`（露出上半截）。**焦点偏上、下方留白** —— 这是原版主界面的样子，
不是居中轮播。第一眼看不习惯，但和固件截图一致。

**为什么不画文字标签**：`48px` 底板 + `56px` 间距 = 只剩 8px 缝隙，
放不下 12px 高的字。固件主界面也是纯图标。

### 3.8 图标是负片 —— 最容易踩的坑

`kIcon0`（芯片图标）的第一个字节是 `14`（边长），后面 28 字节里
**外圈全是 `0xFF`**。如果按"1 = 墨迹"画，得到的是一块实心黑方块 ——
本复刻版第一版就是这样，屏幕上 8 个一模一样的黑方块。

真相：**位 = 1 是背景，位 = 0 才是笔画**。这批图标是为"在黑底板上镂空"设计的：

```cpp
void MonoUI::DrawIcon(const uint8_t* icon, int x, int y, int scale) {
    const int n = icon[0];
    if (n <= 0 || n > 32) return;
    const int stride = (n + 7) >> 3;

    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c) {
            const uint8_t b = icon[1 + r * stride + (c >> 3)];
            if ((b >> (7 - (c & 7))) & 1) continue;   // 1 = 背景，跳过
            u8g2_DrawBox(g2_, x + c * scale, y + r * scale, scale, scale);
        }
}
```

正确用法有两种，都靠"对 0 位落墨"这一个事实：

| 场景 | 底板 | 前景色 | 效果 |
|---|---|---|---|
| 未选中 | 无（白底） | `theme.fg`（黑） | 黑色线条图标 |
| 选中 | 实心黑圆角块 | `theme.bg`（白） | 图标从黑块里"挖"出来 |

实现：

```cpp
if (sel) {
    u8g2_SetDrawColor(g2_, theme_.fg);
    FillRound(bx, by, kIconBox, kIconBox, 8);       // 黑底
    if (icon) {
        u8g2_SetDrawColor(g2_, theme_.bg);          // 白笔
        DrawIcon(icon, bx + 3, by + 3, 3);          // 挖空
    }
} else if (icon) {
    u8g2_SetDrawColor(g2_, theme_.fg);              // 黑笔
    DrawIcon(icon, bx + 3, by + 3, 3);
}
```

`bx + 3` 是因为 `(48 - 42) / 2 = 3`，位图放大 3 倍后（`14*3 = 42`）居中。

---

## 4. 动画系统

### 4.1 为什么用弹簧而不是缓动

线性插值 `pos += (target - pos) * 0.2` 有两个问题：

1. **起步太急** —— 第一帧就跳完整段距离的 20%，像"闪"一下
2. **没有速度继承** —— 中途改目标（用户连按）时位置曲线会折断，看得出突跳

弹簧多存一个 `vel`，于是位置是连续的**二阶**响应：起步慢、中段快、收尾缓，
中途改目标时速度不会突变。

要注意一件事：**这套常数是过阻尼的，不会冲过头再回弹**（实测见 4.3）。
它的价值在加减速的曲线，不在"弹"。如果你照着某些教程的印象
去调常数让它弹起来，界面会显得廉价 —— 128px 的小屏上，回弹比平滑更难看清。

代价是要存 4 个 float（`pos/vel/target/prev`）而不是 1 个。

### 4.2 逐行对照 `0x08038E78`

固件把弹簧内联在渲染代码里，本复刻版提成结构体：

```cpp
struct MonoSpring {
    float pos = 0.0f, vel = 0.0f, target = 0.0f, prev = 0.0f;

    bool settled() const {
        const float e = target - pos;
        return e < MonoAnim::kSnap && e > -MonoAnim::kSnap;   // |e| < 0.001
    }

    void step() {
        if (settled()) { pos = target; vel = 0.0f; return; }   // 吸附，避免永久抖动

        const float k    = MonoAnim::kStiffness;    // 0.01
        const float damp = MonoAnim::kDamping;      // 0.11140846
        const float newPos = pos + k * vel;

        float denom = (k + damp) * k * 0.5f;        // 0.000607
        const float alt = k * damp;                 // 0.001114
        if (alt > denom) denom = alt;               // 取大者当分母

        vel = (((target - damp * vel + ((target - prev) / k) * MonoAnim::kStiffness2)
                - newPos) * k) / denom + vel;

        prev = pos;
        pos  = newPos;
    }
};
```

四个常数全部来自常量池 `0x080393EC`：

| 常量 | 值 | 作用 |
|---|---|---|
| `kSnap` | `0.0010000000474974513f` | 收敛判据 |
| `kStiffness` | `0.009999999776482582f` | 刚度 |
| `kDamping` | `0.11140845715999603f` | 阻尼 |
| `kStiffness2` | `0.006332573480904102f` | 前馈项 |

这些值丑陋得不像人手写的 —— `0.11140845715999603` 是某个计算结果被存进常量池，
不是拍脑袋定的。**原样照抄，不要"优化"成 `0.11`**，否则收敛速度会变。

### 4.3 收敛帧数（实测）

用与 `mono_defs.h` 完全相同的常数跑纯数值模拟（`reset(0)` → `target = 1`），
逐帧记录。**这张表是跑出来的，不是估的**：

```
帧   1: pos=0.0000  vel=14.6601   ← 位置还没动，速度已经爆了
帧   2: pos=0.1466  vel=13.3442
帧   3: pos=0.2800  vel=12.1464
帧   4: pos=0.4015  vel=10.2229
帧   5: pos=0.5037  vel= 8.5468   ← 半程
帧  10: pos=0.8076  vel= 3.3253
帧  15: pos=0.9255  vel= 1.2877
帧  20: pos=0.9712  vel= 0.4986
帧  25: pos=0.9888  vel= 0.1931
帧  30: pos=0.9957  vel= 0.0748
帧  35: pos=0.9983  vel= 0.0289
帧  38: pos=1.0000  vel= 0.0000   ← 满足 |e| < 0.001，吸附
```

关键里程碑（跳 1 行 / 跳 7 行都一样，因为是线性系统）：

| 进度 | 帧数 | @60fps |
|---|---|---|
| 50% | 5 | 83ms |
| 90% | 14 | 233ms |
| 99% | 26 | 433ms |
| 完全吸附 | **38**（跳 1 行）/ **48**（跳 7 行） | 630ms / 800ms |

**为什么跳得远反而停得久**：`kSnap` 是**绝对**阈值 `0.001`，不是相对值。
跳 1 行时 0.1% 的误差就够了；跳 7 行时要收敛到 `0.001/7 ≈ 0.014%` 才算停，
自然多花 10 帧。

**第一帧的怪现象**：`pos` 不动，但 `vel` 直接跳到 `14.66`。
原因是 `prev` 初始化在 `0`（= 起点），而 `target` 已经是 `1`，
于是前馈项 `((target - prev) / k) * kStiffness2 = (1/0.01) * 0.00633 = 0.633`
被一次性灌进速度。这是原版算法的固有行为 —— **它让动画"起得快"，
代价是第一个渲染帧看不出位移**，等于有 1 帧延迟。

这也解释了 `render_test` 的帧步进选择：

```cpp
step(ui, 4);      // 跑 4 帧让字体设进 u8g2
step(ui, 40);     // 进页后等展开 + 弹簧
step(ui, 44);     // 按键后等弹簧收敛（38 够，44 有余量）
```

**步进不足会怎样**：`step(ui, 24)` 时 `pos` 只到 99%（跳 1 行），
渲染出来差一点点 —— 视觉上看不出，但状态行会打印 `pos=0.99` 而不是 `1.00`。
所以验收脚本里"收敛"的判据看的是 `pos` 的小数位。

### 4.4 跑马灯：四态机

`ITEM_TEXT_MARQUEE` 用于放不下的长文本。反编译 `0x08014298` 的 `case 0..3`：

```cpp
enum State : uint8_t {
    ST_WAIT  = 0,   // 原地静置 1s
    ST_SLIDE = 1,   // 左移，每 50ms 一像素
    ST_HOLD  = 2,   // 尾部停顿 1s
    ST_BACK  = 3,   // 右移归位，每 50ms 一像素
};

int16_t step(int textWidth, uint32_t nowMs) {
    switch (state_) {
    case ST_WAIT:
        if (nowMs - stamp_ > 1000) { state_ = ST_SLIDE; stamp_ = nowMs; }
        break;
    case ST_SLIDE:
        if (nowMs - stamp_ > 50) {
            --offset_;
            if (offset_ <= 90 - textWidth) state_ = ST_HOLD;    // 左移终点
            stamp_ = nowMs;
        }
        break;
    case ST_HOLD:
        if (nowMs - stamp_ > 1000) { state_ = ST_BACK; stamp_ = nowMs; }
        break;
    case ST_BACK:
        if (nowMs - stamp_ > 50) {
            ++offset_;
            if (offset_ >= 0) { offset_ = 0; state_ = ST_WAIT; }
            stamp_ = nowMs;
        }
        break;
    }
    return offset_;
}
```

三个关键数字：

| 参数 | 值 | 出处 |
|---|---|---|
| 起跑静置 | 1000ms | `MonoAnim::kMarqueeWaitMs` |
| 移动步进 | 50ms / 1px | `MonoAnim::kMarqueeStepMs` |
| 左移终点 | `offset <= -(90 + textWidth)` | 常数 `0x5A` = 90 |

**终点公式的推导**：文字从 `x = 10` 起画，左移 `offset`（负数）后：

```
左边缘 = 10 + offset
右边缘 = 10 + offset + textWidth
```

要让它整段退出可视区（可视区右界是 `x = 100`，再往右就是色块）：

```
10 + offset + textWidth <= 100
        offset          <= 90 - textWidth
```

`textWidth = 234`（39 字符 × 6px）时终点是 `-144`，即滑 144px、7.2 秒。
这个数字是有意义的：正好是"尾部刚消失"，不多不少。

**这里踩过一个符号坑**：第一版写成 `offset <= -90 - textWidth`，等于 `-324`。
文字出屏之后还要空跑 180 个像素、白等 9 秒，屏幕上什么都没有但状态机不结束。
`-90 - w` 和 `90 - w` 只差一个符号，肉眼在小窗口里根本看不出来
（因为那时屏幕上早就没字了）—— 是写这一节推导公式时才发现的。

固件里 `0x5A`(90) 参与的是一次减法，反编译的符号方向不确定。
本复刻版按上述几何推导取 `90 - w`，理由写在代码注释里。

**触发条件两道闸**：

```cpp
if (tw > MonoLayout::kMarqueeGate && selected)   // 宽度 > 96 且是焦点行
    mx = it.marquee.step(tw, now_);
else
    it.marquee.reset();                          // 失焦立刻归位，不留残影
```

`kMarqueeGate = 96`（`0x60`）：宽度不到 96px 的文字根本不跑 ——
因为不跑也放得下，跑了反而让人眼累。

**每个 item 独享 `MonoMarquee` 实例**，不是全局一个。否则焦点切换时
两个 item 会共享一个 offset，B 项会从 A 项停下的位置开始跑。

### 4.5 进页展开

```cpp
// applyPage()
slide_ = 0.0f;
slidePage_ = curPage_;

// renderList() 每帧
if (slide_ < 1.0f) {
    slide_ += 0.15f;
    if (slide_ > 1.0f) slide_ = 1.0f;
}
```

`0.15/frame` → 7 帧走完（约 120ms）。比弹簧快得多，因为是"整页出现"的
统一动效，不需要每行独立物理。`slidePage_` 记录是哪一页在展开，
防止旧页的 `slide` 状态泄漏到新页。

---

## 5. 焦点双索引

### 5.1 两个索引

```cpp
// 视觉索引：焦点停在哪一行。渲染用它决定谁画成选中态。
int MonoUI::FocusIndex() const {
    const int i = (int)focusS_.pos;        // 截断
    return i < 0 ? 0 : i;
}

// 输入索引：焦点**要去**哪一行。按键以它为准。
int MonoUI::TargetIndex() const {
    const int i = (int)(focusS_.target + 0.5f);   // 四舍五入
    return i < 0 ? 0 : i;
}
```

同一个弹簧，两种取整方式，服务于两个完全不同的目的。

### 5.2 为什么截断而不是四舍五入

`FocusIndex()` 用截断是有意的。假设从第 0 行移到第 1 行，弹簧中途 `pos = 0.5`：

- 截断 → `0`，色块还在第 0 行
- 四舍五入 → `1`，色块已经跳到第 1 行

固件是截断。效果是：**色块在过渡的后半段（`pos > 1.0`）才切到新行**，
前半段还留在旧行。加上左右色块是瞬间切换（不是渐变），
视觉上是"色块追着内容跑"而不是"内容从色块里长出来"。
本复刻版实测 `pos=3.82` 时 `focus=3`，`pos=3.99` 时也是 `focus=3` —— 符合。

### 5.3 连按丢步问题

如果 `MoveFocus` 用 `FocusIndex()` 当基准：

```cpp
void MonoUI::MoveFocus(int delta) {
    SetFocus(FocusIndex() + delta);    // ✗ 错
}
```

按住 DOWN 连按时，弹簧还在追上一帧的目标（`pos = 0.3`，`target = 1`），
此时 `FocusIndex() = 0`，再按一次算出 `0 + 1 = 1` —— **目标没变，这一下丢了**。
用户感觉是"按了没反应"，然后突然后面又跳了两行。

正确做法是以 `target` 为基准：

```cpp
void MonoUI::MoveFocus(int delta) {
    SetFocus(TargetIndex() + delta);   // ✓ 连按不丢步
}
```

`target` 是"意图"，`pos` 是"表现"。输入要叠加在意图上，渲染要读表现。
**这个区分是整个框架最微妙的一处设计**，也是"为什么焦点要存 float"的真正原因。

---

## 6. 输入与导航

### 6.1 事件模型

固件的输入不是"中断里直接改 UI"，而是 **20ms 节拍采样 + 长按判定**
（`TickTask 0x08033EFC`）。本复刻版照这个模型：

```
移植层                        MonoInput                     MonoUI
────────                     ──────────                    ──────
GPIO 电平  ──SetKey()──▶  去抖 8ms
                          长按 500ms 判定        ──Pop()──▶  分派
                          重复 400ms 后 80ms/次
```

移植层**只负责报告电平**，事件判定全在 `Update()` 里。这样做的两个好处：

1. **抖动过滤集中在一处** —— 换按键不用重写去抖
2. **PC 上可以造任意时序** —— `render_test` 直接调 `SetKey` 就模拟了按键

```cpp
enum Key   : uint8_t { KEY_NONE, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_OK, KEY_BACK, KEY_COUNT };
enum Event : uint8_t { EV_NONE, EV_CLICK, EV_LONG, EV_REPEAT };
```

### 6.2 时序常量

| 常量 | 值 | 含义 |
|---|---|---|
| `kDebounceMs`（`mono_input.cpp` 内部） | 8 | 电平变化后静置多久才认 |
| `kLongPressMs` | 500 | 按住多久算长按 |
| `kRepeatDelayMs` | 400 | 长按后多久开始重复 |
| `kRepeatRateMs` | 80 | 重复间隔 |
| `kTickSampleMs` | 20 | 建议采样节拍 |
| `kQueueLen` | 8 | 事件队列深度 |

**400 + 80 的组合**：用户按住 DOWN 时，先等 400ms（这段时间内不重复，
避免"想按一下结果跳了三行"），然后每 80ms 一发。80ms ≈ 12.5 行/秒，
比人手速快，但配合弹簧动画看起来是连续的。

**队列深度 8** 是防"一下子涌进来一堆事件"：如果主循环卡顿 200ms，
采样任务会攒下 10 个事件，队列满就丢——丢比爆内存好。

### 6.3 键位映射

```cpp
case MonoInput::EV_CLICK:
    switch (k) {
    case KEY_UP:    MoveFocus(-1);     break;
    case KEY_DOWN:  MoveFocus(+1);     break;
    case KEY_LEFT:  AdjustCurrent(-1); break;
    case KEY_RIGHT: AdjustCurrent(+1); break;
    case KEY_OK:    Press();           break;
    case KEY_BACK:  Back();            break;
    }

case MonoInput::EV_LONG:
    if (k == KEY_OK) Back();        // 长按 OK = 返回（500ms）
    break;

case MonoInput::EV_REPEAT:
    /* 同 CLICK 的 UP/DOWN/LEFT/RIGHT，但 OK/BACK 不重复 */
```

**长按 OK = 返回**是单键设备的常见做法。硬件的 BACK 键也有，
两者都能回，冗余但符合直觉。

**`EV_REPEAT` 不给 OK/BACK** —— 重复触发"进入子页"会一路往下钻，
重复触发"返回"会一路退回根页。这两个动作必须是单次的。

### 6.4 `AdjustCurrent` 的分派

```cpp
void MonoUI::adjustItem(MonoItem& it, int dir) {
    switch (it.type) {
    case ItemType::ITEM_VALUE:
        if (it.IsEditing()) it.Adjust(dir);       // 只有编辑态才改值
        break;
    case ItemType::ITEM_PROGRESS_BAR:
        it.SetValue(it.value + (float)(dir * 5)); // 每次 5%
        break;
    case ItemType::ITEM_SWITCH:
    case ItemType::ITEM_CHECKBOX:
        it.SetOn(dir > 0);                        // 左右键 = 强制开/关
        break;
    }
}
```

**`ITEM_VALUE` 有两态**：焦点停上去只是"选中"，按 OK 才进编辑态
（`SetEditing(true)`，行外套一个圆角框），这时左右键才改值。
再按 OK 退出。这是防止误改。

`ITEM_SWITCH` 的左右键是"设为开/关"而不是"翻转" —— 翻转在连按时会来回跳。

---

## 7. 覆盖层与 widget

### 7.1 对象池

```cpp
static constexpr int kPoolSize = 4;
static MonoWidget s[kPoolSize];

static MonoWidget* Alloc() {
    for (int i = 0; i < kPoolSize; ++i)
        if (s[i].kind_ == KIND_NONE) return &s[i];
    return nullptr;                     // 池满，静默失败
}
```

**不用 `new`**。覆盖层最多同时存在 4 个（`MonoUI::kMaxOverlays = 4`），
用静态数组省掉堆分配和碎片风险。`PopOverlay()` 调 `Free()` 只清 `kind_`，
内存留着下次用。

池满时返回 `nullptr`，`MsgBox()` 直接返回 —— 用户看不到弹窗。
这是可接受的降级：正常使用不会堆到 4 层。

### 7.2 五种 widget

| `Kind` | 工厂函数 | 模态 | 按键行为 |
|---|---|---|---|
| `KIND_MSG` | `MsgBox(ui, title, text)` | 是 | OK/BACK 关闭 |
| `KIND_TOAST` | `Toast(ui, text, ms=1200)` | **否** | 不拦截，超时自关 |
| `KIND_PROGRESS` | `ProgressBar(ui, title, percent)` | 是 | OK/BACK 关闭 |
| `KIND_EDITOR` | `TextEditor(ui, buf, cap, title)` | 是 | UP/DOWN 换字符，LEFT/RIGHT 移光标 |
| `KIND_READER` | `TextReader(ui, title, text)` | 是 | UP/DOWN 滚动，BACK 关闭 |

**Toast 不拦截按键**是唯一的例外：

```cpp
bool IsModal() const { return kind_ != KIND_TOAST; }
```

因为 Toast 只是"提示一下"，不应该阻断用户操作。按键路由：

```cpp
while (in_->Pop(ev, k)) {
    bool consumed = false;
    if (overlayDepth_ > 0) {
        MonoWidget* w = TopOverlay();
        if (w) consumed = w->OnKey(*this, k);      // Toast 返回 false
    }
    if (!consumed) handleEvent(ev, k);             // 落到页面
}
```

### 7.3 MsgBox 的折行与定高

这是本轮修得最费劲的一个 widget。第一版直接定高 `h = 60`，结果两行正文
被 OK 按钮压住、第二行被下边框切掉。

正确做法是**先折行，再按行数算高度**：

```cpp
const int w = 108;
const int chW = 6, lineH = 13;

char line[2][24];
int  nLine = 0;
if (text_) {
    const int maxCols = (w - 12) / chW;          // 16 字符
    const char* p = text_;
    while (*p && nLine < 2) {                    // 最多 2 行
        int take = 0, lastSpace = -1;
        while (p[take] && take < maxCols) {
            if (p[take] == ' ') lastSpace = take;
            ++take;
        }
        if (p[take] && lastSpace > 0) take = lastSpace;   // 优先在空格处断
        int n = take < 23 ? take : 23;
        memcpy(line[nLine], p, (size_t)n);
        line[nLine][n] = '\0';
        ++nLine;
        p += take;
        while (*p == ' ') ++p;                   // 吃掉断点处的空格
    }
}
if (nLine == 0) { line[0][0] = '\0'; nLine = 1; }

// 标题区 20 + 正文 n*13 + 按钮区 15
const int h = 20 + nLine * lineH + 15;
const int y = (MonoLayout::kScreenH - h) / 2;
```

垂直布局：

```
y          ┌─ 边框
y+2..y+16  │  标题（基线 y+asc+2）
y+19       │  ─── 分隔线 ───
y+20..     │  正文第 1 行（基线 y+20+asc+0）
           │  正文第 2 行（基线 y+20+asc+13）
y+h-bh-3   │  [ OK ] 按钮右下角
y+h        └─ 边框
```

`bh = 13` 是按钮高，`-3` 是离底边框的间距。这样无论 1 行还是 2 行正文，
按钮永远贴底，正文永远不会被压。

**为什么最多 2 行**：`108px` 宽在 6px 字体下是 16 字符，2 行 = 32 字符。
再多的话框高会超过 `h = 20 + 3*13 + 15 = 74`，屏幕上放得下但视觉上太满。
长文本应该用 `TextReader` 而不是 `MsgBox`。

---

## 8. u8g2 深度剖析

这一节是本手册的核心。MonoUI 只是皮，**所有真正的坑都在 u8g2 里**。

### 8.1 u8g2 在 MonoUI 里的位置

MonoUI 只用到 u8g2 的**画布能力**，完全不用它的页面/菜单功能
（`u8g2_DrawStr` 之外的那些 `u8x8_*` 高级 API）。

用到的东西，全部清单：

| API | 用途 | 出现在 |
|---|---|---|
| `u8g2_ClearBuffer` | 每帧清屏 | `Frame()` |
| `u8g2_SetDrawColor` | 0=透明 1=前景 2=异或 | 到处 |
| `u8g2_SetFont` | 切换字库 | `DrawText` |
| `u8g2_GetUTF8Width` | 量宽度（右对齐/截断必需） | `DrawTextRight/Clipped` |
| `u8g2_DrawUTF8` | 画字符串（UTF-8 感知） | `DrawText` |
| `u8g2_DrawPixel` | 单点 | `DrawIcon` / `DrawRawBmp` |
| `u8g2_DrawBox` | 实心矩形 | `FillBox` / 图标放大 |
| `u8g2_DrawFrame` | 空心矩形 | `FrameBox` |
| `u8g2_DrawRBox` | 实心圆角 | `FillRound` |
| `u8g2_DrawRFrame` | 空心圆角 | `FrameRound` |
| `u8g2_SendBuffer` | 提交整帧 | `Frame()` |
| `u8g2_GetBufferPtr` | 取缓冲指针（移植层直接推屏） | `port/` |
| `u8g2_InitDisplay` / `SetPowerSave` | 初始化 | `Begin()` |

**`u8g2_GetUTF8Width` 是关键**：单色屏没有字形缓存，右对齐必须自己算宽度。
这个函数内部走完整的字形查找（第 8.3 节），比想象中贵 —— 一次 `DrawTextRight`
要解码整个字符串的所有字形头。所以 `DrawTextClipped` 逐字画时，
每个字符要调两次解码（一次量宽、一次绘制）—— 在 168MHz 的 F4 上
一行 16 字符约 32 次字形查找，每帧 8 行也就 256 次，可以接受。

### 8.2 字体：23 字节头

`u8g2_font_info_t` 是定长 23 字节。实测 `kFontData6x12` 的头 23 字节，按偏移排开：

```
偏移   0    1    2    3    4    5    6    7    8    9   10   11
字节  bf   00   03   02   03   04   03   05   04   06   0c   00
      │    │    │    │    │    │    │    │    │    │    │    │
      cnt  bbx  bp0  bp1  宽   高    x    y   步进  宽max 高max  xoff

偏移  12   13   14   15   16   17   18   19   20   21   22
字节  fe   07   fe   08   ff   01   42   02   86   07   cc
      │    │    │    │    │    └────┬────┘└────┬────┘└────┬────┘
     yoff  asc  dsc  asp  dsp    322          646        1996
      -2    7   -2    8   -1   upperA       lowera     unicode
```

前 17 个是单字节，后三组是 **16 位大端**（高字节在前）：
`0x0142 = 322`、`0x0286 = 646`、`0x07cc = 1996`。

完整字段表：

| 偏移 | 字段 | 值 | 含义 |
|---|---|---|---|
| 0 | `glyph_cnt` | `0xbf` = **191** | 字形数量 |
| 1 | `bbx_mode` | 0 | 位图格式（0 = 单色 1bpp） |
| 2 | `bits_per_0` | 3 | RLE 里"背景段长度"的位宽 |
| 3 | `bits_per_1` | 2 | RLE 里"前景段长度"的位宽 |
| 4 | `bits_per_char_width` | 3 | 字形宽度的位宽 |
| 5 | `bits_per_char_height` | 4 | 字形高度的位宽 |
| 6 | `bits_per_char_x` | 3 | 字形 x 偏移的位宽 |
| 7 | `bits_per_char_y` | 5 | 字形 y 偏移的位宽 |
| 8 | `bits_per_delta_x` | 4 | 步进宽度的位宽 |
| 9 | `max_char_width` | 6 | 最大字形宽 |
| 10 | `max_char_height` | **12** | 最大字形高 |
| 11 | `x_offset` | 0 | 全局 x 偏移 |
| 12 | `y_offset` | **-2** | 全局 y 偏移 |
| 13 | `ascent_A` | **7** | 大写字母上升高度 |
| 14 | `descent_g` | **-2** | 小写字母下降深度 |
| 15 | `ascent_para` | 8 | 段落上升高度 |
| 16 | `descent_para` | -1 | 段落下降深度 |
| 17-18 | `start_pos_upper_A` | `0x0142` = **322** | 大写区起始字节偏移 |
| 19-20 | `start_pos_lower_a` | `0x0286` = **646** | 小写区起始字节偏移 |
| 21-22 | `start_pos_unicode` | `0x07cc` = **1996** | Unicode 区起始字节偏移 |

**这 8 个 `bits_per_*` 是 u8g2 最精妙的设计**：字形头的长度不是固定的，
而是"每个字段占几位"写在字体头里。6x12 字体用 3+4+3+5+4 = 19 位描述一个字形，
3 字节装得下；而某些字体用 4+5+4+6+5 = 24 位，就要 3~4 字节。
**用同一套解码器能吃所有 u8g2 字体**，代价是每读一个字段都要做位运算。

第二个字库 `kFontData5x12`（972 字节）的头对比：

```
bf → 5f    (95 字形，不是 191)
06 → 05    (max_char_width 5 而不是 6)
07cc → 03ae (942，因为字形少了，偏移也小)
```
其余全同 —— 说明它是同一套工具生成的 95 字形子集（可见 ASCII），
省掉了扩展区。

### 8.3 字形查找：三段式

```
字符串 "A"
  │
  ├─ 编码 <= 0x7F（ASCII）
  │    ├─ 'A'..'Z' → 从 start_pos_upper_A 起顺序扫，找 encoding 匹配
  │    ├─ 'a'..'z' → 从 start_pos_lower_a 起顺序扫
  │    └─ 其他      → 从 start_pos_unicode 起
  │
  └─ 编码 > 0x7F（Unicode）
       └─ start_pos_unicode 起顺序扫，encoding 是 2~3 字节变长
```

**"顺序扫"是字面意思**：u8g2 没有索引表。找 'Z' 要从 'A' 开始
一个字形一个字形地跳（每个字形头长度不同，必须完整解码才能知道下一个在哪）。

这就是为什么 `u8g2_GetUTF8Width` 在长字符串上很贵 —— 它是 O(n) 次线性扫描。
`191` 个字形的 6x12 字体，最坏情况（找 Unicode 区最后一个字）要跳 190 次。
在 `168MHz` 上单次约 `2~5μs`，190 次就是 `1ms` —— **一帧里画 8 行文字
如果每行都算宽度，光是字形查找就能吃掉 8ms**。

优化手段（本复刻版做了两个）：

1. **`DrawTextRight` 才量宽度，普通 `DrawText` 不量** —— 左对齐直接画，
   不需要知道总宽
2. **截断时逐字量**，量到超出就停，不再继续扫后面的字符

### 8.4 位级 RLE：`do-while` 的真实语义 ★

**这是整个项目最关键的 bug，值得单独一节。**

u8g2 的字形位图用 RLE 压缩。`u8g2_font_decode_len` 的签名：

```c
static int u8g2_font_decode_len(u8g2_t *u8g2, uint8_t len, uint8_t is_foreground)
```

`len` 是段长，`is_foreground` 决定填 0 还是 1。看起来简单，
但**"段长"这个 `len` 从哪来**才是关键。

我第一版的实现（错的）：

```cpp
// ✗ 错：以为 flag=1 表示"再来一轮，重新读一对 (a,b)"
do {
    a = fb.get(bits_per_0);
    b = fb.get(bits_per_1);
} while (fb.get(1) != 0);
```

官方实现（`u8g2_font.c` 的 `u8g2_font_decode_glyph`）：

```c
for(;;) {
    decode_len = u8g2_font_decode_get_unsigned_bits(u8g2, u8g2->font_info.bits_per_0);
    /* ... 填背景 ... */
    u8g2_font_decode_len(u8g2, decode_len, 0);

    decode_len = u8g2_font_decode_get_unsigned_bits(u8g2, u8g2->font_info.bits_per_1);
    /* ... 填前景 ... */
    u8g2_font_decode_len(u8g2, decode_len, 1);

    if ( u8g2_font_decode_get_unsigned_bits(u8g2, 1) == 0 )
        break;
}
```

看清楚：**`(a, b)` 一对读一次，然后读 1 个标志位；标志位为 1 表示
"把同一对 `(a, b)` 再重复一遍"，不是"读新的一对"。**

正确的实现：

```cpp
for (int guard = 0; guard < 800 && ly < h; ++guard) {
    const int a = fb.get(b0);       // 背景段长
    const int b = fb.get(b1);       // 前景段长

    // 同一对 (a,b) 可能被重复多次，直到标志位为 0
    for (int rep = 0; rep < 64; ++rep) {
        run(a, false);
        run(b, true);
        if (fb.get(1) == 0) break;
    }
}
```

**为什么这个 bug 特别隐蔽**：

1. **短字符串看起来是对的** —— `"Hi"` 这种字形简单，一对 `(a,b)` 就够了，
   标志位是 0，直接跳出，两种实现结果相同
2. **不会崩** —— 位流读越界只是读到相邻字形的数据，画出来的还是"某种图案"，
   不是乱码也不是全黑
3. **错误表现为"上下颠倒"** —— 因为错读的位流让 `y` 提前跨行，
   字形的下半截用了上半截的数据。第一眼看像"字体基线算错了"

排查过程（供参考，这个思路可以复用）：

| 步骤 | 做法 | 结论 |
|---|---|---|
| 1 | Node 脚本全量校验 191 个字形头 | `fit=172 / over=17`，over 的只少 1 字节（u8g2 允许），**数据没问题** |
| 2 | 怀疑基线公式，WebFetch 拿官方 `u8g2_font.c` | 发现 `target_y -= h+y`，改正 |
| 3 | 本地找到 `/e/Dev/ESP-IDF/u8g2-dl/csrc/`，建 `ref_test` 用真 u8g2 渲染 | **官方输出也是乱的** → 说明问题在参照程序的 `unpack()`，不在字体 |
| 4 | 加 `probe` 模式画 2×8 竖条验证缓冲布局 | `g_buf[0]=0xff, g_buf[1]=0xff` → **字节是列**，`unpack` 修好 |
| 5 | `ref_test` 输出正确了，`sim` 仍不对 | 问题范围缩小到 sim 的解码器 |
| 6 | 复制 `u8g2_font.c` 注入 `printf` 追踪 | 看到一对 `(a=3,b=2)` 后面只跟一次解码，然后直接跳到 `y=6` —— **中间少了重复** |
| 7 | 对比官方循环结构 | **`do-while` 的 flag 是重复语义** |

第 6 步的追踪形状（示意，不是逐字节原始输出）：

```
[pair] a=3 b=2          ← 读一对
[len1] n=3 fg=0
[len1] n=2 fg=1
[end]  y=6              ← 一下子到了第 6 行！中间 4 行去哪了？
```

按正确的语义，这一对 `(3,2)` 应该被重复 4 次（每次 5 行），才够走完 6 行。
追踪里只走了一次就跳，说明重复被漏掉了 —— 这就是全部证据。

### 8.5 基线公式：`target_y -= (h + y)` ★

第二个独立 bug。官方：

```c
static void u8g2_font_decode_glyph(...) {
    ...
    x = glyph_x;          /* 字形左边界相对笔位置 */
    y = glyph_y;          /* 字形上边界相对基线的偏移（负=向上） */
    h = glyph_height;

    /* advance the current position */
    target_x += x;
    target_y -= h + y;    /* ← 关键 */
    ...
}
```

`y` 是字形**上边界**相对基线的偏移，通常是负数。
`target_y` 在 u8g2 里是"当前绘制行的**下边界**"（自底向上坐标系）。

要得到字形顶边，必须：

```
顶边 = 基线 - (字形高 + y)
```

因为 `y` 是负的，`h + y < h`，减掉之后顶边比"基线 - 高"更靠下 —— 这是对的，
小写字母的顶边本来就在大写之下。

我第一版写成 `py0 = baseline + (h + oy)`（加号），结果**整段文字下移一个字高**，
直接掉出屏幕。表现是"上半截全空，下半截是文字的上半部分"。

正确：

```cpp
// test/u8g2_sim.cpp
const int px0 = x + ox;
const int py0 = baseline - (h + oy);   // ← 减，不是加
```

**两个 bug 叠加时的表现极具误导性**：RLE 错 → 字形数据错位，
基线错 → 整体位移。只看屏幕会觉得"字体文件坏了"。
所以第 11 节的"官方参照"是必须的 —— 它把两个变量拆开了。

### 8.6 帧缓冲：图块布局 `字节 = 列、位 = 行` ★

第三个坑，比前两个简单但同样致命。

u8g2 的帧缓冲不是线性像素数组，而是 **8×8 图块（tile）** 组成：

```
128x128 屏幕 → 16 x 16 个图块 → 每个图块 8 字节 = 2048 字节
```

图块内部：

```
        图块内 8 列
     ┌──┬──┬──┬──┬──┬──┬──┬──┐
字节0 │  │  │  │  │  │  │  │  │  ← byte 0 存第 0 列的全部 8 行
字节1 │  │  │  │  │  │  │  │  │  ← byte 1 存第 1 列
 ... │  │  │  │  │  │  │  │  │
字节7 │  │  │  │  │  │  │  │  │
     └──┴──┴──┴──┴──┴──┴──┴──┘
       ↑ bit0 是最上面那行
```

即 **字节是列、位是行**，位序 LSB 在上。u8g2 里这个格式叫
`u8g2_ll_hvline_vertical_top_lsb`。

解包代码（写错就整屏变竖条纹）：

```cpp
// ref_u8g2.cpp 的 unpack()
static void unpack() {
    const int tilesPerRow = 16;
    for (int ty = 0; ty < 16; ++ty)
        for (int tx = 0; tx < 16; ++tx) {
            const uint8_t* tile = g_buf + (ty * tilesPerRow + tx) * 8;
            for (int c = 0; c < 8; ++c)
                for (int r = 0; r < 8; ++r)
                    g_px[(ty * 8 + r) * 128 + (tx * 8 + c)] = (tile[c] >> r) & 1;
                    //                                            ↑       ↑
                    //                                       字节=列   位=行
        }
}
```

**怎么验证**：加一个 `probe` 模式，在 `(0,0)` 画一个点、在 `x=0..1, y=0..7`
画两个竖向 8 像素条，然后打印 `g_buf[0..3]`：

```
画 2x8 竖条后：g_buf[0] = 0xff, g_buf[1] = 0xff
```

如果布局是"字节=行"，应该是 `g_buf[0] = 0x03`（前两位为 1）。
实测 `0xff` 证明**字节=列**。

**这个格式不是 u8g2 随便定的**，它直接对应 SSD1306/ST7565 这类控制器的
显存组织方式 —— 页地址（page）找行、列地址找列。u8g2 让缓冲布局和控制器一致，
`SendBuffer` 就可以整块 DMA 出去，不用做任何转换。

代价是：**任何一个像素级操作都要算两次除法**。
`u8g2_DrawPixel` 内部：

```c
tile = buf + ((y >> 3) * tile_width + (x >> 3)) * 8;
tile[x & 7] |= 1 << (y & 7);
```

所以 `DrawIcon` 用 `u8g2_DrawBox` 而不是循环 `DrawPixel` ——
一个 3×3 的 box 内部会优化成位掩码操作，比 9 次 pixel 快得多。

### 8.7 自定义显示：`u8x8_display_info_t`

要接一块 u8g2 不认识的屏（比如 Sharp Memory LCD），有两种做法：

| 做法 | 工作量 | 适用 |
|---|---|---|
| 写完整的 `u8x8_d_xxx.c` 驱动 | 大（要实现 INIT/SET_POWER_SAVE/DRAW_TILE 等十几个消息） | 屏会被多处复用 |
| **自定义 display_info + 自己推缓冲** | 小 | 单一项目 |

本复刻版用第二种。关键是 `u8x8_display_info_t` 的两个字段：

```cpp
static const u8x8_display_info_t kMlcdInfo = {
    /* chip_enable_level */ 0,
    /* chip_disable_level */ 1,
    /* post_chip_enable_wait_ns */ 0,
    /* pre_chip_disable_wait_ns */ 0,
    /* reset_pulse_width_ms */ 0,
    /* post_reset_wait_ms */ 0,
    /* sda_setup_time_ns */ 0,
    /* sck_pulse_width_ns */ 0,
    /* sck_clock_hz */ 1000000UL,
    /* spi_mode */ 0,
    /* i2c_bus_clock_100kHz */ 4,
    /* data_setup_time_ns */ 0,
    /* write_pulse_width_ns */ 0,
    /* tile_width */  16,     // ← 128 / 8
    /* tile_height */ 16,     // ← 128 / 8
    /* default_x_offset */ 0,
    /* flipmode_x_offset */ 0,
    /* pixel_width */  128,
    /* pixel_height */ 128
};
```

**`tile_width` / `tile_height` 必须是像素尺寸除以 8**，否则 u8g2 算缓冲大小会错，
`u8g2_SetupBuffer` 会写越界。这是最容易错的一处。

然后：

```cpp
u8g2_SetupDisplay(&g_g2, mlcdDisplayCb, u8x8_cad_001, u8x8_byte_empty, gpioCb);
u8g2_SetupBuffer(&g_g2, g_u8g2Buf, kTilesY, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);
```

`mlcdDisplayCb` 大部分消息直接返回 1（什么都不做），只在
`U8X8_MSG_DISPLAY_SETUP_MEMORY` 时调 `u8x8_d_helper_display_setup_memory`
把 `kMlcdInfo` 塞进去：

```cpp
static uint8_t mlcdDisplayCb(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr) {
    switch (msg) {
    case U8X8_MSG_DISPLAY_SETUP_MEMORY:
        u8x8_d_helper_display_setup_memory(u8x8, &kMlcdInfo);
        return 1;
    case U8X8_MSG_DISPLAY_INIT:
        u8x8_d_helper_display_init(u8x8);
        return 1;
    default:
        return 1;      // SET_POWER_SAVE / DRAW_TILE 等全部忽略
    }
}
```

**`DRAW_TILE` 全忽略**是因为我们不走 u8g2 的推屏路径，而是在主循环里
自己拿 `u8g2_GetBufferPtr()` 解包后按 MLCD 协议发出去。
这样做的好处是可以完全控制字节格式（MLCD 要的是"行 + 每行 16 字节 + 行尾 0x00"），
不用和 u8x8 的协议层打架。

### 8.8 `u8g2_SetupBuffer` 的参数

```c
void u8g2_SetupBuffer(u8g2_t *u8g2, uint8_t *buf,
                      uint8_t tile_buf_height,
                      u8g2_uint_t buf_mode,
                      const u8g2_cb_t *u8g2_cb);
```

| 参数 | 本复刻版 | 说明 |
|---|---|---|
| `buf` | `g_u8g2Buf` | **必须 ≥ `tile_height * tile_width * 8` 字节** |
| `tile_buf_height` | `16` | 缓冲里存几行图块。等于屏高/8 就是全屏缓冲 |
| `buf_mode` | `u8g2_ll_hvline_vertical_top_lsb` | 第 8.6 节的布局；**唯一必须记住的枚举** |
| `u8g2_cb` | `U8G2_R0` | 旋转 0 度。`R1/R2/R3` 是 90/180/270 |

`tile_buf_height` 可以小于屏高，做成"分页渲染"省 RAM。
比如 `tile_buf_height = 2` 只需 `2*16*8 = 256` 字节，但要自己循环调用
`u8g2_FirstPage` / `u8g2_NextPage`。128×128 全屏才 2KB，没必要省。

### 8.9 UTF-8 vs `DrawString`

u8g2 有两套画字符串的 API：

| API | 输入编码 | 字形查找 |
|---|---|---|
| `u8g2_DrawStr` | Latin-1（单字节） | 直接查 `start_pos_unicode` |
| `u8g2_DrawUTF8` | UTF-8（变长） | 先解码 UTF-8 再查 |

**必须用 `u8g2_DrawUTF8`**，即使全是 ASCII —— 因为 `getUTF8Width` 和
`DrawUTF8` 必须是同一套编码，混用会导致右对齐算错宽度。

MonoUI 里所有文字路径都走 UTF-8：

```cpp
void MonoUI::DrawText(int x, int y, const char* s) {
    u8g2_SetFont(g2_, fontList_);
    u8g2_DrawUTF8(g2_, (int16_t)x, (int16_t)y, s);
}

int MonoUI::TextWidth(const char* s) {
    u8g2_SetFont(g2_, fontList_);
    return (int)u8g2_GetUTF8Width(g2_, s);
}
```

`DrawTextClipped` 逐字节画那里有个**已知缺陷**：它按字节遍历，
遇到多字节 UTF-8 会拆散。当前字库是纯 ASCII（95/191 字形里没有中文），
所以还没暴露。**要显示中文必须改成按 UTF-8 码点遍历**：

```cpp
// 正确做法（当前未实现，因为用不上）
const char* p = s;
while (*p) {
    int len = (*p & 0x80) == 0    ? 1
            : (*p & 0xE0) == 0xC0 ? 2
            : (*p & 0xF0) == 0xE0 ? 3 : 4;
    char one[5] = {0};
    memcpy(one, p, len);
    if (cx + width > maxX) break;
    u8g2_DrawUTF8(g2_, cx, y, one);
    cx += width;
    p += len;
}
```

### 8.10 三个必须知道的坑（总结）

| # | 坑 | 症状 | 判据 |
|---|---|---|---|
| 1 | RLE `flag=1` 是**重复同一对**，不是读新对 | 字形下半截错位，像"上下颠倒" | `[end]` 追踪显示跨行太快 |
| 2 | 顶边 = `基线 - (高 + y)`，`y` 是负的 | 整段文字下移一个字高，掉出屏幕 | 上半截全空 |
| 3 | 图块内**字节=列、位=行** | 整屏碎成竖条纹 | 画 2×8 竖条后 `buf[0]=0xff` |

三条都不是"读文档能避开"的 —— 必须靠对拍。这就是第 11 节存在的原因。

---

## 9. 资源格式

### 9.1 字体

| 资源 | 地址 | 字节数 | 字形 | 说明 |
|---|---|---|---|---|
| `kFontData6x12` | `0x08079800` | 2028 | 191 | 列表正文，`ascent 7 / descent -2` |
| `kFontData5x12` | `0x08079FEC` | 972 | 95 | 窄体，`max_w 5` |
| 32×38 大数字 | — | 67KB | — | **未内置**，太大 |

`kFontBig` 当前指向 `kFontData6x12`。要用大数字读数，在 `resources.cpp` 里
替换成自己用 `bdfconv` 生成的字体：

```bash
bdfconv -f 1 -m '0-9.:' -n my_font_32x38 my.bdf -o my_font.c
```

**数据完整性已验证**：`kFontData6x12` 的 2028 字节与 `ghidra-work/out/res_fonts.inc`
逐字节一致；解码结果与官方 u8g2 **逐像素 0 差异**。

### 9.2 图标：14×14 1bpp

```
字节 0:     边长 n（实测 14）
字节 1.. :  位图，stride = (n+7)/8 = 2 字节/行
            共 n * stride = 14 * 2 = 28 字节
总长:       1 + 28 = 29 字节
```

位序 **MSB-first**（最高位是最左边的像素）：

```cpp
const uint8_t b = icon[1 + r * stride + (c >> 3)];
if ((b >> (7 - (c & 7))) & 1) ...   // ← 7 - (c&7)，MSB 优先
```

**极性是负片**（第 3.8 节）：位 1 = 背景，位 0 = 笔画。

八枚图标的地址连续：`0x08078AB4` .. `0x08078C28`，间距 29 字节（有对齐填充）。

| 索引 | 名称 | 视觉 |
|---|---|---|
| `kIcon0` | Chip | 芯片（四边引脚） |
| `kIcon1` | Globe | 圆环 / 地球 |
| `kIcon2` | Connector | 双触点连接器 |
| `kIcon3` | Arrow | 左下箭头 |
| `kIcon4` | Path | 回形路径 |
| `kIcon5` | Stair | 阶梯波形 |
| `kIcon6` | Noise | 噪点 / 网格 |
| `kIcon7` | Nav | 四向导航 |

"视觉"这一列是从位图印出来看出来的，不是挖到了字符串 ——
固件里没有图标名字表。

### 9.3 状态图标：10×10 裸位图

```
无长度头，固定 20 字节（stride = 2）
极性：正片（位 1 = 墨迹）
```

**和列表图标极性相反** —— 因为状态图标是直接画在白底上的小标记，
不是镂空图案。用 `DrawRawBmp` 而不是 `DrawIcon`：

```cpp
void MonoUI::DrawRawBmp(const uint8_t* bmp, int x, int y, int w, int h) {
    const int stride = (w + 7) >> 3;
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) {
            const uint8_t b = bmp[r * stride + (c >> 3)];
            if ((b >> (7 - (c & 7))) & 1)          // ← 1 = 墨迹，直接画
                u8g2_DrawPixel(g2_, x + c, y + r);
        }
}
```

地址 `0x08079690`，三枚连续。

---

## 10. 移植指南

### 10.1 只有三个接缝

框架层完全不碰硬件。要换平台，只需实现：

| 接缝 | 做什么 | 在哪 |
|---|---|---|
| **显示** | 初始化 `u8g2_t`，提供缓冲 | `port/mono_port_esp32.cpp: initDisplay()` |
| **输入** | 报告按键电平 | `MonoInput::SetKey(k, down, millis())` |
| **时间** | 提供毫秒时间戳 | `MonoUI::SetMillisSource(fn)` |

除此之外**不需要改框架任何一行**。这是分层的价值。

### 10.2 ESP32-S3 + Sharp Memory LCD

```cpp
void app_main() {
    initHardware();       // SPI + GPIO
    initDisplay();        // u8g2 双初始化
    MonoUI::SetInstance(&g_ui);
    MonoUI::SetMillisSource([]() -> uint32_t {
        return (uint32_t)(esp_timer_get_time() / 1000);
    });
    g_ui.SetFonts(MonoRes::kFontData6x12, MonoRes::kFontBig);
    g_ui.Begin();

    for (;;) {
        pollInput();                  // 编码器 + 按键 → SetKey
        g_ui.Tick(millis());
        g_ui.Frame();
        mlcdSendFrame(u8g2_GetBufferPtr(&g_g2));
        vTaskDelay(pdMS_TO_TICKS(16));   // ≈60fps
    }
}
```

**编码器处理**（`pollInput()`）：A 相下降沿读 B 相判方向，
转成一对 `KEY_UP` 按下 + 松开：

```cpp
if (aFalling) {
    const bool ccw = (bLevel != 0);
    const MonoInput::Key k = ccw ? MonoInput::KEY_UP : MonoInput::KEY_DOWN;
    g_input.SetKey(k, true,  now);
    g_input.SetKey(k, false, now + 1);    // +1ms 造一个"按下并松开"
}
```

`now + 1` 是为了让去抖逻辑看到一个完整的电平跳变。直接
`SetKey(k, true, now); SetKey(k, false, now);` 时间戳相同，
去抖里的 `now - changedAt > 8` 判定会失效。

### 10.3 帧数据解包

MLCD 的协议是：每行 = `0x01`（写行命令）+ 16 字节像素 + `0x00`（行尾）。
像素格式和 u8g2 图块不同，必须转：

```cpp
// 图块布局：字节=列、位=行  →  MLCD 布局：字节的行内位序 MSB-first
const uint8_t* rowBase = tiles + ty * kTilesX * 8;
for (int b = 0; b < kWidth / 8; ++b) {
    const uint8_t* col = rowBase + b * 8;      // 8 个字节 = 8 列
    uint8_t v = 0;
    for (int c = 0; c < 8; ++c)
        if ((col[c] >> r) & 1) v |= (uint8_t)(0x80 >> c);
    line[1 + b] = v;
}
```

`(col[c] >> r) & 1` 取出第 `c` 列第 `r` 行，`0x80 >> c` 放到 MLCD 的
第 `c` 位（MSB-first）。**两层位序都要转**，少转一层就是 8 个像素的镜像。

### 10.4 两个踩过的坑

**坑 1：MSYS2 下 `ninja` 静默失败**

现象：`ninja` 报 `FAILED`，但 stdout/stderr 都是空的，`echo $?` = 1。

根因：`cc1plus.exe` 找不到 `libisl-23.dll`，Windows 返回
`0xC0000135`（`STATUS_DLL_NOT_FOUND`），Git Bash 把它吞了。

验证：
```powershell
# PowerShell 里直接调，才能看到真实退出码
& "C:\msys64\ucrt64\lib\gcc\x86_64-w64-mingw32\*\cc1plus.exe" --version
$LASTEXITCODE   # → -1073741515 = 0xC0000135
```

解决：
```bash
export PATH="/c/msys64/ucrt64/bin:$PATH"
```

**坑 2：ESP-IDF 下 `RLCD_USE_SIMULATOR` 要跟着工具链走**

模拟器用 SDL3（CMake 设 1），真机用 ESP-IDF（设 0）。
切换不能靠手动改，否则忘了改就是一堆链接错误。

### 10.5 语言标准与上板编译选项

#### 各平台的实际默认值

一个常见误解是"MCU 只支持 C++11，所以框架必须迁就"。实测并非如此：

| 平台 | 工具链 | 默认标准 | 证据 |
|---|---|---|---|
| STM32 | `arm-none-eabi-g++ 13.3.1`（STM32CubeCLT 1.20） | **C++17** | `-dM -E -x c++ /dev/null` → `__cplusplus 201703L` |
| ESP32-S3 | ESP-IDF v6.0 → GCC 15.1.0 | **`-std=gnu++23`** | 官方《C++ Support》明确写默认 gnu++23 |
| PC 验收 | MSYS2 g++ 16.2 | C++17 | 本工程 `CMAKE_CXX_STANDARD 17` |

原固件停在 C++11 是**时间原因**：REF-STM32F4-fw 成于 2017 年前后，
当时 arm-none-eabi-gcc 主流是 4.9/6.3，C++17 标准当年才定稿，
GCC 要到第 8 版（2018）才完整支持。作者不是"不敢用"，是"当年没有"。

#### 框架整体的实测开销

```bash
arm-none-eabi-g++ -std=c++17 -mcpu=cortex-m4 -mthumb -Os \
    -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
    -Ithird_party/u8g2_stub -Imono_ui -I. -c mono_ui/*.cpp
```

| 文件 | .text | .data | .bss | 说明 |
|---|---|---|---|---|
| `mono_ui.cpp` | 4944 | 0 | 8 | 绘制主体 |
| `mono_page.cpp` | 661 | 4 | 644 | 644 B = 16 个 `MonoPage` 静态池 |
| `mono_input.cpp` | 379 | 0 | 0 | |
| `resources.cpp` | 3405 | 0 | 0 | 字库 2028+972，图标 8×32，状态 3×20 |
| `mono_user_app.cpp` | 1604 | 32 | 184 | 应用层示例 |
| **合计** | **10993** | **36** | **836** | ≈ 11 KB Flash + 872 B RAM |

对照 STM32F103C8（128 KB Flash / 64 KB RAM）：占 **8.4% Flash / 1.3% RAM**。

#### 哪些写法会拖运行库（`nm -u` 实测）

**零未定义符号**（纯头文件模板，只产出内联代码）：

`std::array`、`std::optional`、`std::variant`、`std::get_if`、`std::visit`、
`if constexpr`、`std::is_*`、`constexpr` 函数

**会拖**：

| 写法 | 未定义符号 | 后果 |
|---|---|---|
| `std::string` | `_M_create`→`operator new`、`_M_dispose`→`operator delete`、`_S_copy_chars` | 需要堆，有碎片风险 |
| `std::vector` | `operator new` / `operator delete` | 同上，且扩容会搬移 |
| `std::function` | `__throw_bad_function_call`、`operator new` | 捕获超 16 字节就走堆 |
| 函数局部 `static`（非平凡构造） | `__cxa_guard_acquire` / `__cxa_guard_release` | 每次进函数查一次守卫标志 |
| 全局对象（非平凡析构） | `__dso_handle`、`__cxa_atexit` | 需要 `.init_array` + atexit 链 |
| `snprintf` | newlib 格式化代码 | 单次引入 6~10 KB（`newlib-nano` 可减半） |

本框架的有意取舍：

- **`MonoItem` 用属性位打包，不用 `std::variant`** —— 8 种 type 共享绝大多数字段，
  variant 按最大成员算尺寸，反而浪费
- **`MonoWidget` 用一个类 + `Kind` tag + 静态池**（`kPoolSize = 4`），
  不用多态基类 —— 覆盖层要支持多层叠加，静态池 + tag 比虚函数表更省
- **文本一律 `const char*`** —— 字符串全是编译期字面量，没有拼接需求
- **`new MonoItem()` 保留**（照固件原样），只在 `InitLayout` 里跑一次、运行期不释放

注意 `MonoWidget::pool()` 是**函数内静态数组** —— 这正是 `__cxa_guard` 的来源。
它在第一次弹窗时才会初始化，属于"用到才建"，代价是每次进函数多一次守卫检查。

#### 推荐的编译选项

```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

target_compile_options(app PRIVATE
    -fno-exceptions            # 无 OS 环境没人 catch，异常表白占 Flash
    -fno-rtti                  # 不用 dynamic_cast/typeid
    -fno-threadsafe-statics    # 去掉 __cxa_guard_*：InitLayout 单任务跑一次
    -fno-use-cxa-atexit        # 去掉 __dso_handle：嵌入式程序不会正常 return
    -ffunction-sections -fdata-sections
    -Os)
target_link_libraries(app PRIVATE -Wl,--gc-sections)
```

实测后两个选项的效果：

```
                     .text    __cxa_guard/__dso_handle 符号数
默认                 10993    3
加上这两个选项       10965    0
```

省下的 28 字节本身不算什么，**关键是这两个符号消失后不必再链接它们的实现**：
`__cxa_guard_acquire` 在 libstdc++ 里要接一层线程原语，
`__cxa_atexit` 要维护函数指针链表。裸机上这两个都得配 stub 才不报链接错误。

ESP-IDF 侧：

```cmake
idf_component_register(SRCS "mono_ui" "port" REQUIRES ...)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
```

ESP-IDF 默认 `gnu++23`，显式钉到 17 只为和 STM32 侧统一；
实测同一份测试用例在 `c++17` / `c++20` / `c++23` 下 `.text` 完全相同（496 字节）。

#### `-fno-exceptions` 下 `std::variant` 还安全吗

安全，但失败路径的语义变了：

- `std::get<T>(v)` 类型不匹配 → 调 `std::__throw_bad_variant_access()`，
  `-fno-exceptions` 下该函数**直接 abort**
- `std::get_if<T>(&v)` 不匹配 → 返回 `nullptr`，**不 abort**

所以框架里只用 `get_if` + 判空，不用 `get`。这条纪律在 PC 上看起来多余，
但在板子上差别是"能恢复"和"死循环"。

---

## 11. 验收方法论

### 11.1 三级验证链

这个项目能修好，靠的是把"哪里错了"变成可判定问题。三级结构：

```
第一级：固件字节                     ref_font_6x12_tr（标准 u8g2 字库）
   │  数据对不对？                        │
   ▼                                      ▼
第二级：手写解码器 test/u8g2_sim.cpp   官方 u8g2 test/ref_u8g2.cpp
   │                                      │
   └────────── 逐像素对拍 ────────────────┘
                    │
                    ▼
第三级：完整界面 test/render_test.cpp（ASCII + BMP）
```

每一级都能独立判定：

| 级别 | 判定方法 | 通过标准 |
|---|---|---|
| 数据 | Node 脚本全量校验 191 个字形头 | 每个字形都能完整解码，不越界 |
| 解码 | `alpha` 模式对拍 | `cmp` 差异 0 字节 |
| 界面 | 8 个场景的 ASCII 输出 | 肉眼可读，无叠字 |

### 11.2 `ref_test` 的用法

```bash
# 指向本机 u8g2 的 csrc
cmake -B build-ref -G Ninja -DU8G2_CSRC=/e/Dev/ESP-IDF/u8g2-dl/csrc
cmake --build build-ref

./build-ref/ref_test probe          # 验证缓冲位布局（画 2x8 竖条）
./build-ref/ref_test glyph A        # 单字形，用固件字库
./build-ref/ref_test glyph A std    # 单字形，用标准 u8g2_font_6x12_tr
./build-ref/ref_test alpha          # 全 ASCII 铺屏
./build-ref/ref_test alpha std      # 同上，标准字库
```

**`std` 参数的意义**：拿标准字库跑同一套参照程序。
如果 `std` 输出正确而 `fw`（固件字库）不正确，说明是**数据问题**；
如果两个都不正确，说明是**参照程序问题**。这一步把"数据"和"代码"分开了 ——
第 8.4 节排查的第 3 步就是靠这个发现"官方输出也是乱的"。

### 11.3 对拍结果

```bash
$ ./build/render_test alpha
已输出 alpha.bmp

$ ./build-ref/ref_test alpha
已输出 ref_alpha.bmp

$ cmp -l alpha.bmp build-ref/ref_alpha.bmp | wc -l
0

$ md5sum alpha.bmp build-ref/ref_alpha.bmp
554fd2eb072ca8df23bb9f2cdce3464d *alpha.bmp
554fd2eb072ca8df23bb9f2cdce3464d *build-ref/ref_alpha.bmp
```

**MD5 相同** —— 262144 个像素（512×512 @ 4 倍放大）逐个全等。
手写解码器与官方实现完全一致。

### 11.4 界面场景的判定

`render_test` 的 9 个场景输出状态行。下面是**真实运行输出**（`t` 是模拟时钟）：

```
=== pageMain  (focus: Show Widgets)              (t= 704ms) ===
    [page=0 "MonoUI" mode=0 items=5 focus=0/pos=0.00]
=== pageMain  (focus: Icon Mode)                 (t=1480ms) ===
    [page=0 "MonoUI" mode=0 items=5 focus=1/pos=1.00]
=== pageIcon  ICON 模式                          (t=2256ms) ===
    [page=2 "Icon Mode" mode=1 items=8 focus=0/pos=0.00]
=== pageIcon  轮播到第 3 项                       (t=3104ms) ===
    [page=2 "Icon Mode" mode=1 items=8 focus=2/pos=2.00]
=== pageMain  按 BACK 返回（焦点恢复到 1）        (t=3432ms) ===
    [page=0 "MonoUI" mode=0 items=5 focus=1/pos=1.00]
=== pageShow  Show Widgets                       (t=4664ms) ===
    [page=1 "Show Widgets" mode=0 items=8 focus=0/pos=0.00]
=== 覆盖层  MsgBox                                (t=5320ms) ===
    [page=1 "Show Widgets" mode=0 items=8 focus=0/pos=1.00]
=== pageShow  Checkbox 行                        (t=5992ms) ===
    [page=1 "Show Widgets" mode=0 items=8 focus=3/pos=3.82]
=== pageShow  Checkbox1 勾选后                    (t=6320ms) ===
    [page=1 "Show Widgets" mode=0 items=8 focus=3/pos=3.99]
```

三处值得对照代码看：

**① 第 5 行验证了焦点记忆** —— 从 Icon Mode 返回主页面，焦点回到第 1 行
（`savedFocus` 生效），而不是回到第 0 行。

**② 第 7 行的 `focus=0` 配 `pos=1.00`** —— 这不是 bug，是**截断**的直接体现。
那一刻 `pos ≈ 0.9997`，`printf("%.2f")` 四舍五入显示 `1.00`，
但 `(int)0.9997 = 0`，所以焦点还没切过去。这一帧上色块仍停在第 0 行。

**③ 第 8 行 `pos=3.82` 是"没跑够帧"** —— 连按 3 次 DOWN 后只步进 16 帧。
按第 4.3 节的曲线，`target=4`、起点 `1.0`，16 帧走到 `1 + 0.93*3 ≈ 3.79`，
实测 `3.82`（因为起点略高于 1.0）。再步进 16 帧就到 `3.99`。

**为什么最终是 `3.99` 而不是 `4.00`**：`kSnap` 阈值是 `0.001`，
`pos` 只要落在 `[3.999, 4.001]` 就算收敛。`%.2f` 显示 `3.99`
说明那一刻还在 `3.99x` 区间，下一帧才会被吸附成整数 `4.0`。
要看到 `4.00` 得多步进 1~2 帧。

**验收的正确姿势**：不要只看 `pos` 的两位小数，要看
`focus` 是否等于按键次数、`page` 是否等于预期页、`items` 是否等于 `AddItem` 的条数。
`pos` 只用于判断"动画在不在动"。

每帧还会存一张 `shot_XX.bmp`（4 倍放大），肉眼检查行高、缩进、前缀、色块、
图标位置。ASCII 看不出圆角和图标形状，BMP 才能。

---

## 12. 已知偏差

以下几处是**有意**偏离固件常量的，改回去只需动一行。诚实记录在这里，
免得后来者以为抄错了。

| 位置 | 固件 | 本复刻版 | 原因 | 改回去的后果 |
|---|---|---|---|---|
| 行内右侧控件右边界 | `0x77` = 119 | `kWidgetRight = 96` | 119 落在选中行右侧色块（100..132）里，会被整块盖住 | `OFF` 只看到 `O`，数值看不见，进度条全黑 |
| 标签过长 | 直接画 | `DrawTextClipped` 逐字截断 | 128px 放不下 "Progress Bar"(72px) + 进度条(32px) | 文字压在进度条上 |
| 复选框位置 | 位置未知 | 从 `x = 10` 起画，标签 `+12` | 原代码 `bx = x - 7 = 3` 会探进左侧色块（0..9） | 勾选框被切掉一半 |
| 跑马灯终点 | `0x5A` 参与减法，符号不确定 | `offset <= 90 - textWidth` | 几何推导（第 4.4 节），保证"尾部刚消失"就停 | 写成 `-90 - w` 会空跑十几秒 |

**为什么是"96"而不是别的数**：右色块从 100 开始，留 4px 间隙 → 96。
这个数不是猜的，是 `kScrollBarX - 4`。

**为什么复选框不学固件往左探**：左边 0..9 是选中色块的绝对领地。
一块 9×9 的勾选框要完整显示，起点必须 ≥ 10。而 `kPadItem = 10` 正好。

---

## 13. 固件地址对照表

加载基址 `0x08008000`。所有地址都是 Cortex-M4 Thumb，函数入口地址末位为 1（已省略）。

| 地址 | 内容 | 本复刻版对应 |
|---|---|---|
| `0x08011C68` | `MonoItem` 构造函数 | `MonoPage::AddItemRaw` |
| `0x08012028` | 页面构造函数 | `MonoPage::AddPage` |
| `0x08014298` | **通用列表渲染器**（最大函数，8 种 item 的分支） | `MonoUI::drawRow` |
| `0x080145A8` | 行坐标计算 `(row*16+1)*slide+1` | `drawRow` 的 `y` |
| `0x0801453C` | `kIconCell = 56` | `MonoLayout::kIconCell` |
| `0x08014540` | `kIconOriginY = -69` | `MonoLayout::kIconOriginY` |
| `0x08015BC8` | 帧循环（清屏→分派→覆盖层→提交） | `MonoUI::Frame` |
| `0x08033EFC` | `TickTask`（20ms 输入采样 + 长按） | `MonoInput::Update` |
| `0x08038E78` | **弹簧积分** | `MonoSpring::step` |
| `0x080393EC` | 动画常量池（4 个 float） | `MonoAnim::k*` |
| `0x08079800` | `kFontData6x12`（2028 字节） | `MonoRes::kFontData6x12` |
| `0x08079FEC` | `kFontData5x12`（972 字节） | `MonoRes::kFontData5x12` |
| `0x08078AB4` | `kIcon0`（图标区起始） | `MonoRes::kIcon0` |
| `0x08079690` | 状态图标区（10×10 裸位图） | `MonoRes::kStatusBox` |
| `0x08038xxx` | 应用层（`InitLayout` 调用点） | `mono_user_app.cpp` |
| `0x20014590` | **MonoUI 单例** | `MonoUI::s_inst_` |
| `0x20014590+0x12C` | `pageCount` (int) | `MonoPage::s_count_` |
| `0x20014590+0x130` | `cap` (int) | `MonoPage::kMaxPages` |
| `0x20014590+0x134` | 页面指针数组 | `MonoPage::s_pool_` |

`MonoItem` 字段偏移：

| 偏移 | 字段 | 本复刻版 |
|---|---|---|
| `+0x08` | `type`（uint8） | `MonoItem::type` |
| `+0x0C` | `text`（`std::string`，SSO 0x20） | `const char* text` + `textBuf[48]` |
| `+0x24` | `value`（float） | `MonoItem::value` |
| `+0x3C` | `iconSrc` | `MonoItem::iconSrc` |
| `+0x40` | `callback` | `MonoItem::callback` |
| `+0x44` | `param` / `payload` | `MonoItem::param1` |

**唯一的实质差异**：原版 `text` / `value` 是 `std::string`（带 SSO，容量 0x20）。
本复刻版改成 `const char* + 定长缓冲`，每项省 40+ 字节堆，且无碎片风险。
语义等价 —— 描述文本基本都是字面量，指过去即可；需要动态内容时用
`SetTextCopy()` 走 `textBuf`。

---

## 附录 A：从零复刻清单

如果要在一个全新平台上从零搭这套东西，按这个顺序：

1. **先跑通 u8g2**
   - `u8g2_SetupDisplay` + `u8g2_SetupBuffer`
   - 用 `u8g2_DrawBox(0,0,16,16)` 确认能画出方块
   - **用 `probe` 方法验证缓冲布局**（第 8.6 节）—— 这一步别跳

2. **接字库**
   - 把字体数组贴进 `resources.cpp`
   - 打印字体头 23 字节，对照第 8.2 节的表逐字段核对
   - `u8g2_SetFont` + `u8g2_DrawUTF8("Ag", 0, 20)`，确认字形完整

3. **搭框架骨架**
   - `MonoLayout` 常量（第 3.2 节）
   - `MonoSpring`（第 4.2 节，**常数照抄，别优化**）
   - `MonoUI::Frame` 空实现 + `u8g2_SendBuffer`

4. **页面与条目**
   - `MonoPage::AddPage` 静态池
   - `AddItem` 六参数模板 + `monoToUptr`
   - `renderList` + `drawRow`，先只画 `ITEM_TEXT`

5. **输入**
   - `MonoInput` 去抖 + 长按
   - `MoveFocus` 用 `TargetIndex()` 当基准（第 5.3 节）

6. **补齐八种 item**
   - 每加一种，跑一次 `render_test` 看 ASCII
   - **注意右边界 `kWidgetRight`**（第 12 节）

7. **覆盖层**
   - `MonoWidget` 对象池
   - `MsgBox` 先折行再定高（第 7.3 节）

8. **ICON 模式**
   - 几何（第 3.7 节）
   - **图标负片极性**（第 3.8 节）—— 先画一枚看对不对

9. **对拍验收**
   - `ref_test` vs `render_test`，`cmp` 必须 0

---

## 附录 B：文件规模与依赖

```
248  mono_ui/mono_defs.h        常量、ItemType、MonoSpring、MonoMarquee
100  mono_ui/mono_item.h        列表项
111  mono_ui/mono_page.h        页池声明、AddItem 模板
120  mono_ui/mono_page.cpp      页池实现、参数解释
165  mono_ui/mono_ui.h          核心类声明
631  mono_ui/mono_ui.cpp        帧循环、三种渲染器、导航、绘制工具
 73  mono_ui/mono_input.h       输入声明
112  mono_ui/mono_input.cpp     去抖、长按、重复
502  mono_ui/mono_widget.h      五种覆盖层 + 对象池（单头文件实现）
 72  mono_ui/resources.h        资源声明
323  mono_ui/resources.cpp      字库 + 图标数据
146  mono_ui/mono_user_app.cpp  ★ 应用层
122  uvector/uvector.h          极简容器
235  port/mono_port_esp32.cpp   ESP32-S3 + MLCD 移植
196  test/ref_u8g2.cpp          官方参照
183  test/render_test.cpp       界面验收
393  test/u8g2_sim.cpp          手写 u8g2 后端 + 字体解码器
 73  test/font_test.cpp         单字形测试
```

**框架总计约 2000 行**（不含测试与移植层）。这个规模是刻意的：
128×128 单色屏不值得用 LVGL（约 200KB Flash），
也不该每次都手写坐标逻辑（那才是真正的浪费）。

**依赖**：

| 依赖 | 用途 | 可否替换 |
|---|---|---|
| u8g2 | 字形解码、图形基本体 | 难（字体格式绑死） |
| 无 STL | `uvector` 替代 `std::vector` | 是 |
| 无 RTOS | 主循环 + `millis()` 即可 | 是 |

**u8g2 是唯一硬依赖**。如果目标平台已经有别的图形库（比如 emWin），
可以把 `DrawText` / `FillBox` / `DrawIcon` 这几个函数重写一遍接过去 ——
框架层只通过这 9 个函数和图形库打交道。

---

*本手册对应代码版本：`MonoUI-port/` build 通过、`render_test` 8 场景全绿、
`alpha.bmp` 与官方 u8g2 渲染 MD5 一致。*
