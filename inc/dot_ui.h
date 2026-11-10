// dot_ui.h —— DotUI 核心
#ifndef DOT_UI_H
#define DOT_UI_H

#include "dot_defs.h"
#include "dot_page.h"
#include "dot_item.h"
#include "dot_input.h"
#include "u8g2.h"

class DotUI {
public:
    static constexpr int kMaxOverlays = 4;   // 覆盖层栈深度（STEP 12 用）

    explicit DotUI(u8g2_t* g2, DotInput* input);

    // ---- 毫秒时钟：移植层注入（MCU 上接 HAL_GetTick 之类）----
    typedef uint32_t (*MillisFn)(void);
    static void SetMillisSource(MillisFn fn);

    // ---- 单例：控件池/回调需要全局访问 ----
    static DotUI& Instance();
    static void    SetInstance(DotUI* ui);

    // ---- 生命周期 ----
    void Begin();       // 初始化显示、布局、首页与输入
    void Frame();       // 渲染一帧（清屏→页面→覆盖层→提交）
    void Tick(uint32_t now);  // 消费输入事件（每帧调用）
    void Run();         // MCU 一体式主循环（PC 上不用）

    // ---- 应用层钩子：由应用文件实现 ----
    void InitLayout();
    void HomeScreen(void* arg = nullptr);
    void SaveConfig(void* arg = nullptr);

    // ---- 页面导航 ----
    void GotoPage(DotPage* page, int item = -1);
    void GotoPage(int index, int item = -1);
    void Back();

    // ---- 焦点 ----
    void SetFocus(int index);
    void MoveFocus(int delta);
    void Press();
    void AdjustCurrent(int dir);

    DotPage* CurrentPage();
    DotItem* CurrentItem();
    int FocusIndex()  const;    // 焦点弹簧当前位置（渲染用，向下取整）
    int TargetIndex() const;    // 焦点目标索引（逻辑判定用，四舍五入）

    // ---- 覆盖层（STEP 12）----
    void        PushOverlay(DotWidget* w);
    void        PopOverlay();
    DotWidget* TopOverlay();
    bool        HasOverlay() const { return overlayDepth_ > 0; }

    // ---- 访问器 ----
    u8g2_t*    G2()      const { return g2_; }
    DotInput& Input()         { return *in_; }
    DotTheme& Theme()         { return theme_; }
    uint32_t   Now()     const { return now_; }
    uint32_t   Uptime()  const { return uptime_; }
    uint8_t    Fps()     const { return fps_; }

    void SetFonts(const uint8_t* listFont, const uint8_t* bigFont);

    // ---- 运行时显示/动画调节（设置页用）----
    // 屏幕方向：cb 取 u8g2_cb_r0 / u8g2_cb_r2 / u8g2_cb_mirror 等；
    // 注意 90° 旋转（r1/r3）会交换宽高，SDL 窗口尺寸不变时会裁剪，
    // 演示用 r0/r2/mirror 最稳妥。
    void RotateTo(const u8g2_cb_t* cb);
    // 全局阻尼：同时设置 4 个弹簧的阻尼比（0.5 欠/1.0 临界/1.6 过）
    void SetSpringZeta(float zeta);

    // ---- 文本工具（自动绑定列表字体，UTF-8 安全）----
    int  TextWidth(const char* s);
    int  FontAscent();
    void DrawText(int x, int baselineY, const char* s);
    void DrawTextCenter(int cx, int baselineY, const char* s);
    void DrawTextRight(int rightX, int baselineY, const char* s);
    void DrawTextClipped(int x, int baselineY, const char* s, int maxX);

    // ---- 几何工具 ----
    void FillBox   (int x, int y, int w, int h);
    void FillRound (int x, int y, int w, int h, int r);       // STEP 9
    void FrameBox  (int x, int y, int w, int h);
    void FrameRound(int x, int y, int w, int h, int r);       // STEP 9
    void FillRoundOnce(int x, int y, int w, int h, int r);    // STEP 10
    // scale>0：按倍数放大绘制；dstSize>0：缩放绘制到 dstSize 盒内（互斥，dstSize 优先）
    void DrawIcon(const uint8_t* icon, int x, int y, int scale, int dstSize = 0);

private:
    // ---- 渲染 ----
    void renderPage();
    void renderList(DotPage& p);
    void renderIconPage(DotPage& p);     // STEP 11
    void renderCustom(DotPage& p);
    void drawRow(DotItem& it, float rowF, bool selected, float slide);
    void selectionMetrics(DotItem& it, int& x, int& w);      // STEP 10
    void syncSelectionTarget();                               // STEP 10
    void resetSelectionAnim();                                // STEP 10
    void drawScrollbar(int count, int rows, float maxScroll); // STEP 8
    void DrawRawBmp(const uint8_t* bmp, int x, int y, int w, int h); // STEP 9
    void drawOverlays();                  // STEP 12
    int  RowBaseline(int rowTop);         // 行内文本垂直居中基线

    // ---- 输入与行为 ----
    void handleEvent(DotInput::Event ev, DotInput::Key k);
    void switchItem(DotItem& it);        // STEP 9
    void adjustItem(DotItem& it, int dir); // STEP 9

    void applyPage(DotPage* p, int item);

    // ---- 状态 ----
    u8g2_t*    g2_;
    DotInput* in_;
    DotTheme  theme_;

    int        curPage_ = -1;
    DotSpring focusS_;    // 焦点行弹簧（单位：行）        ★ STEP 8 起生效
    DotSpring scroll_;    // 滚动弹簧（单位：行）          ★ STEP 8 起生效
    DotSpring selX_;      // 选中框 x 弹簧（像素）         ★ STEP 10 起生效
    DotSpring selW_;      // 选中框宽度弹簧（像素）        ★ STEP 10 起生效
    float      slide_  = 1.0f;   // 页面进入动画进度 0→1      ★ STEP 8 起生效
    float      dt_     = 0.016f; // 上一帧间隔（秒）

    const uint8_t* fontList_ = nullptr;
    const uint8_t* fontBig_  = nullptr;

    uint32_t now_ = 0, uptime_ = 0, lastFrame_ = 0, frameCount_ = 0;
    uint8_t  fps_ = 0;

    DotWidget* overlay_[kMaxOverlays] = {nullptr};   // ★ STEP 12
    int         overlayDepth_ = 0;

    static DotUI*  s_inst_;
    static MillisFn s_millis_;
};

#endif