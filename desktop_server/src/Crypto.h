#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Cryptographic primitives backing the phone<->desktop secure session (see
// SecureSession). Implemented on top of Windows CNG (bcrypt.lib) so no
// third-party crypto library is needed - matches the rest of desktop_server,
// which only ever links Windows SDK libraries.
//
// Wire format note: ECDH public keys are exchanged as a standard SEC1
// uncompressed point (0x04 || X || Y, 65 bytes for P-256) so this matches
// what android_app's JCA-based implementation produces from an
// ECPublicKey's W coordinate. CNG's own key blobs and its raw
// secret-agreement output use different internal encodings; Crypto.cpp
// converts to/from this wire format at the boundary.
namespace Crypto {

constexpr size_t kPublicKeySize = 65;
constexpr size_t kNonceSize = 16;
constexpr size_t kSharedSecretSize = 32;
constexpr size_t kAeadKeySize = 32;
constexpr size_t kAeadSaltSize = 4;
constexpr size_t kAeadNonceSize = 12;  // salt(4) || counter(8)
constexpr size_t kAeadTagSize = 16;

using PublicKey = std::array<uint8_t, kPublicKeySize>;
using Nonce = std::array<uint8_t, kNonceSize>;
using SharedSecret = std::array<uint8_t, kSharedSecretSize>;
using AeadKey = std::array<uint8_t, kAeadKeySize>;
using AeadSalt = std::array<uint8_t, kAeadSaltSize>;
using AeadNonce = std::array<uint8_t, kAeadNonceSize>;
using AeadTag = std::array<uint8_t, kAeadTagSize>;

// Fills buffer with cryptographically random bytes.
void RandomBytes(uint8_t* buffer, size_t length);

// An ephemeral NIST P-256 ECDH key pair, valid for one handshake.
class EcdhKeyPair {
public:
    EcdhKeyPair();
    ~EcdhKeyPair();

    EcdhKeyPair(const EcdhKeyPair&) = delete;
    EcdhKeyPair& operator=(const EcdhKeyPair&) = delete;

    const PublicKey& LocalPublicKey() const { return publicKey_; }

    // Computes the raw ECDH shared secret with a peer's public key.
    SharedSecret AgreeWith(const PublicKey& peerPublicKey) const;

private:
    void* algHandle_;
    void* keyHandle_;
    PublicKey publicKey_;
};

// HKDF-SHA256 (RFC 5869): Extract-then-Expand, truncated/expanded to outputLen bytes.
std::vector<uint8_t> Hkdf(const uint8_t* secret, size_t secretLen,
                           const uint8_t* salt, size_t saltLen,
                           const std::string& info, size_t outputLen);

// AES-256-GCM seal. Pass plaintextLen == 0 (with the data instead placed in
// aad) to authenticate without encrypting - used for packet types that only
// need integrity, not confidentiality.
struct SealedPacket {
    AeadTag tag;
    std::vector<uint8_t> ciphertext;  // same length as plaintextLen
};

SealedPacket Seal(const AeadKey& key, const AeadNonce& nonce, const uint8_t* aad, size_t aadLen,
                   const uint8_t* plaintext, size_t plaintextLen);

// AES-256-GCM open. Returns std::nullopt if the tag doesn't verify.
std::optional<std::vector<uint8_t>> Open(const AeadKey& key, const AeadNonce& nonce, const uint8_t* aad,
                                          size_t aadLen, const uint8_t* ciphertext, size_t ciphertextLen,
                                          const AeadTag& tag);

}  // namespace Crypto
