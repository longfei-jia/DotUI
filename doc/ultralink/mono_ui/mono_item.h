// mono_item.h —— 列表项
//
// 对应 3rdParty/mono_ui/mono_item.h。
//
// 字段布局参照 UltraLink 固件的实测结果（构造函数 0x08011C68）：
//   +0x08 type      +0x0C text      +0x24 value
//   +0x3C iconSrc   +0x40 callback  +0x44 param/payload
//
// 与原版唯一的差异：原版 text/value 是 std::string（带 SSO，容量 0x20），
// 这里改成 `const char* + 定长缓冲`，每项省 40+ 字节堆，且无碎片风险。
// 语义完全等价 —— 描述文本基本都是字面量，指过去即可。
#ifndef MONO_ITEM_H
#define MONO_ITEM_H

#include "mono_defs.h"
#include <string.h>
#include <stdio.h>

class MonoItem {
public:
    // ---------------------------------------------------------------------
    // 字段（对应固件偏移，公开以便渲染层直接读，减少访问器开销）
    // ---------------------------------------------------------------------
    ItemType       type       = ItemType::ITEM_TEXT;
    const char*    text       = "";        // +0x0C：描述文字
    const uint8_t* iconSrc    = nullptr;   // +0x3C：14x14 1bpp 图标
    const uint8_t* statusIcon = nullptr;   // 行尾 10x10 状态图标
    MonoCallback   callback   = nullptr;   // +0x40
    uintptr_t      param1     = 0;         // +0x44：语义随 type 变化
    uintptr_t      param2     = 0;         //       第二参数

    float          value      = 0.0f;      // ITEM_VALUE 的当前值
    float          valueMin   = 0.0f;
    float          valueMax   = 100.0f;

    uint8_t        state      = 0;         // bit0=on/checked, bit1=editing

    // 动态文本（TextEditor 写回时用；平时 text 直接指向字面量）
    char           textBuf[48]  = {0};
    char           valueBuf[16] = {0};

    // 跑马灯状态（每项独享，切换焦点时由 UI 复位）
    MonoMarquee    marquee;

    // ---------------------------------------------------------------------
    // 状态位
    // ---------------------------------------------------------------------
    bool IsOn()      const { return (state & 0x01) != 0; }
    bool IsEditing() const { return (state & 0x02) != 0; }

    void SetOn(bool v)   { if (v) state |= 0x01; else state &= (uint8_t)~0x01; }
    void Toggle()        { state ^= 0x01; }
    void SetEditing(bool v) { if (v) state |= 0x02; else state &= (uint8_t)~0x02; }

    // ---------------------------------------------------------------------
    // 文本
    // ---------------------------------------------------------------------
    // 零拷贝：直接把指针指过去。要求 src 生命周期足够长（字面量/静态变量）。
    void SetText(const char* src) { text = src ? src : ""; }

    // 拷贝：用于 TextEditor 这类运行时生成的内容。
    void SetTextCopy(const char* src) {
        if (!src) src = "";
        strncpy(textBuf, src, sizeof(textBuf) - 1);
        textBuf[sizeof(textBuf) - 1] = '\0';
        text = textBuf;
    }

    const char* GetText() const { return text ? text : ""; }

    // ---------------------------------------------------------------------
    // 数值
    // ---------------------------------------------------------------------
    void SetRange(float lo, float hi) { valueMin = lo; valueMax = hi; }
    void SetValue(float v) { value = monoClampf(v, valueMin, valueMax); }

    void Adjust(int dir) {
        const float span = (valueMax - valueMin);
        const float step = span > 0.0f ? span / 100.0f : 1.0f;
        SetValue(value + dir * step);
    }

    // ---------------------------------------------------------------------
    // 参数
    // ---------------------------------------------------------------------
    // ITEM_CALL_PAGE 的 param1 是目标页面
    MonoPage* TargetPage() const;

    // 回调上下文：把 param2 当作 void*。截图里 Checkbox 的 param2 写 false/nullptr，
    // 说明它对大多数类型无意义，仅在需要时当上下文用。
    void* Context() const { return monoFromUptr<void*>(param2); }

    // ---------------------------------------------------------------------
    // 命中：某一行的 item 被"按下"时该做什么
    // 返回 true 表示需要 UI 重绘（值变了）
    // ---------------------------------------------------------------------
    bool Activate(MonoUI* ui);
};

#endif // MONO_ITEM_H
