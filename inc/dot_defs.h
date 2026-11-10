// dot_defs.h —— DotUI 公共定义
#ifndef DOT_DEFS_H
#define DOT_DEFS_H

#include <stdint.h>
#include <stddef.h>
#include <type_traits>

// 前置声明：各头文件互相只引用指针，避免循环包含
class DotUI;
class DotPage;
class DotItem;
class DotWidget;
class DotInput;

// 通用回调：菜单项/弹窗关闭时由框架调用
typedef void (*DotCallback)(DotUI* ui, void* arg);

// 控件类型：决定列表项的绘制方式与按下行为
enum class ItemType : uint8_t {
    ITEM_CALL_PAGE     = 0,   // 进入子页面（param1 = DotPage*，行首显示 "+"）
    ITEM_CALL_FUNCTION = 1,   // 触发回调（行首显示 "-"）
    ITEM_TEXT          = 2,   // 纯文本行
    ITEM_SWITCH        = 3,   // 开关：右侧显示 开/关
    ITEM_VALUE         = 4,   // 数值：OK 进入/退出编辑，左右键调值
    ITEM_TEXT_MARQUEE  = 5,   // 长文本：选中时跑马灯滚动
    ITEM_PROGRESS_BAR  = 6,   // 进度条
    ITEM_CHECKBOX      = 7,   // 勾选框
};

// ---------------------------------------------------------------------------
// 运行时布局参数：全部 inline 全局变量，默认按 128x128 初始化；
// DotUI::Begin() 会用实际显示尺寸调 init() 重算——128x128 / 144x168 共用
// 同一套渲染代码的秘密就在这里。
// ---------------------------------------------------------------------------
namespace DotLayout {
inline int kScreenW = 128;
inline int kScreenH = 128;
inline int kRowH    = 16;      // 列表行高（行数 = kScreenH / kRowH）
inline int kListTop = 0;       // 行数不整除时上下居中的留白
inline int kPadItem = 10;      // 列表正文左起点（前缀 +/- 之后）
inline int kPrefixX = 1;       // 前缀 +/- 的 x 起点

inline int kNavW       = 8;    // 右侧导航栏宽度
inline int kNavX       = 120;  // 导航栏左边界（= kScreenW - kNavW）
inline int kScrollBarX = 122;
inline int kScrollBarW = 4;

inline int kWidgetRight = 116; // 右侧控件右对齐到这里（导航栏左再留 4px）
inline int kMarqueeGate = 120; // 跑马灯触发门槛：文本超过此宽才滚动
inline int kSelectionOverhang = 3;
inline int kSelectionRadius   = 3;
inline int kBarW = 32, kBarH = 7;
inline int kStatusIconX = 110, kStatusIconS = 10;
inline int kIconCell = 56, kIconLogical = 14, kIconTop = 6;
inline int kIconNameY = 60; // 焦点图标名称基线相对 kIconTop 的距离

// 按实际显示尺寸重算全部布局参数（Begin() 时调用一次）。
// 所有参数都从 kScreenW/kScreenH 推导，换分辨率无需改渲染代码。
inline void init(int width, int height) {
    kScreenW = width;
    kScreenH = height;

    // ---- 列表行 ----
    kRowH    = 16;              // 行高固定 16px：128 屏 8 行 / 168 屏 10 行
    kPadItem = 10;              // 正文左起点（前缀 + 间距）
    kPrefixX = 1;               // 前缀 +/- 的 x 起点

    // ---- 右侧导航栏与滚动条 ----
    kNavW       = 8;                        // 导航栏宽度固定
    kNavX       = kScreenW - kNavW;         // 导航栏左边界贴屏幕右缘
    kScrollBarX = kNavX + 2;                // 滚动条轨道在导航栏内居中
    kScrollBarW = 4;

    // ---- 内容右边界 ----
    kWidgetRight = kNavX - 4;   // 右侧控件（开关/数值/进度条）右对齐线
    kMarqueeGate = kNavX;       // 文本超过导航栏左缘才触发跑马灯

    // ---- 选中框 ----
    kSelectionOverhang = 3;     // 右侧外扩量（包围感）
    kSelectionRadius   = 3;     // 圆角半径

    // ---- 行内控件 ----
    kBarW = 32; kBarH = 7;      // 进度条尺寸
    kStatusIconX = kNavX - 10;  // 行尾状态图标（10x10）贴导航栏左侧
    kStatusIconS = 10;

    // ---- 图标页 ----
    kIconCell    = 56;                      // 图标横向间距（含留白）
    kIconLogical = 14;                      // 图标原始边长（绘制放大 3 倍 = 42px）
    // 图标 + 名称整体垂直居中：内容总高 = 图标 42px + 名称区约 15px，
    // 让名称底部贴近屏幕下缘，剩余空间上下均分。
    kIconTop     = (kScreenH - (kIconLogical * 3 + 15)) / 2;
    if (kIconTop < 0) kIconTop = 0;
    kIconNameY   = 57;   // 名称基线 = kIconTop + 57，即紧贴图标下方 15px 处

    // 168/16=10 行余 8px → 上下各留 4px；整除时留白为 0
    kListTop = (kScreenH - (kScreenH / kRowH) * kRowH) / 2;
}
} // namespace DotLayout

// 动画与输入时间常量
namespace DotAnim {
// 弹簧：x'' = ωn²·(target-x) - 2ζωn·x'
//   ωn 越大响应越快（稳定时间≈4/(ζ·ωn)），ζ=1 临界阻尼最快无过冲
constexpr float kSpringOmega   = 22.0f;   // 固有角频率 (rad/s)，22 → 约 180ms 收敛
constexpr float kSpringZeta    = 0.8f;    // 阻尼比：<1 欠阻尼轻微回弹，1 = 临界
constexpr float kSpringSnapPos = 0.001f;  // 判“到位”的位置误差阈值
constexpr float kSpringSnapVel = 0.05f;   // 判“到位”的速度误差阈值

constexpr uint32_t kMarqueeWaitMs = 1000;  // 跑马灯两端停留
constexpr uint32_t kMarqueeStepMs = 50;    // 滚动一步间隔
constexpr int      kMarqueeEndGap = 90;    // 滚出后尾部间隙

constexpr uint32_t kLongPressMs   = 500;   // 长按阈值
constexpr uint32_t kRepeatDelayMs = 400;   // 长按后开始连发的延迟
constexpr uint32_t kRepeatRateMs  = 80;    // 连发间隔
constexpr uint32_t kToastMs       = 1200;  // Toast 存活时间
} // namespace DotAnim

// 主题：1-bit 屏只有前景/背景
struct DotTheme {
    uint8_t fg = 1, bg = 0, accent = 1;
};

// ---------------------------------------------------------------------------
// DotSpring —— 二阶弹簧阻尼动画（框架所有平滑运动的发动机）
// ---------------------------------------------------------------------------
struct DotSpring {
    float pos = 0, vel = 0, target = 0;
    float omega = DotAnim::kSpringOmega;
    float zeta  = DotAnim::kSpringZeta;

    void reset(float v) { pos = target = v; vel = 0.0f; }  // 瞬移
    void setTarget(float t) { target = t; }                // 带动画逼近

    // 位置和速度都收敛才算到位（只看位置会在高速穿过目标时误判）
    bool settled() const {
        const float e = target - pos;
        return (e < DotAnim::kSpringSnapPos && e > -DotAnim::kSpringSnapPos &&
                vel < DotAnim::kSpringSnapVel && vel > -DotAnim::kSpringSnapVel);
    }

    // 推进 dt 秒：半隐式欧拉积分，大 dt 拆成 ≤1/240s 的子步保证稳定
    void step(float dt) {
        if (dt <= 0.0f) return;
        if (settled()) { pos = target; vel = 0; return; }

        const float kMaxSub = 1.0f / 240.0f;
        const int   n = (int)(dt / kMaxSub) + 1;
        const float h = dt / (float)n;
        const float w2 = omega * omega;
        const float c  = 2.0f * zeta * omega;

        for (int i = 0; i < n; ++i) {
            vel += (w2 * (target - pos) - c * vel) * h;  // 先速度
            pos += vel * h;                              // 后位置（半隐式）
        }
        if (settled()) { pos = target; vel = 0.0f; }     // 消除亚像素残差
    }
};

// ---------------------------------------------------------------------------
// DotMarquee —— 跑马灯四态机：WAIT→SLIDE→HOLD→BACK→WAIT
// ---------------------------------------------------------------------------
class DotMarquee {
public:
    enum State : uint8_t { ST_WAIT = 0, ST_SLIDE = 1, ST_HOLD = 2, ST_BACK = 3 };

    void reset() { state_ = ST_WAIT; offset_ = 0; stamp_ = 0; }

    // 返回当前水平偏移（≤0，绘制时加到文本 x 上）
    int16_t step(int textWidth, uint32_t nowMs) {
        switch (state_) {
        case ST_WAIT:
            if (nowMs - stamp_ > DotAnim::kMarqueeWaitMs)
                { state_ = ST_SLIDE; stamp_ = nowMs; }
            break;
        case ST_SLIDE:
            if (nowMs - stamp_ > DotAnim::kMarqueeStepMs) {
                --offset_;
                if (offset_ <= DotAnim::kMarqueeEndGap - textWidth)
                    state_ = ST_HOLD;
                stamp_ = nowMs;
            }
            break;
        case ST_HOLD:
            if (nowMs - stamp_ > DotAnim::kMarqueeWaitMs)
                { state_ = ST_BACK; stamp_ = nowMs; }
            break;
        case ST_BACK:
            if (nowMs - stamp_ > DotAnim::kMarqueeStepMs) {
                ++offset_;
                if (offset_ >= 0) { offset_ = 0; state_ = ST_WAIT; }
                stamp_ = nowMs;
            }
            break;
        }
        return offset_;
    }

private:
    State    state_  = ST_WAIT;
    int16_t  offset_ = 0;
    uint32_t stamp_  = 0;
};

// ---------------------------------------------------------------------------
// 工具函数
// ---------------------------------------------------------------------------
// 类型擦除：AddItem 的模板参数 → uintptr_t，只接受指针/整数/布尔/nullptr
template <typename T>
constexpr uintptr_t dotToUptr(T v) noexcept
{
    if constexpr (std::is_pointer_v<T>)      return reinterpret_cast<uintptr_t>(v);
    else if constexpr (std::is_same_v<T, std::nullptr_t>) return 0u;
    else if constexpr (std::is_same_v<T, bool>) return v ? 1u : 0u;
    else if constexpr (std::is_enum_v<T>)
        return static_cast<uintptr_t>(static_cast<intptr_t>(v));
    else {
        static_assert(std::is_integral_v<T>,
                      "AddItem 的 param 只接受指针 / 整数 / 布尔 / nullptr");
        return static_cast<uintptr_t>(static_cast<intptr_t>(v));
    }
}

// uintptr_t → 指针（与 DotToUptr 配对）
template <typename T>
inline T dotFromUptr(uintptr_t v) { return reinterpret_cast<T>(v); }

inline float dotClampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
inline int dotClampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

#endif