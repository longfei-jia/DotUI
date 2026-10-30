// dotui_theme.h —— 主题（反色）控制
//
// 反色的实现位置与平台无关地固定在显示驱动层：
//   - PC 模拟器：SDL 显示驱动内的调色板切换（u8g_sdl_set_inverse）
//   - 真机：LS013B7DH03 等驱动层实现的反色
// 应用层（MonoUI 回调）只调用本接口，不感知任何后端细节。
#ifndef DOTUI_THEME_H
#define DOTUI_THEME_H

#ifdef __cplusplus
extern "C" {
#endif

// 切换显示反色：
//   false = 浅色（灰白底、黑像素，LS013B7DH03 反射屏观感）
//   true  = 深色（黑底、白像素）
void dotui_set_dark_mode(bool dark);

#ifdef __cplusplus
}
#endif

#endif // DOTUI_THEME_H
