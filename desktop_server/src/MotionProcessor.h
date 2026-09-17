#pragma once

#include "InputSimulator.h"
#include "Math3D.h"
#include "Protocol.h"

// Turns the phone's absolute orientation into an absolute cursor position.
//
// The phone is a thin sensor relay: it only reports its current game
// rotation quaternion (see android_app's MotionTracker). All of the
// "world frame" work happens here:
//   - CALIBRATE captures the phone's orientation at a known physical pose
//     (lying flat, screen up, top edge pointed at the screen) and is kept
//     as the reference rotation; every later SENSOR sample is measured
//     relative to it.
//   - Each sample's rotation, relative to that reference, is decomposed
//     into a yaw angle (turning left/right) and a pitch angle (tilting up/
//     down).
//   - Those angles map directly to an absolute cursor position: flat is
//     screen center, and Config::kEdgeRotationFraction of a 90-degree turn
//     reaches the screen edge. There is no velocity or integration - the
//     cursor position is a pure function of the phone's current tilt.
class MotionProcessor {
public:
    void Calibrate(const Protocol::CalibratePacket& packet);
    void OnSensor(const Protocol::SensorPacket& packet, InputSimulator& inputSimulator);

    void setYawMaxPercent(float newMax);
    void setPitchMaxPercent(float newMax);

    void setInvertYaw(bool invert);
    void setInvertPitch(bool invert);

private:
    bool calibrated_ = false;
    Matrix3x3 referenceRotation_{};

    float yawMaxPercent = 1.0;
    float pitchMaxPercent = 1.0;

    bool invertYaw = false;
    bool invertPitch = false;
};
