#pragma once

#include <cstdint>
#include <cstring>
#include <optional>
#include <variant>

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
namespace Protocol {

enum class PacketType : uint8_t {
    kSensor = 1,
    kClick = 2,
    kKey = 3,
    kCalibrate = 4,
    kConfig = 5,
};

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
    yawInvert = 2,
    pitchInvert = 3,
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

}  // namespace Protocol
