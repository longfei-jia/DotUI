#include "dot_input.h"

namespace { constexpr uint32_t kDebounceMs = 8; }

void DotInput::Begin() {
    for (int i = 0; i < KEY_COUNT; ++i) st_[i] = KeyState();
    head_ = tail_ = 0;
}

void DotInput::SetKey(Key k, bool down, uint32_t now){
    if (k <= KEY_NONE || k >= KEY_COUNT) return;
    KeyState& s = st_[k];
    if(down == s.down) return;
    if(s.changedAt != 0 && (now - s.changedAt) < kDebounceMs) return;

    s.changedAt = now;
    s.down = down;

    if(down){
        s.downAt = now;
        s.longFired = false;
        s.lastRep = now;
    }else{
        if(!s.longFired) push(EV_CLICK, k);
    }

}

void DotInput::Update(uint32_t now) {
    for (int i = KEY_UP; i < KEY_COUNT; ++i) {
        KeyState& s = st_[i];
        if (!s.down) continue;

        const uint32_t held = now - s.downAt;

        if (!s.longFired) {
            if (held >= DotAnim::kLongPressMs) {
                s.longFired = true;
                s.lastRep   = now;
                push(EV_LONG, (Key)i);
            }
            continue;
        }
        if (held >= DotAnim::kRepeatDelayMs &&
            (now - s.lastRep) >= DotAnim::kRepeatRateMs) {
            s.lastRep = now;
            push(EV_REPEAT, (Key)i);
        }
    }
}

void DotInput::push(Event ev, Key k) {
    const uint8_t next = (uint8_t)((tail_ + 1) % kQueueLen);
    if (next == head_) return;          // 队满丢弃
    q_[tail_].ev = ev;
    q_[tail_].k  = k;
    tail_ = next;
}

bool DotInput::Pop(Event& ev, Key& k) {
    if (head_ == tail_) return false;
    ev = q_[head_].ev;
    k  = q_[head_].k;
    head_ = (uint8_t)((head_ + 1) % kQueueLen);
    return true;
}

bool DotInput::IsDown(Key k) const {
    if (k <= KEY_NONE || k >= KEY_COUNT) return false;
    return st_[k].down;
}