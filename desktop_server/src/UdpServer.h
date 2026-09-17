#pragma once

#include <cstdint>
#include <functional>
#include <vector>

// Minimal blocking UDP server. Receives datagrams on the configured port
// and hands each one's raw bytes to a callback; parsing/dispatch lives
// elsewhere (see PacketHandler) so this class only knows about sockets.
class UdpServer {
public:
    using PacketCallback = std::function<void(const uint8_t* data, size_t length)>;

    explicit UdpServer(uint16_t port);
    ~UdpServer();

    UdpServer(const UdpServer&) = delete;
    UdpServer& operator=(const UdpServer&) = delete;

    // Blocks the calling thread, invoking onPacket for each datagram received.
    // Returns only on a fatal socket error.
    void Run(const PacketCallback& onPacket);

private:
    uint16_t port_;
    uintptr_t socket_;
};
