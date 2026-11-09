#ifndef DOT_ITEM_H
#define DOT_ITEM_H

#include "dot_defs.h"
#include <string.h>

class DotItem{
    public:

    // ---- 基本属性（由 DotPage::AddItemRaw 初始化）----
    ItemType       type       = ItemType::ITEM_TEXT;
    const char*    text       = "";      // 零拷贝指针
    const uint8_t* iconSrc    = nullptr; // 图标（图标页用）
    const uint8_t* statusIcon = nullptr; // 行尾状态位图（可选）
    DotCallback   callback   = nullptr;
    uintptr_t      param1     = 0;       // 类型相关参数（见 AddItemRaw 注释）
    uintptr_t      param2     = 0;

    // 数值类控件（ITEM_VALUE / ITEM_PROGRESS_BAR）
    float value = 0.0f, valueMin = 0.0f, valueMax = 100.0f;

    // 状态位：bit0 = 开/勾选，bit1 = 数值编辑中
    uint8_t state = 0;

    char textBuf[48]  = {0};   // SetTextCopy 的目标缓冲
    char valueBuf[16] = {0};   // 每帧格式化数值用（避免临时分配）

    DotMarquee marquee;      // 跑马灯状态机

    bool IsOn()  const { return (state & 0x01) != 0; }
    void SetOn(bool v) { if (v) state |= 0x01; else state &= (uint8_t)~0x01; }
    void Toggle()      { state ^= 0x01; }

    bool IsEditing() const  { return (state & 0x02) != 0; }
    void SetEditing(bool v) { if (v) state |= 0x02; else state &= (uint8_t)~0x02; }

    // ---- 文本：零拷贝 vs 拷贝 ----
    void SetText(const char* src) { text = src ? src : ""; }
    void SetTextCopy(const char* src) {
        if (!src) src = "";
        strncpy(textBuf, src, sizeof(textBuf) - 1);
        textBuf[sizeof(textBuf) - 1] = '\0';
        text = textBuf;
    }
    const char* GetText() const { return text ? text : ""; }

    // ---- 数值 ----
    void SetRange(float lo, float hi) { valueMin = lo; valueMax = hi; }
    void SetValue(float v)            { value = dotClampf(v, valueMin, valueMax); }
    // 左右键调值：步长 = 量程 / 100（任何量程 100 步走完）
    void Adjust(int dir) {
        const float span = valueMax - valueMin;
        const float step = span > 0.0f ? span / 100.0f : 1.0f;
        SetValue(value + dir * step);
    }

    DotPage* TargetPage() const;                            // 跳转目标页
    void*     Context() const { return dotFromUptr<void*>(param2); }

    bool Activate(DotUI* ui);   // 按下处理（实现在 dot_item.cpp）
};

#endif