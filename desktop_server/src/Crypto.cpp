#include "Crypto.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

#pragma comment(lib, "bcrypt.lib")

namespace Crypto {
namespace {

void ThrowIfFailed(NTSTATUS status, const char* what) {
    if (!BCRYPT_SUCCESS(status)) {
        throw std::runtime_error(std::string(what) + " failed (status 0x" + std::to_string(static_cast<unsigned long>(status)) + ")");
    }
}

// BCrypt's ECC key blobs (BCRYPT_ECCKEY_BLOB) store the public point as a
// magic/size header followed by big-endian X and Y, with no format prefix.
// This project's wire format is the standard SEC1 uncompressed point
// (0x04 || X || Y) so it matches what android_app derives from a JCA
// ECPublicKey. These helpers convert between the two at the boundary.
std::vector<uint8_t> ToEccPublicBlob(const PublicKey& wireKey) {
    constexpr size_t kCoordSize = (kPublicKeySize - 1) / 2;  // 32 bytes for P-256
    std::vector<uint8_t> blob(sizeof(BCRYPT_ECCKEY_BLOB) + 2 * kCoordSize);

    auto* header = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data());
    header->dwMagic = BCRYPT_ECDH_PUBLIC_P256_MAGIC;
    header->cbKey = static_cast<ULONG>(kCoordSize);

    // wireKey[0] is the 0x04 uncompressed-point marker; skip it.
    std::memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), wireKey.data() + 1, 2 * kCoordSize);
    return blob;
}

PublicKey FromEccPublicBlob(const uint8_t* blob, size_t blobLen) {
    constexpr size_t kCoordSize = (kPublicKeySize - 1) / 2;
    if (blobLen < sizeof(BCRYPT_ECCKEY_BLOB) + 2 * kCoordSize) {
        throw std::runtime_error("ECC public key blob too short");
    }

    PublicKey wireKey{};
    wireKey[0] = 0x04;
    std::memcpy(wireKey.data() + 1, blob + sizeof(BCRYPT_ECCKEY_BLOB), 2 * kCoordSize);
    return wireKey;
}

std::array<uint8_t, 32> HmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t dataLen) {
    BCRYPT_ALG_HANDLE algHandle = nullptr;
    ThrowIfFailed(
        BCryptOpenAlgorithmProvider(&algHandle, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG),
        "BCryptOpenAlgorithmProvider(SHA256/HMAC)");

    BCRYPT_HASH_HANDLE hashHandle = nullptr;
    NTSTATUS status = BCryptCreateHash(algHandle, &hashHandle, nullptr, 0, const_cast<PUCHAR>(key),
                                        static_cast<ULONG>(keyLen), 0);
    if (BCRYPT_SUCCESS(status) && dataLen > 0) {
        status = BCryptHashData(hashHandle, const_cast<PUCHAR>(data), static_cast<ULONG>(dataLen), 0);
    }

    std::array<uint8_t, 32> result{};
    if (BCRYPT_SUCCESS(status)) {
        status = BCryptFinishHash(hashHandle, result.data(), static_cast<ULONG>(result.size()), 0);
    }

    if (hashHandle) BCryptDestroyHash(hashHandle);
    BCryptCloseAlgorithmProvider(algHandle, 0);
    ThrowIfFailed(status, "HMAC-SHA256");
    return result;
}

}  // namespace

void RandomBytes(uint8_t* buffer, size_t length) {
    ThrowIfFailed(
        BCryptGenRandom(nullptr, buffer, static_cast<ULONG>(length), BCRYPT_USE_SYSTEM_PREFERRED_RNG),
        "BCryptGenRandom");
}

EcdhKeyPair::EcdhKeyPair() : algHandle_(nullptr), keyHandle_(nullptr) {
    BCRYPT_ALG_HANDLE algHandle = nullptr;
    ThrowIfFailed(BCryptOpenAlgorithmProvider(&algHandle, BCRYPT_ECDH_P256_ALGORITHM, nullptr, 0),
                  "BCryptOpenAlgorithmProvider(ECDH_P256)");
    algHandle_ = algHandle;

    BCRYPT_KEY_HANDLE keyHandle = nullptr;
    NTSTATUS status = BCryptGenerateKeyPair(algHandle, &keyHandle, 256, 0);
    if (BCRYPT_SUCCESS(status)) {
        status = BCryptFinalizeKeyPair(keyHandle, 0);
    }
    if (!BCRYPT_SUCCESS(status)) {
        if (keyHandle) BCryptDestroyKey(keyHandle);
        BCryptCloseAlgorithmProvider(algHandle, 0);
        ThrowIfFailed(status, "BCryptGenerateKeyPair/FinalizeKeyPair");
    }
    keyHandle_ = keyHandle;

    ULONG blobSize = 0;
    ThrowIfFailed(
        BCryptExportKey(keyHandle, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &blobSize, 0),
        "BCryptExportKey(size query)");
    std::vector<uint8_t> blob(blobSize);
    ThrowIfFailed(
        BCryptExportKey(keyHandle, nullptr, BCRYPT_ECCPUBLIC_BLOB, blob.data(), blobSize, &blobSize, 0),
        "BCryptExportKey");
    publicKey_ = FromEccPublicBlob(blob.data(), blob.size());
}

EcdhKeyPair::~EcdhKeyPair() {
    if (keyHandle_) BCryptDestroyKey(static_cast<BCRYPT_KEY_HANDLE>(keyHandle_));
    if (algHandle_) BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(algHandle_), 0);
}

SharedSecret EcdhKeyPair::AgreeWith(const PublicKey& peerPublicKey) const {
    std::vector<uint8_t> peerBlob = ToEccPublicBlob(peerPublicKey);

    BCRYPT_KEY_HANDLE peerKeyHandle = nullptr;
    ThrowIfFailed(BCryptImportKeyPair(static_cast<BCRYPT_ALG_HANDLE>(algHandle_), nullptr, BCRYPT_ECCPUBLIC_BLOB,
                                       &peerKeyHandle, peerBlob.data(), static_cast<ULONG>(peerBlob.size()), 0),
                  "BCryptImportKeyPair(peer public key)");

    BCRYPT_SECRET_HANDLE secretHandle = nullptr;
    NTSTATUS status = BCryptSecretAgreement(static_cast<BCRYPT_KEY_HANDLE>(keyHandle_), peerKeyHandle, &secretHandle, 0);

    SharedSecret secret{};
    if (BCRYPT_SUCCESS(status)) {
        ULONG resultSize = 0;
        status = BCryptDeriveKey(secretHandle, BCRYPT_KDF_RAW_SECRET, nullptr, secret.data(),
                                  static_cast<ULONG>(secret.size()), &resultSize, 0);
    }

    if (secretHandle) BCryptDestroySecret(secretHandle);
    BCryptDestroyKey(peerKeyHandle);
    ThrowIfFailed(status, "BCryptSecretAgreement/DeriveKey");

    // CNG's raw secret agreement output is little-endian; reverse it to the
    // standard big-endian representation (what android_app's JCA-based
    // ECDH shared secret is, and what we feed into HKDF).
    std::reverse(secret.begin(), secret.end());
    return secret;
}

std::vector<uint8_t> Hkdf(const uint8_t* secret, size_t secretLen, const uint8_t* salt, size_t saltLen,
                           const std::string& info, size_t outputLen) {
    // RFC 5869: Extract, then Expand.
    auto prk = HmacSha256(salt, saltLen, secret, secretLen);

    std::vector<uint8_t> okm;
    std::vector<uint8_t> previousBlock;
    uint8_t counter = 1;
    while (okm.size() < outputLen) {
        std::vector<uint8_t> input;
        input.insert(input.end(), previousBlock.begin(), previousBlock.end());
        input.insert(input.end(), info.begin(), info.end());
        input.push_back(counter);

        auto block = HmacSha256(prk.data(), prk.size(), input.data(), input.size());
        previousBlock.assign(block.begin(), block.end());
        okm.insert(okm.end(), block.begin(), block.end());
        ++counter;
    }
    okm.resize(outputLen);
    return okm;
}

namespace {

// Shared setup for AES-256-GCM seal/open.
struct GcmKeyHandle {
    BCRYPT_ALG_HANDLE algHandle = nullptr;
    BCRYPT_KEY_HANDLE keyHandle = nullptr;

    explicit GcmKeyHandle(const AeadKey& key) {
        ThrowIfFailed(BCryptOpenAlgorithmProvider(&algHandle, BCRYPT_AES_ALGORITHM, nullptr, 0),
                      "BCryptOpenAlgorithmProvider(AES)");
        NTSTATUS status = BCryptSetProperty(algHandle, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                                             sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
        if (BCRYPT_SUCCESS(status)) {
            status = BCryptGenerateSymmetricKey(algHandle, &keyHandle, nullptr, 0,
                                                 const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0);
        }
        if (!BCRYPT_SUCCESS(status)) {
            BCryptCloseAlgorithmProvider(algHandle, 0);
            ThrowIfFailed(status, "AES-GCM key setup");
        }
    }

    ~GcmKeyHandle() {
        if (keyHandle) BCryptDestroyKey(keyHandle);
        if (algHandle) BCryptCloseAlgorithmProvider(algHandle, 0);
    }

    GcmKeyHandle(const GcmKeyHandle&) = delete;
    GcmKeyHandle& operator=(const GcmKeyHandle&) = delete;
};

}  // namespace

SealedPacket Seal(const AeadKey& key, const AeadNonce& nonce, const uint8_t* aad, size_t aadLen,
                   const uint8_t* plaintext, size_t plaintextLen) {
    GcmKeyHandle gcm(key);

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = const_cast<PUCHAR>(nonce.data());
    authInfo.cbNonce = static_cast<ULONG>(nonce.size());
    authInfo.pbAuthData = aadLen > 0 ? const_cast<PUCHAR>(aad) : nullptr;
    authInfo.cbAuthData = static_cast<ULONG>(aadLen);

    SealedPacket sealed;
    sealed.ciphertext.resize(plaintextLen);
    authInfo.pbTag = sealed.tag.data();
    authInfo.cbTag = static_cast<ULONG>(sealed.tag.size());

    ULONG resultSize = 0;
    ThrowIfFailed(BCryptEncrypt(gcm.keyHandle, plaintextLen > 0 ? const_cast<PUCHAR>(plaintext) : nullptr,
                                 static_cast<ULONG>(plaintextLen), &authInfo, nullptr, 0,
                                 plaintextLen > 0 ? sealed.ciphertext.data() : nullptr,
                                 static_cast<ULONG>(sealed.ciphertext.size()), &resultSize, 0),
                  "BCryptEncrypt(AES-GCM)");
    return sealed;
}

std::optional<std::vector<uint8_t>> Open(const AeadKey& key, const AeadNonce& nonce, const uint8_t* aad,
                                          size_t aadLen, const uint8_t* ciphertext, size_t ciphertextLen,
                                          const AeadTag& tag) {
    GcmKeyHandle gcm(key);

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = const_cast<PUCHAR>(nonce.data());
    authInfo.cbNonce = static_cast<ULONG>(nonce.size());
    authInfo.pbAuthData = aadLen > 0 ? const_cast<PUCHAR>(aad) : nullptr;
    authInfo.cbAuthData = static_cast<ULONG>(aadLen);
    authInfo.pbTag = const_cast<PUCHAR>(tag.data());
    authInfo.cbTag = static_cast<ULONG>(tag.size());

    std::vector<uint8_t> plaintext(ciphertextLen);
    ULONG resultSize = 0;
    NTSTATUS status = BCryptDecrypt(gcm.keyHandle, ciphertextLen > 0 ? const_cast<PUCHAR>(ciphertext) : nullptr,
                                     static_cast<ULONG>(ciphertextLen), &authInfo, nullptr, 0,
                                     ciphertextLen > 0 ? plaintext.data() : nullptr,
                                     static_cast<ULONG>(plaintext.size()), &resultSize, 0);
    if (!BCRYPT_SUCCESS(status)) {
        return std::nullopt;
    }
    return plaintext;
}

}  // namespace Crypto
