#include "PacketHandler.h"

#include "Protocol.h"

PacketHandler::PacketHandler(InputSimulator& inputSimulator, MotionProcessor& motionProcessor, UdpServer& udpServer,
                              bool requireConfirmation)
    : inputSimulator_(inputSimulator),
      motionProcessor_(motionProcessor),
      udpServer_(udpServer),
      requireConfirmation_(requireConfirmation) {}

// Handle() runs on the UDP receive thread; WaitForConfirmationCode() and
// OnOperatorDecision() run on the console thread while Internet mode's
// pairing prompt is up. mutex_ guards session_/peerAddr_ across both.
void PacketHandler::Handle(const UdpEndpoint& sender, const uint8_t* data, size_t length) {
    if (length == 0) return;

    std::lock_guard<std::mutex> lock(mutex_);

    if (static_cast<Protocol::PacketType>(data[0]) == Protocol::PacketType::kHello) {
        HandleHello(sender, data, length);
        return;
    }

    if (!session_ || !peerAddr_ || sender != *peerAddr_) return;  // only the current peer may send data packets

    auto packetBytes = session_->Decrypt(data, length);
    if (!packetBytes) return;  // failed authentication, replay, or malformed envelope
    if (session_->phase() != SecureSession::Phase::kConfirmed) return;  // still awaiting operator decision

    DispatchPacket(*packetBytes);
}

// Precondition: mutex_ is held by the caller (Handle()).
void PacketHandler::HandleHello(const UdpEndpoint& sender, const uint8_t* data, size_t length) {
    auto hello = Protocol::ParseHello(data, length);
    if (!hello) return;

    // Don't let an unsolicited HELLO (e.g. from a stranger scanning the
    // internet in Internet mode) silently displace an already-confirmed
    // session with a different peer.
    if (session_ && session_->phase() == SecureSession::Phase::kConfirmed && peerAddr_ && sender != *peerAddr_) {
        return;
    }

    session_.emplace(requireConfirmation_);
    peerAddr_ = sender;

    auto helloAck = session_->HandleHello(*hello);
    udpServer_.SendTo(sender, helloAck.data(), helloAck.size());

    if (!requireConfirmation_) {
        auto confirmEnvelope = session_->Confirm();
        udpServer_.SendTo(sender, confirmEnvelope.data(), confirmEnvelope.size());
    } else {
        confirmationReady_.notify_all();
    }
}

uint32_t PacketHandler::WaitForConfirmationCode() {
    std::unique_lock<std::mutex> lock(mutex_);
    confirmationReady_.wait(lock, [this] {
        return session_.has_value() && session_->phase() == SecureSession::Phase::kAwaitingConfirm;
    });
    return session_->ConfirmationCode();
}

void PacketHandler::OnOperatorDecision(bool accept) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!session_ || !peerAddr_) return;
    auto envelope = accept ? session_->Confirm() : session_->Reject();
    udpServer_.SendTo(*peerAddr_, envelope.data(), envelope.size());
}

void PacketHandler::DispatchPacket(const std::vector<uint8_t>& packetBytes) {
    auto packet = Protocol::Parse(packetBytes.data(), packetBytes.size());
    if (!packet) return;

    if (auto* sensor = std::get_if<Protocol::SensorPacket>(&*packet)) {
        motionProcessor_.OnSensor(*sensor, inputSimulator_);
    } else if (auto* click = std::get_if<Protocol::ClickPacket>(&*packet)) {
        inputSimulator_.Click(click->button);
    } else if (auto* key = std::get_if<Protocol::KeyPacket>(&*packet)) {
        if (key->action == Protocol::KeyAction::kChar) {
            inputSimulator_.TypeChar(key->character);
        } else {
            inputSimulator_.PressBackspace();
        }
    } else if (auto* calibrate = std::get_if<Protocol::CalibratePacket>(&*packet)) {
        motionProcessor_.Calibrate(*calibrate);
    } else if (auto* config = std::get_if<Protocol::ConfigPacket>(&*packet)) {
        switch (static_cast<Protocol::ConfigOpt>(config->option)) {
            case Protocol::ConfigOpt::yawMaxPercent:
                motionProcessor_.setYawMaxPercent(config->data.f);
                break;
            case Protocol::ConfigOpt::pitchMaxPercent:
                motionProcessor_.setPitchMaxPercent(config->data.f);
                break;
        }
    }
}
