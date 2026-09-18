#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#include "Config.h"
#include "InputSimulator.h"
#include "MotionProcessor.h"
#include "PacketHandler.h"
#include "RendezvousClient.h"
#include "StunClient.h"
#include "UdpServer.h"

namespace {

bool PromptYesNo(const std::string& question) {
    while (true) {
        std::cout << question << " [y/n]: ";
        std::string line;
        if (!std::getline(std::cin, line)) return false;
        if (!line.empty() && (line[0] == 'y' || line[0] == 'Y')) return true;
        if (!line.empty() && (line[0] == 'n' || line[0] == 'N')) return false;
    }
}

std::string FormatEndpoint(const Stun::Endpoint& endpoint) {
    in_addr addr{};
    addr.S_un.S_addr = endpoint.ipv4;
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr, buf, sizeof(buf));
    return std::string(buf) + ":" + std::to_string(endpoint.port);
}

// Parses "ip:port" (dotted-quad IPv4) as returned by the rendezvous server.
std::optional<UdpEndpoint> ParseEndpoint(const std::string& text) {
    size_t colon = text.find(':');
    if (colon == std::string::npos) return std::nullopt;

    in_addr addr{};
    if (inet_pton(AF_INET, text.substr(0, colon).c_str(), &addr) != 1) return std::nullopt;

    int port = 0;
    try {
        port = std::stoi(text.substr(colon + 1));
    } catch (...) {
        return std::nullopt;
    }
    if (port < 1 || port > 65535) return std::nullopt;

    UdpEndpoint endpoint{};
    endpoint.ipv4 = addr.S_un.S_addr;
    endpoint.port = static_cast<uint16_t>(port);
    return endpoint;
}

// Rendezvous + STUN + hole punching + confirmation, for Internet mode only.
void RunInternetSetup(UdpServer& server, PacketHandler& packetHandler) {
    std::cout << "Discovering your public address via STUN...\n";
    auto publicAddr = Stun::DiscoverPublicAddress(server.NativeSocketHandle(), Config::kStunServerHost,
                                                   Config::kStunServerPort, Config::kStunTimeoutMs);
    if (!publicAddr) {
        throw std::runtime_error("Could not reach the STUN server - check your internet connection");
    }

    RendezvousClient rendezvous(Config::kRendezvousHost, Config::kRendezvousPort, Config::kRendezvousUseTls);
    std::string pairingCode = rendezvous.RequestPairingCode();
    rendezvous.Connect(pairingCode, "desktop");

    std::cout << "\n=====================================\n";
    std::cout << " Pairing code: " << pairingCode << "\n";
    std::cout << " Enter this code on your phone (Internet mode).\n";
    std::cout << "=====================================\n\n";

    rendezvous.SendCandidate(FormatEndpoint(*publicAddr));
    std::cout << "Waiting for the phone to connect...\n";
    auto peerCandidateText = rendezvous.ReceiveCandidate(Config::kInternetHandshakeTimeoutMs);
    if (!peerCandidateText) {
        throw std::runtime_error("Timed out waiting for the phone - check the pairing code and try again");
    }
    auto peerCandidate = ParseEndpoint(*peerCandidateText);
    if (!peerCandidate) {
        throw std::runtime_error("Rendezvous server returned an invalid address");
    }
    rendezvous.Close();

    // A handful of unsolicited datagrams opens this device's NAT mapping
    // toward the phone, which does the same toward us at the same time -
    // once both mappings are open, packets flow directly with no relay.
    // Content is irrelevant: PacketHandler safely ignores anything that
    // doesn't parse as a HELLO or a valid secure envelope.
    std::cout << "Punching through to " << *peerCandidateText << "...\n";
    const uint8_t punch = 0;
    for (int i = 0; i < 5; ++i) {
        server.SendTo(*peerCandidate, &punch, sizeof(punch));
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    uint32_t code = packetHandler.WaitForConfirmationCode();
    std::cout << "\nConfirmation code: " << code << "\n";
    bool accept = PromptYesNo("Does this match the code shown on your phone?");
    packetHandler.OnOperatorDecision(accept);
    std::cout << (accept ? "Connection confirmed.\n" : "Connection rejected.\n");
}

}  // namespace

int main() {
    std::cout << "PhoneCursor desktop server\n";
    std::cout << "Connect over [L]ocal network or the [I]nternet? ";
    std::string modeLine;
    std::getline(std::cin, modeLine);
    bool internetMode = !modeLine.empty() && (modeLine[0] == 'i' || modeLine[0] == 'I');

    InputSimulator inputSimulator;
    MotionProcessor motionProcessor;

    try {
        UdpServer server(Config::kDefaultPort);
        PacketHandler packetHandler(inputSimulator, motionProcessor, server, /*requireConfirmation=*/internetMode);

        std::thread receiveThread([&] {
            server.Run([&](const UdpEndpoint& sender, const uint8_t* data, size_t length) {
                packetHandler.Handle(sender, data, length);
            });
        });

        if (internetMode) {
            RunInternetSetup(server, packetHandler);
        } else {
            std::cout << "Listening on UDP port " << Config::kDefaultPort << "\n";
            std::cout << "Find this PC's local IP with 'ipconfig' and enter it in the "
                         "Android app (look for IPv4 Address under your Wi-Fi adapter).\n\n";
        }

        receiveThread.join();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
