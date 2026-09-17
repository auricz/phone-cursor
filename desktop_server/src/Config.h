#pragma once

// Central place for every tunable value used by the desktop server.
// Keep networking and buffer-sizing constants here instead of scattering
// literals through the code, per project convention.
namespace Config {

constexpr unsigned short kDefaultPort = 24800;
constexpr int kReceiveBufferSize = 64;

// Motion tracking (see MotionProcessor for how these are used). The phone
// sends its raw device-frame orientation only; all of the world-frame
// projection and angle-to-cursor mapping tuning lives here instead of being
// split across both apps.

// Fraction of a 90-degree turn (from the calibrated flat pose) needed for
// the cursor to reach the screen edge. 1.0 means a full 90-degree tilt/turn
// is needed; 0.5 means only 45 degrees is needed (more sensitive).
constexpr float kEdgeRotationFraction = 0.5f;

// Flip these if a phone's yaw (left/right turn) or pitch (up/down tilt)
// ends up moving the cursor the opposite of the expected direction.
constexpr bool kInvertYaw = false;
constexpr bool kInvertPitch = false;

}  // namespace Config
