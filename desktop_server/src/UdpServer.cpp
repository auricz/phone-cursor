#include "UdpServer.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <iostream>
#include <stdexcept>
#include <string>

#include "Config.h"

namespace {
constexpr uintptr_t kInvalidSocket = static_cast<uintptr_t>(INVALID_SOCKET);
}

UdpServer::UdpServer(uint16_t port) : port_(port), socket_(kInvalidSocket) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        throw std::runtime_error("WSAStartup failed");
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        throw std::runtime_error("Failed to create UDP socket");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        throw std::runtime_error("Failed to bind UDP socket to port " + std::to_string(port_));
    }

    socket_ = static_cast<uintptr_t>(sock);
}

UdpServer::~UdpServer() {
    if (socket_ != kInvalidSocket) {
        closesocket(static_cast<SOCKET>(socket_));
    }
    WSACleanup();
}

void UdpServer::Run(const PacketCallback& onPacket) {
    std::vector<uint8_t> buffer(Config::kReceiveBufferSize);
    sockaddr_in senderAddr{};
    int senderAddrLen = sizeof(senderAddr);

    while (true) {
        int received = recvfrom(
            static_cast<SOCKET>(socket_),
            reinterpret_cast<char*>(buffer.data()),
            static_cast<int>(buffer.size()),
            0,
            reinterpret_cast<sockaddr*>(&senderAddr),
            &senderAddrLen);

        if (received == SOCKET_ERROR) {
            std::cerr << "recvfrom failed with error " << WSAGetLastError() << "\n";
            continue;
        }
        if (received > 0) {
            UdpEndpoint sender{senderAddr.sin_addr.S_un.S_addr, ntohs(senderAddr.sin_port)};
            onPacket(sender, buffer.data(), static_cast<size_t>(received));
        }
    }
}

void UdpServer::SendTo(const UdpEndpoint& recipient, const uint8_t* data, size_t length) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.S_un.S_addr = recipient.ipv4;
    addr.sin_port = htons(recipient.port);

    sendto(static_cast<SOCKET>(socket_), reinterpret_cast<const char*>(data), static_cast<int>(length), 0,
           reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
}
