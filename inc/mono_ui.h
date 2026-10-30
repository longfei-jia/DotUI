// mono_ui.h —— MonoUI 核心
//
// 框架入口：持有 u8g2 句柄与输入设备，负责
//   生命周期（Begin/Frame/Tick/Run）
//   页面导航（GotoPage/Back/SetFocus）
//   事件分派（覆盖层优先，其余交给页面）
//   渲染分派（LIST / ICON / CUSTOM + 覆盖层）
//   绘制工具（文本/几何/图标封装，统一处理字体与裁剪）
//
// 应用层只需实现三个钩子：InitLayout / HomeScreen / SaveConfig。
#ifndef MONO_UI_H
#define MONO_UI_H

#include "mono_defs.h"
#include "mono_page.h"
#include "mono_item.h"
#include "mono_input.h"
#include "u8g2.h"

class MonoUI {
public:
    static constexpr int kMaxOverlays = 4;   // 覆盖层栈深度

    explicit MonoUI(u8g2_t* g2, MonoInput* input);

    // ---- 毫秒时钟：移植层注入，MCU 上接 HAL_GetTick 之类 ----
    typedef uint32_t (*MillisFn)(void);
    static void SetMillisSource(MillisFn fn);

    // ---- 单例：控件池/回调需要全局访问 ----
    static MonoUI& Instance();
    static void    SetInstance(MonoUI* ui);

    // ---- 生命周期 ----
    void Begin();                    // 初始化显示、布局、首页与输入
    void Frame();                    // 渲染一帧（清屏→页面→覆盖层→提交）
    void Tick(uint32_t now);         // 消费输入事件（每帧调用）
    void Run();                      // MCU 一体式主循环（PC 上不用）

    // ---- 应用层钩子：由 mono_user_app.cpp 实现 ----
    void InitLayout();                          // 注册页面（必须实现）
    void HomeScreen(void* arg = nullptr);       // 返回主页
    void SaveConfig(void* arg = nullptr);       // 保存配置

    // ---- 页面导航 ----
    void GotoPage(MonoPage* page, int item = -1);    // item>=0 时定位到指定项
    void GotoPage(int index, int item = -1);
    void Back();                                     // 返回父页（无父页则回主页）

    // ---- 焦点 ----
    void SetFocus(int index);        // 设置目标焦点（同时同步选中框目标）
    void MoveFocus(int delta);       // 相对移动
    void Press();                    // 按下当前项
    void AdjustCurrent(int dir);     // 左右键调整当前项（数值/开关/进度条）

    MonoPage* CurrentPage();         // 当前页面（可能为 nullptr）
    MonoItem* CurrentItem();         // 目标项（按 TargetIndex 取）

    int FocusIndex()  const;         // 焦点弹簧的当前整数位置（渲染用）
    int TargetIndex() const;         // 焦点目标索引（逻辑判定用）

    // ---- 覆盖层（MsgBox/Toast/ProgressBar）----
    void        PushOverlay(MonoWidget* w);
    void        PopOverlay();
    MonoWidget* TopOverlay();
    bool        HasOverlay() const { return overlayDepth_ > 0; }

    // ---- 访问器 ----
    u8g2_t*    G2()      const { return g2_; }
    MonoInput& Input()         { return *in_; }
    MonoTheme& Theme()         { return theme_; }
    uint32_t   Now()     const { return now_; }
    uint32_t   Uptime()  const { return uptime_; }
    uint8_t    Fps()     const { return fps_; }

    // ---- 字体 ----
    // listFont：列表/控件正文字体；bigFont：图标页名称等大字
    void SetFonts(const uint8_t* listFont, const uint8_t* bigFont);

    // ---- 文本工具（自动绑定列表字体，UTF-8 安全）----
    int  TextWidth(const char* s);
    int  FontAscent();
    void DrawText(int x, int baselineY, const char* s);
    void DrawTextCenter(int cx, int baselineY, const char* s);
    void DrawTextRight(int rightX, int baselineY, const char* s);
    void DrawTextClipped(int x, int baselineY, const char* s, int maxX);

    // ---- 几何工具（使用当前 u8g2 绘制色）----
    void FillBox   (int x, int y, int w, int h);
    void FillRound (int x, int y, int w, int h, int r);
    void FrameBox  (int x, int y, int w, int h);
    void FrameRound(int x, int y, int w, int h, int r);
    // 逐行填充圆角矩形：每个像素只绘制一次。
    // XOR（SetDrawColor(2)）反色时必须用它代替 FillRound，
    // 否则 u8g2_DrawRBox 的拼接重叠会让角部被反色两次而出现空洞。
    void FillRoundOnce(int x, int y, int w, int h, int r);

    // ---- 图标 ----
    // 绘制固件格式图标：icon[0] = 边长，MSB-first，1 = 透明（不画）
    void DrawIcon(const uint8_t* icon, int x, int y, int scale);

private:
    // ---- 渲染 ----
    void renderPage();                       // 按页面模式分派
    void renderList(MonoPage& p);            // 垂直列表：滚动/选中框/滚动条
    void renderIconPage(MonoPage& p);        // 图标横向轮播
    void renderCustom(MonoPage& p);          // 用户自定义绘制
    void drawRow(MonoItem& it, float rowF, bool selected, float slide);

    // 计算某一项的选中框 x / 宽度（只覆盖文本，右侧控件不参与）
    void selectionMetrics(MonoItem& it, int& x, int& w);
    // 把目标项的选中框 x/宽度同步到 selX_/selW_ 的 target
    void syncSelectionTarget();
    // 页面切换时把选中框弹簧瞬移到目标位置
    void resetSelectionAnim();
    // 右侧导航栏内的滚动条（内容超过可视行数时显示）
    void drawScrollbar(int count, int rows, float maxScroll);

    void DrawRawBmp(const uint8_t* bmp, int x, int y, int w, int h); // 裸位图
    void drawOverlays();                     // 逐层绘制覆盖层
    // 计算行内文本的垂直居中基线（按当前字体度量，而非写死常量）
    int RowBaseline(int rowTop);

    // ---- 输入与行为 ----
    void handleEvent(MonoInput::Event ev, MonoInput::Key k);
    void switchItem(MonoItem& it);           // 开关/勾选翻转并回调
    void adjustItem(MonoItem& it, int dir);  // 左右键调整

    void applyPage(MonoPage* p, int item);   // 切页：复位弹簧并定位滚动

    // ---- 状态 ----
    u8g2_t*    g2_;                 // u8g2 句柄
    MonoInput* in_;                 // 输入设备
    MonoTheme  theme_;              // 当前主题

    int        curPage_   = -1;     // 当前页面索引
    MonoSpring focusS_;             // 焦点行弹簧（单位：行）
    MonoSpring scroll_;             // 滚动弹簧（单位：行）
    MonoSpring selX_;               // 选中框 x 弹簧（单位：像素）
    MonoSpring selW_;               // 选中框宽度弹簧（单位：像素）
    float      slide_     = 1.0f;   // 页面进入动画进度 0→1
    float      dt_        = 0.016f; // 上一帧间隔（秒），弹簧按真实时间积分

    const uint8_t* fontList_ = nullptr;   // 列表字体
    const uint8_t* fontBig_  = nullptr;   // 大字体

    uint32_t now_        = 0;         // 当前毫秒
    uint32_t uptime_     = 0;         // 累计运行毫秒
    uint32_t lastFrame_  = 0;         // 上一帧时间戳（算 FPS）
    uint32_t frameCount_ = 0;         // 帧计数（调试日志降频用）
    uint8_t  fps_        = 0;

    // ---- 覆盖层栈 ----
    MonoWidget* overlay_[kMaxOverlays] = {nullptr, nullptr, nullptr, nullptr};
    int         overlayDepth_ = 0;

    static MonoUI*  s_inst_;
    static MillisFn s_millis_;
};

#endif // MONO_UI_H
