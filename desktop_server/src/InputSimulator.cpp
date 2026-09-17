#include "InputSimulator.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>

void InputSimulator::MoveCursorTo(float fractionX, float fractionY) {
    const float clampedX = std::clamp(fractionX, -1.f, 1.f);
    const float clampedY = std::clamp(fractionY, -1.f, 1.f);

    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    const int x = static_cast<int>(std::lround((screenWidth - 1) / 2.f * (1.f + clampedX)));
    const int y = static_cast<int>(std::lround((screenHeight - 1) / 2.f * (1.f + clampedY)));
    SetCursorPos(x, y);
}

void InputSimulator::Click(Protocol::Button button) {
    const bool isRight = button == Protocol::Button::kRight;

    INPUT down{};
    down.type = INPUT_MOUSE;
    down.mi.dwFlags = isRight ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;

    INPUT up{};
    up.type = INPUT_MOUSE;
    up.mi.dwFlags = isRight ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;

    INPUT events[] = {down, up};
    SendInput(2, events, sizeof(INPUT));
}

void InputSimulator::TypeChar(uint16_t utf16Char) {
    INPUT down{};
    down.type = INPUT_KEYBOARD;
    down.ki.wScan = utf16Char;
    down.ki.dwFlags = KEYEVENTF_UNICODE;

    INPUT up = down;
    up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

    INPUT events[] = {down, up};
    SendInput(2, events, sizeof(INPUT));
}

void InputSimulator::PressBackspace() {
    INPUT down{};
    down.type = INPUT_KEYBOARD;
    down.ki.wVk = VK_BACK;

    INPUT up{};
    up.type = INPUT_KEYBOARD;
    up.ki.wVk = VK_BACK;
    up.ki.dwFlags = KEYEVENTF_KEYUP;

    INPUT events[] = {down, up};
    SendInput(2, events, sizeof(INPUT));
}
