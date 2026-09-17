#pragma once

#include <cstdint>
#include <cstddef>

#include "InputSimulator.h"
#include "MotionProcessor.h"

// Parses raw datagram bytes and dispatches the result to an InputSimulator
// (clicks/keys) or a MotionProcessor (sensor/calibration). Separated from
// UdpServer (networking) so each piece has one reason to change.
class PacketHandler {
public:
    PacketHandler(InputSimulator& inputSimulator, MotionProcessor& motionProcessor);

    void Handle(const uint8_t* data, size_t length);

private:
    InputSimulator& inputSimulator_;
    MotionProcessor& motionProcessor_;
};
