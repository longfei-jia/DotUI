#ifndef DOT_INPUT_H
#define DOT_INPUT_H

#include "dot_defs.h"

class DotInput
{
public:
    enum Key : uint8_t
    {
        KEY_NONE = 0,
        KEY_UP = 1,
        KEY_DOWN = 2,
        KEY_LEFT = 3,
        KEY_RIGHT = 4,
        KEY_OK = 5,
        KEY_BACK = 6 ,
        KEY_COUNT = 7,
    };
    enum Event : uint8_t
    {
        EV_NONE = 0,
        EV_CLICK = 1,
        EV_LONG = 2,
        EV_REPEAT = 3,
    };

    void Begin();
    void SetKey(Key key, bool down, uint32_t now);
    void Update(uint32_t now);
    bool Pop(Event &ev, Key &k);
    bool IsDown(Key k) const;

private:
    struct KeyState
    {
        bool down = false;
        bool longFired = false;
        uint32_t downAt = 0, lastRep = 0, changedAt = 0;
    };
    struct Slot
    {
        Event ev;
        Key k;
    };

    void push(Event ev, Key k);
    static constexpr int kQueueLen = 8;
    KeyState st_[KEY_COUNT];
    Slot q_[kQueueLen];
    uint8_t head_ = 0, tail_ = 0;
};

#endif