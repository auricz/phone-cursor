#pragma once

#include <cstdint>
#include <functional>
#include <vector>

// A UDP peer address, kept as a plain struct (rather than sockaddr_in) so
// this header doesn't need to pull in <winsock2.h> - callers that already
// include <windows.h> for other reasons (e.g. InputSimulator) can include
// this header in any order without the classic winsock1/winsock2 clash.
struct UdpEndpoint {
    uint32_t ipv4 = 0;  // network byte order, as in sockaddr_in::sin_addr
    uint16_t port = 0;  // host byte order

    bool operator==(const UdpEndpoint& other) const { return ipv4 == other.ipv4 && port == other.port; }
    bool operator!=(const UdpEndpoint& other) const { return !(*this == other); }
};

// Minimal blocking UDP server. Receives datagrams on the configured port
// and hands each one's raw bytes (plus sender address, needed now that the
// server also replies during the secure handshake and, in Internet mode,
// during hole punching) to a callback; parsing/dispatch lives elsewhere
// (see PacketHandler) so this class only knows about sockets.
class UdpServer {
public:
    using PacketCallback = std::function<void(const UdpEndpoint& sender, const uint8_t* data, size_t length)>;

    explicit UdpServer(uint16_t port);
    ~UdpServer();

    UdpServer(const UdpServer&) = delete;
    UdpServer& operator=(const UdpServer&) = delete;

    // Blocks the calling thread, invoking onPacket for each datagram received.
    // Returns only on a fatal socket error.
    void Run(const PacketCallback& onPacket);

    void SendTo(const UdpEndpoint& recipient, const uint8_t* data, size_t length);

    // Exposes the raw socket handle for STUN (see StunClient), which must
    // send its binding request on the exact socket that will carry the
    // peer-to-peer session, so the NAT mapping it discovers is the one that
    // actually matters for hole punching.
    uintptr_t NativeSocketHandle() const { return socket_; }

private:
    uint16_t port_;
    uintptr_t socket_;
};
