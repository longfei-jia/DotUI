// DotUI 模拟器最小入口：u8g2 + SDL2，128x64 单色全帧缓冲
// 仅验证模拟环境，UI 框架逻辑后续再接入

// 使用 VC 版 SDL2 且不链接 SDL2main，需要自己接管入口初始化
#define SDL_MAIN_HANDLED

#include "u8g2.h"
#include <SDL.h>

// u8g2 的 SDL 后端提供的符号（不在 u8g2.h 中声明，C++ 里必须显式声明）
extern "C" int u8g_sdl_get_key(void);
extern "C" void u8g2_SetupBuffer_SDL_128x128(u8g2_t *u8g2, const u8g2_cb_t *u8g2_cb);

namespace {

u8g2_t g_u8g2;

// 绘制一帧测试画面
void render(u8g2_t *u) {
    u8g2_ClearBuffer(u);

    // 画边框，方便确认 128x128 的边界
    u8g2_DrawFrame(u, 0, 0, 128, 128);

    u8g2_SetFont(u, u8g2_font_7x13B_tr);
    u8g2_DrawStr(u, 10, 60, "Hello World!");

    u8g2_SetFont(u, u8g2_font_6x10_tf);
    u8g2_DrawStr(u, 10, 80, "DotUI u8g2 SDL");

    u8g2_SendBuffer(u);
}

} // namespace

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    // 不链接 SDL2main 时，必须先告诉 SDL 主线程已就绪
    SDL_SetMainReady();

    // 初始化 128x128 全帧缓冲，SDL 窗口由 u8g2 后端创建
    u8g2_SetupBuffer_SDL_128x128(&g_u8g2, &u8g2_cb_r0);
    u8x8_InitDisplay(u8g2_GetU8x8(&g_u8g2));
    u8x8_SetPowerSave(u8g2_GetU8x8(&g_u8g2), 0);

    bool quit = false;
    while (!quit) {
        // 读取按键，q 退出（关闭窗口由 SDL 后端处理）
        int key = -1;
        while ((key = u8g_sdl_get_key()) >= 0) {
            if (key == 'q') {
                quit = true;
            }
        }

        if (quit) {
            break;
        }

        render(&g_u8g2);
        SDL_Delay(33); // 约 30 FPS
    }

    return 0;
}
