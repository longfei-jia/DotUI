// resources.h —— 字体与图标资源
//
// 对应 3rdParty/mono_ui/resources.h。
//
// 这里的字库和图标都是从 UltraLink 固件里按字节 dump 出来的真数据，
// 不是占位图。目的是让复刻版一上手就能显示中文和图标，不需要你再去找 bdfconv。
//
// 格式备忘（实测，见技术手册第 10 节）：
//   字体  —— 标准 u8g2 字体，23 字节 u8g2_font_info_t 头 + 编码区 + Unicode 段
//   图标  —— 首字节 = 边长，stride = (n+7)/8，MSB-first，bit=1 画前景
//   状态  —— 10x10 **裸位图**，无长度头，stride=2，固定 20 字节
#ifndef MONO_RESOURCES_H
#define MONO_RESOURCES_H

#include <stdint.h>

namespace MonoRes {

// ---------------------------------------------------------------------------
// 字体（u8g2 字体结构，可直接传给 u8g2_SetFont）
// ---------------------------------------------------------------------------
extern const uint8_t kFontData6x12[];   // 191 字形，6x12，ascent 7 / descent -2
extern const uint8_t kFontData5x12[];   // 95 字形，5x12，窄体

extern const uint8_t* const kFontSmall;   // 列表正文，指向 6x12
extern const uint8_t* const kFontTiny;    // 窄体，指向 5x12
extern const uint8_t* const kFontBig;     // 大标题。默认复用 6x12，
                                          // 需要 32x38 大读数时在这里替换成自己的字体

// ---------------------------------------------------------------------------
// 图标：14x14 单色，8 枚
//
// 用法与固件一致：先铺一块实心圆角底板，再用 `bit ? fg : bg` 绘制图标，
// 图案会从底板里"镂空"出来。这也是为什么多数图标墨迹占比很高 ——
// 大部分位是底板，图案是挖白的线条。
// ---------------------------------------------------------------------------
extern const uint8_t kIcon0[];   // 芯片（四边引脚）
extern const uint8_t kIcon1[];   // 圆环 / 地球
extern const uint8_t kIcon2[];   // 双触点连接器
extern const uint8_t kIcon3[];   // 左下箭头
extern const uint8_t kIcon4[];   // 回形路径
extern const uint8_t kIcon5[];   // 阶梯波形
extern const uint8_t kIcon6[];   // 噪点 / 网格
extern const uint8_t kIcon7[];   // 四向导航

// 索引表，配合 MonoPage::ICON 模式的整型 iconId 用
extern const uint8_t* const kIcons[8];
constexpr int kIconCount = 8;

// 图标位图的原始边长（渲染时要按倍数放大到 42）
constexpr int kIconLogicalSize = 14;

// ---------------------------------------------------------------------------
// 状态图标：10x10 裸位图，行尾用
// ---------------------------------------------------------------------------
extern const uint8_t kStatusBox[];     // 带边框的电池/信号
extern const uint8_t kStatusDot[];     // 实心圆点
extern const uint8_t kStatusSquare[];  // 实心方块

// ---------------------------------------------------------------------------
// 文本常量（与固件一致，改这里就能改文案）
// ---------------------------------------------------------------------------
extern const char* const kPrefixPage;      // "+" 可进入子页
extern const char* const kPrefixFunction;  // "-" 纯动作
extern const char* const kTextOn;          // "ON"
extern const char* const kTextOff;         // "OFF"
extern const char* const kFormatValue;     // "%.2f"
extern const char* const kTextBack;        // "返回"

} // namespace MonoRes

#endif // MONO_RESOURCES_H
