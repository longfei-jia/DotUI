// dotui_theme.h —— 主题（反色）控制
// 反色固定在显示驱动层实现，应用层只调用本接口，不感知后端细节
#ifndef DOTUI_THEME_H
#define DOTUI_THEME_H
#ifdef __cplusplus
extern "C" {
#endif
void dot_set_dark_mode(bool dark);   // false 浅色 / true 深色
#ifdef __cplusplus
}
#endif
#endif