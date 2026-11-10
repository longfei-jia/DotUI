#ifndef RESOURCES_H
#define RESOURCES_H

#include <stdint.h>

namespace DotRes {
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

// 64x64 默认图标（填充正方形）：图标项未指定 iconSrc 时的兑底
extern const uint8_t kIconDefault[513];

extern const char* const kPrefixPage;       // 子页面前缀 "+"
extern const char* const kPrefixFunction;   // 动作项前缀 "-"
extern const char* kTextOn;                 // 可写指针：语言切换时改指向
extern const char* kTextOff;
extern const char* kTextOk;
extern const char* const kFormatValue;  
}
#endif