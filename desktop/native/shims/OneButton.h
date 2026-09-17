#pragma once
// Desktop replacement for the OneButton library. The firmware's Matrix
// module includes the header but drives debounce itself; nothing here is
// exercised on the desktop port yet. Kept for include compatibility.

#include <stdint.h>

class OneButton
{
public:
    OneButton(uint8_t pin = 0, bool activeLow = true, bool pullupActive = true)
    {
        (void)pin;
        (void)activeLow;
        (void)pullupActive;
    }
    void tick() {}
    void attachClick(void (*)()) {}
    void attachLongPressStart(void (*)()) {}
    void attachDuringLongPress(void (*)()) {}
    void attachLongPressStop(void (*)()) {}
    void attachRelease(void (*)()) {}
    void setClickMs(unsigned long) {}
    void setPressMs(unsigned long) {}
    void debounce(void (*)()) {}
};
