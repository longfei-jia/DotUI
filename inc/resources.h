// resources.h —— 字体与图标资源
//
// PC 模拟器使用 u8g2 内置字体；需要逐像素对齐固件时，把这里换成
// doc/ultralink/mono_ui/resources.cpp 中的固件字库数据即可。
//
// 带注释的语言相关指针（kTextOn/kTextOff/kTextOk/kLanguage）是
// 可写变量：语言切换时由应用层改写其指向。
#ifndef MONO_RESOURCES_H
#define MONO_RESOURCES_H

#include <stdint.h>

namespace MonoRes {

// ---- 字体（u8g2 字体指针）----
extern const uint8_t* const kFontSmall;     // 列表正文 6x12
extern const uint8_t* const kFontBig;       // 大字 7x13B（图标页名称等）
extern const uint8_t* const kFontChinese;   // 中文 wqy12（约 202KB，仅 PC 用）

// ---- 语言 ----
extern int kLanguage;                       // 0 = English，1 = 中文

// ---- 图标：14x14，1bpp，首字节为边长，1 = 透明 ----
extern const uint8_t kIcon0[];
extern const uint8_t kIcon1[];
extern const uint8_t kIcon2[];
extern const uint8_t kIcon3[];
extern const uint8_t kIcon4[];
extern const uint8_t kIcon5[];
extern const uint8_t kIcon6[];
extern const uint8_t kIcon7[];

extern const uint8_t* const kIcons[8];      // 图标指针表
constexpr int kIconCount = 8;               // 图标数量
constexpr int kIconLogicalSize = 14;        // 图标原始边长

// ---- 文本常量 ----
extern const char* const kPrefixPage;       // 子页面前缀 "+"
extern const char* const kPrefixFunction;   // 动作项前缀 "-"
extern const char* kTextOn;                 // 开关"开"（随语言切换）
extern const char* kTextOff;                // 开关"关"（随语言切换）
extern const char* kTextOk;                 // 弹窗按钮（随语言切换）
extern const char* const kFormatValue;      // 数值格式化 "%.2f"

} // namespace MonoRes

#endif // MONO_RESOURCES_H
