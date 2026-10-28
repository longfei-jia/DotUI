// mono_input.h —— 按键输入
//
// 对应 3rdParty/mono_ui/mono_input.h。
//
// 固件的输入不是"中断里直接改 UI"，而是 20ms 节拍采样 + 长按判定
// （TickTask 0x08033EFC）。这里照这个模型：移植层只负责报告电平，
// 事件判定（短按/长按/重复）全部在 Update() 里做。
#ifndef MONO_INPUT_H
#define MONO_INPUT_H

#include "mono_defs.h"

class MonoInput {
public:
    // 按键编号。KEY_BACK 通常是长按 OK 或独立返回键。
    enum Key : uint8_t {
        KEY_NONE  = 0,
        KEY_UP    = 1,
        KEY_DOWN  = 2,
        KEY_LEFT  = 3,
        KEY_RIGHT = 4,
        KEY_OK    = 5,
        KEY_BACK  = 6,
        KEY_COUNT = 7,
    };

    // 判定后的事件
    enum Event : uint8_t {
        EV_NONE   = 0,
        EV_CLICK  = 1,   // 按下并松开
        EV_LONG   = 2,   // 按住超过 kLongPressMs
        EV_REPEAT = 3,   // 长按后持续重复
    };

    void Begin();

    // 移植层调用：报告某个键当前是否按下。
    // 建议放在 20ms 的定时任务里，或在 GPIO 中断里调用。
    void SetKey(Key k, bool down, uint32_t now);

    // 每帧调用一次，产出事件
    void Update(uint32_t now);

    // 取事件。返回 false 表示队列空。
    bool Pop(Event& ev, Key& k);

    bool     IsDown(Key k) const;
    uint32_t HeldMs(Key k, uint32_t now) const;

    static const char* KeyName(Key k);

private:
    struct KeyState {
        bool     down      = false;
        bool     longFired = false;
        uint32_t downAt    = 0;
        uint32_t lastRep   = 0;
        uint32_t changedAt = 0;
    };

    struct Slot { Event ev; Key k; };

    void push(Event ev, Key k);

    static constexpr int kQueueLen = 8;

    KeyState st_[KEY_COUNT];
    Slot     q_[kQueueLen];
    uint8_t  head_ = 0;
    uint8_t  tail_ = 0;
};

#endif // MONO_INPUT_H
