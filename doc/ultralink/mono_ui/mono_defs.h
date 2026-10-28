// mono_defs.h —— MonoUI 公共定义
//
// 对应 3rdParty/mono_ui/mono_defs.h。
//
// 命名约定：框架类（MonoUI / MonoPage / MonoItem / MonoWidget / MonoInput）
// 与 ItemType 都在**全局命名空间**，因为作者的示例代码直接写
// `MonoUI::InitLayout()` / `MonoPage::AddPage(...)` / `ItemType::ITEM_SWITCH`，
// 没有 `mono::` 限定。
//
// 常量集中在 MonoLayout / MonoAnim / MonoColor 三个命名空间，避免污染。
#ifndef MONO_DEFS_H
#define MONO_DEFS_H

#include <stdint.h>
#include <stddef.h>
#include <type_traits>   // monoToUptr 的 if constexpr 分派（C++17）

// ---------------------------------------------------------------------------
// 前置声明
// ---------------------------------------------------------------------------
class MonoUI;
class MonoPage;
class MonoItem;
class MonoWidget;
class MonoInput;

// 回调签名。与截图 Outline 面板里的 `TestMsgBox(MonoUI *, void *)` 一致。
typedef void (*MonoCallback)(MonoUI* ui, void* arg);

// ---------------------------------------------------------------------------
// 控件类型 —— 对应 MonoItem::type（固件里是 Item+0x08）
//
// 0..5 已由 UltraLink 固件逆向实证（列表渲染器 0x08014298 的分支）；
// 6..7 来自 MonoUI 作者的公开示例截图（ITEM_PROGRESS_BAR / ITEM_CHECKBOX），
// 固件中只出现过 type 7 的判断（TickTask 内），语义本轮确认为勾选框。
// ---------------------------------------------------------------------------
enum class ItemType : uint8_t {
    ITEM_CALL_PAGE     = 0,   // 进入子页面，param1 = MonoPage*
    ITEM_CALL_FUNCTION = 1,   // 触发回调，callback = 函数
    ITEM_TEXT          = 2,   // 纯文本，无前缀、无图标
    ITEM_SWITCH        = 3,   // 开关，行尾 "ON"/"OFF"
    ITEM_VALUE         = 4,   // 数值，右对齐 "%.2f"，可上下调节
    ITEM_TEXT_MARQUEE  = 5,   // 长文本，超宽自动跑马灯，行尾状态图标
    ITEM_PROGRESS_BAR  = 6,   // 进度条，param1 = 0..100 初值
    ITEM_CHECKBOX      = 7,   // 勾选框，param1 = 初值
};

// ---------------------------------------------------------------------------
// 布局常量 —— 实测自通用列表渲染器 0x08014298
// ---------------------------------------------------------------------------
namespace MonoLayout {

constexpr int kScreenW    = 128;
constexpr int kScreenH    = 128;

constexpr int kRowH       = 16;    // 行高，反编译里的 "* 16.0"
constexpr int kRowPadY    = 1;     // 行内 y 补偿，"row*16 + 1"
constexpr int kPadText    = 3;     // ITEM_TEXT 缩进
constexpr int kPadItem    = 10;    // 其余控件缩进（给图标列留位）

constexpr int kPrefixX    = 1;     // 行首 "+"/"-" 的 x
constexpr int kToggleRight = 96;   // ITEM_SWITCH 的 "ON"/"OFF" 右边界
constexpr int kValueRight = 96;    // ITEM_VALUE 右对齐边界
constexpr int kScrollBarX = 100;   // 选中行右侧色块 x
constexpr int kScrollBarW = 33;    // 右侧色块宽（超出屏宽由驱动裁剪）
constexpr int kMarkW      = 10;    // 选中行左侧色块宽

// 行内右侧控件统一右对齐到这个 x —— 必须小于 kScrollBarX，
// 否则选中行的色块会把 ON/OFF、数值、进度条盖掉。
constexpr int kWidgetRight = 96;

// ITEM_PROGRESS_BAR 的条
constexpr int kBarW = 32;
constexpr int kBarH = 7;

constexpr int kStatusIconX = 110;  // ITEM_TEXT_MARQUEE 行尾图标 x（0x6E）
constexpr int kStatusIconS = 10;   // 状态图标边长
constexpr int kStatusHiS   = 12;   // 选中时的高亮块边长

constexpr int kMarqueeGate = 96;   // 触发跑马灯的文字宽度阈值（0x60）

// ---- ICON 模式（MonoPage::ICON）----
// 固件主界面的图标轮播：间距 56px（0x0801453C）、基准 -69（0x08014540）
constexpr int kIconCell     = 56;  // 相邻图标中心的间距
constexpr int kIconOriginY  = -69; // 第一格的中心 y
constexpr int kIconBox      = 48;  // 图标底板边长
constexpr int kIconInner    = 42;  // 位图放大后的边长（14 * 3）
constexpr int kIconLogical  = 14;  // 图标位图原始边长

} // namespace MonoLayout

// ---------------------------------------------------------------------------
// 动画常量 —— 实测自常量池 0x080393EC
// ---------------------------------------------------------------------------
namespace MonoAnim {

constexpr float kSnap       = 0.0010000000474974513f;
constexpr float kStiffness  = 0.009999999776482582f;
constexpr float kDamping    = 0.11140845715999603f;
constexpr float kStiffness2 = 0.006332573480904102f;

constexpr uint32_t kMarqueeWaitMs = 1000;  // 起跑前静置
constexpr uint32_t kMarqueeStepMs = 50;    // 每 50ms 移 1px
constexpr int      kMarqueeEndGap = 90;    // 左移终点 x <= 90 - textWidth（0x5A）

constexpr uint32_t kLongPressMs   = 500;   // 长按返回判定
constexpr uint32_t kTickSampleMs  = 20;    // 输入采样节拍
constexpr uint32_t kRepeatDelayMs = 400;   // 按键重复起始延迟
constexpr uint32_t kRepeatRateMs  = 80;    // 按键重复间隔

constexpr uint32_t kToastMs       = 1200;  // Toast 默认停留

} // namespace MonoAnim

// ---------------------------------------------------------------------------
// 颜色 —— 适配 u8g2 的 0=透明 1=前景 2=异或，同时兼容灰度屏
// ---------------------------------------------------------------------------
struct MonoTheme {
    uint8_t fg     = 1;
    uint8_t bg     = 0;
    uint8_t accent = 1;   // 选中行色块
};

// ---------------------------------------------------------------------------
// 阻尼弹簧 —— 固件里内联在渲染代码中，这里提成结构体
//
// 不是关键帧动画，是临界阻尼物理积分：位置会被"拉"向目标，
// 停下来的判据是误差小于 kSnap。
// ---------------------------------------------------------------------------
struct MonoSpring {
    float pos    = 0.0f;
    float vel    = 0.0f;
    float target = 0.0f;
    float prev   = 0.0f;

    void reset(float v) { pos = prev = target = v; vel = 0.0f; }

    void setTarget(float t) { target = t; }

    bool settled() const {
        const float e = target - pos;
        return e < MonoAnim::kSnap && e > -MonoAnim::kSnap;
    }

    // 每帧调用一次。逐行对应 0x08038E78 的算法。
    void step() {
        if (settled()) { pos = target; vel = 0.0f; return; }

        const float k    = MonoAnim::kStiffness;
        const float damp = MonoAnim::kDamping;
        const float newPos = pos + k * vel;

        float denom = (k + damp) * k * 0.5f;
        const float alt = k * damp;
        if (alt > denom) denom = alt;

        vel = (((target - damp * vel + ((target - prev) / k) * MonoAnim::kStiffness2)
                - newPos) * k) / denom + vel;

        prev = pos;
        pos  = newPos;
    }
};

// ---------------------------------------------------------------------------
// 跑马灯 —— 四态机，还原 0x08014298 的 case 0..3
// ---------------------------------------------------------------------------
class MonoMarquee {
public:
    enum State : uint8_t {
        ST_WAIT  = 0,   // 原地静置，等 1s
        ST_SLIDE = 1,   // 左移，每 50ms 一像素
        ST_HOLD  = 2,   // 尾部停顿
        ST_BACK  = 3,   // 右移归位
    };

    void reset() { state_ = ST_WAIT; offset_ = 0; stamp_ = 0; }

    // 返回本帧的 x 偏移（负数表示左移）
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
                // 终点：文字右端正好移出可视区。
                //   文字左边缘 = 10 + offset，右边缘 = 10 + offset + textWidth
                //   要让它退出 x=100（右侧色块左边），需 offset <= 90 - textWidth
                // 注意是 (90 - textWidth)，不是 (-90 - textWidth) —— 后者会让
                // 文字滑出屏幕之后继续空跑几百像素，白等十几秒。
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

    bool scrolling() const { return state_ != ST_WAIT; }

private:
    State    state_  = ST_WAIT;
    int16_t  offset_ = 0;
    uint32_t stamp_  = 0;
};

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------

// 把任意"可当参数"的实参统一成 uintptr_t。
// 这是 AddItem 模板能同时接受 `true` / `0` / `pageShow` / `nullptr`
// 的原因（截图里三种写法混用）。
//
// C++11 时期这里是一组重载：
//     uintptr_t monoToUptr(uintptr_t);
//     uintptr_t monoToUptr(unsigned);
//     ...
// 在 64 位 PC 上没问题，但**交叉编译到 32 位 ARM 就炸**：
// 32 位下 uintptr_t 就是 unsigned int，这两条签名完全相同 →
// redefinition。这类 bug 只在换架构时暴露，PC 上永远编不出来。
//
// C++17 的 if constexpr 把不命中的分支整条丢弃，所以：
//   1. 只剩一个函数，不可能重定义
//   2. reinterpret_cast 只存在于指针分支，函数还能标 constexpr
//   3. 传错类型（比如 float）会被 static_assert 挡住，而不是静默截断
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

template <typename T>
inline T monoFromUptr(uintptr_t v)            { return reinterpret_cast<T>(v); }

inline float monoClampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
inline int monoClampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

#endif // MONO_DEFS_H
