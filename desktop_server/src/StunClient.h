#pragma once

#include <cstdint>
#include <optional>
#include <string>

// Minimal STUN (RFC 5389) binding-request client, used only in Internet
// mode. Cloudflare Workers (the rendezvous server's runtime) can't open raw
// UDP sockets, so it can't itself observe a device's NAT-mapped public
// address - that's exactly what STUN is for, so each device asks a
// standard public STUN server directly instead. The rendezvous server only
// relays the resulting address between the two devices (see
// RendezvousClient).
namespace Stun {

struct Endpoint {
    uint32_t ipv4;  // network byte order, as in sockaddr_in::sin_addr
    uint16_t port;  // host byte order
};

// Sends a STUN binding request on socketHandle (an already-bound,
// already-connected-to-nothing UDP SOCKET) and returns this host's
// server-reflexive (public) address as seen by the STUN server. Must be
// called on the same socket that will later be used for the peer-to-peer
// session, since that's the NAT mapping that actually matters for hole
// punching. Reads the reply directly from the socket, so no other thread may
// be receiving on it (e.g. UdpServer::Run) while this runs. Returns
// std::nullopt on timeout or a malformed response.
std::optional<Endpoint> DiscoverPublicAddress(uintptr_t socketHandle, const std::string& stunHost, uint16_t stunPort,
                                               int timeoutMs);

}  // namespace Stun
