#include "MotionProcessor.h"

#include <algorithm>
#include <cmath>

#include "Config.h"

namespace {

constexpr float kQuarterTurnRadians = 1.5707963267948966f;  // 90 degrees

}  // namespace

void MotionProcessor::Calibrate(const Protocol::CalibratePacket& packet) {
    referenceRotation_ = Matrix3x3::FromQuaternion({packet.qw, packet.qx, packet.qy, packet.qz});
    calibrated_ = true;
}

void MotionProcessor::OnSensor(const Protocol::SensorPacket& packet, InputSimulator& inputSimulator) {
    if (!calibrated_) return;  // No reference pose yet; drop until the phone calibrates.

    const Matrix3x3 currentRotation =
        Matrix3x3::FromQuaternion({packet.qw, packet.qx, packet.qy, packet.qz});

    // How the phone has rotated since calibration, expressed in the
    // reference pose's own axes (local X = right, Y = toward the screen,
    // Z = up, since at calibration those line up with the world frame).
    const Matrix3x3 delta = Transpose(referenceRotation_) * currentRotation;

    // Reads delta as Rz(yaw) * Rx(pitch): yaw turns the phone about the
    // vertical axis, then pitch tilts it about its own (already-yawed)
    // right edge. atan2 (rather than asin) keeps the full +-180 degree
    // range well-defined.
    const float yawRadians = std::atan2(delta.m[1][0], delta.m[0][0]);
    const float pitchRadians = std::atan2(delta.m[2][1], delta.m[2][2]);

    const float maxAngleRadiansX = kQuarterTurnRadians * yawMaxPercent;
    const float maxAngleRadiansY = kQuarterTurnRadians * pitchMaxPercent;

    // Yawing the phone's top edge to the right reads as a negative yaw
    // angle (see MotionProcessor.h), so it's negated to make "yaw right"
    // move the cursor right. Pitching up reads as positive, and the screen
    // must move the cursor toward smaller Y (up), so pitch is negated too.
    float xFraction = -yawRadians / maxAngleRadiansX;
    float yFraction = -pitchRadians / maxAngleRadiansY;
    if (invertYaw) xFraction = -xFraction;
    if (invertPitch) yFraction = -yFraction;

    inputSimulator.MoveCursorTo(std::clamp(xFraction, -1.f, 1.f), std::clamp(yFraction, -1.f, 1.f));
}

void MotionProcessor::setYawMaxPercent(float newMax) { yawMaxPercent = std::clamp(newMax, 0.0f, 1.0f); }
void MotionProcessor::setPitchMaxPercent(float newMax) { pitchMaxPercent = std::clamp(newMax, 0.0f, 1.0f); }

void MotionProcessor::setInvertYaw(bool invert) { invertYaw = invert; }
void MotionProcessor::setInvertPitch(bool invert) { invertPitch = invert; }
