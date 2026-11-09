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
    ITEM_CALL_PAGE     = 0,   // 进入子页面（param1 = MonoPage*，行首显示 "+"）
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
// MonoUI::Begin() 会用实际显示尺寸调 init() 重算——128x128 / 144x168 共用
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

inline void init(int width, int height) {
    kScreenW = width;
    kScreenH = height;
    kRowH    = 16;
    kPadItem = 10;
    kPrefixX = 1;
    kNavW       = 8;
    kNavX       = kScreenW - kNavW;
    kScrollBarX = kNavX + 2;
    kScrollBarW = 4;
    kWidgetRight = kNavX - 4;
    kMarqueeGate = kNavX;
    kSelectionOverhang = 3;
    kSelectionRadius   = 3;
    kBarW = 32; kBarH = 7;
    kStatusIconX = kNavX - 10;
    kStatusIconS = 10;
    kIconCell    = 56;
    kIconLogical = 14;
    kIconTop     = (kScreenH > 128) ? 24 : 6;
    // 168/16=10 行余 8px → 上下各留 4px；整除时留白为 0
    kListTop = (kScreenH - (kScreenH / kRowH) * kRowH) / 2;
}
} // namespace MonoLayout

// 动画与输入时间常量
namespace DotAnim {
// 弹簧：x'' = ωn²·(target-x) - 2ζωn·x'
//   ωn 越大响应越快（稳定时间≈4/(ζ·ωn)），ζ=1 临界阻尼最快无过冲
constexpr float kSpringOmega   = 22.0f;
constexpr float kSpringZeta    = 0.8f;
constexpr float kSpringSnapPos = 0.001f;
constexpr float kSpringSnapVel = 0.05f;

constexpr uint32_t kMarqueeWaitMs = 1000;  // 跑马灯两端停留
constexpr uint32_t kMarqueeStepMs = 50;    // 滚动一步间隔
constexpr int      kMarqueeEndGap = 90;    // 滚出后尾部间隙

constexpr uint32_t kLongPressMs   = 500;   // 长按阈值
constexpr uint32_t kRepeatDelayMs = 400;   // 长按后开始连发的延迟
constexpr uint32_t kRepeatRateMs  = 80;    // 连发间隔
constexpr uint32_t kToastMs       = 1200;  // Toast 存活时间
} // namespace MonoAnim

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

// uintptr_t → 指针（与 monoToUptr 配对）
template <typename T>
inline T dotFromUptr(uintptr_t v) { return reinterpret_cast<T>(v); }

inline float dotClampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
inline int dotClampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

#endif