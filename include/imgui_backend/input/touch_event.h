#ifndef NS_INPUT_TOUCH_EVENT_H
#define NS_INPUT_TOUCH_EVENT_H

#include <cstdint>

namespace ns
{

struct TouchEvent
{
    enum Phase
    {
        Phase_Down = 0,
        Phase_Move = 1,
        Phase_Up = 2,
        Phase_Cancel = 3,
    };

    Phase phase = Phase_Move;
    int32_t id = 0;
    float normX = 0.0f;
    float normY = 0.0f;
    float rawX = 0.0f;
    float rawY = 0.0f;
    uint64_t timestampUs = 0;
};

}  // namespace ns

#endif
