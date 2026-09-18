#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

#include "InputSimulator.h"
#include "MotionProcessor.h"
#include "SecureSession.h"
#include "UdpServer.h"

// Parses raw datagram bytes, drives the SecureSession handshake for
// whichever peer is currently connecting/connected, and dispatches
// authenticated packets to an InputSimulator (clicks/keys) or a
// MotionProcessor (sensor/calibration). Separated from UdpServer
// (networking) so each piece has one reason to change.
//
// Only one peer is tracked at a time, matching this app's existing
// single-client design.
class PacketHandler {
public:
    PacketHandler(InputSimulator& inputSimulator, MotionProcessor& motionProcessor, UdpServer& udpServer,
                  bool requireConfirmation);

    void Handle(const UdpEndpoint& sender, const uint8_t* data, size_t length);

    // Blocks the calling thread until a HELLO has produced a confirmation
    // code for the operator to approve. Internet mode only.
    uint32_t WaitForConfirmationCode();

    // The operator's answer to the confirmation code prompt. Internet mode only.
    void OnOperatorDecision(bool accept);

private:
    InputSimulator& inputSimulator_;
    MotionProcessor& motionProcessor_;
    UdpServer& udpServer_;
    bool requireConfirmation_;

    std::mutex mutex_;
    std::condition_variable confirmationReady_;
    std::optional<SecureSession> session_;
    std::optional<UdpEndpoint> peerAddr_;

    void HandleHello(const UdpEndpoint& sender, const uint8_t* data, size_t length);
    void DispatchPacket(const std::vector<uint8_t>& packetBytes);
};
