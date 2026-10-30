// mono_defs.h —— MonoUI 公共定义
//
// 本头文件是整个框架的"地基"，按顺序包含五部分：
//   1. 回调与控件类型      （MonoCallback / ItemType）
//   2. 运行时布局参数      （MonoLayout，由 MonoUI::Begin() 按实际分辨率计算）
//   3. 动画与输入时间常量  （MonoAnim）
//   4. 基础数据结构        （MonoTheme / MonoSpring / MonoMarquee）
//   5. 小工具函数          （类型擦除、数值钳制）
//
// 命名约定：框架类位于全局命名空间（MonoUI/MonoPage/...），
// 常量集中放在 MonoLayout / MonoAnim 命名空间，避免污染全局。
#ifndef MONO_DEFS_H
#define MONO_DEFS_H

#include <stdint.h>
#include <stddef.h>
#include <type_traits>

// ---------------------------------------------------------------------------
// 前置声明：各头文件互相只引用指针，避免循环包含
// ---------------------------------------------------------------------------
class MonoUI;
class MonoPage;
class MonoItem;
class MonoWidget;
class MonoInput;

// 通用回调：菜单项 / 弹窗关闭时由框架调用
typedef void (*MonoCallback)(MonoUI* ui, void* arg);

// ---------------------------------------------------------------------------
// 控件类型：决定列表项的绘制方式与按下行为
// ---------------------------------------------------------------------------
enum class ItemType : uint8_t {
    ITEM_CALL_PAGE     = 0,   // 进入子页面（param1 = MonoPage*，行首显示 "+"）
    ITEM_CALL_FUNCTION = 1,   // 触发回调（行首显示 "-"）
    ITEM_TEXT          = 2,   // 纯文本行
    ITEM_SWITCH        = 3,   // 开关：右侧显示 开/关 文本
    ITEM_VALUE         = 4,   // 数值：右侧显示数值，OK 进入/退出编辑
    ITEM_TEXT_MARQUEE  = 5,   // 长文本：选中时跑马灯滚动
    ITEM_PROGRESS_BAR  = 6,   // 进度条：右侧内嵌进度条
    ITEM_CHECKBOX      = 7,   // 勾选框：行首方块 + 对勾
};

// ---------------------------------------------------------------------------
// 运行时布局参数
//
// 全部为 inline 全局变量：默认按 128x128 初始化，MonoUI::Begin() 会用
// u8g2 实际显示尺寸调用 init() 重新计算，因此 128x128 / 144x168 等
// 分辨率共用同一套渲染代码。
// ---------------------------------------------------------------------------
namespace MonoLayout {

// ---- 屏幕与行 ----
inline int kScreenW = 128;          // 屏幕宽度（像素）
inline int kScreenH = 128;          // 屏幕高度（像素）
inline int kRowH    = 16;           // 列表行高（行数 = kScreenH / kRowH）
inline int kListTop = 0;            // 列表区顶部留白（行数不整除时上下居中）
inline int kPadItem = 10;           // 列表正文左起点（前缀 + / - 之后）
inline int kPrefixX = 1;            // 前缀 + / - 的 x 起点

// ---- 右侧固定导航栏 ----
// 导航栏独占屏幕最右 kNavW 像素：先铺底色再画滚动条，
// 列表内容与选中框一律不允许进入该区域，避免"透视"。
inline int kNavW       = 8;         // 导航栏宽度
inline int kNavX       = 120;       // 导航栏左边界（= kScreenW - kNavW）
inline int kScrollBarX = 122;       // 滚动条轨道 x（导航栏内居中）
inline int kScrollBarW = 4;         // 滚动条轨道宽度

// ---- 内容右边界 ----
// 右侧控件（开关文本/数值/进度条）右对齐到导航栏左侧，再留 4px 间距
inline int kWidgetRight = 116;
inline int kMarqueeGate = 120;      // 跑马灯触发门槛：文本超过此宽度才滚动

// ---- 选中框 ----
inline int kSelectionOverhang = 3;  // 选中框右侧外扩量（视觉包围感）
inline int kSelectionRadius   = 1;  // 选中框圆角半径

// ---- 行内控件尺寸 ----
inline int kBarW        = 32;       // 进度条宽度
inline int kBarH        = 7;        // 进度条高度
inline int kStatusIconX = 110;      // 行尾状态图标 x
inline int kStatusIconS = 10;       // 行尾状态图标边长

// ---- 图标页 ----
inline int kIconCell    = 56;       // 图标横向间距（含留白）
inline int kIconLogical = 14;       // 图标原始边长（绘制时放大 3 倍 = 42px）
inline int kIconTop     = 6;        // 图标页顶部起点（高屏时下移留白）

// 根据实际显示尺寸重算全部布局参数（只需在 Begin() 调用一次）
inline void init(int width, int height) {
    kScreenW = width;
    kScreenH = height;

    kRowH    = 16;
    kPadItem = 10;
    kPrefixX = 1;

    // 导航栏与滚动条
    kNavW       = 8;
    kNavX       = kScreenW - kNavW;
    kScrollBarX = kNavX + 2;
    kScrollBarW = 4;

    // 内容右边界：控件右对齐到导航栏左側再留 4px；跑马灯门槛即导航栏
    kWidgetRight = kNavX - 4;
    kMarqueeGate = kNavX;

    // 选中框
    kSelectionOverhang = 3;
    kSelectionRadius   = 3;

    // 行内控件
    kBarW        = 32;
    kBarH        = 7;
    kStatusIconX = kNavX - 10;
    kStatusIconS = 10;

    // 图标页
    kIconCell    = 56;
    kIconLogical = 14;
    kIconTop     = (kScreenH > 128) ? 24 : 6;

    // 168 / 16 = 10 行，余 8px → 上下各留 4px；整除时留白为 0
    kListTop = (kScreenH - (kScreenH / kRowH) * kRowH) / 2;
}

} // namespace MonoLayout

// ---------------------------------------------------------------------------
// 动画与输入时间常量
// ---------------------------------------------------------------------------
namespace MonoAnim {

// ---- 弹簧（二阶弹簧阻尼系统，见 MonoSpring::step）----
//
// 连续模型：x'' = ωn²·(target - x) - 2ζωn·x'
//   ωn 固有角频率(rad/s)：越大响应越快，稳定时间 ≈ 4/(ζ·ωn)
//   ζ  阻尼比：ζ<1 欠阻尼(过冲回弹) / ζ=1 临界阻尼(最快且无过冲) / ζ>1 过阻尼
// 调参参考：ωn=15→270ms、22→180ms、30→130ms（ζ=1 时）
constexpr float kSpringOmega   = 22.0f;   // 固有频率 ωn (rad/s)
constexpr float kSpringZeta    = 1.5f;    // 阻尼比 ζ（1.0 = 临界阻尼，列表推荐）
constexpr float kSpringSnapPos = 0.001f;  // 视为"到位"的位置误差
constexpr float kSpringSnapVel = 0.05f;   // 视为"到位"的速度误差（px/s）

// ---- 跑马灯 ----
constexpr uint32_t kMarqueeWaitMs = 1000;  // 两端停留时间
constexpr uint32_t kMarqueeStepMs = 50;    // 滚动一步的间隔
constexpr int      kMarqueeEndGap = 90;     // 文本完全滚出后保留的尾部间隙

// ---- 按键 ----
constexpr uint32_t kLongPressMs   = 500;   // 长按判定阈值
constexpr uint32_t kTickSampleMs  = 20;    // 输入采样节拍（未直接使用，保留给移植层参考）
constexpr uint32_t kRepeatDelayMs = 400;   // 长按后开始连发的延迟
constexpr uint32_t kRepeatRateMs  = 80;    // 连发间隔

// ---- 弹窗 ----
constexpr uint32_t kToastMs = 1200;        // Toast 自动消失时间

} // namespace MonoAnim

// ---------------------------------------------------------------------------
// 主题：1-bit 屏幕只有前景/背景两种颜色，accent 预留给灰度屏
// ---------------------------------------------------------------------------
struct MonoTheme {
    uint8_t fg     = 1;   // 前景（u8g2 绘制色 1）
    uint8_t bg     = 0;   // 背景（u8g2 绘制色 0）
    uint8_t accent = 1;   // 强调色（预留给灰度屏）
};

// ---------------------------------------------------------------------------
// MonoSpring —— 二阶弹簧阻尼动画
//
// 连续系统：x'' = ωn²·(target - x) - 2ζωn·x'
//   ωn 控制响应速度，ζ 控制阻尼特性（欠阻尼/临界/过阻尼）。
// 每帧按真实帧间隔 dt 积分推进，帧率变化不再影响动画速度；
// 大 dt 会拆成不超过 1/240s 的子步，保证显式积分的数值稳定。
// 到位判定同时要求位置与速度收敛：旧实现只看位置，高速穿过目标的
// 瞬间被误判到位后强行拽回，正是"到位后又多走一段"的根源之一。
// ---------------------------------------------------------------------------
struct MonoSpring {
    float pos    = 0.0f;   // 当前位置（渲染使用）
    float vel    = 0.0f;   // 当前速度（单位/秒）
    float target = 0.0f;   // 目标位置（逻辑设置）
    float omega  = MonoAnim::kSpringOmega;   // 固有频率 ωn
    float zeta   = MonoAnim::kSpringZeta;    // 阻尼比 ζ

    // 瞬移到 v：常用于页面切换、初始定位
    void reset(float v) { pos = target = v; vel = 0.0f; }

    void setTarget(float t) { target = t; }

    // 位置与速度都收敛才认为到位
    bool settled() const {
        const float e = target - pos;
        return (e < MonoAnim::kSpringSnapPos && e > -MonoAnim::kSpringSnapPos &&
                vel < MonoAnim::kSpringSnapVel && vel > -MonoAnim::kSpringSnapVel);
    }

    // 推进 dt 秒（半隐式欧拉积分，子步长 ≤ 1/240s）
    void step(float dt) {
        if (dt <= 0.0f) return;

        if (settled()) {
            pos = target;
            vel = 0.0f;
            return;
        }

        // 拆分子步：单步不超过 1/240s，保证 ωn=30 量级下积分稳定
        const float kMaxSub = 1.0f / 240.0f;
        const int   n = (int)(dt / kMaxSub) + 1;
        const float h = dt / (float)n;

        const float w2 = omega * omega;          // ωn²
        const float c  = 2.0f * zeta * omega;    // 2ζωn

        for (int i = 0; i < n; ++i) {
            // 半隐式欧拉：先更新速度再更新位置，能量特性比显式欧拉稳定
            vel += (w2 * (target - pos) - c * vel) * h;
            pos += vel * h;
        }

        // 收敛后强制贴合目标，消除无限小的亚像素残差
        if (settled()) { pos = target; vel = 0.0f; }
    }
};

// ---------------------------------------------------------------------------
// MonoMarquee —— 跑马灯四态机
//
// WAIT（原地停留）→ SLIDE（向左滚出）→ HOLD（尾部停留）→ BACK（滚回）→ WAIT
// step() 由渲染层在"该行被选中且文本超宽"时调用，返回当前水平偏移。
// ---------------------------------------------------------------------------
class MonoMarquee {
public:
    enum State : uint8_t {
        ST_WAIT  = 0,   // 原地停留
        ST_SLIDE = 1,   // 向左滚动
        ST_HOLD  = 2,   // 滚出后停留
        ST_BACK  = 3,   // 滚回起点
    };

    void reset() { state_ = ST_WAIT; offset_ = 0; stamp_ = 0; }

    // 返回当前水平偏移（<= 0，绘制时加到文本 x 上）
    int16_t step(int textWidth, uint32_t nowMs) {
        switch (state_) {
        case ST_WAIT:
            if (nowMs - stamp_ > MonoAnim::kMarqueeWaitMs) {
                state_ = ST_SLIDE;
                stamp_ = nowMs;
            }
            break;

        case ST_SLIDE:
            if (nowMs - stamp_ > MonoAnim::kMarqueeStepMs) {
                --offset_;
                // 文本完全滚出屏幕（留 kMarqueeEndGap 间隙）后进入停留
                if (offset_ <= MonoAnim::kMarqueeEndGap - textWidth)
                    state_ = ST_HOLD;
                stamp_ = nowMs;
            }
            break;

        case ST_HOLD:
            if (nowMs - stamp_ > MonoAnim::kMarqueeWaitMs) {
                state_ = ST_BACK;
                stamp_ = nowMs;
            }
            break;

        case ST_BACK:
            if (nowMs - stamp_ > MonoAnim::kMarqueeStepMs) {
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
    int16_t  offset_ = 0;     // 当前水平偏移
    uint32_t stamp_  = 0;     // 上次状态切换的时间戳
};

// ---------------------------------------------------------------------------
// 工具函数
// ---------------------------------------------------------------------------

// AddItem 模板参数 → uintptr_t 的类型擦除：
// 只接受指针 / 整数 / 布尔 / nullptr，其他类型编译期报错
template <typename T>
constexpr uintptr_t monoToUptr(T v) noexcept
{
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<uintptr_t>(v);
    } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
        return 0u;
    } else if constexpr (std::is_same_v<T, bool>) {
        return v ? 1u : 0u;
    } else if constexpr (std::is_enum_v<T>) {
        return static_cast<uintptr_t>(static_cast<intptr_t>(v));
    } else {
        static_assert(std::is_integral_v<T>,
                      "AddItem 的 param 只接受指针 / 整数 / 布尔 / nullptr");
        return static_cast<uintptr_t>(static_cast<intptr_t>(v));
    }
}

// uintptr_t → 指针（T 必须是指针类型，与 monoToUptr 配对使用）
template <typename T>
inline T monoFromUptr(uintptr_t v) { return reinterpret_cast<T>(v); }

// 浮点 / 整数钳制
inline float monoClampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
inline int monoClampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

#endif // MONO_DEFS_H
