#pragma once

#include <cstdint>

// Central place for every tunable value used by the desktop server.
namespace Config {

constexpr unsigned short kDefaultPort = 24800;
constexpr int kReceiveBufferSize = 1024;

// Rendezvous server (see rendezvous_server/), used only in Internet mode.
constexpr const char* kRendezvousHost = "localhost";
constexpr uint16_t kRendezvousPort = 8787;
constexpr bool kRendezvousUseTls = false;

// Public STUN server used to discover this device's internet-facing
// IP:port for hole punching (see StunClient). Any standard STUN server works.
constexpr const char* kStunServerHost = "stun.l.google.com";
constexpr uint16_t kStunServerPort = 19302;
constexpr int kStunTimeoutMs = 3000;

// How long to wait for the phone to appear over the internet (rendezvous +
// hole punch + handshake) before giving up.
constexpr int kInternetHandshakeTimeoutMs = 20000;

// Digits in both the pairing code (rendezvous lookup) and the confirmation
// code (visual comparison before an Internet connection is trusted).
constexpr int kPairingCodeDigits = 6;
constexpr int kConfirmationCodeDigits = 6;

// HKDF info strings used to derive independent keys/codes from one ECDH
// shared secret. Any distinct, stable strings work; changing them changes
// what desktop and phone derive, so keep both sides in sync.
constexpr const char* kHkdfInfoConfirmationCode = "phonecursor-confirmation-code-v1";
constexpr const char* kHkdfInfoPhoneToDesktopKey = "phonecursor-phone-to-desktop-key-v1";
constexpr const char* kHkdfInfoDesktopToPhoneKey = "phonecursor-desktop-to-phone-key-v1";

}  // namespace Config
