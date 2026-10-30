// mono_input.cpp —— 按键去抖与事件判定
#include "mono_input.h"

namespace {
constexpr uint32_t kDebounceMs = 8;   // 电平去抖窗口
}

void MonoInput::Begin() {
    for (int i = 0; i < KEY_COUNT; ++i) st_[i] = KeyState();
    head_ = tail_ = 0;
}

// 电平变化入口：去抖通过后记录按下/松开。
// 松开且未触发过长按时产生一次 EV_CLICK。
void MonoInput::SetKey(Key k, bool down, uint32_t now) {
    if (k <= KEY_NONE || k >= KEY_COUNT) return;

    KeyState& s = st_[k];
    if (down == s.down) return;                                   // 电平未变
    if (s.changedAt != 0 && (now - s.changedAt) < kDebounceMs) return; // 抖动

    s.changedAt = now;
    s.down      = down;

    if (down) {
        s.downAt    = now;      // 开始新一轮按住计时
        s.longFired = false;
        s.lastRep   = now;
    } else {
        if (!s.longFired) push(EV_CLICK, k);   // 短按：松开时确认
    }
}

// 每帧扫描按住的键：先判长按，之后按周期连发
void MonoInput::Update(uint32_t now) {
    for (int i = KEY_UP; i < KEY_COUNT; ++i) {
        KeyState& s = st_[i];
        if (!s.down) continue;

        const uint32_t held = now - s.downAt;

        // 尚未触发长按：到达阈值时发一次 EV_LONG
        if (!s.longFired) {
            if (held >= MonoAnim::kLongPressMs) {
                s.longFired = true;
                s.lastRep   = now;
                push(EV_LONG, (Key)i);
            }
            continue;
        }

        // 已长按：超过连发延迟后按固定速率连发
        if (held >= MonoAnim::kRepeatDelayMs &&
            (now - s.lastRep) >= MonoAnim::kRepeatRateMs) {
            s.lastRep = now;
            push(EV_REPEAT, (Key)i);
        }
    }
}

void MonoInput::push(Event ev, Key k) {
    const uint8_t next = (uint8_t)((tail_ + 1) % kQueueLen);
    if (next == head_) return;   // 队满丢弃，UI 场景可接受
    q_[tail_].ev = ev;
    q_[tail_].k  = k;
    tail_ = next;
}

bool MonoInput::Pop(Event& ev, Key& k) {
    if (head_ == tail_) return false;
    ev = q_[head_].ev;
    k  = q_[head_].k;
    head_ = (uint8_t)((head_ + 1) % kQueueLen);
    return true;
}

bool MonoInput::IsDown(Key k) const {
    if (k <= KEY_NONE || k >= KEY_COUNT) return false;
    return st_[k].down;
}
