#pragma once

#include <cstdint>

#include "Protocol.h"

// Wraps the Win32 SendInput/cursor APIs for the mouse/keyboard actions this
// app needs. Kept separate from networking/parsing so each class has a
// single responsibility.
class InputSimulator {
public:
    // Moves the cursor to an absolute position on the primary screen.
    // fractionX/fractionY are in [-1, 1], where (0, 0) is screen center,
    // -1 is the left/top edge and 1 is the right/bottom edge. Values
    // outside that range are clamped.
    void MoveCursorTo(float fractionX, float fractionY);
    void Click(Protocol::Button button);
    void TypeChar(uint16_t utf16Char);
    void PressBackspace();
};
