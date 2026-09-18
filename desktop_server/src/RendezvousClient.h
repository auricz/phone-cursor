#pragma once

#include <cstdint>
#include <optional>
#include <string>

// Talks to the Cloudflare Worker rendezvous server (see rendezvous_server/)
// over plain HTTP (to allocate a pairing code) and WebSocket (to exchange
// each side's STUN-discovered candidate address once paired). Built on
// WinHTTP, already part of the Windows SDK, so no extra dependency is
// needed - matches the rest of desktop_server.
class RendezvousClient {
public:
    RendezvousClient(std::string host, uint16_t port, bool useTls);
    ~RendezvousClient();

    RendezvousClient(const RendezvousClient&) = delete;
    RendezvousClient& operator=(const RendezvousClient&) = delete;

    // POST /session. Returns the newly allocated pairing code. Throws on failure.
    std::string RequestPairingCode();

    // Opens the WebSocket for an existing pairing code under this device's
    // role ("desktop" or "phone"). Throws with a descriptive message if the
    // code is unknown/expired or that role is already connected.
    void Connect(const std::string& pairingCode, const std::string& role);

    // Sends this device's candidate address as "ip:port" text.
    void SendCandidate(const std::string& ipPort);

    // Waits up to timeoutMs for the peer's candidate address ("ip:port").
    // Returns std::nullopt on timeout or if the connection closes first.
    std::optional<std::string> ReceiveCandidate(int timeoutMs);

    void Close();

private:
    std::string host_;
    uint16_t port_;
    bool useTls_;
    void* sessionHandle_;
    void* webSocketHandle_;
};
