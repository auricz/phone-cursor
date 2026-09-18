#include "SecureSession.h"

#include <cstring>

#include "Config.h"

namespace {

uint32_t Pow10(int exponent) {
    uint32_t result = 1;
    for (int i = 0; i < exponent; ++i) result *= 10;
    return result;
}

// HKDF salt binding both sides' handshake nonces together, so the derived
// keys/code are tied to this exact handshake (not just the ECDH secret).
std::vector<uint8_t> HandshakeSalt(const Crypto::Nonce& phoneNonce, const Crypto::Nonce& desktopNonce) {
    std::vector<uint8_t> salt;
    salt.insert(salt.end(), phoneNonce.begin(), phoneNonce.end());
    salt.insert(salt.end(), desktopNonce.begin(), desktopNonce.end());
    return salt;
}

// Derives a direction's AEAD salt + key together from one HKDF call.
void DeriveDirectionKey(const Crypto::SharedSecret& sharedSecret, const std::vector<uint8_t>& hkdfSalt,
                         const std::string& info, Crypto::AeadSalt& outSalt, Crypto::AeadKey& outKey) {
    auto material = Crypto::Hkdf(sharedSecret.data(), sharedSecret.size(), hkdfSalt.data(), hkdfSalt.size(), info,
                                  Crypto::kAeadSaltSize + Crypto::kAeadKeySize);
    std::memcpy(outSalt.data(), material.data(), Crypto::kAeadSaltSize);
    std::memcpy(outKey.data(), material.data() + Crypto::kAeadSaltSize, Crypto::kAeadKeySize);
}

}  // namespace

SecureSession::SecureSession(bool requireConfirmation)
    : requireConfirmation_(requireConfirmation), phase_(Phase::kAwaitingHello) {}

std::vector<uint8_t> SecureSession::HandleHello(const Protocol::HelloPacket& hello) {
    Crypto::RandomBytes(localNonce_.data(), localNonce_.size());

    Crypto::SharedSecret sharedSecret = localKeyPair_.AgreeWith(hello.publicKey);
    std::vector<uint8_t> hkdfSalt = HandshakeSalt(hello.nonce, localNonce_);

    auto codeBytes = Crypto::Hkdf(sharedSecret.data(), sharedSecret.size(), hkdfSalt.data(), hkdfSalt.size(),
                                   Config::kHkdfInfoConfirmationCode, sizeof(uint32_t));
    uint32_t codeValue = (static_cast<uint32_t>(codeBytes[0]) << 24) | (static_cast<uint32_t>(codeBytes[1]) << 16) |
                          (static_cast<uint32_t>(codeBytes[2]) << 8) | codeBytes[3];
    confirmationCode_ = codeValue % Pow10(Config::kConfirmationCodeDigits);

    DeriveDirectionKey(sharedSecret, hkdfSalt, Config::kHkdfInfoDesktopToPhoneKey, sendSalt_, sendKey_);
    DeriveDirectionKey(sharedSecret, hkdfSalt, Config::kHkdfInfoPhoneToDesktopKey, receiveSalt_, receiveKey_);

    phase_ = requireConfirmation_ ? Phase::kAwaitingConfirm : Phase::kConfirmed;
    return Protocol::BuildHello(Protocol::PacketType::kHelloAck, localKeyPair_.LocalPublicKey(), localNonce_);
}

std::vector<uint8_t> SecureSession::Confirm() {
    phase_ = Phase::kConfirmed;
    return Encrypt(Protocol::PacketType::kHandshakeConfirm, nullptr, 0);
}

std::vector<uint8_t> SecureSession::Reject() {
    phase_ = Phase::kRejected;
    return Encrypt(Protocol::PacketType::kHandshakeReject, nullptr, 0);
}

Crypto::AeadNonce SecureSession::BuildNonce(const Crypto::AeadSalt& salt, uint64_t counter) const {
    Crypto::AeadNonce nonce{};
    std::memcpy(nonce.data(), salt.data(), salt.size());
    for (size_t i = 0; i < sizeof(counter); ++i) {
        nonce[salt.size() + i] = static_cast<uint8_t>((counter >> (8 * (sizeof(counter) - 1 - i))) & 0xFF);
    }
    return nonce;
}

std::vector<uint8_t> SecureSession::Encrypt(Protocol::PacketType type, const uint8_t* payload, size_t payloadLen) {
    uint64_t counter = sendCounter_++;
    Crypto::AeadNonce nonce = BuildNonce(sendSalt_, counter);

    std::vector<uint8_t> aad{static_cast<uint8_t>(type)};
    if (Protocol::RequiresConfidentiality(type)) {
        auto sealed = Crypto::Seal(sendKey_, nonce, aad.data(), aad.size(), payload, payloadLen);
        return Protocol::BuildSecureEnvelope(type, counter, sealed.tag, sealed.ciphertext.data(),
                                              sealed.ciphertext.size());
    }

    aad.insert(aad.end(), payload, payload + payloadLen);
    auto sealed = Crypto::Seal(sendKey_, nonce, aad.data(), aad.size(), nullptr, 0);
    return Protocol::BuildSecureEnvelope(type, counter, sealed.tag, payload, payloadLen);
}

std::optional<std::vector<uint8_t>> SecureSession::Decrypt(const uint8_t* data, size_t length) {
    auto envelope = Protocol::ParseSecureEnvelope(data, length);
    if (!envelope) return std::nullopt;
    if (hasReceivedAny_ && envelope->counter <= highestReceivedCounter_) return std::nullopt;  // replay

    Crypto::AeadNonce nonce = BuildNonce(receiveSalt_, envelope->counter);
    std::vector<uint8_t> aad{static_cast<uint8_t>(envelope->innerType)};

    std::optional<std::vector<uint8_t>> plaintext;
    if (Protocol::RequiresConfidentiality(envelope->innerType)) {
        plaintext = Crypto::Open(receiveKey_, nonce, aad.data(), aad.size(), envelope->body, envelope->bodyLength,
                                  envelope->tag);
    } else {
        aad.insert(aad.end(), envelope->body, envelope->body + envelope->bodyLength);
        if (Crypto::Open(receiveKey_, nonce, aad.data(), aad.size(), nullptr, 0, envelope->tag)) {
            plaintext = std::vector<uint8_t>(envelope->body, envelope->body + envelope->bodyLength);
        }
    }
    if (!plaintext) return std::nullopt;

    highestReceivedCounter_ = envelope->counter;
    hasReceivedAny_ = true;

    std::vector<uint8_t> reconstructed;
    reconstructed.reserve(1 + plaintext->size());
    reconstructed.push_back(static_cast<uint8_t>(envelope->innerType));
    reconstructed.insert(reconstructed.end(), plaintext->begin(), plaintext->end());
    return reconstructed;
}
