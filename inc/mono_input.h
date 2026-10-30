// mono_input.h —— 按键输入
//
// 移植层只负责调用 SetKey() 报告按键电平；
// 本类负责去抖、长按、连发判定，并把事件放入 FIFO 队列，
// 由 MonoUI::Tick() 每帧消费。
//
// 事件模型：
//   EV_CLICK  —— 按下后在长按阈值内松开（一次"点击"）
//   EV_LONG   —— 按住超过 kLongPressMs（每按一次只触发一回）
//   EV_REPEAT —— 长按后按 kRepeatRateMs 周期连发
#ifndef MONO_INPUT_H
#define MONO_INPUT_H

#include "mono_defs.h"

class MonoInput {
public:
    // 物理按键（不含 KEY_NONE）
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

    // 输入事件
    enum Event : uint8_t {
        EV_NONE   = 0,
        EV_CLICK  = 1,
        EV_LONG   = 2,
        EV_REPEAT = 3,
    };

    void Begin();                                    // 清空状态与队列
    void SetKey(Key k, bool down, uint32_t now);     // 移植层报告电平（内部去抖）
    void Update(uint32_t now);                       // 每帧调用：判定长按/连发

    bool Pop(Event& ev, Key& k);                     // 取出一个事件，无事件返回 false

    bool IsDown(Key k) const;                        // 当前是否按着（组合键等场景用）

private:
    // 单键状态
    struct KeyState {
        bool     down      = false;   // 当前电平
        bool     longFired = false;   // 本轮按住是否已触发过 EV_LONG
        uint32_t downAt    = 0;       // 按下时刻
        uint32_t lastRep   = 0;       // 上次连发时刻
        uint32_t changedAt = 0;       // 上次电平变化时刻（去抖基准）
    };

    struct Slot { Event ev; Key k; };

    void push(Event ev, Key k);      // 事件入队（队满则丢弃）

    static constexpr int kQueueLen = 8;   // 事件环形队列长度

    KeyState st_[KEY_COUNT];
    Slot     q_[kQueueLen];
    uint8_t  head_ = 0;
    uint8_t  tail_ = 0;
};

#endif // MONO_INPUT_H
