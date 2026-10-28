// mono_ui.h —— MonoUI 核心
//
// 对应 3rdParty/mono_ui/mono_ui.h。
//
// 分工（从截图反推）：
//   mono_ui.h/cpp     —— 框架：帧循环、渲染、导航、动画、覆盖层
//   mono_user_app.cpp —— 应用：InitLayout / HomeScreen / SaveConfig + 业务回调
//
// 注意 InitLayout/HomeScreen/SaveConfig 三个函数**只有声明没有定义**。
// mono_ui.cpp 会调用它们，但实现必须由 mono_user_app.cpp 提供 ——
// 少了 app 层就链接不过，这正是"框架"与"应用"的强制边界。
#ifndef MONO_UI_H
#define MONO_UI_H

#include "mono_defs.h"
#include "mono_page.h"
#include "mono_item.h"
#include "mono_input.h"
#include "u8g2.h"

class MonoUI {
public:
    static constexpr int kMaxOverlays = 4;

    // =====================================================================
    // 构造 / 单例
    // =====================================================================
    MonoUI(u8g2_t* g2, MonoInput* input);

    // 时间源。Run() 需要它；只调 Tick(now)/Frame() 的话可以不设。
    typedef uint32_t (*MillisFn)(void);
    static void SetMillisSource(MillisFn fn);

    // 固件里 UI 是全局单例（0x20014590）。回调签名是 (MonoUI*, void*)，
    // 运行时把 this 传进去，所以这里也保留一个全局指针。
    static MonoUI& Instance();
    static void    SetInstance(MonoUI* ui);

    // =====================================================================
    // 生命周期
    // =====================================================================
    void Begin();              // 初始化显示，调用 InitLayout()
    void Frame();              // 渲染一帧并提交
    void Tick(uint32_t now);   // 处理输入事件
    void Run();                // 阻塞主循环（Frame + Tick）

    // =====================================================================
    // 应用层钩子 —— 必须在 mono_user_app.cpp 里实现
    // =====================================================================
    void InitLayout();
    void HomeScreen(void* arg = nullptr);
    void SaveConfig(void* arg = nullptr);

    // =====================================================================
    // 导航
    // =====================================================================
    void GotoPage(MonoPage* page, int item = -1);
    void GotoPage(int index, int item = -1);
    void Back();

    void SetFocus(int index);
    void MoveFocus(int delta);
    void Press();
    void AdjustCurrent(int dir);

    MonoPage* CurrentPage();
    MonoItem* CurrentItem();

    // 视觉索引：焦点停在哪一行（浮点焦点的整数部分）。
    // 渲染用它决定"哪一行画成选中态"，所以动画期间会滞后于按键。
    int   FocusIndex()  const;

    // 输入索引：焦点**要去**哪一行。
    // 连续快速按键时以它为准，否则弹簧还没走到位就会漏掉按键。
    int   TargetIndex() const;

    float FocusPos() const { return focusS_.pos; }

    // =====================================================================
    // 覆盖层（MsgBox / Toast / ProgressBar / TextEditor / TextReader）
    // =====================================================================
    void        PushOverlay(MonoWidget* w);
    void        PopOverlay();
    MonoWidget* TopOverlay();
    bool        HasOverlay() const { return overlayDepth_ > 0; }

    // =====================================================================
    // 上下文
    // =====================================================================
    u8g2_t*    G2()      const { return g2_; }
    MonoInput& Input()         { return *in_; }
    MonoTheme& Theme()         { return theme_; }
    uint32_t   Now()     const { return now_; }
    uint32_t   Uptime()  const { return uptime_; }
    uint8_t    Fps()     const { return fps_; }

    void SetFonts(const uint8_t* listFont, const uint8_t* bigFont);

    // =====================================================================
    // 绘制工具（widget 层复用）
    // =====================================================================
    int  TextWidth(const char* s);
    int  FontAscent();
    void DrawText(int x, int baselineY, const char* s);
    void DrawTextCenter(int cx, int baselineY, const char* s);
    void DrawTextRight(int rightX, int baselineY, const char* s);   // 右对齐到 rightX
    void DrawTextClipped(int x, int baselineY, const char* s, int maxX);  // 超宽自动截断

    void FillBox   (int x, int y, int w, int h);
    void FillRound (int x, int y, int w, int h, int r);
    void FrameBox  (int x, int y, int w, int h);
    void FrameRound(int x, int y, int w, int h, int r);

    // 图标：首字节是边长，MSB-first，按 scale 放大
    void DrawIcon(const uint8_t* icon, int x, int y, int scale);

private:
    // 渲染
    void renderPage();
    void renderList(MonoPage& p);
    void renderIconPage(MonoPage& p);
    void renderCustom(MonoPage& p);
    void drawRow(MonoItem& it, float rowF, bool selected, float slide);

    // 无长度头的裸位图（行尾 10x10 状态图标用）
    void DrawRawBmp(const uint8_t* bmp, int x, int y, int w, int h);

    void drawOverlays();

    // 输入
    void handleEvent(MonoInput::Event ev, MonoInput::Key k);
    void activateItem(MonoItem& it);
    void switchItem(MonoItem& it);
    void adjustItem(MonoItem& it, int dir);

    // 页面切换
    void applyPage(MonoPage* p, int item);

    u8g2_t*    g2_;
    MonoInput* in_;
    MonoTheme  theme_;

    int        curPage_   = -1;
    MonoSpring focusS_;                 // 焦点索引，浮点 + 弹簧
    MonoSpring scroll_;                 // 滚动量，单位 = 项
    float      slide_     = 1.0f;       // 进页展开进度 0→1
    int        slidePage_ = -1;

    const uint8_t* fontList_ = nullptr;
    const uint8_t* fontBig_  = nullptr;

    uint32_t now_        = 0;
    uint32_t uptime_     = 0;
    uint32_t lastFrame_  = 0;
    uint32_t frameCount_ = 0;
    uint8_t  fps_        = 0;

    MonoWidget* overlay_[kMaxOverlays] = {nullptr, nullptr, nullptr, nullptr};
    int         overlayDepth_ = 0;

    static MonoUI*  s_inst_;
    static MillisFn s_millis_;
};

#endif // MONO_UI_H
