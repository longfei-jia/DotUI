// mono_item.h —— 列表项
//
// MonoItem 是菜单里的一行：文本 + 类型 + 参数 + 回调。
// 字段采用公开 POD 风格（框架内部直接读写），行为方法只保留最小集合。
// 注意：SetText 只保存指针（零拷贝），要求字符串生命周期覆盖整个运行期；
//       需要短生命周期文本时用 SetTextCopy 拷入内部缓冲。
#ifndef MONO_ITEM_H
#define MONO_ITEM_H

#include "mono_defs.h"
#include <string.h>

class MonoItem {
public:
    // ---- 基本属性（由 MonoPage::AddItemRaw 初始化）----
    ItemType       type       = ItemType::ITEM_TEXT; // 控件类型
    const char*    text       = "";                  // 文本（零拷贝指针）
    const uint8_t* iconSrc    = nullptr;             // 图标（图标页使用）
    const uint8_t* statusIcon = nullptr;             // 行尾状态位图（可选）
    MonoCallback   callback   = nullptr;             // 按下/切换时的回调
    uintptr_t      param1     = 0;                   // 类型相关参数 1
    uintptr_t      param2     = 0;                   // 类型相关参数 2

    // ---- 数值类控件（ITEM_VALUE / ITEM_PROGRESS_BAR）----
    float value    = 0.0f;     // 当前值
    float valueMin = 0.0f;     // 最小值
    float valueMax = 100.0f;   // 最大值

    // ---- 状态位 ----
    // bit0 = 开/勾选，bit1 = 数值编辑中
    uint8_t state = 0;

    // ---- 内部缓冲 ----
    char textBuf[48]  = {0};   // SetTextCopy 的目标缓冲
    char valueBuf[16] = {0};   // drawRow 格式化数值用（避免每帧临时分配）

    MonoMarquee marquee;       // 跑马灯状态机（ITEM_TEXT_MARQUEE 使用）

    // ---- 开关 / 勾选状态 ----
    bool IsOn()  const { return (state & 0x01) != 0; }
    void SetOn(bool v) { if (v) state |= 0x01; else state &= (uint8_t)~0x01; }
    void Toggle()      { state ^= 0x01; }

    // ---- 数值编辑状态（ITEM_VALUE）----
    bool IsEditing() const    { return (state & 0x02) != 0; }
    void SetEditing(bool v)   { if (v) state |= 0x02; else state &= (uint8_t)~0x02; }

    // ---- 文本 ----
    void SetText(const char* src) { text = src ? src : ""; }   // 零拷贝

    void SetTextCopy(const char* src) {                        // 拷贝进内部缓冲
        if (!src) src = "";
        strncpy(textBuf, src, sizeof(textBuf) - 1);
        textBuf[sizeof(textBuf) - 1] = '\0';
        text = textBuf;
    }

    const char* GetText() const { return text ? text : ""; }

    // ---- 数值 ----
    void SetRange(float lo, float hi) { valueMin = lo; valueMax = hi; }
    void SetValue(float v)            { value = monoClampf(v, valueMin, valueMax); }

    // 左右方向键调值：步长 = 量程 / 100
    void Adjust(int dir) {
        const float span = (valueMax - valueMin);
        const float step = span > 0.0f ? span / 100.0f : 1.0f;
        SetValue(value + dir * step);
    }

    // ---- 类型相关取值 ----
    MonoPage* TargetPage() const;                            // ITEM_CALL_PAGE 的目标页
    void*     Context()    const { return monoFromUptr<void*>(param2); } // 回调附加参数

    // 按下处理：跳页 / 触发回调 / 切换开关 / 进入编辑
    bool Activate(MonoUI* ui);
};

#endif // MONO_ITEM_H
