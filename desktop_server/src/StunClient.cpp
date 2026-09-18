#include "StunClient.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <cstring>
#include <vector>

#include "Crypto.h"

namespace Stun {
namespace {

constexpr uint32_t kMagicCookie = 0x2112A442;
constexpr uint16_t kBindingRequest = 0x0001;
constexpr uint16_t kBindingSuccessResponse = 0x0101;
constexpr uint16_t kAttrMappedAddress = 0x0001;
constexpr uint16_t kAttrXorMappedAddress = 0x0020;
constexpr size_t kHeaderSize = 20;
constexpr size_t kTransactionIdSize = 12;

void WriteU16(std::vector<uint8_t>& buf, uint16_t value) {
    buf.push_back(static_cast<uint8_t>(value >> 8));
    buf.push_back(static_cast<uint8_t>(value & 0xFF));
}

void WriteU32(std::vector<uint8_t>& buf, uint32_t value) {
    buf.push_back(static_cast<uint8_t>(value >> 24));
    buf.push_back(static_cast<uint8_t>(value >> 16));
    buf.push_back(static_cast<uint8_t>(value >> 8));
    buf.push_back(static_cast<uint8_t>(value & 0xFF));
}

uint16_t ReadU16(const uint8_t* p) { return (static_cast<uint16_t>(p[0]) << 8) | p[1]; }
uint32_t ReadU32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

std::vector<uint8_t> BuildBindingRequest(const std::array<uint8_t, kTransactionIdSize>& transactionId) {
    std::vector<uint8_t> msg;
    msg.reserve(kHeaderSize);
    WriteU16(msg, kBindingRequest);
    WriteU16(msg, 0);  // message length: no attributes in the request
    WriteU32(msg, kMagicCookie);
    msg.insert(msg.end(), transactionId.begin(), transactionId.end());
    return msg;
}

std::optional<Endpoint> ParseMappedAddress(const uint8_t* attrValue, uint16_t attrLen, bool isXor) {
    if (attrLen < 8 || attrValue[1] != 0x01) return std::nullopt;  // family must be IPv4

    uint16_t port = ReadU16(attrValue + 2);
    uint32_t addr = ReadU32(attrValue + 4);

    if (isXor) {
        port ^= static_cast<uint16_t>(kMagicCookie >> 16);
        addr ^= kMagicCookie;
    }

    Endpoint endpoint{};
    endpoint.port = port;
    // Endpoint::ipv4 is stored in network byte order (like sockaddr_in), but
    // we decoded addr into host byte order above; convert back.
    endpoint.ipv4 = htonl(addr);
    return endpoint;
}

std::optional<Endpoint> ParseBindingResponse(const uint8_t* data, size_t length,
                                              const std::array<uint8_t, kTransactionIdSize>& expectedTransactionId) {
    if (length < kHeaderSize) return std::nullopt;
    if (ReadU16(data) != kBindingSuccessResponse) return std::nullopt;
    if (ReadU32(data + 4) != kMagicCookie) return std::nullopt;
    if (std::memcmp(data + 8, expectedTransactionId.data(), kTransactionIdSize) != 0) return std::nullopt;

    uint16_t attrsLength = ReadU16(data + 2);
    size_t offset = kHeaderSize;
    size_t end = kHeaderSize + attrsLength;
    if (end > length) end = length;

    std::optional<Endpoint> fallback;
    while (offset + 4 <= end) {
        uint16_t attrType = ReadU16(data + offset);
        uint16_t attrLen = ReadU16(data + offset + 2);
        if (offset + 4 + attrLen > end) break;

        if (attrType == kAttrXorMappedAddress) {
            if (auto ep = ParseMappedAddress(data + offset + 4, attrLen, /*isXor=*/true)) {
                return ep;
            }
        } else if (attrType == kAttrMappedAddress && !fallback) {
            fallback = ParseMappedAddress(data + offset + 4, attrLen, /*isXor=*/false);
        }

        // Attributes are padded to a 4-byte boundary.
        offset += 4 + ((attrLen + 3) & ~static_cast<uint16_t>(3));
    }
    return fallback;
}

}  // namespace

std::optional<Endpoint> DiscoverPublicAddress(uintptr_t socketHandle, const std::string& stunHost, uint16_t stunPort,
                                               int timeoutMs) {
    SOCKET sock = static_cast<SOCKET>(socketHandle);

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* resolved = nullptr;
    if (getaddrinfo(stunHost.c_str(), std::to_string(stunPort).c_str(), &hints, &resolved) != 0 || !resolved) {
        return std::nullopt;
    }
    sockaddr_in serverAddr = *reinterpret_cast<sockaddr_in*>(resolved->ai_addr);
    freeaddrinfo(resolved);

    std::array<uint8_t, kTransactionIdSize> transactionId{};
    Crypto::RandomBytes(transactionId.data(), transactionId.size());
    std::vector<uint8_t> request = BuildBindingRequest(transactionId);

    DWORD timeout = static_cast<DWORD>(timeoutMs);
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    std::optional<Endpoint> result;
    if (sendto(sock, reinterpret_cast<const char*>(request.data()), static_cast<int>(request.size()), 0,
               reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) != SOCKET_ERROR) {
        std::array<uint8_t, 512> responseBuf{};
        sockaddr_in fromAddr{};
        int fromLen = sizeof(fromAddr);
        int received = recvfrom(sock, reinterpret_cast<char*>(responseBuf.data()),
                                 static_cast<int>(responseBuf.size()), 0, reinterpret_cast<sockaddr*>(&fromAddr),
                                 &fromLen);
        if (received > 0) {
            result = ParseBindingResponse(responseBuf.data(), static_cast<size_t>(received), transactionId);
        }
    }

    // Restore blocking mode: UdpServer::Run expects to block indefinitely.
    timeout = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    return result;
}

}  // namespace Stun
