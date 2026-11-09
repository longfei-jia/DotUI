// main.cpp —— DotUI 桌面模拟器入口
//
// 职责：
//   1. 初始化 u8g2 + SDL2 后端（命令行选择 128x128 / 144x168）
//   2. 把 SDL 按键事件转成 DotInput 的虚拟按键电平
//   3. 驱动 DotUI 的 Tick/Frame 循环
//   4. 提供深色模式切换（直接改 SDL 调色表）
//
// 说明：使用 VC 版 SDL2 且不链接 SDL2main，需 SDL_MAIN_HANDLED 接管入口。
#define SDL_MAIN_HANDLED

#include "u8g2.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "dot_defs.h"
#include "dot_input.h"
#include "resources.h"
#include "dot_page.h"
#include "dot_item.h"
#include "dot_ui.h"
#include "dot_theme.h"      // dot_set_dark_mode 声明（应用层 onDarkMode 会调用）

extern "C" void u8g2_SetupBuffer_SDL_128x128(u8g2_t *u8g2, const u8g2_cb_t *u8g2_cb);
extern "C" void u8g2_SetupBuffer_SDL_144x168(u8g2_t *u8g2, const u8g2_cb_t *u8g2_cb);
extern "C" int  u8g_sdl_get_key(void);     // SDL 事件泵 + 按键轮询
extern "C" void u8g_sdl_set_inverse(uint8_t dark);   // 显示后端反色接口
extern "C" uint8_t u8g_sdl_get_inverse(void);

u8g2_t    g_u8g2;
DotInput  g_input;
DotUI     g_ui(&g_u8g2, &g_input);

// 虚拟按键保持按下的截止时刻：
// SDL 只给按下事件，这里按住 60ms 再释放，让 DotInput 产生一次 EV_CLICK。
uint32_t g_pressUntil[DotInput::KEY_COUNT] = {0};

// SDL 键码 → DotInput 键
void queueKey(int key, uint32_t now) {
    DotInput::Key k = DotInput::KEY_NONE;
    switch (key) {
    case 273: k = DotInput::KEY_UP;    break;   // ↑
    case 274: k = DotInput::KEY_DOWN;  break;   // ↓
    case 276: k = DotInput::KEY_LEFT;  break;   // ←
    case 275: k = DotInput::KEY_RIGHT; break;   // →
    case 's': case ' ': k = DotInput::KEY_OK;   break;   // 确认
    case 'b': case 'q': k = DotInput::KEY_BACK; break;   // 返回（q 不退出）
    default: return;
    }
    g_pressUntil[k] = now + 60;
}

// 深色模式：直接调用显示后端的反色接口。
// 与真机一致——反色在显示驱动层实现，应用层只发开关命令，
// 不触碰调色表/窗口表面等后端内部状态。
//   false = 浅色（灰白底、黑像素，LS013B7DH03 反射屏观感）
//   true  = 深色（黑底、白像素）
void dot_set_dark_mode(bool dark) {
    u8g_sdl_set_inverse(dark ? 1 : 0);
}

int main(int argc, char *argv[]) {
    bool use144   = false;   // 144x168 分辨率
    bool debugNav = false;   // 自动上下移动（弹簧边界测试）

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "144x168") == 0 ||
            strcmp(argv[i], "--144x168") == 0) {
            use144 = true;
        }
        if (strcmp(argv[i], "--debug-nav") == 0) {
            debugNav = true;
        }
    }

    // 不链接 SDL2main 时必须先告知 SDL 主线程已就绪
    SDL_SetMainReady();

    // 按命令行选择显示后端
    if (use144)
        u8g2_SetupBuffer_SDL_144x168(&g_u8g2, &u8g2_cb_r0);
    else
        u8g2_SetupBuffer_SDL_128x128(&g_u8g2, &u8g2_cb_r0);

    u8x8_InitDisplay(u8g2_GetU8x8(&g_u8g2));
    u8x8_SetPowerSave(u8g2_GetU8x8(&g_u8g2), 0);   // 退出省电模式

    DotUI::SetInstance(&g_ui);
    DotUI::SetMillisSource([]{ return SDL_GetTicks(); });
    g_ui.SetFonts(DotRes::kFontSmall, DotRes::kFontBig);
    g_ui.Begin();          // InitLayout 由 dot_user_app.cpp 提供

    printf("[DotUI] display = %dx%d, rows = %d\n",
           DotLayout::kScreenW, DotLayout::kScreenH,
           DotLayout::kScreenH / DotLayout::kRowH);
    fflush(stdout);

    // ---- 主循环 ----
    bool quit = false;

    // 调试模式状态：每 700ms 移动一次，16 步后反向（覆盖滚动边界）
    uint32_t debugNext = SDL_GetTicks() + 800;
    int debugDir  = 1;
    int debugStep = 0;

    while (!quit) {
        const uint32_t now = SDL_GetTicks();

        // 调试模式：自动上下移动焦点
        if (debugNav && now >= debugNext) {
            debugNext = now + 700;
            g_ui.MoveFocus(debugDir);
            if (++debugStep >= 16) {
                debugStep = 0;
                debugDir  = -debugDir;
            }
        }

        // 取空 SDL 事件队列里的按键（这一步同时就是事件泵，
        // 缺了它窗口会"未响应"且键盘状态不刷新）
        int key;
        while ((key = u8g_sdl_get_key()) >= 0) {
            if (key != 0) queueKey(key, now);
        }

        // 维持虚拟按键电平：到期自动"松开"
        for (int i = DotInput::KEY_UP; i < DotInput::KEY_COUNT; ++i) {
            const bool down = now < g_pressUntil[i];
            g_input.SetKey((DotInput::Key)i, down, now);
        }

        // DotUI 内部完成事件消费与渲染
        g_ui.Tick(now);
        g_ui.Frame();

        SDL_Delay(16);   // ~60 FPS
    }

    return 0;
}
