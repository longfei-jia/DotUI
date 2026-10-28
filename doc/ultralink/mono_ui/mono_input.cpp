// mono_input.cpp —— 按键事件判定
//
// 对应 3rdParty/mono_ui/mono_input.cpp。
#include "mono_input.h"

namespace {
constexpr uint32_t kDebounceMs = 8;   // 机械按键抖动窗口
}

void MonoInput::Begin() {
    for (int i = 0; i < KEY_COUNT; ++i) st_[i] = KeyState();
    head_ = tail_ = 0;
}

// ---------------------------------------------------------------------------
// 边沿检测
//
// 移植层可以每帧无条件调用（如 SetKey(KEY_OK, gpio_get(), now)），
// 这里会过滤掉重复电平与抖动。
// ---------------------------------------------------------------------------
void MonoInput::SetKey(Key k, bool down, uint32_t now) {
    if (k <= KEY_NONE || k >= KEY_COUNT) return;

    KeyState& s = st_[k];
    if (down == s.down) return;                       // 电平没变
    if (s.changedAt != 0 && (now - s.changedAt) < kDebounceMs) return;  // 抖动

    s.changedAt = now;
    s.down      = down;

    if (down) {
        s.downAt    = now;
        s.longFired = false;
        s.lastRep   = now;
        // 短按在"松开"时才产事件，这样才能区分长短按
    } else {
        if (!s.longFired) push(EV_CLICK, k);
    }
}

// ---------------------------------------------------------------------------
// 长按 / 重复
// ---------------------------------------------------------------------------
void MonoInput::Update(uint32_t now) {
    for (int i = KEY_UP; i < KEY_COUNT; ++i) {
        KeyState& s = st_[i];
        if (!s.down) continue;

        const uint32_t held = now - s.downAt;

        if (!s.longFired) {
            if (held >= MonoAnim::kLongPressMs) {
                s.longFired = true;
                s.lastRep   = now;
                push(EV_LONG, (Key)i);
            }
            continue;
        }

        // 长按之后的连发：先等 kRepeatDelayMs，再按 kRepeatRateMs 重复
        if (held >= MonoAnim::kRepeatDelayMs &&
            (now - s.lastRep) >= MonoAnim::kRepeatRateMs) {
            s.lastRep = now;
            push(EV_REPEAT, (Key)i);
        }
    }
}

// ---------------------------------------------------------------------------
// 队列
// ---------------------------------------------------------------------------
void MonoInput::push(Event ev, Key k) {
    const uint8_t next = (uint8_t)((tail_ + 1) % kQueueLen);
    if (next == head_) return;      // 满则丢弃最旧
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

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------
bool MonoInput::IsDown(Key k) const {
    if (k <= KEY_NONE || k >= KEY_COUNT) return false;
    return st_[k].down;
}

uint32_t MonoInput::HeldMs(Key k, uint32_t now) const {
    if (k <= KEY_NONE || k >= KEY_COUNT) return 0;
    if (!st_[k].down) return 0;
    return now - st_[k].downAt;
}

const char* MonoInput::KeyName(Key k) {
    switch (k) {
    case KEY_UP:    return "UP";
    case KEY_DOWN:  return "DOWN";
    case KEY_LEFT:  return "LEFT";
    case KEY_RIGHT: return "RIGHT";
    case KEY_OK:    return "OK";
    case KEY_BACK:  return "BACK";
    default:        return "-";
    }
}
