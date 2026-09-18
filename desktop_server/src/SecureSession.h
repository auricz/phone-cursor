#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "Crypto.h"
#include "Protocol.h"

// Owns the ECDH handshake and derived AES-256-GCM state for one
// phone<->desktop session (see the SecureEnvelope docs in Protocol.h for
// the wire format this produces/consumes).
//
// Local-mode sessions (requireConfirmation = false) are confirmed
// automatically: the operator already authenticated the peer by typing its
// LAN IP themselves. Internet-mode sessions (requireConfirmation = true)
// sit in kAwaitingConfirm until the operator compares ConfirmationCode()
// against the phone's screen and calls Confirm() or Reject() - this is the
// "confirm the connecting device" step for Internet connections.
class SecureSession {
public:
    enum class Phase {
        kAwaitingHello,
        kAwaitingConfirm,
        kConfirmed,
        kRejected,
    };

    explicit SecureSession(bool requireConfirmation);

    Phase phase() const { return phase_; }

    // Consumes the phone's HELLO: derives session key material and
    // (Internet mode) the confirmation code, and returns the HELLO_ACK
    // bytes to send back.
    std::vector<uint8_t> HandleHello(const Protocol::HelloPacket& hello);

    // Valid only after HandleHello(); the 6-digit code to compare against
    // the phone's screen (Internet mode only - Local mode never shows one).
    uint32_t ConfirmationCode() const { return confirmationCode_; }

    // Moves to kConfirmed and returns the HANDSHAKE_CONFIRM envelope to send.
    std::vector<uint8_t> Confirm();
    // Moves to kRejected and returns the HANDSHAKE_REJECT envelope to send.
    std::vector<uint8_t> Reject();

    // Wraps an outgoing desktop->phone packet body into a SecureEnvelope.
    std::vector<uint8_t> Encrypt(Protocol::PacketType type, const uint8_t* payload, size_t payloadLen);

    // Unwraps an incoming SecureEnvelope. Returns the reconstructed packet
    // bytes (type byte + payload, ready for Protocol::Parse), or
    // std::nullopt if it's malformed, fails authentication, or replays an
    // already-seen counter.
    std::optional<std::vector<uint8_t>> Decrypt(const uint8_t* data, size_t length);

private:
    bool requireConfirmation_;
    Phase phase_;

    Crypto::EcdhKeyPair localKeyPair_;
    Crypto::Nonce localNonce_{};

    Crypto::AeadSalt sendSalt_{};
    Crypto::AeadKey sendKey_{};
    uint64_t sendCounter_ = 0;

    Crypto::AeadSalt receiveSalt_{};
    Crypto::AeadKey receiveKey_{};
    uint64_t highestReceivedCounter_ = 0;
    bool hasReceivedAny_ = false;

    uint32_t confirmationCode_ = 0;

    Crypto::AeadNonce BuildNonce(const Crypto::AeadSalt& salt, uint64_t counter) const;
};
