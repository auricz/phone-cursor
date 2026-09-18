#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <variant>
#include <vector>

// Wire format shared with android_app's Protocol.kt. Every packet is a
// small, fixed-layout, little-endian UDP datagram:
//
//   SENSOR    : u8 type=1, float32 qw, qx, qy, qz (device orientation,
//               game rotation vector)                            (17 bytes)
//   CLICK     : u8 type=2, u8 button (0=left, 1=right)            (2 bytes)
//   KEY       : u8 type=3, u8 action (0=char, 1=backspace),
//               u16 char (UTF-16 code unit, only used when action=0)
//                                                                  (4 bytes)
//   CALIBRATE : u8 type=4, float32 qw, qx, qy, qz (device
//               orientation to treat as the reference pose)      (17 bytes)
//
// The world-frame projection (comparing SENSOR's orientation against the
// pose captured by the last CALIBRATE to get an absolute cursor position)
// is done entirely on the desktop; see MotionProcessor. Windows only ever
// runs on little-endian x86/x64, so raw bytes can be copied directly into
// the integer/float fields below without manual byte swapping. Keep both
// sides of this format in sync when changing it.
//
// SENSOR/CLICK/KEY/CALIBRATE/CONFIG packets are never sent on the wire in
// the layout above once a session is established - they're wrapped in a
// SecureEnvelope (see below and SecureSession) for integrity, and KEY
// packets additionally for confidentiality, per this project's security
// requirements. The layout above is what's inside the envelope.
//
// Two more packet types bootstrap that envelope's keys:
//
//   HELLO     : u8 type=6, 65-byte ECDH public key (SEC1 uncompressed
//               point: 0x04 || X || Y), 16-byte random nonce   (82 bytes)
//   HELLO_ACK : u8 type=7, same layout as HELLO, desktop's key/nonce
//                                                                (82 bytes)
//   HANDSHAKE_CONFIRM : u8 type=8, sent (via SecureEnvelope, empty body)
//               once the desktop operator accepts the confirmation code.
//   HANDSHAKE_REJECT  : u8 type=9, sent (via SecureEnvelope, empty body)
//               if the operator declines it instead.
//
// SecureEnvelope: u8 innerType, u64 counter (big-endian, unique per
// direction, monotonically increasing), 16-byte AES-256-GCM tag, then the
// body - ciphertext for KEY, plaintext (also covered by the tag as
// associated data) for everything else.
namespace Protocol {

enum class PacketType : uint8_t {
    kSensor = 1,
    kClick = 2,
    kKey = 3,
    kCalibrate = 4,
    kConfig = 5,
    kHello = 6,
    kHelloAck = 7,
    kHandshakeConfirm = 8,
    kHandshakeReject = 9,
};

// Returns true if a packet type's body must stay confidential (encrypted)
// once wrapped in a SecureEnvelope, not just authenticated.
inline bool RequiresConfidentiality(PacketType type) {
    return type == PacketType::kKey;
}

enum class Button : uint8_t {
    kLeft = 0,
    kRight = 1,
};

enum class KeyAction : uint8_t {
    kChar = 0,
    kBackspace = 1,
};

enum class ConfigOpt : uint8_t {
    yawMaxPercent = 0,
    pitchMaxPercent = 1,
};

struct SensorPacket {
    float qw, qx, qy, qz;
};

struct ClickPacket {
    Button button;
};

struct KeyPacket {
    KeyAction action;
    uint16_t character;  // UTF-16 code unit; only meaningful when action == kChar
};

struct CalibratePacket {
    float qw, qx, qy, qz;
};

struct ConfigPacket {
    uint8_t option;
    union {
        uint32_t i;
        float f;
    } data;
};

using Packet = std::variant<SensorPacket, ClickPacket, KeyPacket, CalibratePacket, ConfigPacket>;

namespace detail {

inline float ReadFloat(const uint8_t* data) {
    float value;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

}  // namespace detail

inline std::optional<Packet> Parse(const uint8_t* data, size_t length) {
    if (length < 1) return std::nullopt;

    switch (static_cast<PacketType>(data[0])) {
        case PacketType::kSensor: {
            if (length < 17) return std::nullopt;
            SensorPacket packet{};
            packet.qw = detail::ReadFloat(data + 1);
            packet.qx = detail::ReadFloat(data + 5);
            packet.qy = detail::ReadFloat(data + 9);
            packet.qz = detail::ReadFloat(data + 13);
            return packet;
        }
        case PacketType::kClick: {
            if (length < 2) return std::nullopt;
            return ClickPacket{static_cast<Button>(data[1])};
        }
        case PacketType::kKey: {
            if (length < 4) return std::nullopt;
            uint16_t character;
            std::memcpy(&character, data + 2, sizeof(character));
            return KeyPacket{static_cast<KeyAction>(data[1]), character};
        }
        case PacketType::kCalibrate: {
            if (length < 17) return std::nullopt;
            CalibratePacket packet{};
            packet.qw = detail::ReadFloat(data + 1);
            packet.qx = detail::ReadFloat(data + 5);
            packet.qy = detail::ReadFloat(data + 9);
            packet.qz = detail::ReadFloat(data + 13);
            return packet;
        }
        case PacketType::kConfig: {
            if (length < 6) return std::nullopt;
            ConfigPacket packet{};
            packet.option = data[1];
            std::memcpy(&packet.data, data + 2, sizeof(uint32_t));
            return packet;
        }
        default:
            return std::nullopt;
    }
}

constexpr size_t kPublicKeySize = 65;
constexpr size_t kHandshakeNonceSize = 16;
constexpr size_t kHelloPacketSize = 1 + kPublicKeySize + kHandshakeNonceSize;

struct HelloPacket {
    std::array<uint8_t, kPublicKeySize> publicKey;
    std::array<uint8_t, kHandshakeNonceSize> nonce;
};

// Shared layout for HELLO and HELLO_ACK; `type` distinguishes them.
inline std::vector<uint8_t> BuildHello(PacketType type, const std::array<uint8_t, kPublicKeySize>& publicKey,
                                        const std::array<uint8_t, kHandshakeNonceSize>& nonce) {
    std::vector<uint8_t> packet;
    packet.reserve(kHelloPacketSize);
    packet.push_back(static_cast<uint8_t>(type));
    packet.insert(packet.end(), publicKey.begin(), publicKey.end());
    packet.insert(packet.end(), nonce.begin(), nonce.end());
    return packet;
}

inline std::optional<HelloPacket> ParseHello(const uint8_t* data, size_t length) {
    if (length < kHelloPacketSize) return std::nullopt;
    HelloPacket packet{};
    std::memcpy(packet.publicKey.data(), data + 1, kPublicKeySize);
    std::memcpy(packet.nonce.data(), data + 1 + kPublicKeySize, kHandshakeNonceSize);
    return packet;
}

constexpr size_t kSecureEnvelopeTagSize = 16;
constexpr size_t kSecureEnvelopeHeaderSize = 1 + 8 + kSecureEnvelopeTagSize;

struct SecureEnvelope {
    PacketType innerType;
    uint64_t counter;
    std::array<uint8_t, kSecureEnvelopeTagSize> tag;
    const uint8_t* body;  // points into the buffer passed to ParseSecureEnvelope
    size_t bodyLength;
};

inline std::vector<uint8_t> BuildSecureEnvelope(PacketType innerType, uint64_t counter,
                                                 const std::array<uint8_t, kSecureEnvelopeTagSize>& tag,
                                                 const uint8_t* body, size_t bodyLength) {
    std::vector<uint8_t> envelope;
    envelope.reserve(kSecureEnvelopeHeaderSize + bodyLength);
    envelope.push_back(static_cast<uint8_t>(innerType));
    for (int shift = 56; shift >= 0; shift -= 8) {
        envelope.push_back(static_cast<uint8_t>((counter >> shift) & 0xFF));
    }
    envelope.insert(envelope.end(), tag.begin(), tag.end());
    envelope.insert(envelope.end(), body, body + bodyLength);
    return envelope;
}

inline std::optional<SecureEnvelope> ParseSecureEnvelope(const uint8_t* data, size_t length) {
    if (length < kSecureEnvelopeHeaderSize) return std::nullopt;

    SecureEnvelope envelope{};
    envelope.innerType = static_cast<PacketType>(data[0]);

    uint64_t counter = 0;
    for (size_t i = 0; i < 8; ++i) {
        counter = (counter << 8) | data[1 + i];
    }
    envelope.counter = counter;

    std::memcpy(envelope.tag.data(), data + 9, kSecureEnvelopeTagSize);
    envelope.body = data + kSecureEnvelopeHeaderSize;
    envelope.bodyLength = length - kSecureEnvelopeHeaderSize;
    return envelope;
}

}  // namespace Protocol
